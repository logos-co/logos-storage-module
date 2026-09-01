#include "storage_module_plugin.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

// Module package version, injected at build time from metadata.json.
#ifndef STORAGE_MODULE_VERSION
#define STORAGE_MODULE_VERSION "0.0.0-dev"
#endif

// The constructor builds the whole node, so it gets a longer budget.
static constexpr int CREATE_TIMEOUT_MS = 10000;
static constexpr int SYNC_TIMEOUT_MS = 1000;
static constexpr int MANIFEST_TIMEOUT_MS = 3000;
static constexpr int FETCH_TIMEOUT_MS = 3000;

// A chunk is arbitrary bytes and json::dump() demands valid UTF-8.
static std::string base64Encode(const char* data, size_t len) {
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        const unsigned char b0 = static_cast<unsigned char>(data[i]);
        const unsigned char b1 = (i + 1 < len) ? static_cast<unsigned char>(data[i + 1]) : 0;
        const unsigned char b2 = (i + 2 < len) ? static_cast<unsigned char>(data[i + 2]) : 0;
        out += kTable[b0 >> 2];
        out += kTable[((b0 & 0x03) << 4) | (b1 >> 4)];
        out += (i + 1 < len) ? kTable[((b1 & 0x0F) << 2) | (b2 >> 6)] : '=';
        out += (i + 2 < len) ? kTable[b2 & 0x3F] : '=';
    }
    return out;
}

// A reply value belongs to the binding and dies with the callback, so copy it.
static std::string fromFfi(const NimFfiStr& s) {
    return s.data ? std::string(s.data, s.len) : std::string();
}

static std::string replyText(int ret, const NimFfiStr* reply, const char* errMsg) {
    if (ret == RET_OK) return reply ? fromFfi(*reply) : std::string();
    return errMsg ? std::string(errMsg) : std::string();
}

struct AsyncCallbackBase {
    virtual void handleResponse(bool ok, const std::string& msg) = 0;
    virtual ~AsyncCallbackBase() = default;
};

// A non-zero wrapper return means this already ran and freed the context.
static void asyncReply(int ret, const NimFfiStr* reply, const char* errMsg, void* userData) {
    if (!userData) return;
    auto* base = static_cast<AsyncCallbackBase*>(userData);
    base->handleResponse(ret == RET_OK, replyText(ret, reply, errMsg));
    delete base;
}

struct SyncCtx {
    std::mutex mtx;
    std::condition_variable ready;
    int resultCode = -1;
    std::string resultMsg;
    bool received = false;
    // Set when the caller gives up first: the callback then owns the deletion.
    std::atomic<bool> abandoned{false};
    // Only init() reads this: the constructor delivers it by callback.
    StorageCtx* created = nullptr;

    SyncCtx() = default;
    SyncCtx(const SyncCtx&) = delete;
    SyncCtx& operator=(const SyncCtx&) = delete;
};

// Returns true when the caller timed out first, which leaves this reply owning
// whatever it carries.
static bool signalSync(SyncCtx* ctx, int ret, const std::string& msg, StorageCtx* created) {
    bool abandoned;
    {
        std::unique_lock<std::mutex> lock(ctx->mtx);
        ctx->resultCode = ret;
        ctx->resultMsg = msg;
        ctx->created = created;
        ctx->received = true;
        ctx->ready.notify_all();
        // Read abandoned under the lock the timeout path also takes.
        abandoned = ctx->abandoned.load();
    }
    if (abandoned) {
        delete ctx;
    }
    return abandoned;
}

static void syncReply(int ret, const NimFfiStr* reply, const char* errMsg, void* userData) {
    if (!userData) return;
    auto* ctx = static_cast<SyncCtx*>(userData);
    signalSync(ctx, ret, replyText(ret, reply, errMsg), nullptr);
}

static void syncCreateReply(int ret, StorageCtx* created, const char* errMsg, void* userData) {
    if (!userData) return;
    auto* ctx = static_cast<SyncCtx*>(userData);
    std::string msg = (ret == RET_OK || !errMsg) ? std::string() : std::string(errMsg);

    if (signalSync(ctx, ret, msg, created) && created) {
        // init() gave up on the wait, so nothing else can reach this node.
        storage_ctx_destroy(created);
    }
}

static constexpr int64_t DEFAULT_CHUNK_SIZE = 65536;

struct SyncResult {
    bool ok = false;
    std::string message;
    StorageCtx* created = nullptr;
};

static SyncResult waitSync(SyncCtx* ctx, int timeoutMs) {
    SyncResult r;
    bool shouldDelete;
    {
        std::unique_lock<std::mutex> lock(ctx->mtx);
        ctx->ready.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                         [ctx] { return ctx->received; });
        r.ok = ctx->received && ctx->resultCode == RET_OK;
        r.message = ctx->resultMsg;
        r.created = ctx->created;
        shouldDelete = ctx->received;
        if (!shouldDelete) {
            ctx->abandoned.store(true);
        }
    }
    if (shouldDelete) {
        delete ctx;
    }
    return r;
}

// `dispatch` gets the SyncCtx to hand to the wrapper as user data.
template <typename Dispatch>
static SyncResult syncCall(StorageCtx* ctx, Dispatch dispatch, int timeoutMs) {
    if (!ctx) return {false, "Storage context not initialized.", nullptr};
    auto* sctx = new SyncCtx();
    dispatch(sctx);
    return waitSync(sctx, timeoutMs);
}

struct Progress {
    int64_t total = 0;
    int64_t done = 0;
    int64_t pending = 0;
    int lastPercent = -1;
    // Download only: deliver the bytes themselves instead of a byte count.
    bool asChunks = false;
};

// Returns 0 while still inside the percentage point reported last.
static int64_t stepProgress(Progress& p, int64_t bytes) {
    p.done += bytes;
    p.pending += bytes;
    if (p.total > 0) {
        int percent = static_cast<int>((p.done * 100LL) / p.total);
        if (percent <= p.lastPercent) return 0;
        p.lastPercent = percent;
    }
    int64_t batch = p.pending;
    p.pending = 0;
    return batch;
}

// libstorage numbers upload sessions per node from zero, so the owner is part
// of the key: two instances in one process would otherwise share session "0".
using TransferKey = std::pair<const StorageModuleImpl*, std::string>;

struct StepResult {
    bool tracked = false;
    bool asChunks = false;
    int64_t batch = 0;
};

// Progress lands on the libstorage event thread while the reply that ends the
// transfer lands on the dispatch thread, so the table owns the lock.
class TransferTable {
public:
    void track(const TransferKey& key, const Progress& p) {
        std::lock_guard<std::mutex> lock(mtx);
        transfers[key] = p;
    }

    void forget(const TransferKey& key) {
        std::lock_guard<std::mutex> lock(mtx);
        transfers.erase(key);
    }

    void forgetOwner(const StorageModuleImpl* owner) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto it = transfers.begin(); it != transfers.end();) {
            it = (it->first.first == owner) ? transfers.erase(it) : std::next(it);
        }
    }

    StepResult advance(const TransferKey& key, int64_t bytes) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = transfers.find(key);
        if (it == transfers.end()) return {};

        StepResult step;
        step.tracked = true;
        step.asChunks = it->second.asChunks;
        if (!step.asChunks) step.batch = stepProgress(it->second, bytes);
        return step;
    }

private:
    std::mutex mtx;
    std::map<TransferKey, Progress> transfers;
};

static TransferTable uploads;
static TransferTable downloads;

using StorageEvent = void (StorageModuleImpl::*)(const std::string&);

// An uncaught exception inside a libstorage callback is fatal.
static void emitJsonEvent(StorageModuleImpl* impl, StorageEvent emit,
                          const json& payload, const char* caller) {
    std::string data;
    try {
        data = payload.dump();
    } catch (const std::exception& e) {
        fprintf(stderr, "%s: failed to serialize event payload: %s\n", caller, e.what());
        return;
    } catch (...) {
        fprintf(stderr, "%s: failed to serialize event payload (unknown error)\n", caller);
        return;
    }
    (impl->*emit)(data);
}

static void emitBasicResponse(StorageModuleImpl* impl, StorageEvent emit,
                              bool ok, const std::string& message, const char* caller) {
    json j;
    j["success"] = ok;
    j["message"] = message;
    emitJsonEvent(impl, emit, j, caller);
}

static void emitSessionProgress(StorageModuleImpl* impl, StorageEvent emit,
                                const std::string& sessionId, int64_t bytes,
                                const char* caller) {
    json j;
    j["success"] = true;
    j["sessionId"] = sessionId;
    j["bytes"] = bytes;
    emitJsonEvent(impl, emit, j, caller);
}

static void emitSessionResult(StorageModuleImpl* impl, StorageEvent emit,
                              bool ok, const std::string& sessionId,
                              const std::string& message,
                              const std::string& okField, const char* caller) {
    json j;
    j["success"] = ok;
    j["sessionId"] = sessionId;
    if (ok && !okField.empty()) j[okField] = message;
    else if (!ok) j["error"] = message;
    emitJsonEvent(impl, emit, j, caller);
}

// Percentage throttling needs the total size, which only the manifest carries.
// Returns 0 when it cannot be read.
static int64_t manifestDatasetSize(StorageCtx* ctx, const std::string& cid) {
    auto r = syncCall(ctx, [ctx, &cid](SyncCtx* s) {
        storage_ctx_download_manifest(ctx, nimffi_str(cid.c_str()), syncReply, s);
    }, MANIFEST_TIMEOUT_MS);
    if (!r.ok) {
        fprintf(stderr, "manifestDatasetSize: failed to get the manifest for %s: %s\n",
                cid.c_str(), r.message.c_str());
        return 0;
    }

    try {
        json manifest = json::parse(r.message);
        if (!manifest.contains("datasetSize") || manifest["datasetSize"].is_null()) return 0;

        const auto& size = manifest["datasetSize"];
        int64_t bytes = 0;
        if (size.is_number_integer()) bytes = size.get<int64_t>();
        else if (size.is_number()) bytes = static_cast<int64_t>(size.get<double>());
        else if (size.is_string()) bytes = std::stoll(size.get_ref<const std::string&>());
        // A peer wrote this manifest, so a negative size is a thing that happens.
        return bytes > 0 ? bytes : 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "manifestDatasetSize: failed to parse the manifest for %s: %s\n",
                cid.c_str(), e.what());
    } catch (...) {
        fprintf(stderr, "manifestDatasetSize: failed to parse the manifest for %s\n",
                cid.c_str());
    }
    return 0;
}

static void onUploadProgressEvent(const OnUploadProgressPayload* evt, void* userData) {
    if (!evt || !userData) return;
    auto* impl = static_cast<StorageModuleImpl*>(userData);
    std::string sessionId = fromFfi(evt->sessionId);

    StepResult step = uploads.advance({impl, sessionId},
                                      static_cast<int64_t>(evt->storedBytes));
    if (step.batch == 0) return;

    emitSessionProgress(impl, &StorageModuleImpl::storageUploadProgress, sessionId,
                        step.batch, "onUploadProgressEvent");
}

static void onDownloadChunkEvent(const OnDownloadChunkPayload* evt, void* userData) {
    if (!evt || !userData) return;
    auto* impl = static_cast<StorageModuleImpl*>(userData);
    std::string cid = fromFfi(evt->cid);

    StepResult step = downloads.advance({impl, cid}, static_cast<int64_t>(evt->data.len));
    if (!step.tracked) return;

    if (step.asChunks) {
        json j;
        j["success"] = true;
        j["sessionId"] = cid;
        j["chunk"] = base64Encode(reinterpret_cast<const char*>(evt->data.data), evt->data.len);
        emitJsonEvent(impl, &StorageModuleImpl::storageDownloadProgress, j,
                      "onDownloadChunkEvent");
        return;
    }

    if (step.batch == 0) return;
    emitSessionProgress(impl, &StorageModuleImpl::storageDownloadProgress, cid,
                        step.batch, "onDownloadChunkEvent");
}

// A listener registration answers with its id, and zero means it failed.
static bool registerTransferListeners(StorageCtx* ctx, StorageModuleImpl* impl) {
    if (storage_ctx_add_on_upload_progress_listener(ctx, onUploadProgressEvent, impl) == 0)
        return false;

    return storage_ctx_add_on_download_chunk_listener(ctx, onDownloadChunkEvent, impl) != 0;
}

struct SimpleEventCtx : AsyncCallbackBase {
    StorageModuleImpl* impl;
    StorageEvent event;

    SimpleEventCtx(StorageModuleImpl* i, StorageEvent ev)
        : impl(i), event(ev) {}

    void handleResponse(bool ok, const std::string& msg) override {
        emitBasicResponse(impl, event, ok, msg, "SimpleEventCtx");
    }
};

struct UploadFileCtx : AsyncCallbackBase {
    StorageModuleImpl* impl;
    std::string sessionId;

    UploadFileCtx(StorageModuleImpl* i, std::string sid)
        : impl(i), sessionId(std::move(sid)) {}

    void handleResponse(bool ok, const std::string& msg) override {
        uploads.forget({impl, sessionId});
        emitSessionResult(impl, &StorageModuleImpl::storageUploadDone, ok,
                          sessionId, msg, "cid", "UploadFileCtx");
    }
};

// A failed chunk leaves the session usable, so the caller decides what next.
struct UploadChunkCtx : AsyncCallbackBase {
    StorageModuleImpl* impl;
    std::string sessionId;
    int64_t bytes;

    UploadChunkCtx(StorageModuleImpl* i, std::string sid, int64_t n)
        : impl(i), sessionId(std::move(sid)), bytes(n) {}

    void handleResponse(bool ok, const std::string& msg) override {
        json j;
        j["success"] = ok;
        j["sessionId"] = sessionId;
        if (ok) j["bytes"] = bytes;
        else j["error"] = msg;
        emitJsonEvent(impl, &StorageModuleImpl::storageUploadProgress, j,
                      "UploadChunkCtx");
    }
};

struct DownloadStreamCtx : AsyncCallbackBase {
    StorageModuleImpl* impl;
    std::string cid;

    DownloadStreamCtx(StorageModuleImpl* i, std::string c)
        : impl(i), cid(std::move(c)) {}

    void handleResponse(bool ok, const std::string& msg) override {
        downloads.forget({impl, cid});
        emitSessionResult(impl, &StorageModuleImpl::storageDownloadDone, ok,
                          cid, msg, "", "DownloadStreamCtx");
    }
};

struct FetchManifestCtx : AsyncCallbackBase {
    StorageModuleImpl* impl;
    std::string cid;

    FetchManifestCtx(StorageModuleImpl* i, std::string c)
        : impl(i), cid(std::move(c)) {}

    void handleResponse(bool ok, const std::string& msg) override {
        json j;
        j["cid"] = cid;
        if (ok) {
            try {
                j["manifest"] = json::parse(msg);
                j["success"] = true;
            } catch (...) {
                j["success"] = false;
                j["error"] = "Failed to parse manifest.";
            }
        } else {
            j["success"] = false;
            j["error"] = msg;
        }
        emitJsonEvent(impl, &StorageModuleImpl::storageDownloadManifestDone, j,
                      "FetchManifestCtx");
    }
};

struct RemoveCtx : AsyncCallbackBase {
    StorageModuleImpl* impl;
    std::string cid;

    RemoveCtx(StorageModuleImpl* i, std::string c)
        : impl(i), cid(std::move(c)) {}

    void handleResponse(bool ok, const std::string& msg) override {
        json j;
        j["cid"] = cid;
        j["success"] = ok;
        if (!ok) j["error"] = msg;
        emitJsonEvent(impl, &StorageModuleImpl::storageRemoveDone, j, "RemoveCtx");
    }
};

StorageModuleImpl::StorageModuleImpl() : storageCtx(nullptr) {
    fprintf(stderr, "StorageModuleImpl: Initializing...\n");
}

StorageModuleImpl::~StorageModuleImpl() {
    if (storageCtx) {
        fprintf(stderr,
                "StorageModuleImpl: Warning - storage context was not "
                "destroyed before plugin destruction\n");
        storageCtx = nullptr;
    }
}

bool StorageModuleImpl::init(const std::string& cfg) {
    fprintf(stderr, "StorageModuleImpl::init called\n");

    if (storageCtx) {
        fprintf(stderr, "StorageModuleImpl::init: context already initialized\n");
        return false;
    }

    auto* sctx = new SyncCtx();
    storage_ctx_create(nimffi_str(cfg.c_str()), syncCreateReply, sctx);
    SyncResult r = waitSync(sctx, CREATE_TIMEOUT_MS);

    if (!r.ok || !r.created) {
        fprintf(stderr, "StorageModuleImpl::init failed: %s\n",
                r.message.c_str());
        return false;
    }
    storageCtx = r.created;

    if (!registerTransferListeners(storageCtx, this)) {
        fprintf(stderr, "StorageModuleImpl::init: failed to register the "
                        "libstorage event listeners\n");
        storage_ctx_destroy(storageCtx);
        storageCtx = nullptr;
        return false;
    }
    return true;
}

bool StorageModuleImpl::start() {
    fprintf(stderr, "StorageModuleImpl::start called\n");
    if (!storageCtx) {
        fprintf(stderr, "StorageModuleImpl::start: context not initialized\n");
        return false;
    }
    auto* ctx = new SimpleEventCtx(this, &StorageModuleImpl::storageStart);
    if (storage_ctx_start(storageCtx, asyncReply, ctx) != RET_OK) {
        return false;
    }
    return true;
}

StdLogosResult StorageModuleImpl::stop() {
    fprintf(stderr, "StorageModuleImpl::stop called\n");
    if (!storageCtx)
        return {false, {}, "Storage context not initialized."};
    auto* ctx = new SimpleEventCtx(this, &StorageModuleImpl::storageStop);
    if (storage_ctx_stop(storageCtx, asyncReply, ctx) != RET_OK) {
        return {false, {}, "Failed to send stop command."};
    }
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::destroy() {
    fprintf(stderr, "StorageModuleImpl::destroy called\n");
    if (!storageCtx)
        return {false, {}, "Storage context not initialized."};

    int ret = storage_ctx_destroy(storageCtx);
    if (ret != RET_OK)
        return {false, {}, "Failed to destroy storage context."};

    storageCtx = nullptr;
    uploads.forgetOwner(this);
    downloads.forgetOwner(this);

    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::libstorageVersion() {
    // The buffer is thread-local and belongs to libstorage: copy, never free.
    const char* v = storage_version();
    if (!v) return {false, {}, "Failed to get version."};
    return {true, std::string(v), ""};
}

std::string StorageModuleImpl::moduleVersion() {
    return STORAGE_MODULE_VERSION;
}

StdLogosResult StorageModuleImpl::dataDir() {
    auto r = syncCall(storageCtx, [this](SyncCtx* s) {
        storage_ctx_repo(storageCtx, syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message, ""};
}

StdLogosResult StorageModuleImpl::peerId() {
    auto r = syncCall(storageCtx, [this](SyncCtx* s) {
        storage_ctx_peer_id(storageCtx, syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message, ""};
}

StdLogosResult StorageModuleImpl::spr() {
    auto r = syncCall(storageCtx, [this](SyncCtx* s) {
        storage_ctx_spr(storageCtx, syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message, ""};
}

StdLogosResult StorageModuleImpl::debug() {
    auto r = syncCall(storageCtx, [this](SyncCtx* s) {
        storage_ctx_debug(storageCtx, syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    try {
        return {true, json::parse(r.message), ""};
    } catch (...) {
        return {false, {}, "Failed to parse debug info."};
    }
}

LogosMap StorageModuleImpl::collectMetrics() {
    auto emptyMetrics = [] { return json{{"metrics", json::array()}}; };

    auto r = syncCall(storageCtx, [this](SyncCtx* s) {
        storage_ctx_get_metrics(storageCtx, syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return emptyMetrics();
    try {
        json parsed = json::parse(r.message);
        if (!parsed.is_object()) return emptyMetrics();
        auto metrics = parsed.find("metrics");
        if (metrics == parsed.end() || !metrics->is_array()) return emptyMetrics();
        return parsed;
    } catch (...) {
        return emptyMetrics();
    }
}

StdLogosResult StorageModuleImpl::updateLogLevel(const std::string& logLevel) {
    auto r = syncCall(storageCtx, [this, &logLevel](SyncCtx* s) {
        storage_ctx_log_level(storageCtx, nimffi_str(logLevel.c_str()), syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::connect(const std::string& peerId,
                                           const std::vector<std::string>& peerAddresses) {
    if (!storageCtx)
        return {false, {}, "Storage context not initialized."};

    // The wrapper encodes before it returns, so a borrowed view can be local.
    std::vector<NimFfiStr> addrs;
    addrs.reserve(peerAddresses.size());
    for (const auto& a : peerAddresses) addrs.push_back(nimffi_str(a.c_str()));
    StorageSeq_Str addrList{addrs.data(), addrs.size()};

    auto* ctx = new SimpleEventCtx(this, &StorageModuleImpl::storageConnect);
    if (storage_ctx_connect(storageCtx, nimffi_str(peerId.c_str()), &addrList,
                            asyncReply, ctx) != RET_OK) {
        return {false, {}, "Failed to send connect command."};
    }
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::togglePrivateQueries(bool enabled) {
    auto r = syncCall(storageCtx, [this, enabled](SyncCtx* s) {
        storage_ctx_toggle_private_queries(storageCtx, enabled, syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message == "true", ""};
}

StdLogosResult StorageModuleImpl::uploadInit(const std::string& filename,
                                              int64_t chunkSize) {
    auto r = syncCall(storageCtx, [this, &filename, chunkSize](SyncCtx* s) {
        storage_ctx_upload_init(storageCtx, nimffi_str(filename.c_str()),
                                static_cast<uint64_t>(chunkSize), syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message, ""};
}

StdLogosResult StorageModuleImpl::uploadUrl(const std::string& filePath,
                                             int64_t chunkSize) {
    fprintf(stderr, "StorageModuleImpl::uploadUrl called with path=%s\n",
            filePath.c_str());
    if (!storageCtx || chunkSize <= 0)
        return {false, {}, "Invalid arguments."};

    std::error_code ec;
    if (!fs::exists(filePath, ec) || !fs::is_regular_file(filePath, ec)) {
        fprintf(stderr, "StorageModuleImpl::uploadUrl: file not found or not regular: %s\n",
                filePath.c_str());
        return {false, {}, "File not found: " + filePath};
    }

    // An unknown size disables the percentage throttling instead of poisoning it.
    int64_t fileSize = static_cast<int64_t>(fs::file_size(filePath, ec));
    if (ec) fileSize = 0;

    StdLogosResult ir = uploadInit(filePath, chunkSize);
    if (!ir.success)
        return ir;
    std::string sessionId = ir.value.get<std::string>();

    Progress p;
    p.total = fileSize;
    uploads.track({this, sessionId}, p);

    auto* ctx = new UploadFileCtx(this, sessionId);
    if (storage_ctx_upload_file(storageCtx, nimffi_str(sessionId.c_str()),
                                asyncReply, ctx) != RET_OK) {
        uploadCancel(sessionId);
        return {false, {}, "Failed to start file upload."};
    }
    return {true, sessionId, ""};
}

StdLogosResult StorageModuleImpl::uploadChunk(const std::string& sessionId,
                                               const std::string& chunk) {
    if (!storageCtx)
        return {false, {}, "Storage context not initialized."};

    NimFfiBytes data;
    data.data = reinterpret_cast<uint8_t*>(const_cast<char*>(chunk.data()));
    data.len = chunk.size();

    auto* ctx = new UploadChunkCtx(this, sessionId, static_cast<int64_t>(chunk.size()));
    if (storage_ctx_upload_chunk(storageCtx, nimffi_str(sessionId.c_str()), &data,
                                 asyncReply, ctx) != RET_OK) {
        return {false, {}, "Failed to send chunk."};
    }
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::uploadFinalize(const std::string& sessionId) {
    auto r = syncCall(storageCtx, [this, &sessionId](SyncCtx* s) {
        storage_ctx_upload_finalize(storageCtx, nimffi_str(sessionId.c_str()), syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message, ""};
}

StdLogosResult StorageModuleImpl::uploadCancel(const std::string& sessionId) {
    auto r = syncCall(storageCtx, [this, &sessionId](SyncCtx* s) {
        storage_ctx_upload_cancel(storageCtx, nimffi_str(sessionId.c_str()), syncReply, s);
    }, SYNC_TIMEOUT_MS);
    uploads.forget({this, sessionId});
    if (!r.ok) return {false, {}, r.message};
    return {true, {}, ""};
}

// Returns the session ID (= cid), or an empty string on failure.
std::string StorageModuleImpl::downloadChunksInternal(const std::string& cid,
                                                       const std::string& filepath,
                                                       bool local,
                                                       int64_t chunkSize) {
    if (!storageCtx || chunkSize <= 0) return {};

    int64_t totalBytes = 0;
    if (!filepath.empty()) {
        totalBytes = manifestDatasetSize(storageCtx, cid);
        if (totalBytes == 0) return {};
    }

    auto r = syncCall(storageCtx, [this, &cid, chunkSize, local](SyncCtx* s) {
        storage_ctx_download_init(storageCtx, nimffi_str(cid.c_str()),
                                  static_cast<uint64_t>(chunkSize), local, syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {};

    Progress p;
    p.total = totalBytes;
    p.asChunks = filepath.empty();
    downloads.track({this, cid}, p);

    auto* ctx = new DownloadStreamCtx(this, cid);
    if (storage_ctx_download_stream(storageCtx, nimffi_str(cid.c_str()),
                                    static_cast<uint64_t>(chunkSize), local,
                                    nimffi_str(filepath.c_str()),
                                    asyncReply, ctx) != RET_OK) {
        downloadCancel(cid);
        return {};
    }
    return cid;
}

StdLogosResult StorageModuleImpl::downloadToUrl(const std::string& cid,
                                                 const std::string& filePath,
                                                 bool local, int64_t chunkSize) {
    std::string sessionId = downloadChunksInternal(cid, filePath, local, chunkSize);
    if (sessionId.empty())
        return {false, {}, "Failed to start download."};
    return {true, sessionId, ""};
}

StdLogosResult StorageModuleImpl::downloadChunks(const std::string& cid, bool local,
                                                  int64_t chunkSize) {
    std::string sessionId = downloadChunksInternal(cid, "", local, chunkSize);
    if (sessionId.empty())
        return {false, {}, "Failed to start chunk download."};
    return {true, sessionId, ""};
}

StdLogosResult StorageModuleImpl::downloadCancel(const std::string& sessionId) {
    auto r = syncCall(storageCtx, [this, &sessionId](SyncCtx* s) {
        storage_ctx_download_cancel(storageCtx, nimffi_str(sessionId.c_str()), syncReply, s);
    }, SYNC_TIMEOUT_MS);
    downloads.forget({this, sessionId});
    if (!r.ok) return {false, {}, r.message};
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::exists(const std::string& cid) {
    auto r = syncCall(storageCtx, [this, &cid](SyncCtx* s) {
        storage_ctx_exists(storageCtx, nimffi_str(cid.c_str()), syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message == "true", ""};
}

StdLogosResult StorageModuleImpl::fetch(const std::string& cid) {
    auto r = syncCall(storageCtx, [this, &cid](SyncCtx* s) {
        storage_ctx_fetch(storageCtx, nimffi_str(cid.c_str()), syncReply, s);
    }, FETCH_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::remove(const std::string& cid) {
    if (!storageCtx)
        return {false, {}, "Storage context not initialized."};
    auto* ctx = new RemoveCtx(this, cid);
    if (storage_ctx_delete(storageCtx, nimffi_str(cid.c_str()), asyncReply, ctx) !=
        RET_OK) {
        return {false, {}, "Failed to send remove command."};
    }
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::space() {
    auto r = syncCall(storageCtx, [this](SyncCtx* s) {
        storage_ctx_space(storageCtx, syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    try {
        return {true, json::parse(r.message), ""};
    } catch (...) {
        return {false, {}, "Failed to parse space info."};
    }
}

StdLogosResult StorageModuleImpl::manifests() {
    auto r = syncCall(storageCtx, [this](SyncCtx* s) {
        storage_ctx_list(storageCtx, syncReply, s);
    }, SYNC_TIMEOUT_MS);
    if (!r.ok) return {false, {}, r.message};
    try {
        json raw = json::parse(r.message);
        if (!raw.is_array())
            return {false, {}, "Failed to parse manifests."};
        json list = json::array();
        for (const auto& item : raw) {
            json entry;
            if (item.contains("cid"))      entry["cid"]         = item["cid"];
            if (item.contains("manifest")) {
                const auto& m = item["manifest"];
                if (m.contains("treeCid"))     entry["treeCid"]     = m["treeCid"];
                if (m.contains("datasetSize")) entry["datasetSize"] = m["datasetSize"];
                if (m.contains("blockSize"))   entry["blockSize"]   = m["blockSize"];
                if (m.contains("filename"))    entry["filename"]    = m["filename"];
                if (m.contains("mimetype"))    entry["mimetype"]    = m["mimetype"];
            }
            list.push_back(entry);
        }
        return {true, list, ""};
    } catch (...) {
        return {false, {}, "Failed to parse manifests."};
    }
}

StdLogosResult StorageModuleImpl::downloadManifest(const std::string& cid) {
    if (!storageCtx)
        return {false, {}, "Storage context not initialized."};
    auto* ctx = new FetchManifestCtx(this, cid);
    if (storage_ctx_download_manifest(storageCtx, nimffi_str(cid.c_str()), asyncReply,
                                      ctx) != RET_OK) {
        return {false, {}, "Failed to send download manifest command."};
    }
    return {true, {}, ""};
}

void StorageModuleImpl::importFiles(const std::string& path) {
    fprintf(stderr, "StorageModuleImpl::importFiles from path=%s\n",
            path.c_str());
    std::error_code ec;
    if (!fs::is_directory(path, ec)) {
        fprintf(stderr,
                "StorageModuleImpl::importFiles: not a directory: %s\n",
                path.c_str());
        return;
    }
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string fp = entry.path().string();
        fprintf(stderr, "StorageModuleImpl::importFiles: uploading %s\n",
                fp.c_str());
        StdLogosResult result = uploadUrl(fp, DEFAULT_CHUNK_SIZE);
        if (!result.success) {
            fprintf(stderr,
                    "StorageModuleImpl::importFiles: failed to start upload "
                    "for %s\n",
                    fp.c_str());
        } else {
            std::string sid = result.value.is_string()
                                  ? result.value.get<std::string>()
                                  : std::string();
            fprintf(stderr,
                    "StorageModuleImpl::importFiles: upload started, "
                    "session=%s\n",
                    sid.c_str());
        }
    }
}
