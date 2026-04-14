#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <logos_json.h>
#include <logos_result.h>

extern "C" {
#include "lib/libstorage.h"
}

class StorageModuleImpl {
public:
    StorageModuleImpl();
    ~StorageModuleImpl();

    // Wired automatically by the generated glue layer.
    // Call this to emit named events to other modules / the host application.
    // Data is a JSON-encoded string (object or array).
    std::function<void(const std::string& eventName, const std::string& data)> emitEvent;

    // Create a new storage node instance and configure it.
    //
    // `cfg` is a JSON string with configuration values that override defaults.
    // Supported keys include:
    //
    //   "log-level"       – TRACE | DEBUG | INFO | NOTICE | WARN | ERROR | FATAL
    //   "log-format"      – "auto" | "json" | "text"
    //   "metrics"         – bool, enable Prometheus metrics
    //   "metrics-address" – "127.0.0.1"
    //   "metrics-port"    – 8008
    //   "data-dir"        – ".cache/storage"
    //   "listen-addrs"    – ["/ip4/0.0.0.0/tcp/0"]
    //   "nat"             – "any"
    //   "disc-port"       – 8090
    //   "net-privkey"     – "key"
    //   "bootstrap-node"  – ["spr:ABCD1234"]
    //   "max-peers"       – 160
    //   "num-threads"     – 0 (auto)
    //   "storage-quota"   – 21474836480 (20 GiB)
    //   "block-ttl"       – "4w2d"
    //   "block-mi"        – "10m"
    //   "block-mn"        – 1000
    //   "cache-size"      – 0
    //   "log-file"        – "/tmp/storage.log"
    //   "api-bindaddr"    – "127.0.0.1"
    //   "api-port"        – 8080
    //   "api-cors-origin" – "*"
    //
    // Do not call init() more than once per instance.
    //
    // Returns true on success.  The method is synchronous.
    bool init(const std::string& cfg);

    // Start the storage node.
    //
    // Returns true if the start command was accepted by libstorage.  Actual
    // completion is signalled asynchronously via the "storageStart" event:
    //   { "success": bool, "message": string }
    //
    // The method is asynchronous.
    bool start();

    // Stop the storage node.
    //
    // The node can be started and stopped multiple times.  Returns a
    // StdLogosResult indicating whether the stop command was sent; actual
    // completion is signalled via the "storageStop" event:
    //   { "success": bool, "message": string }
    //
    // The method is asynchronous.
    StdLogosResult stop();

    // Destroy the storage context and free all resources.
    //
    // Internally calls storage_close then storage_destroy.  The node should
    // be stopped before calling destroy().  Not stopping first can lead to
    // undefined behaviour (e.g. data loss or crashes).
    //
    // Returns StdLogosResult::success = true on success.
    // The method is synchronous.
    StdLogosResult destroy();

    // Get the libstorage version string.
    //
    // Does not require the node to be started.
    //
    // Returns StdLogosResult::value as a std::string on success.
    // The method is synchronous.
    StdLogosResult version();

    // Get the storage data directory path.
    //
    // Returns StdLogosResult::value as a std::string on success.
    // The method is synchronous.
    StdLogosResult dataDir();

    // Get the node's peer ID.
    //
    // The peer ID is the libp2p peer identity as described at
    // https://docs.libp2p.io/concepts/fundamentals/peers/
    //
    // Returns StdLogosResult::value as a std::string on success.
    // The method is synchronous.
    StdLogosResult peerId();

    // Get the node's Signed Peer Record (SPR).
    //
    // Returns StdLogosResult::value as a std::string on success.
    // The method is synchronous.
    StdLogosResult spr();

    // Get debug information for the node.
    //
    // Returns StdLogosResult::value as a JSON object on success:
    //   {
    //     "id": string,
    //     "addrs": [string],
    //     "announceAddresses": [string],
    //     "table": {
    //       "nodes": [{ "nodeId": string, "peerId": string,
    //                   "record": string, "seen": bool }]
    //     }
    //   }
    //
    // The method is synchronous.
    StdLogosResult debug();

    // Set the log level at runtime.
    //
    // `logLevel` must be one of: TRACE, DEBUG, INFO, NOTICE, WARN, ERROR, FATAL
    //
    // Returns StdLogosResult::success = true on success.
    // The method is synchronous.
    StdLogosResult updateLogLevel(const std::string& logLevel);

    // Connect to a peer.
    //
    // Uses `peerAddresses` as explicit dial targets when provided; otherwise
    // the peer must be discoverable via the DHT using `peerId`.
    //
    // Returns a StdLogosResult indicating whether the connect command was sent;
    // actual completion is signalled via the "storageConnect" event:
    //   { "success": bool, "message": string }
    //
    // The method is asynchronous.
    StdLogosResult connect(const std::string& peerId, const std::vector<std::string>& peerAddresses);

    // Upload a local file by absolute path.
    //
    // Internally calls storage_upload_init followed by storage_upload_file.
    // If init succeeds but the file upload command fails, the session is
    // cancelled automatically.
    //
    // `filePath`  – absolute path to the file on disk.
    // `chunkSize` – upload chunk size in bytes (default 65536).
    //
    // Returns StdLogosResult::value as a session ID string on success.
    //
    // The method is asynchronous; progress and completion are signalled via:
    //
    //   "storageUploadProgress" (throttled to at most one event per % point):
    //     { "success": true, "sessionId": string, "bytes": number }
    //
    //   "storageUploadDone":
    //     On success: { "success": true,  "sessionId": string, "cid": string }
    //     On failure: { "success": false, "sessionId": string, "error": string }
    StdLogosResult uploadUrl(const std::string& filePath, int64_t chunkSize);

    // Create a manual upload session for chunk-by-chunk streaming.
    //
    // Use this only when uploadUrl() cannot be used (e.g. you are streaming
    // data that is not on disk).  After creating a session, send all chunks
    // with uploadChunk(), then call uploadFinalize() to get the CID.
    //
    // `filename`  – used to populate manifest metadata (mimetype, name).
    // `chunkSize` – upload chunk size in bytes (default 65536).
    //
    // Returns StdLogosResult::value as the session ID string on success.
    // The method is synchronous.
    StdLogosResult uploadInit(const std::string& filename, int64_t chunkSize);

    // Upload a single data chunk for a session created with uploadInit().
    //
    // A failed chunk does not corrupt the session; the caller may retry or
    // call uploadCancel().
    //
    // Emits "storageUploadProgress" on completion:
    //   On success: { "success": true,  "sessionId": string, "bytes": number }
    //   On failure: { "success": false, "sessionId": string, "error": string }
    //
    // The method is asynchronous.
    StdLogosResult uploadChunk(const std::string& sessionId, const std::string& chunk);

    // Finalize a manual upload session and retrieve the CID.
    //
    // Must be called after all chunks have been sent with uploadChunk().
    //
    // Returns StdLogosResult::value as the CID string on success.
    // The method is synchronous.
    StdLogosResult uploadFinalize(const std::string& sessionId);

    // Cancel an ongoing upload session.
    //
    // Returns StdLogosResult::success = true on success.
    // The method is synchronous.
    StdLogosResult uploadCancel(const std::string& sessionId);

    // Download content by CID and write it to a local file.
    //
    // Internally fetches the manifest first to obtain the total size (required
    // for progress throttling); returns an error if the manifest is unavailable.
    //
    // `cid`       – content identifier to download.
    // `filePath`  – destination path on disk.
    // `local`     – if true, only reads from locally cached data (no network).
    // `chunkSize` – download chunk size in bytes (default 65536).
    //
    // Returns StdLogosResult::value as the session ID (= CID) on success.
    //
    // The method is asynchronous; progress and completion are signalled via:
    //
    //   "storageDownloadProgress" (throttled to at most one event per % point):
    //     { "success": true, "sessionId": string, "bytes": number }
    //
    //   "storageDownloadDone":
    //     On success: { "success": true,  "sessionId": string }
    //     On failure: { "success": false, "sessionId": string, "error": string }
    StdLogosResult downloadToUrl(const std::string& cid, const std::string& filePath,
                                  bool local, int64_t chunkSize);

    // Download content by CID and deliver it as a stream of base64-encoded chunks.
    //
    // Use this when you want to process or forward the data without writing it
    // to disk.  For large files, downloadToUrl() is more efficient as it avoids
    // the base64 encoding overhead.
    //
    // `cid`       – content identifier to download.
    // `local`     – if true, only reads from locally cached data (no network).
    // `chunkSize` – download chunk size in bytes (default 65536).
    //
    // Returns StdLogosResult::value as the session ID (= CID) on success.
    //
    // The method is asynchronous; chunks and completion are signalled via:
    //
    //   "storageDownloadProgress" (one event per chunk, not throttled):
    //     { "success": true, "sessionId": string, "chunk": string (base64) }
    //   The chunk field is base64-encoded so arbitrary binary data can be
    //   safely embedded in a JSON string.
    //
    //   "storageDownloadDone":
    //     On success: { "success": true,  "sessionId": string }
    //     On failure: { "success": false, "sessionId": string, "error": string }
    StdLogosResult downloadChunks(const std::string& cid, bool local, int64_t chunkSize);

    // Cancel an ongoing download session.
    //
    // Returns StdLogosResult::success = true on success.
    // The method is synchronous.
    StdLogosResult downloadCancel(const std::string& sessionId);

    // Check whether content identified by CID exists in local storage.
    //
    // Returns StdLogosResult::value as bool (true = exists) on success.
    // The method is synchronous.
    StdLogosResult exists(const std::string& cid);

    // Fetch content from the network and cache it locally in the background.
    //
    // The method returns as soon as the fetch request is accepted; no event is
    // emitted when the background download completes.
    //
    // Returns StdLogosResult::success = true if the request was accepted.
    // The method is synchronous.
    StdLogosResult fetch(const std::string& cid);

    // Remove content identified by CID from local storage.
    //
    // Returns StdLogosResult::success = true on success.
    // The method is synchronous.
    StdLogosResult remove(const std::string& cid);

    // Get storage space information.
    //
    // Returns StdLogosResult::value as a JSON object on success:
    //   {
    //     "totalBlocks":        number,
    //     "quotaMaxBytes":      number,
    //     "quotaUsedBytes":     number,
    //     "quotaReservedBytes": number
    //   }
    //
    // The method is synchronous.
    StdLogosResult space();

    // List all manifests stored locally.
    //
    // Returns StdLogosResult::value as a JSON array on success; each item:
    //   {
    //     "cid":         string,
    //     "treeCid":     string,
    //     "datasetSize": number,
    //     "blockSize":   number,
    //     "filename":    string,
    //     "mimetype":    string
    //   }
    //
    // The method is synchronous.
    StdLogosResult manifests();

    // Download and return the manifest for a given CID.
    //
    // Returns StdLogosResult::value as a JSON object on success:
    //   {
    //     "cid":         string,
    //     "treeCid":     string,
    //     "datasetSize": number,
    //     "blockSize":   number,
    //     "filename":    string,
    //     "mimetype":    string
    //   }
    //
    // The method is synchronous.
    StdLogosResult downloadManifest(const std::string& cid);

    // Import all files from a directory (headless helper).
    //
    // Iterates regular files in `path` and calls uploadUrl() for each.
    // Does not wait for uploads to complete; listen for "storageUploadDone"
    // events to track results.
    void importFiles(const std::string& path);

    void emitEventSafe(const std::string& name, const std::string& data) const;

private:
    void* storageCtx;

    // Shared internal download helper used by downloadToUrl and downloadChunks.
    // Returns session ID (= cid) on success, empty string on failure.
    std::string downloadChunksInternal(const std::string& cid,
                                       const std::string& filepath,
                                       bool local, int64_t chunkSize);
};
