// Mock implementation of libstorage C functions.
// Replaces the real Nim library at link time during unit tests.
//
// All functions invoke the callback immediately (synchronously) so that
// syncDispatch can signal the condvar before waitSync starts waiting,
// and asyncDispatch can dispatch events without needing a separate thread.
// Return values and callback messages are controlled via LogosCMockStore:
//   t.mockCFunction("storage_peer_id").returns("QmTestPeerId");

#include <logos_clib_mock.h>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#define RET_OK 0
#define RET_ERR 1
#define RET_PROGRESS 3

typedef void (*StorageCallback)(int callerRet, const char *msg, size_t len, void *userData);

// Sentinel address used as a fake non-null storage context.
static char s_fakeCtx = 0;

// Helper: invoke callback with RET_OK and the string from the mock store.
static void invokeOk(const char* funcName, StorageCallback cb, void* userData) {
    if (!cb) return;
    const char* msg = LogosCMockStore::instance().getReturnString(funcName);
    cb(RET_OK, msg ? msg : "", msg ? strlen(msg) : 0, userData);
}

extern "C" {

void* storage_new(const char* configJson, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_new");
    int ok = LOGOS_CMOCK_RETURN(int, "storage_new");
    if (ok && cb) {
        cb(RET_OK, "", 0, userData);
    }
    return ok ? static_cast<void*>(&s_fakeCtx) : nullptr;
}

char* storage_version(void* ctx) {
    LOGOS_CMOCK_RECORD("storage_version");
    const char* ret = LOGOS_CMOCK_RETURN_STRING("storage_version");
    return strdup(ret ? ret : "0.0.0-mock");
}

int storage_destroy(void* ctx) {
    LOGOS_CMOCK_RECORD("storage_destroy");
    return RET_OK;
}

// No-arg async functions (StorageNoArgFunction)

int storage_start(void* ctx, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_start");
    invokeOk("storage_start", cb, userData);
    return RET_OK;
}

int storage_stop(void* ctx, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_stop");
    invokeOk("storage_stop", cb, userData);
    return RET_OK;
}

int storage_close(void* ctx, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_close");
    invokeOk("storage_close", cb, userData);
    return RET_OK;
}

int storage_repo(void* ctx, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_repo");
    invokeOk("storage_repo", cb, userData);
    return RET_OK;
}

int storage_peer_id(void* ctx, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_peer_id");
    invokeOk("storage_peer_id", cb, userData);
    return RET_OK;
}

int storage_spr(void* ctx, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_spr");
    invokeOk("storage_spr", cb, userData);
    return RET_OK;
}

int storage_debug(void* ctx, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_debug");
    invokeOk("storage_debug", cb, userData);
    return RET_OK;
}

int storage_get_metrics(void* ctx, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_get_metrics");
    invokeOk("storage_get_metrics", cb, userData);
    return RET_OK;
}

int storage_space(void* ctx, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_space");
    invokeOk("storage_space", cb, userData);
    return RET_OK;
}

int storage_list(void* ctx, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_list");
    invokeOk("storage_list", cb, userData);
    return RET_OK;
}

// String-arg async functions (StorageStringArgFunction)

int storage_log_level(void* ctx, const char* logLevel, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_log_level");
    invokeOk("storage_log_level", cb, userData);
    return RET_OK;
}

int storage_exists(void* ctx, const char* cid, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_exists");
    invokeOk("storage_exists", cb, userData);
    return RET_OK;
}

int storage_upload_file(void* ctx, const char* sessionId, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_upload_file");
    // Unset return defaults to RET_OK; on a forced failure the callback never fires.
    int rc = LOGOS_CMOCK_RETURN(int, "storage_upload_file");
    if (rc == RET_OK) invokeOk("storage_upload_file", cb, userData);
    return rc;
}

int storage_upload_finalize(void* ctx, const char* sessionId, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_upload_finalize");
    invokeOk("storage_upload_finalize", cb, userData);
    return RET_OK;
}

int storage_upload_cancel(void* ctx, const char* sessionId, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_upload_cancel");
    invokeOk("storage_upload_cancel", cb, userData);
    return RET_OK;
}

int storage_download_cancel(void* ctx, const char* cid, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_download_cancel");
    invokeOk("storage_download_cancel", cb, userData);
    return RET_OK;
}

// String+size_t async functions (StorageStringArgAndIntArgFunction)

int storage_fetch(void* ctx, const char* cid, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_fetch");
    invokeOk("storage_fetch", cb, userData);
    return RET_OK;
}

int storage_delete(void* ctx, const char* cid, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_delete");
    invokeOk("storage_delete", cb, userData);
    return RET_OK;
}

int storage_download_manifest(void* ctx, const char* cid, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_download_manifest");
    invokeOk("storage_download_manifest", cb, userData);
    return RET_OK;
}

int storage_upload_init(void* ctx, const char* filepath, size_t chunkSize,
                        StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_upload_init");
    invokeOk("storage_upload_init", cb, userData);
    return RET_OK;
}

int storage_toggle_private_queries(void* ctx, bool enabled, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_toggle_private_queries");
    invokeOk("storage_toggle_private_queries", cb, userData);
    return RET_OK;
}

// Multi-arg functions

int storage_connect(void* ctx, const char* peerId, const char** addrs,
                    size_t addrsSize, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_connect");
    invokeOk("storage_connect", cb, userData);
    return RET_OK;
}

int storage_upload_chunk(void* ctx, const char* sessionId, const uint8_t* chunk,
                         size_t len, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_upload_chunk");
    invokeOk("storage_upload_chunk", cb, userData);
    return RET_OK;
}

int storage_download_init(void* ctx, const char* cid, size_t chunkSize, bool local,
                          StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_download_init");
    invokeOk("storage_download_init", cb, userData);
    return RET_OK;
}

int storage_download_stream(void* ctx, const char* cid, size_t chunkSize, bool local,
                            const char* filepath, StorageCallback cb, void* userData) {
    LOGOS_CMOCK_RECORD("storage_download_stream");
    // Unset return defaults to RET_OK; on a forced failure the callback never fires.
    int rc = LOGOS_CMOCK_RETURN(int, "storage_download_stream");
    if (rc == RET_OK) invokeOk("storage_download_stream", cb, userData);
    return rc;
}

} // extern "C"
