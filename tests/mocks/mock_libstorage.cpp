// Replaces the real libstorage at link time; every reply fires synchronously, so waitSync never waits.

#include <logos_clib_mock.h>
#include <cstdint>
#include <map>
#include <string>
#include <nlohmann/json.hpp>

#include <libstorage.h>

// Last arguments by C entry point, for forwarding regression tests.
std::map<std::string, nlohmann::json> storageMockArgs;

// Sentinel address used as a fake non-null storage context.
static char s_fakeCtx = 0;

static StorageCtx* fakeCtx() {
    return reinterpret_cast<StorageCtx*>(&s_fakeCtx);
}

static StorageOnUploadProgressFn s_onUploadProgress = nullptr;
static void* s_onUploadProgressUserData = nullptr;
static StorageOnDownloadChunkFn s_onDownloadChunk = nullptr;
static void* s_onDownloadChunkUserData = nullptr;
static StorageOnDownloadDoneFn s_onDownloadDone = nullptr;
static void* s_onDownloadDoneUserData = nullptr;

static bool s_holdTransferReplies = false;
static StorageStrReplyFn s_heldReply = nullptr;
static void* s_heldUserData = nullptr;

static void invokeOk(const char* funcName, StorageStrReplyFn cb, void* userData) {
    if (!cb) return;
    const char* msg = LogosCMockStore::instance().getReturnString(funcName);
    const char* reply = msg ? msg : "";
    cb(RET_OK, &reply, nullptr, userData);
}

static void invokeErr(StorageStrReplyFn cb, void* userData) {
    if (!cb) return;
    cb(RET_ERR, nullptr, "libstorage error: mock failure", userData);
}

static bool holdReply(StorageStrReplyFn cb, void* userData) {
    if (!s_holdTransferReplies) return false;
    s_heldReply = cb;
    s_heldUserData = userData;
    return true;
}

// The variadic part is the parameters between ctx and the callback, each with a trailing comma.
#define MOCK_CMD(fn, ...)                                                                \
    int fn(const StorageCtx* ctx, __VA_ARGS__ StorageStrReplyFn cb, void* userData) {    \
        LOGOS_CMOCK_RECORD(#fn);                                                         \
        if (LOGOS_CMOCK_RETURN(int, #fn "_reply_err")) {                                 \
            invokeErr(cb, userData);                                                     \
            return RET_OK;                                                               \
        }                                                                                \
        invokeOk(#fn, cb, userData);                                                     \
        return RET_OK;                                                                   \
    }

extern "C" {

const char* storage_version(void) {
    LOGOS_CMOCK_RECORD("storage_version");
    const char* ret = LOGOS_CMOCK_RETURN_STRING("storage_version");
    return ret ? ret : "0.0.0-mock";
}

static StorageCreateFn s_heldCreate = nullptr;
static void* s_heldCreateUserData = nullptr;

// Answers a create that storage_ctx_create_pending left unanswered.
void mock_release_create(int rc) {
    StorageCreateFn cb = s_heldCreate;
    void* userData = s_heldCreateUserData;
    s_heldCreate = nullptr;
    s_heldCreateUserData = nullptr;
    if (!cb) return;
    if (rc == RET_OK) cb(RET_OK, fakeCtx(), nullptr, userData);
    else cb(rc, nullptr, "libstorage error: mock failure", userData);
}

int storage_ctx_create(const char* configJson, StorageCreateFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_create");
    if (LOGOS_CMOCK_RETURN(int, "storage_ctx_create_pending")) {
        s_heldCreate = cb;
        s_heldCreateUserData = userData;
        return RET_OK;
    }
    // Unset return defaults to RET_OK.
    int rc = LOGOS_CMOCK_RETURN(int, "storage_ctx_create");
    if (!cb) return rc;
    if (rc == RET_OK) cb(RET_OK, fakeCtx(), nullptr, userData);
    else cb(rc, nullptr, "libstorage error: mock failure", userData);
    return rc;
}

int storage_ctx_destroy(StorageCtx* ctx) {
    LOGOS_CMOCK_RECORD("storage_ctx_destroy");
    return LOGOS_CMOCK_RETURN(int, "storage_ctx_destroy");
}

uint64_t storage_ctx_add_on_download_chunk_listener(StorageCtx* ctx,
                                                    StorageOnDownloadChunkFn fn,
                                                    void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_add_on_download_chunk_listener");
    if (LOGOS_CMOCK_RETURN(int, "storage_ctx_add_on_download_chunk_listener_fails")) return 0;
    s_onDownloadChunk = fn;
    s_onDownloadChunkUserData = userData;
    return 1;
}

uint64_t storage_ctx_add_on_download_done_listener(StorageCtx* ctx,
                                                   StorageOnDownloadDoneFn fn,
                                                   void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_add_on_download_done_listener");
    if (LOGOS_CMOCK_RETURN(int, "storage_ctx_add_on_download_done_listener_fails")) return 0;
    s_onDownloadDone = fn;
    s_onDownloadDoneUserData = userData;
    return 1;
}

uint64_t storage_ctx_add_on_upload_progress_listener(StorageCtx* ctx,
                                                     StorageOnUploadProgressFn fn,
                                                     void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_add_on_upload_progress_listener");
    if (LOGOS_CMOCK_RETURN(int, "storage_ctx_add_on_upload_progress_listener_fails")) return 0;
    s_onUploadProgress = fn;
    s_onUploadProgressUserData = userData;
    return 1;
}

void mock_fire_upload_progress(const char* sessionId, int64_t storedBytes) {
    if (!s_onUploadProgress) return;
    OnUploadProgressPayload evt;
    evt.sessionId = sessionId;
    evt.storedBytes = storedBytes;
    s_onUploadProgress(&evt, s_onUploadProgressUserData);
}

void mock_fire_download_chunk(const char* cid, const uint8_t* data, size_t len) {
    if (!s_onDownloadChunk) return;
    OnDownloadChunkPayload evt;
    evt.cid = cid;
    evt.data.data = const_cast<uint8_t*>(data);
    evt.data.len = len;
    s_onDownloadChunk(&evt, s_onDownloadChunkUserData);
}

void mock_fire_download_done(const char* cid, const char* error) {
    if (!s_onDownloadDone) return;
    OnDownloadDonePayload evt;
    evt.cid = cid;
    evt.error = error;
    s_onDownloadDone(&evt, s_onDownloadDoneUserData);
}

// A held transfer stays tracked, which is what the progress listeners need.
void mock_hold_transfer_replies(bool hold) {
    s_holdTransferReplies = hold;
    if (!hold) {
        s_heldReply = nullptr;
        s_heldUserData = nullptr;
    }
}

void mock_release_transfer_reply(void) {
    StorageStrReplyFn cb = s_heldReply;
    void* userData = s_heldUserData;
    s_heldReply = nullptr;
    s_heldUserData = nullptr;
    if (cb) {
        const char* reply = "";
        cb(RET_OK, &reply, nullptr, userData);
    }
}

int storage_ctx_start(const StorageCtx* ctx, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_start");

    // Leaves the node starting: the callback never fires.
    if (LOGOS_CMOCK_RETURN(int, "storage_ctx_start_pending")) {
        return RET_OK;
    }

    invokeOk("storage_ctx_start", cb, userData);
    return RET_OK;
}

MOCK_CMD(storage_ctx_stop)
MOCK_CMD(storage_ctx_repo)
MOCK_CMD(storage_ctx_peer_id)
MOCK_CMD(storage_ctx_spr)
MOCK_CMD(storage_ctx_network)
MOCK_CMD(storage_ctx_debug)
MOCK_CMD(storage_ctx_space)
MOCK_CMD(storage_ctx_list)
MOCK_CMD(storage_ctx_log_level, const char* logLevel,)
MOCK_CMD(storage_ctx_exists, const char* cid,)
MOCK_CMD(storage_ctx_delete, const char* cid,)
MOCK_CMD(storage_ctx_download_cancel, const char* cid,)
MOCK_CMD(storage_ctx_upload_finalize, const char* sessionId,)
MOCK_CMD(storage_ctx_upload_cancel, const char* sessionId,)
MOCK_CMD(storage_ctx_connect, const char* peerId, const StorageSeq_Str* peerAddresses,)
MOCK_CMD(storage_ctx_upload_chunk, const char* sessionId, const NimFfiBytes* data,)

int storage_ctx_get_metrics(const StorageCtx* ctx, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_get_metrics");
    const char* msg = LogosCMockStore::instance().getReturnString("storage_ctx_get_metrics");
    if (msg && *msg) {
        invokeOk("storage_ctx_get_metrics", cb, userData);
        return RET_OK;
    }

    int rc = LOGOS_CMOCK_RETURN(int, "storage_ctx_get_metrics");
    if (rc == RET_OK) invokeOk("storage_ctx_get_metrics", cb, userData);
    else invokeErr(cb, userData);
    return rc;
}

int storage_ctx_fetch(const StorageCtx* ctx, const char* cid, bool isPrivate, bool advertise,
                      StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_fetch");
    storageMockArgs["storage_ctx_fetch"] = {{"cid", cid}, {"isPrivate", isPrivate}, {"advertise", advertise}};
    invokeOk("storage_ctx_fetch", cb, userData);
    return RET_OK;
}

int storage_ctx_download_manifest(const StorageCtx* ctx, const char* cid, bool isPrivate,
                                  bool advertise, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_download_manifest");
    storageMockArgs["storage_ctx_download_manifest"] = {{"cid", cid}, {"isPrivate", isPrivate}, {"advertise", advertise}};
    invokeOk("storage_ctx_download_manifest", cb, userData);
    return RET_OK;
}

int storage_ctx_upload_file(const StorageCtx* ctx, const char* sessionId,
                            StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_upload_file");
    int rc = LOGOS_CMOCK_RETURN(int, "storage_ctx_upload_file");
    if (rc != RET_OK) {
        invokeErr(cb, userData);
        return rc;
    }
    if (holdReply(cb, userData)) return RET_OK;
    invokeOk("storage_ctx_upload_file", cb, userData);
    return RET_OK;
}

int storage_ctx_get_advertise(const StorageCtx* ctx, const char* cid,
                              StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_get_advertise");
    storageMockArgs["storage_ctx_get_advertise"] = {{"cid", cid}};
    const char* msg = LOGOS_CMOCK_RETURN_STRING("storage_ctx_get_advertise");
    int rc = (msg && *msg) ? RET_OK : LOGOS_CMOCK_RETURN(int, "storage_ctx_get_advertise");
    if (rc != RET_OK) {
        invokeErr(cb, userData);
        return rc;
    }
    invokeOk("storage_ctx_get_advertise", cb, userData);
    return RET_OK;
}

int storage_ctx_set_advertise(const StorageCtx* ctx, const char* cid, bool advertise,
                              StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_set_advertise");
    storageMockArgs["storage_ctx_set_advertise"] = {{"cid", cid}, {"advertise", advertise}};
    const char* msg = LOGOS_CMOCK_RETURN_STRING("storage_ctx_set_advertise");
    int rc = (msg && *msg) ? RET_OK : LOGOS_CMOCK_RETURN(int, "storage_ctx_set_advertise");
    if (rc != RET_OK) {
        invokeErr(cb, userData);
        return rc;
    }
    invokeOk("storage_ctx_set_advertise", cb, userData);
    return RET_OK;
}

int storage_ctx_upload_init(const StorageCtx* ctx, const char* filepath, uint64_t chunkSize,
                            bool advertise, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_upload_init");
    storageMockArgs["storage_ctx_upload_init"] = {{"filename", filepath}, {"chunkSize", chunkSize}, {"advertise", advertise}};
    invokeOk("storage_ctx_upload_init", cb, userData);
    return RET_OK;
}

int storage_ctx_download_init(const StorageCtx* ctx, const char* cid, uint64_t chunkSize,
                              bool local, bool isPrivate, bool advertise,
                              StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_download_init");
    storageMockArgs["storage_ctx_download_init"] = {{"cid", cid}, {"chunkSize", chunkSize}, {"local", local}, {"isPrivate", isPrivate}, {"advertise", advertise}};
    invokeOk("storage_ctx_download_init", cb, userData);
    return RET_OK;
}

int storage_ctx_download_stream(const StorageCtx* ctx, const char* cid, uint64_t chunkSize,
                                const char* filepath,
                                StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_download_stream");
    int rc = LOGOS_CMOCK_RETURN(int, "storage_ctx_download_stream");
    if (rc != RET_OK) {
        invokeErr(cb, userData);
        return rc;
    }
    if (holdReply(cb, userData)) return RET_OK;
    invokeOk("storage_ctx_download_stream", cb, userData);
    mock_fire_download_done(cid, "");
    return RET_OK;
}

} // extern "C"
