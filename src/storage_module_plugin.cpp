#include "storage_module_plugin.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

// Module package version, injected at build time from metadata.json.
#ifndef STORAGE_MODULE_VERSION
#define STORAGE_MODULE_VERSION "0.0.0-dev"
#endif

// ---------------------------------------------------------------------------
// Storage Module — libstorage C++ wrapper
//
// libstorage functions are asynchronous: a command is dispatched to a worker
// thread and the result arrives via a StorageCallback.  This file implements
// several callback context types (a strategy pattern) that handle results in
// different ways.  The type hierarchy is:
//
//   AsyncCallbackBase
//     │  Base for all fire-and-forget async contexts.  The dispatcher calls
//     │  handleResponse() and deletes the context on any non-PROGRESS code.
//     │
//     ├── SimpleEventCtx    – start/stop: emits a named event to the host.
//     ├── ConnectCtx        – connect: same as Simple, but also owns and
//     │                       frees the C-string peer-address array.
//     ├── UploadFileCtx     – file upload: throttled progress + done event.
//     ├── UploadChunkCtx    – single-chunk upload: emits progress event.
//     └── DownloadStreamCtx – dual-mode download:
//                             * file-mode  → write to path, emit byte-count
//                             * chunk-mode → emit base64-encoded data chunks
//
//   SyncCtx
//     Synchronous-wait pattern: the caller allocates a SyncCtx on the heap,
//     issues the command, then blocks on the condvar.  On callback arrival
//     the result is copied into the context and the condvar is signalled.
//     An "abandoned" flag handles the rare race where the caller times out
//     before the callback fires — in that case the callback itself deletes
//     the context instead of the caller.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// base64 encoding helper — needed to safely embed binary chunk data in JSON.
// nlohmann::json::dump() requires valid UTF-8; raw download chunks are
// arbitrary bytes and will throw type_error.316 without encoding.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Callback base — all context objects inherit from this.
// Only used for the async (event-emitting) dispatch path.
// ---------------------------------------------------------------------------

struct AsyncCallbackBase {
    virtual void handleResponse(int ret, const char* msg, size_t len) = 0;
    virtual ~AsyncCallbackBase() = default;
};

// Static callback for async contexts (start/stop/connect/upload progress/download).
// Ownership: each AsyncCallbackBase is heap-allocated and deleted here on non-PROGRESS.
static void asyncCallback(int ret, const char* msg, size_t len, void* userData) {
    if (!userData) return;
    auto* base = static_cast<AsyncCallbackBase*>(userData);
    base->handleResponse(ret, msg, len);
    if (ret != RET_PROGRESS) {
        delete base;
    }
}

// ---------------------------------------------------------------------------
// SyncCtx — used for synchronous (blocking) libstorage calls.
//
// Lifetime rules:
//   - Allocated on the heap by the caller before issuing the command.
//   - Caller waits on the condvar, then checks ctx->received.
//   - If received == true before timeout: caller reads result and deletes ctx.
//   - If timeout fires before callback: caller marks ctx->abandoned = true
//     (under the same mutex) and does NOT delete; the callback will delete
//     when it eventually fires.
//
// This `abandoned` pattern prevents use-after-free if libstorage calls the
// callback after the waiting thread has timed out.
// ---------------------------------------------------------------------------

struct SyncCtx {
    std::mutex mtx;
    std::condition_variable ready;
    int resultCode = -1;
    std::string resultMsg;
    bool received = false;
    std::atomic<bool> abandoned{false};
    // Keeps the string argument alive across the (potentially async) C call.
    std::string lifetimeArg;

    SyncCtx() = default;
    SyncCtx(const SyncCtx&) = delete;
    SyncCtx& operator=(const SyncCtx&) = delete;
};

static void syncCallback(int ret, const char* msg, size_t len, void* userData) {
    if (!userData) return;
    auto* ctx = static_cast<SyncCtx*>(userData);
    bool shouldDelete;
    {
        std::unique_lock<std::mutex> lock(ctx->mtx);
        ctx->resultCode = ret;
        ctx->resultMsg = (msg && len > 0) ? std::string(msg, len) : std::string();
        ctx->received = true;
        ctx->ready.notify_all();
        // Read abandoned while holding the lock so there is no race with the
        // caller's timeout path that also sets this flag under the lock.
        shouldDelete = ctx->abandoned.load();
    }
    if (shouldDelete) {
        delete ctx;
    }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static constexpr int64_t DEFAULT_CHUNK_SIZE = 65536;

static std::string fromMsg(const char* msg, size_t len) {
    return (msg && len > 0) ? std::string(msg, len) : std::string();
}

struct SyncResult {
    bool ok = false;
    std::string message;
};

// Wait for a SyncCtx to be signalled (or time out).
// Returns the result and handles the abandoned-flag cleanup.
static SyncResult waitSync(SyncCtx* ctx, int timeoutMs) {
    SyncResult r;
    bool shouldDelete;
    {
        std::unique_lock<std::mutex> lock(ctx->mtx);
        ctx->ready.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                         [ctx] { return ctx->received; });
        r.ok = ctx->received && ctx->resultCode == RET_OK;
        r.message = ctx->resultMsg;
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

// ---------------------------------------------------------------------------
// JSON event helpers — build and emit common response patterns.
//
// Each helper takes a pointer-to-member to the typed event method declared
// in `storage_module_plugin.h`'s `logos_events:` block; the method bodies
// are codegen-emitted in `storage_module_events_cdylib.cpp` and dispatch
// through `LogosModuleContext::emitEventImpl_`.
//
// Serialization of the JSON payload runs inside libstorage callbacks where
// uncaught exceptions would be fatal; do that first under a try/catch and
// only invoke the typed event method when we have a valid string.
// ---------------------------------------------------------------------------

using StorageEvent = void (StorageModuleImpl::*)(const std::string&);

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
                              int ret, const std::string& message, const char* caller) {
    json j;
    j["success"] = (ret == RET_OK);
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
                              int ret, const std::string& sessionId,
                              const std::string& message,
                              const std::string& okField, const char* caller) {
    json j;
    j["success"] = (ret == RET_OK);
    j["sessionId"] = sessionId;
    if (ret == RET_OK && !okField.empty()) j[okField] = message;
    else if (ret != RET_OK) j["error"] = message;
    emitJsonEvent(impl, emit, j, caller);
}

// ---------------------------------------------------------------------------
// Concrete async context implementations
// ---------------------------------------------------------------------------

// Dispatches the typed event member pointer passed in `event` on completion.
// JSON payload: {success, message}.
struct SimpleEventCtx : AsyncCallbackBase {
    StorageModuleImpl* impl;
    StorageEvent event;

    SimpleEventCtx(StorageModuleImpl* i, StorageEvent ev)
        : impl(i), event(ev) {}

    void handleResponse(int ret, const char* msg, size_t len) override {
        emitBasicResponse(impl, event, ret, fromMsg(msg, len), "SimpleEventCtx");
    }
};

// Same as SimpleEventCtx but owns the C-string peer-address array allocated
// by the caller and frees it on destruction.
// JSON payload: {success, message}.
struct ConnectCtx : AsyncCallbackBase {
    StorageModuleImpl* impl;
    std::string peerIdBuf;
    std::vector<char*> addrs;

    ConnectCtx(StorageModuleImpl* i, std::string pid, std::vector<char*> a)
        : impl(i), peerIdBuf(std::move(pid)), addrs(std::move(a)) {}

    ~ConnectCtx() override {
        for (char* p : addrs) free(p);
    }

    void handleResponse(int ret, const char* msg, size_t len) override {
        emitBasicResponse(impl, &StorageModuleImpl::storageConnect, ret,
                          fromMsg(msg, len), "ConnectCtx");
    }
};

// Handles file upload callbacks.
//
// On RET_PROGRESS: accumulates bytes and emits "storageUploadProgress" events
// throttled to at most one per percentage point (max 100 events total) to avoid
// flooding the caller.  When totalBytes is 0 (unknown), every event is forwarded.
// JSON payload: {success:true, sessionId, bytes}
//
// On RET_OK / error: emits "storageUploadDone".
// JSON payload: {success, sessionId, cid} on success; {success:false, sessionId, error} on failure.
struct UploadFileCtx : AsyncCallbackBase {
    StorageModuleImpl* impl;
    std::string sessionId;
    int64_t totalBytes;
    mutable int64_t bytesUploaded = 0;
    mutable int64_t pendingBytes = 0;
    mutable int lastEmittedPercent = -1;

    UploadFileCtx(StorageModuleImpl* i, std::string sid, int64_t total)
        : impl(i), sessionId(std::move(sid)), totalBytes(total) {}

    void handleResponse(int ret, const char* msg, size_t len) override {
        if (ret == RET_PROGRESS) {
            bytesUploaded += static_cast<int64_t>(len);
            pendingBytes  += static_cast<int64_t>(len);
            if (totalBytes > 0) {
                int percent =
                    static_cast<int>((bytesUploaded * 100LL) / totalBytes);
                if (percent <= lastEmittedPercent) return;
                lastEmittedPercent = percent;
            }
            emitSessionProgress(impl, &StorageModuleImpl::storageUploadProgress,
                                sessionId, pendingBytes, "UploadFileCtx");
            pendingBytes = 0;
            return;
        }
        emitSessionResult(impl, &StorageModuleImpl::storageUploadDone, ret,
                          sessionId, fromMsg(msg, len), "cid", "UploadFileCtx");
    }
};

// Handles a single manual chunk upload.
// Emits "storageUploadProgress" on completion.
// JSON payload on success:  {success:true,  sessionId, bytes}
// JSON payload on failure:  {success:false, sessionId, error}
//
// Note: we do NOT cancel the upload session on failure — a failed chunk does
// not corrupt the session, and the caller may choose to retry or abort.
struct UploadChunkCtx : AsyncCallbackBase {
    StorageModuleImpl* impl;
    std::string sessionId;
    std::string chunk;

    UploadChunkCtx(StorageModuleImpl* i, std::string sid, std::string c)
        : impl(i), sessionId(std::move(sid)), chunk(std::move(c)) {}

    void handleResponse(int ret, const char* msg, size_t len) override {
        json j;
        j["success"] = (ret == RET_OK);
        j["sessionId"] = sessionId;
        if (ret == RET_OK) j["bytes"] = static_cast<int64_t>(chunk.size());
        else j["error"] = fromMsg(msg, len);
        emitJsonEvent(impl, &StorageModuleImpl::storageUploadProgress, j,
                      "UploadChunkCtx");
    }
};

// Handles streaming download callbacks.  Operates in two modes depending on
// whether filepath is empty:
//
//   Chunk mode (filepath empty):
//     On RET_PROGRESS: emits "storageDownloadProgress" with the received data
//     base64-encoded in the "chunk" field.  The data MUST be copied and encoded
//     here — the msg pointer is only valid for the duration of this call.
//     Base64 encoding is required because raw download data is arbitrary bytes
//     and nlohmann::json::dump() will throw on invalid UTF-8 sequences.
//     JSON payload: {success:true, sessionId, chunk:<base64>}
//
//   File mode (filepath non-empty):
//     On RET_PROGRESS: accumulates bytes and emits "storageDownloadProgress"
//     throttled to at most one event per percentage point to avoid flooding.
//     JSON payload: {success:true, sessionId, bytes}
//
//   Both modes on completion:
//     Emits "storageDownloadDone".
//     JSON payload on success:  {success:true,  sessionId}
//     JSON payload on failure:  {success:false, sessionId, error}
struct DownloadStreamCtx : AsyncCallbackBase {
    StorageModuleImpl* impl;
    std::string cid;
    std::string filepath;
    int64_t totalBytes;
    mutable int64_t bytesDownloaded = 0;
    mutable int64_t pendingBytes = 0;
    mutable int lastEmittedPercent = -1;

    DownloadStreamCtx(StorageModuleImpl* i, std::string c, std::string fp,
                      int64_t total = 0)
        : impl(i), cid(std::move(c)), filepath(std::move(fp)),
          totalBytes(total) {}

    void handleResponse(int ret, const char* msg, size_t len) override {
        if (ret == RET_PROGRESS) {
            if (filepath.empty()) {
                // Chunk mode — base64-encode the raw bytes so they can be
                // safely embedded in JSON.  The pointer is only valid here.
                json j;
                j["success"] = true;
                j["sessionId"] = cid;
                j["chunk"] = base64Encode(msg, len);
                emitJsonEvent(impl, &StorageModuleImpl::storageDownloadProgress,
                              j, "DownloadStreamCtx");
            } else {
                // File mode — report byte count, throttled to one event per
                // percentage point so large files don't flood the caller.
                bytesDownloaded += static_cast<int64_t>(len);
                pendingBytes    += static_cast<int64_t>(len);
                if (totalBytes > 0) {
                    int percent = static_cast<int>(
                        (bytesDownloaded * 100LL) / totalBytes);
                    if (percent <= lastEmittedPercent) return;
                    lastEmittedPercent = percent;
                }
                emitSessionProgress(impl,
                                    &StorageModuleImpl::storageDownloadProgress,
                                    cid, pendingBytes, "DownloadStreamCtx");
                pendingBytes = 0;
            }
            return;
        }
        emitSessionResult(impl, &StorageModuleImpl::storageDownloadDone, ret,
                          cid, fromMsg(msg, len), "", "DownloadStreamCtx");
    }
};

// ---------------------------------------------------------------------------
// syncCall wrappers — shorthand for the synchronous wait pattern.
//
// Each variant:
//   1. Allocates a SyncCtx on the heap.
//   2. Stores any string argument in ctx->lifetimeArg to keep it alive for
//      the duration of the async C call.
//   3. Issues the libstorage command; on immediate failure, deletes ctx and
//      returns an error.
//   4. Calls waitSync() to block until the callback fires or timeout expires.
// ---------------------------------------------------------------------------

using StorageNoArgFn = int (*)(void*, StorageCallback, void*);
using StorageBoolFn = int (*)(void*, bool, StorageCallback, void*);
using StorageStringFn = int (*)(void*, const char*, StorageCallback, void*);
using StorageStringIntFn = int (*)(void*, const char*, size_t, StorageCallback, void*);
using StorageDownloadInitFn =
    int (*)(void*, const char*, size_t, bool, StorageCallback, void*);

static SyncResult syncCallNoArg(void* ctx, StorageNoArgFn fn, int timeoutMs) {
    if (!ctx) return {false, "Storage context not initialized."};
    auto* sctx = new SyncCtx();
    if (fn(ctx, syncCallback, sctx) != RET_OK) {
        delete sctx;
        return {false, "Failed to send command."};
    }
    return waitSync(sctx, timeoutMs);
}

static SyncResult syncCallBool(void* ctx, StorageBoolFn fn, bool arg, int timeoutMs) {
    if (!ctx) return {false, "Storage context not initialized."};
    auto* sctx = new SyncCtx();
    if (fn(ctx, arg, syncCallback, sctx) != RET_OK) {
        delete sctx;
        return {false, "Failed to send command."};
    }
    return waitSync(sctx, timeoutMs);
}

static SyncResult syncCallString(void* ctx, StorageStringFn fn,
                                  const std::string& arg, int timeoutMs) {
    if (!ctx) return {false, "Storage context not initialized."};
    auto* sctx = new SyncCtx();
    sctx->lifetimeArg = arg;
    if (fn(ctx, sctx->lifetimeArg.c_str(), syncCallback, sctx) != RET_OK) {
        delete sctx;
        return {false, "Failed to send command."};
    }
    return waitSync(sctx, timeoutMs);
}

static SyncResult syncCallStringAndSize(void* ctx, StorageStringIntFn fn,
                                      const std::string& arg, size_t n,
                                      int timeoutMs) {
    if (!ctx) return {false, "Storage context not initialized."};
    auto* sctx = new SyncCtx();
    sctx->lifetimeArg = arg;
    if (fn(ctx, sctx->lifetimeArg.c_str(), n, syncCallback, sctx) != RET_OK) {
        delete sctx;
        return {false, "Failed to send command."};
    }
    return waitSync(sctx, timeoutMs);
}

static SyncResult syncCallDownloadInit(void* ctx, StorageDownloadInitFn fn,
                                        const std::string& cid, size_t chunkSize,
                                        bool local, int timeoutMs) {
    if (!ctx) return {false, "Storage context not initialized."};
    auto* sctx = new SyncCtx();
    sctx->lifetimeArg = cid;
    if (fn(ctx, sctx->lifetimeArg.c_str(), chunkSize, local, syncCallback, sctx) !=
        RET_OK) {
        delete sctx;
        return {false, "Failed to send command."};
    }
    return waitSync(sctx, timeoutMs);
}

// ---------------------------------------------------------------------------
// StorageModuleImpl
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool StorageModuleImpl::init(const std::string& cfg) {
    fprintf(stderr, "StorageModuleImpl::init called\n");

    auto* sctx = new SyncCtx();
    storageCtx = storage_new(cfg.c_str(), syncCallback, sctx);
    SyncResult r = waitSync(sctx, 1000);

    if (!r.ok || !storageCtx) {
        fprintf(stderr, "StorageModuleImpl::init failed: %s\n",
                r.message.c_str());
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
    if (storage_start(storageCtx, asyncCallback, ctx) != RET_OK) {
        delete ctx;
        return false;
    }
    return true;
}

StdLogosResult StorageModuleImpl::stop() {
    fprintf(stderr, "StorageModuleImpl::stop called\n");
    if (!storageCtx)
        return {false, {}, "Storage context not initialized."};
    auto* ctx = new SimpleEventCtx(this, &StorageModuleImpl::storageStop);
    if (storage_stop(storageCtx, asyncCallback, ctx) != RET_OK) {
        delete ctx;
        return {false, {}, "Failed to send stop command."};
    }
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::destroy() {
    fprintf(stderr, "StorageModuleImpl::destroy called\n");
    if (!storageCtx)
        return {false, {}, "Storage context not initialized."};
    syncCallNoArg(storageCtx, storage_close, 1000);
    int ret = storage_destroy(storageCtx);
    if (ret == RET_OK) {
        storageCtx = nullptr;
        return {true, {}, ""};
    }
    return {false, {}, "Failed to destroy storage context."};
}

// ---------------------------------------------------------------------------
// Info
// ---------------------------------------------------------------------------

StdLogosResult StorageModuleImpl::version() {
    if (!storageCtx)
        return {false, {}, "Storage context not initialized."};
    char* v = storage_version(storageCtx);
    if (!v) return {false, {}, "Failed to get version."};
    std::string result(v);
    free(v);
    return {true, result, ""};
}

std::string StorageModuleImpl::moduleVersion() {
    return STORAGE_MODULE_VERSION;
}

StdLogosResult StorageModuleImpl::dataDir() {
    auto r = syncCallNoArg(storageCtx, storage_repo, 1000);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message, ""};
}

StdLogosResult StorageModuleImpl::peerId() {
    auto r = syncCallNoArg(storageCtx, storage_peer_id, 1000);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message, ""};
}

StdLogosResult StorageModuleImpl::spr() {
    auto r = syncCallNoArg(storageCtx, storage_spr, 1000);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message, ""};
}

StdLogosResult StorageModuleImpl::debug() {
    auto r = syncCallNoArg(storageCtx, storage_debug, 1000);
    if (!r.ok) return {false, {}, r.message};
    try {
        return {true, json::parse(r.message), ""};
    } catch (...) {
        return {false, {}, "Failed to parse debug info."};
    }
}

LogosMap StorageModuleImpl::collectMetrics() {
    auto emptyMetrics = [] { return json{{"metrics", json::array()}}; };

    auto r = syncCallNoArg(storageCtx, storage_get_metrics, 1000);
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
    auto r = syncCallString(storageCtx, storage_log_level, logLevel, 1000);
    if (!r.ok) return {false, {}, r.message};
    return {true, {}, ""};
}

// ---------------------------------------------------------------------------
// Connect
// ---------------------------------------------------------------------------

StdLogosResult StorageModuleImpl::connect(const std::string& peerId,
                                           const std::vector<std::string>& peerAddresses) {
    if (!storageCtx)
        return {false, {}, "Storage context not initialized."};
    std::vector<char*> addrs;
    addrs.reserve(peerAddresses.size());
    for (const auto& a : peerAddresses) addrs.push_back(strdup(a.c_str()));

    auto* ctx = new ConnectCtx(this, peerId, addrs);
    if (storage_connect(storageCtx, ctx->peerIdBuf.c_str(),
                        const_cast<const char**>(ctx->addrs.data()),
                        ctx->addrs.size(), asyncCallback, ctx) != RET_OK) {
        delete ctx;
        return {false, {}, "Failed to send connect command."};
    }
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::togglePrivateQueries(bool enabled) {
    auto r = syncCallBool(storageCtx, storage_toggle_private_queries, enabled, 1000);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message == "true", ""};
}

// ---------------------------------------------------------------------------
// Upload
// ---------------------------------------------------------------------------

StdLogosResult StorageModuleImpl::uploadInit(const std::string& filename,
                                              int64_t chunkSize) {
    auto r = syncCallStringAndSize(storageCtx, storage_upload_init, filename,
                                static_cast<size_t>(chunkSize), 1000);
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

    int64_t fileSize = static_cast<int64_t>(fs::file_size(filePath, ec));

    auto ir = syncCallStringAndSize(storageCtx, storage_upload_init, filePath,
                                  static_cast<size_t>(chunkSize), 1000);
    if (!ir.ok)
        return {false, {}, ir.message};
    std::string sessionId = ir.message;

    auto* ctx = new UploadFileCtx(this, sessionId, fileSize);
    if (storage_upload_file(storageCtx, ctx->sessionId.c_str(),
                            asyncCallback, ctx) != RET_OK) {
        delete ctx;
        syncCallString(storageCtx, storage_upload_cancel, sessionId, 1000);
        return {false, {}, "Failed to start file upload."};
    }
    return {true, sessionId, ""};
}

StdLogosResult StorageModuleImpl::uploadChunk(const std::string& sessionId,
                                               const std::string& chunk) {
    if (!storageCtx)
        return {false, {}, "Storage context not initialized."};
    auto* ctx = new UploadChunkCtx(this, sessionId, chunk);
    const auto* data = reinterpret_cast<const uint8_t*>(ctx->chunk.data());
    if (storage_upload_chunk(storageCtx, ctx->sessionId.c_str(), data,
                             ctx->chunk.size(), asyncCallback, ctx) != RET_OK) {
        delete ctx;
        return {false, {}, "Failed to send chunk."};
    }
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::uploadFinalize(const std::string& sessionId) {
    auto r = syncCallString(storageCtx, storage_upload_finalize, sessionId, 1000);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message, ""};
}

StdLogosResult StorageModuleImpl::uploadCancel(const std::string& sessionId) {
    auto r = syncCallString(storageCtx, storage_upload_cancel, sessionId, 1000);
    if (!r.ok) return {false, {}, r.message};
    return {true, {}, ""};
}

// ---------------------------------------------------------------------------
// Download
// ---------------------------------------------------------------------------

std::string StorageModuleImpl::downloadChunksInternal(const std::string& cid,
                                                       const std::string& filepath,
                                                       bool local,
                                                       int64_t chunkSize) {
    if (!storageCtx || chunkSize <= 0) return {};

    // For file-mode download, fetch the manifest to determine total size so
    // that progress events can be throttled to one per percentage point.
    int64_t totalBytes = 0;
    if (!filepath.empty()) {
        auto mr = syncCallString(storageCtx, storage_download_manifest, cid, 3000);
        if (mr.ok) {
            try {
                json manifest = json::parse(mr.message);
                if (manifest.contains("datasetSize") && !manifest["datasetSize"].is_null()) {
                    const auto& ds = manifest["datasetSize"];
                    if (ds.is_number_integer()) totalBytes = ds.get<int64_t>();
                    else if (ds.is_number()) totalBytes = static_cast<int64_t>(ds.get<double>());
                    else if (ds.is_string()) totalBytes = std::stoll(ds.get_ref<const std::string&>());
                }
            } catch (const std::exception& e) {
                fprintf(stderr,
                        "StorageModuleImpl::downloadChunksInternal: failed to parse manifest for %s: %s\n",
                        cid.c_str(), e.what());
            } catch (...) {
                fprintf(stderr,
                        "StorageModuleImpl::downloadChunksInternal: failed to parse manifest for %s (unknown error)\n",
                        cid.c_str());
            }
        }
        if (totalBytes == 0) {
            fprintf(stderr,
                    "StorageModuleImpl::downloadChunksInternal: failed to get "
                    "manifest for %s\n",
                    cid.c_str());
            return {};
        }
    }

    auto r = syncCallDownloadInit(storageCtx, storage_download_init, cid,
                                  static_cast<size_t>(chunkSize), local, 1000);
    if (!r.ok) return {};

    auto* ctx = new DownloadStreamCtx(this, cid, filepath, totalBytes);
    if (storage_download_stream(storageCtx, ctx->cid.c_str(),
                                static_cast<size_t>(chunkSize), local,
                                ctx->filepath.c_str(),
                                asyncCallback, ctx) != RET_OK) {
        delete ctx;
        syncCallString(storageCtx, storage_download_cancel, cid, 1000);
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
    auto r = syncCallString(storageCtx, storage_download_cancel, sessionId, 1000);
    if (!r.ok) return {false, {}, r.message};
    return {true, {}, ""};
}

// ---------------------------------------------------------------------------
// Data management
// ---------------------------------------------------------------------------

StdLogosResult StorageModuleImpl::exists(const std::string& cid) {
    auto r = syncCallString(storageCtx, storage_exists, cid, 1000);
    if (!r.ok) return {false, {}, r.message};
    return {true, r.message == "true", ""};
}

StdLogosResult StorageModuleImpl::fetch(const std::string& cid) {
    auto r = syncCallString(storageCtx, storage_fetch, cid, 3000);
    if (!r.ok) return {false, {}, r.message};
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::remove(const std::string& cid) {
    auto r = syncCallString(storageCtx, storage_delete, cid, 3000);
    if (!r.ok) return {false, {}, r.message};
    return {true, {}, ""};
}

StdLogosResult StorageModuleImpl::space() {
    auto r = syncCallNoArg(storageCtx, storage_space, 1000);
    if (!r.ok) return {false, {}, r.message};
    try {
        return {true, json::parse(r.message), ""};
    } catch (...) {
        return {false, {}, "Failed to parse space info."};
    }
}

StdLogosResult StorageModuleImpl::manifests() {
    auto r = syncCallNoArg(storageCtx, storage_list, 1000);
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
    auto r = syncCallString(storageCtx, storage_download_manifest, cid, 3000);
    if (!r.ok) return {false, {}, r.message};
    try {
        return {true, json::parse(r.message), ""};
    } catch (...) {
        return {false, {}, "Failed to parse manifest."};
    }
}

// ---------------------------------------------------------------------------
// importFiles (headless helper)
// ---------------------------------------------------------------------------

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
