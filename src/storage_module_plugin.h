#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <logos_json.h>
#include <logos_module_context.h>
#include <logos_result.h>

extern "C" {
#include "lib/libstorage.h"
}

/// Logos Storage Module API.
///
/// Wraps a libstorage node and exposes upload, download, and data-management
/// operations. Synchronous methods return immediately; asynchronous methods
/// report completion through the typed events declared in the `logos_events:`
/// block at the bottom of this class.
class StorageModuleImpl : public LogosModuleContext {
public:
    StorageModuleImpl();
    ~StorageModuleImpl();

    /// Create a new storage node instance and configure it.
    ///
    /// `cfg` is a JSON string with the configuration overwriting defaults.
    ///
    /// Example of JSON config:
    /// @code{.json}
    /// {
    ///     "log-level": "info",
    ///     "log-format": "auto",
    ///     "metrics": false,
    ///     "metrics-address": "127.0.0.1",
    ///     "metrics-port": 8008,
    ///     "data-dir": ".cache/storage",
    ///     "listen-ip": "0.0.0.0",
    ///     "listen-port": 0,
    ///     "nat": "any",
    ///     "disc-port": 8090,
    ///     "net-privkey": "key",
    ///     "bootstrap-node": [],
    ///     "no-bootstrap-node": false,
    ///     "network": "logos.test",
    ///     "dht-mix-proxy": [],
    ///     "mix-enabled": false,
    ///     "mix-pool": "",
    ///     "mix-pool-json": "",
    ///     "max-peers": 160,
    ///     "num-threads": 0,
    ///     "agent-string": "Logos Storage",
    ///     "repo-kind": "fs",
    ///     "storage-quota": 21474836480,
    ///     "block-ttl": "30d",
    ///     "block-mi": "10m",
    ///     "block-mn": 1000,
    ///     "block-retries": 300,
    ///     "log-file": "/tmp/storage-log-624036264.log"
    /// }
    /// @endcode
    ///
    /// Do not call init() more than once per instance.
    ///
    /// Returns true on success.  The method is synchronous.
    bool init(const std::string& cfg);

    /// Start the storage node.
    ///
    /// Returns true if the start command was accepted by libstorage.  Actual
    /// completion is signalled asynchronously via the "storageStart" event:
    ///   { "success": bool, "message": string }
    ///
    /// The method is asynchronous.
    bool start();

    /// Stop the storage node.
    ///
    /// The node can be started and stopped multiple times.  Returns a
    /// StdLogosResult indicating whether the stop command was sent; actual
    /// completion is signalled via the "storageStop" event:
    ///   { "success": bool, "message": string }
    ///
    /// The method is asynchronous.
    StdLogosResult stop();

    /// Destroy the storage context and free all resources.
    ///
    /// Internally calls storage_close then storage_destroy.  The node should
    /// be stopped before calling destroy().  Not stopping first can lead to
    /// undefined behaviour (e.g. data loss or crashes).
    ///
    /// Returns StdLogosResult::success = true on success.
    /// The method is synchronous.
    StdLogosResult destroy();

    /// Get the libstorage version string.
    ///
    /// Does not require the node to be started.
    ///
    /// Returns StdLogosResult::value as a std::string on success.
    /// The method is synchronous.
    StdLogosResult version();

    /// Get this module's version, as declared in `metadata.json`.
    ///
    /// The method is synchronous.
    std::string moduleVersion();

    /// Get the storage data directory path.
    ///
    /// Returns StdLogosResult::value as a std::string on success.
    /// The method is synchronous.
    StdLogosResult dataDir();

    /// Get the node's peer ID.
    ///
    /// The peer ID is the libp2p peer identity as described at
    /// https://docs.libp2p.io/concepts/fundamentals/peers/
    ///
    /// Returns StdLogosResult::value as a std::string on success.
    /// The method is synchronous.
    StdLogosResult peerId();

    /// Get the node's Signed Peer Record (SPR).
    ///
    /// Returns StdLogosResult::value as a std::string on success.
    /// The method is synchronous.
    StdLogosResult spr();

    /// Get debug information for the node.
    ///
    /// Returns StdLogosResult::value as a JSON object on success:
    /// @code{.json}
    /// {
    ///   "id": string,
    ///   "addrs": [string],
    ///   "spr": string,
    ///   "announceAddresses": [string],
    ///   "table": {
    ///     "localNode": { "nodeId": string, "peerId": string,
    ///                    "record": string, "address": string, "seen": bool },
    ///     "nodes": [{ "nodeId": string, "peerId": string,
    ///                 "record": string, "address": string, "seen": bool }]
    ///   }
    /// }
    /// @endcode
    ///
    /// The method is synchronous.
    StdLogosResult debug();

    /// Set the log level at runtime.
    ///
    /// `logLevel` must be one of: TRACE, DEBUG, INFO, NOTICE, WARN, ERROR, FATAL
    ///
    /// Returns StdLogosResult::success = true on success.
    /// The method is synchronous.
    StdLogosResult updateLogLevel(const std::string& logLevel);

    /// Connect to a peer.
    ///
    /// Uses `peerAddresses` as explicit dial targets when provided; otherwise
    /// the peer must be discoverable via the DHT using `peerId`.
    ///
    /// Returns a StdLogosResult indicating whether the connect command was sent;
    /// actual completion is signalled via the "storageConnect" event:
    ///   { "success": bool, "message": string }
    ///
    /// The method is asynchronous.
    StdLogosResult connect(const std::string& peerId, const std::vector<std::string>& peerAddresses);

    /// Toggle routing of DHT queries over the Logos mix network.
    ///
    /// When enabled, all subsequent DHT queries are tunnelled over Mix; this
    /// affects queries only, not advertisements.
    ///
    /// Enabling requires Mix to be configured: `mix-enabled` true and at
    /// least one `dht-mix-proxy` set (see init()). Otherwise enabling fails
    /// with an error. Disabling is always allowed.
    ///
    /// This is a temporary API and will likely be removed before mainnet.
    ///
    /// On success, returns StdLogosResult::value as a bool: the previous toggle
    /// state (true = private queries were already enabled).
    /// The method is synchronous.
    StdLogosResult togglePrivateQueries(bool enabled);

    /// Upload a local file by absolute path.
    ///
    /// Internally calls storage_upload_init followed by storage_upload_file.
    /// If init succeeds but the file upload command fails, the session is
    /// cancelled automatically.
    ///
    /// `filePath`  – absolute path to the file on disk.
    /// `chunkSize` – upload chunk size in bytes (default 65536).
    ///
    /// Returns StdLogosResult::value as a session ID string on success.
    ///
    /// The method is asynchronous; progress and completion are signalled via:
    ///
    ///   "storageUploadProgress" (throttled to at most one event per % point):
    ///     { "success": true, "sessionId": string, "bytes": number }
    ///
    ///   "storageUploadDone":
    ///     On success: { "success": true,  "sessionId": string, "cid": string }
    ///     On failure: { "success": false, "sessionId": string, "error": string }
    StdLogosResult uploadUrl(const std::string& filePath, int64_t chunkSize);

    /// Create a manual upload session for chunk-by-chunk streaming.
    ///
    /// Use this only when uploadUrl() cannot be used (e.g. you are streaming
    /// data that is not on disk).  After creating a session, send all chunks
    /// with uploadChunk(), then call uploadFinalize() to get the CID.
    ///
    /// `filename`  – used to populate manifest metadata (mimetype, name).
    /// `chunkSize` – upload chunk size in bytes (default 65536).
    ///
    /// Returns StdLogosResult::value as the session ID string on success.
    /// The method is synchronous.
    StdLogosResult uploadInit(const std::string& filename, int64_t chunkSize);

    /// Upload a single data chunk for a session created with uploadInit().
    ///
    /// A failed chunk does not corrupt the session; the caller may retry or
    /// call uploadCancel().
    ///
    /// Emits "storageUploadProgress" on completion:
    ///   On success: { "success": true,  "sessionId": string, "bytes": number }
    ///   On failure: { "success": false, "sessionId": string, "error": string }
    ///
    /// The method is asynchronous.
    StdLogosResult uploadChunk(const std::string& sessionId, const std::string& chunk);

    /// Finalize a manual upload session and retrieve the CID.
    ///
    /// Must be called after all chunks have been sent with uploadChunk().
    ///
    /// Returns StdLogosResult::value as the CID string on success.
    /// The method is synchronous.
    StdLogosResult uploadFinalize(const std::string& sessionId);

    /// Cancel an ongoing upload session.
    ///
    /// Returns StdLogosResult::success = true on success.
    /// The method is synchronous.
    StdLogosResult uploadCancel(const std::string& sessionId);

    /// Download content by CID and write it to a local file.
    ///
    /// Internally fetches the manifest first to obtain the total size (required
    /// for progress throttling); returns an error if the manifest is unavailable.
    ///
    /// `cid`       – content identifier to download.
    /// `filePath`  – destination path on disk.
    /// `local`     – if true, only reads from locally cached data (no network).
    /// `chunkSize` – download chunk size in bytes (default 65536).
    ///
    /// Returns StdLogosResult::value as the session ID (= CID) on success.
    ///
    /// The method is asynchronous; progress and completion are signalled via:
    ///
    ///   "storageDownloadProgress" (throttled to at most one event per % point):
    ///     { "success": true, "sessionId": string, "bytes": number }
    ///
    ///   "storageDownloadDone":
    ///     On success: { "success": true,  "sessionId": string }
    ///     On failure: { "success": false, "sessionId": string, "error": string }
    StdLogosResult downloadToUrl(const std::string& cid, const std::string& filePath, bool local, int64_t chunkSize);

    /// Download content by CID and deliver it as a stream of base64-encoded chunks.
    ///
    /// Use this when you want to process or forward the data without writing it
    /// to disk.  For large files, downloadToUrl() is more efficient as it avoids
    /// the base64 encoding overhead.
    ///
    /// `cid`       – content identifier to download.
    /// `local`     – if true, only reads from locally cached data (no network).
    /// `chunkSize` – download chunk size in bytes (default 65536).
    ///
    /// Returns StdLogosResult::value as the session ID (= CID) on success.
    ///
    /// The method is asynchronous; chunks and completion are signalled via:
    ///
    ///   "storageDownloadProgress" (one event per chunk, not throttled):
    ///     { "success": true, "sessionId": string, "chunk": string (base64) }
    ///   The chunk field is base64-encoded so arbitrary binary data can be
    ///   safely embedded in a JSON string.
    ///
    ///   "storageDownloadDone":
    ///     On success: { "success": true,  "sessionId": string }
    ///     On failure: { "success": false, "sessionId": string, "error": string }
    StdLogosResult downloadChunks(const std::string& cid, bool local, int64_t chunkSize);

    /// Cancel an ongoing download session.
    ///
    /// Returns StdLogosResult::success = true on success.
    /// The method is synchronous.
    StdLogosResult downloadCancel(const std::string& sessionId);

    /// Check whether content identified by CID exists in local storage.
    ///
    /// Returns StdLogosResult::value as bool (true = exists) on success.
    /// The method is synchronous.
    StdLogosResult exists(const std::string& cid);

    /// Fetch content from the network and cache it locally in the background.
    ///
    /// The method returns as soon as the fetch request is accepted; no event is
    /// emitted when the background download completes.
    ///
    /// Returns StdLogosResult::success = true if the request was accepted.
    /// The method is synchronous.
    StdLogosResult fetch(const std::string& cid);

    /// Remove content identified by CID from local storage.
    ///
    /// Returns StdLogosResult::success = true on success.
    /// The method is synchronous.
    StdLogosResult remove(const std::string& cid);

    /// Get storage space information.
    ///
    /// Returns StdLogosResult::value as a JSON object on success:
    /// @code{.json}
    /// {
    ///   "totalBlocks":        number,
    ///   "quotaMaxBytes":      number,
    ///   "quotaUsedBytes":     number,
    ///   "quotaReservedBytes": number
    /// }
    /// @endcode
    ///
    /// The method is synchronous.
    StdLogosResult space();

    /// List all manifests stored locally.
    ///
    /// Returns StdLogosResult::value as a JSON array on success; each item:
    /// @code{.json}
    /// {
    ///   "cid":         string,
    ///   "treeCid":     string,
    ///   "datasetSize": number,
    ///   "blockSize":   number,
    ///   "filename":    string,
    ///   "mimetype":    string
    /// }
    /// @endcode
    ///
    /// The method is synchronous.
    StdLogosResult manifests();

    /// Download and return the manifest for a given CID.
    ///
    /// Returns StdLogosResult::value as a JSON object on success:
    /// @code{.json}
    /// {
    ///   "manifestVersion": number,
    ///   "treeCid":     string,
    ///   "datasetSize": number,
    ///   "blockSize":   number,
    ///   "filename":    string,
    ///   "mimetype":    string
    /// }
    /// @endcode
    ///
    /// The method is synchronous.
    StdLogosResult downloadManifest(const std::string& cid);

    /// Import all files from a directory (headless helper).
    ///
    /// Iterates regular files in `path` and calls uploadUrl() for each.
    /// Does not wait for uploads to complete; listen for "storageUploadDone"
    /// events to track results.
    void importFiles(const std::string& path);

    /// Asynchronous-completion events emitted by the methods above.
    ///
    /// Each declaration is a forward declaration only — the bodies are
    /// codegen-emitted at build time into `storage_module_events_cdylib.cpp`
    /// and marshal their args through `LogosModuleContext::emitEventImpl_`.
    /// See `LogosModuleContext` (`<logos_module_context.h>`) for the
    /// `logos_events:` access-specifier convention and dispatch wiring.
    ///
    /// `payload` is a JSON-encoded string; see the originating method's
    /// Doxygen comment above for the field set on each event.
logos_events:
    void storageStart(const std::string& payload);
    void storageStop(const std::string& payload);
    void storageConnect(const std::string& payload);
    void storageUploadProgress(const std::string& payload);
    void storageUploadDone(const std::string& payload);
    void storageDownloadProgress(const std::string& payload);
    void storageDownloadDone(const std::string& payload);

private:
    void* storageCtx;

    /// Shared internal download helper used by downloadToUrl and downloadChunks.
    /// Returns session ID (= cid) on success, empty string on failure.
    std::string downloadChunksInternal(const std::string& cid,
                                       const std::string& filepath,
                                       bool local, int64_t chunkSize);
};
