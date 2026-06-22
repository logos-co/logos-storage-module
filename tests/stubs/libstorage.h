// Stub header for libstorage - provides the same declarations as the real
// Nim-generated header so that storage_module sources compile in tests.
// Signatures match the branch used by the module (fix/delete-dataset-crash).

#ifndef __libstorage__
#define __libstorage__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define RET_OK 0
#define RET_ERR 1
#define RET_MISSING_CALLBACK 2
#define RET_PROGRESS 3

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*StorageCallback)(int callerRet, const char *msg, size_t len, void *userData);

void *storage_new(const char *configJson, StorageCallback callback, void *userData);
char *storage_version(void *ctx);
int storage_destroy(void *ctx);

int storage_start(void *ctx, StorageCallback callback, void *userData);
int storage_stop(void *ctx, StorageCallback callback, void *userData);
int storage_close(void *ctx, StorageCallback callback, void *userData);

int storage_repo(void *ctx, StorageCallback callback, void *userData);
int storage_peer_id(void *ctx, StorageCallback callback, void *userData);
int storage_spr(void *ctx, StorageCallback callback, void *userData);
int storage_debug(void *ctx, StorageCallback callback, void *userData);
int storage_space(void *ctx, StorageCallback callback, void *userData);
int storage_list(void *ctx, StorageCallback callback, void *userData);

int storage_log_level(void *ctx, const char *logLevel, StorageCallback callback, void *userData);
int storage_exists(void *ctx, const char *cid, StorageCallback callback, void *userData);
int storage_upload_file(void *ctx, const char *sessionId, StorageCallback callback, void *userData);
int storage_upload_finalize(void *ctx, const char *sessionId, StorageCallback callback, void *userData);
int storage_upload_cancel(void *ctx, const char *sessionId, StorageCallback callback, void *userData);
int storage_download_cancel(void *ctx, const char *cid, StorageCallback callback, void *userData);

int storage_fetch(void *ctx, const char *cid, StorageCallback callback, void *userData);
int storage_delete(void *ctx, const char *cid, StorageCallback callback, void *userData);
int storage_download_manifest(void *ctx, const char *cid, StorageCallback callback, void *userData);
int storage_upload_init(void *ctx, const char *filepath, size_t chunkSize, StorageCallback callback, void *userData);

int storage_toggle_private_queries(void *ctx, bool enabled, StorageCallback callback, void *userData);

int storage_connect(void *ctx, const char *peerId, const char **peerAddresses,
                    size_t peerAddressesSize, StorageCallback callback, void *userData);
int storage_upload_chunk(void *ctx, const char *sessionId, const uint8_t *chunk,
                         size_t len, StorageCallback callback, void *userData);
int storage_download_init(void *ctx, const char *cid, size_t chunkSize, bool local,
                          StorageCallback callback, void *userData);
int storage_download_stream(void *ctx, const char *cid, size_t chunkSize, bool local,
                            const char *filepath, StorageCallback callback, void *userData);

#ifdef __cplusplus
}
#endif

#endif /* __libstorage__ */
