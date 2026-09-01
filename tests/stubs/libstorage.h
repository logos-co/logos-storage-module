// The call surface of the generated library/generated/storage.h, minus the CBOR wire, for unit tests to mock.

#ifndef __libstorage__
#define __libstorage__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define RET_OK 0
#define RET_ERR 1
#define RET_MISSING_CALLBACK 2

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *data;
    size_t len;
} NimFfiStr;

typedef struct {
    uint8_t *data;
    size_t len;
} NimFfiBytes;

typedef struct {
    NimFfiStr *data;
    size_t len;
} StorageSeq_Str;

// Borrowed view: the binding reads it while encoding and never frees it.
static inline NimFfiStr nimffi_str(const char *s) {
    NimFfiStr v;
    v.data = (char *)s;
    v.len = s ? strlen(s) : 0;
    return v;
}

typedef struct StorageCtx StorageCtx;

typedef void (*StorageCreateFn)(int errCode, StorageCtx *ctx, const char *errMsg, void *userData);
typedef void (*StorageStrReplyFn)(int errCode, const NimFfiStr *reply, const char *errMsg, void *userData);

typedef struct {
    NimFfiStr cid;
    NimFfiBytes data;
} OnDownloadChunkPayload;

typedef struct {
    NimFfiStr sessionId;
    int64_t storedBytes;
} OnUploadProgressPayload;

typedef void (*StorageOnDownloadChunkFn)(const OnDownloadChunkPayload *evt, void *userData);
typedef void (*StorageOnUploadProgressFn)(const OnUploadProgressPayload *evt, void *userData);

// No ctx, no callback. The buffer is thread-local and lives until the next call.
const char *storage_version(void);

int storage_ctx_create(NimFfiStr configJson, StorageCreateFn onCreated, void *userData);
int storage_ctx_destroy(StorageCtx *ctx);

uint64_t storage_ctx_add_on_download_chunk_listener(StorageCtx *ctx, StorageOnDownloadChunkFn fn, void *userData);
uint64_t storage_ctx_add_on_upload_progress_listener(StorageCtx *ctx, StorageOnUploadProgressFn fn, void *userData);

int storage_ctx_start(const StorageCtx *ctx, StorageStrReplyFn onReply, void *userData);
int storage_ctx_stop(const StorageCtx *ctx, StorageStrReplyFn onReply, void *userData);

int storage_ctx_repo(const StorageCtx *ctx, StorageStrReplyFn onReply, void *userData);
int storage_ctx_peer_id(const StorageCtx *ctx, StorageStrReplyFn onReply, void *userData);
int storage_ctx_spr(const StorageCtx *ctx, StorageStrReplyFn onReply, void *userData);
int storage_ctx_debug(const StorageCtx *ctx, StorageStrReplyFn onReply, void *userData);
int storage_ctx_get_metrics(const StorageCtx *ctx, StorageStrReplyFn onReply, void *userData);
int storage_ctx_space(const StorageCtx *ctx, StorageStrReplyFn onReply, void *userData);
int storage_ctx_list(const StorageCtx *ctx, StorageStrReplyFn onReply, void *userData);

int storage_ctx_log_level(const StorageCtx *ctx, NimFfiStr logLevel, StorageStrReplyFn onReply, void *userData);
int storage_ctx_exists(const StorageCtx *ctx, NimFfiStr cid, StorageStrReplyFn onReply, void *userData);
int storage_ctx_fetch(const StorageCtx *ctx, NimFfiStr cid, StorageStrReplyFn onReply, void *userData);
int storage_ctx_delete(const StorageCtx *ctx, NimFfiStr cid, StorageStrReplyFn onReply, void *userData);
int storage_ctx_download_manifest(const StorageCtx *ctx, NimFfiStr cid, StorageStrReplyFn onReply, void *userData);
int storage_ctx_download_cancel(const StorageCtx *ctx, NimFfiStr cid, StorageStrReplyFn onReply, void *userData);
int storage_ctx_upload_file(const StorageCtx *ctx, NimFfiStr sessionId, StorageStrReplyFn onReply, void *userData);
int storage_ctx_upload_finalize(const StorageCtx *ctx, NimFfiStr sessionId, StorageStrReplyFn onReply, void *userData);
int storage_ctx_upload_cancel(const StorageCtx *ctx, NimFfiStr sessionId, StorageStrReplyFn onReply, void *userData);

int storage_ctx_toggle_private_queries(const StorageCtx *ctx, bool enabled, StorageStrReplyFn onReply, void *userData);

int storage_ctx_connect(const StorageCtx *ctx, NimFfiStr peerId, const StorageSeq_Str *peerAddresses,
                        StorageStrReplyFn onReply, void *userData);
int storage_ctx_upload_init(const StorageCtx *ctx, NimFfiStr filepath, uint64_t chunkSize,
                            StorageStrReplyFn onReply, void *userData);
int storage_ctx_upload_chunk(const StorageCtx *ctx, NimFfiStr sessionId, const NimFfiBytes *data,
                             StorageStrReplyFn onReply, void *userData);
int storage_ctx_download_init(const StorageCtx *ctx, NimFfiStr cid, uint64_t chunkSize, bool local,
                              StorageStrReplyFn onReply, void *userData);
int storage_ctx_download_stream(const StorageCtx *ctx, NimFfiStr cid, uint64_t chunkSize, bool local,
                                NimFfiStr filepath, StorageStrReplyFn onReply, void *userData);

#ifdef __cplusplus
}
#endif

#endif /* __libstorage__ */
