// Replaces the real libstorage at link time; every reply fires synchronously, so waitSync never waits.

#include <logos_clib_mock.h>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#include <libstorage.h>

// Sentinel address used as a fake non-null storage context.
static char s_fakeCtx = 0;

static StorageCtx* fakeCtx() {
    return reinterpret_cast<StorageCtx*>(&s_fakeCtx);
}

static StorageOnUploadProgressFn s_onUploadProgress = nullptr;
static void* s_onUploadProgressUserData = nullptr;
static StorageOnDownloadChunkFn s_onDownloadChunk = nullptr;
static void* s_onDownloadChunkUserData = nullptr;

static bool s_holdTransferReplies = false;
static StorageStrReplyFn s_heldReply = nullptr;
static void* s_heldUserData = nullptr;

// Invoke the reply callback with RET_OK and the string from the mock store.
static void invokeOk(const char* funcName, StorageStrReplyFn cb, void* userData) {
    if (!cb) return;
    const char* msg = LogosCMockStore::instance().getReturnString(funcName);
    NimFfiStr reply = nimffi_str(msg ? msg : "");
    cb(RET_OK, &reply, nullptr, userData);
}

// Mirrors libstorage: always invokes the callback with RET_ERR.
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

extern "C" {

const char* storage_version(void) {
    LOGOS_CMOCK_RECORD("storage_version");
    const char* ret = LOGOS_CMOCK_RETURN_STRING("storage_version");
    return ret ? ret : "0.0.0-mock";
}

int storage_ctx_create(NimFfiStr configJson, StorageCreateFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_create");
    // Unset return defaults to RET_OK.
    int rc = LOGOS_CMOCK_RETURN(int, "storage_ctx_create");
    if (!cb) return rc;
    if (rc == RET_OK) cb(RET_OK, fakeCtx(), nullptr, userData);
    else cb(rc, nullptr, "libstorage error: mock failure", userData);
    return rc;
}

int storage_ctx_destroy(StorageCtx* ctx) {
    LOGOS_CMOCK_RECORD("storage_ctx_destroy");
    return RET_OK;
}

uint64_t storage_ctx_add_on_download_chunk_listener(StorageCtx* ctx,
                                                    StorageOnDownloadChunkFn fn,
                                                    void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_add_on_download_chunk_listener");
    s_onDownloadChunk = fn;
    s_onDownloadChunkUserData = userData;
    return 1;
}

uint64_t storage_ctx_add_on_upload_progress_listener(StorageCtx* ctx,
                                                     StorageOnUploadProgressFn fn,
                                                     void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_add_on_upload_progress_listener");
    s_onUploadProgress = fn;
    s_onUploadProgressUserData = userData;
    return 1;
}

void mock_fire_upload_progress(const char* sessionId, int64_t storedBytes) {
    if (!s_onUploadProgress) return;
    OnUploadProgressPayload evt;
    evt.sessionId = nimffi_str(sessionId);
    evt.storedBytes = storedBytes;
    s_onUploadProgress(&evt, s_onUploadProgressUserData);
}

void mock_fire_download_chunk(const char* cid, const uint8_t* data, size_t len) {
    if (!s_onDownloadChunk) return;
    OnDownloadChunkPayload evt;
    evt.cid = nimffi_str(cid);
    evt.data.data = const_cast<uint8_t*>(data);
    evt.data.len = len;
    s_onDownloadChunk(&evt, s_onDownloadChunkUserData);
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
        NimFfiStr reply = nimffi_str("");
        cb(RET_OK, &reply, nullptr, userData);
    }
}

// No-arg commands

int storage_ctx_start(const StorageCtx* ctx, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_start");
    invokeOk("storage_ctx_start", cb, userData);
    return RET_OK;
}

int storage_ctx_stop(const StorageCtx* ctx, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_stop");
    invokeOk("storage_ctx_stop", cb, userData);
    return RET_OK;
}

int storage_ctx_repo(const StorageCtx* ctx, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_repo");
    invokeOk("storage_ctx_repo", cb, userData);
    return RET_OK;
}

int storage_ctx_peer_id(const StorageCtx* ctx, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_peer_id");
    invokeOk("storage_ctx_peer_id", cb, userData);
    return RET_OK;
}

int storage_ctx_spr(const StorageCtx* ctx, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_spr");
    invokeOk("storage_ctx_spr", cb, userData);
    return RET_OK;
}

int storage_ctx_debug(const StorageCtx* ctx, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_debug");
    invokeOk("storage_ctx_debug", cb, userData);
    return RET_OK;
}

int storage_ctx_get_metrics(const StorageCtx* ctx, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_get_metrics");
    const char* msg = LogosCMockStore::instance().getReturnString("storage_ctx_get_metrics");
    if (msg && *msg) {
        invokeOk("storage_ctx_get_metrics", cb, userData);
        return RET_OK;
    }

    // Unset return defaults to RET_OK.
    int rc = LOGOS_CMOCK_RETURN(int, "storage_ctx_get_metrics");
    if (rc == RET_OK) invokeOk("storage_ctx_get_metrics", cb, userData);
    else invokeErr(cb, userData);
    return rc;
}

int storage_ctx_space(const StorageCtx* ctx, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_space");
    invokeOk("storage_ctx_space", cb, userData);
    return RET_OK;
}

int storage_ctx_list(const StorageCtx* ctx, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_list");
    invokeOk("storage_ctx_list", cb, userData);
    return RET_OK;
}

// String-arg commands

int storage_ctx_log_level(const StorageCtx* ctx, NimFfiStr logLevel,
                          StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_log_level");
    invokeOk("storage_ctx_log_level", cb, userData);
    return RET_OK;
}

int storage_ctx_exists(const StorageCtx* ctx, NimFfiStr cid,
                       StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_exists");
    invokeOk("storage_ctx_exists", cb, userData);
    return RET_OK;
}

int storage_ctx_fetch(const StorageCtx* ctx, NimFfiStr cid,
                      StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_fetch");
    invokeOk("storage_ctx_fetch", cb, userData);
    return RET_OK;
}

int storage_ctx_delete(const StorageCtx* ctx, NimFfiStr cid,
                       StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_delete");
    invokeOk("storage_ctx_delete", cb, userData);
    return RET_OK;
}

int storage_ctx_download_manifest(const StorageCtx* ctx, NimFfiStr cid,
                                  StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_download_manifest");
    invokeOk("storage_ctx_download_manifest", cb, userData);
    return RET_OK;
}

int storage_ctx_download_cancel(const StorageCtx* ctx, NimFfiStr cid,
                                StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_download_cancel");
    invokeOk("storage_ctx_download_cancel", cb, userData);
    return RET_OK;
}

int storage_ctx_upload_file(const StorageCtx* ctx, NimFfiStr sessionId,
                            StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_upload_file");
    // Unset return defaults to RET_OK.
    int rc = LOGOS_CMOCK_RETURN(int, "storage_ctx_upload_file");
    if (rc != RET_OK) {
        invokeErr(cb, userData);
        return rc;
    }
    if (holdReply(cb, userData)) return RET_OK;
    invokeOk("storage_ctx_upload_file", cb, userData);
    return RET_OK;
}

int storage_ctx_upload_finalize(const StorageCtx* ctx, NimFfiStr sessionId,
                                StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_upload_finalize");
    invokeOk("storage_ctx_upload_finalize", cb, userData);
    return RET_OK;
}

int storage_ctx_upload_cancel(const StorageCtx* ctx, NimFfiStr sessionId,
                              StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_upload_cancel");
    invokeOk("storage_ctx_upload_cancel", cb, userData);
    return RET_OK;
}

int storage_ctx_toggle_private_queries(const StorageCtx* ctx, bool enabled,
                                       StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_toggle_private_queries");
    invokeOk("storage_ctx_toggle_private_queries", cb, userData);
    return RET_OK;
}

// Multi-arg commands

int storage_ctx_connect(const StorageCtx* ctx, NimFfiStr peerId,
                        const StorageSeq_Str* peerAddresses,
                        StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_connect");
    invokeOk("storage_ctx_connect", cb, userData);
    return RET_OK;
}

int storage_ctx_upload_init(const StorageCtx* ctx, NimFfiStr filepath, uint64_t chunkSize,
                            StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_upload_init");
    invokeOk("storage_ctx_upload_init", cb, userData);
    return RET_OK;
}

int storage_ctx_upload_chunk(const StorageCtx* ctx, NimFfiStr sessionId,
                             const NimFfiBytes* data,
                             StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_upload_chunk");
    invokeOk("storage_ctx_upload_chunk", cb, userData);
    return RET_OK;
}

int storage_ctx_download_init(const StorageCtx* ctx, NimFfiStr cid, uint64_t chunkSize,
                              bool local, StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_download_init");
    invokeOk("storage_ctx_download_init", cb, userData);
    return RET_OK;
}

int storage_ctx_download_stream(const StorageCtx* ctx, NimFfiStr cid, uint64_t chunkSize,
                                bool local, NimFfiStr filepath,
                                StorageStrReplyFn cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_ctx_download_stream");
    // Unset return defaults to RET_OK.
    int rc = LOGOS_CMOCK_RETURN(int, "storage_ctx_download_stream");
    if (rc != RET_OK) {
        invokeErr(cb, userData);
        return rc;
    }
    if (holdReply(cb, userData)) return RET_OK;
    invokeOk("storage_ctx_download_stream", cb, userData);
    return RET_OK;
}

} // extern "C"
