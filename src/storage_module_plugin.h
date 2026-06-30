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
/// report completion through the typed events in the
/// `Asynchronous-completion events` section.
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
    /// completion is signalled asynchronously via the `storageStart` event.
    ///
    /// The method is asynchronous.
    bool start();

    /// Stop the storage node.
    ///
    /// The node can be started and stopped multiple times.  Returns a
    /// StdLogosResult indicating whether the stop command was sent; actual
    /// completion is signalled via the `storageStop` event.
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

    /// Collect node metrics for the openmetrics module.
    ///
    /// Implements the openmetrics-module IMetricsSource interface. Returns a
    /// Logos openmetrics-compatible JSON object. On libstorage errors or invalid
    /// payloads, returns: { "metrics": [] }.
    /// @code{.json}
    /// {
    ///   "metrics": [
    ///     {
    ///       "name": string,
    ///       "type": string,
    ///       "help": string,
    ///       "value": number,
    ///       "labels": object
    ///     }
    ///   ]
    /// }
    /// @endcode
    ///
    /// The method is synchronous.
    LogosMap collectMetrics();

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
    /// actual completion is signalled via the `storageConnect` event.
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
    /// The method is asynchronous; progress is signalled via the
    /// `storageUploadProgress` event (throttled to at most one event per %
    /// point) and completion via the `storageUploadDone` event.
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
    /// Emits the `storageUploadProgress` event on completion.
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
    /// The method is asynchronous; progress is signalled via the
    /// `storageDownloadProgress` event (throttled to at most one event per %
    /// point) and completion via the `storageDownloadDone` event.
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
    /// The method is asynchronous; each chunk is delivered via the
    /// `storageDownloadProgress` event (one event per chunk, not throttled) and
    /// completion via the `storageDownloadDone` event.
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

    /// Remove content identified by CID from local storage in the background.
    ///
    /// The delete may touch the network and can take a while, so this method
    /// does not block: the returned StdLogosResult only reports whether the
    /// command was dispatched. The real outcome arrives later via the
    /// `storageRemoveDone` event.
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

    /// Fetch the manifest for a given CID in the background.
    ///
    /// The lookup may query the DHT and can take a long time, so this method
    /// does not block: the returned StdLogosResult only reports whether the
    /// command was dispatched. The real outcome arrives later via the
    /// `storageDownloadManifestDone` event.
    StdLogosResult downloadManifest(const std::string& cid);

    /// Import all files from a directory (headless helper).
    ///
    /// Iterates regular files in `path` and calls uploadUrl() for each.
    /// Does not wait for uploads to complete; listen for `storageUploadDone`
    /// events to track results.
    void importFiles(const std::string& path);

    /// @name Asynchronous-completion events
    ///
    /// Each event delivers a single JSON-encoded string `payload`, described
    /// with the event below.
    /// @{
logos_events:
    /// Emitted when start() has finished starting the node.
    /// @code{.json}
    /// {
    ///   "success": bool,
    ///   "message": string
    /// }
    /// @endcode
    void storageStart(const std::string& payload);

    /// Emitted when stop() has finished stopping the node.
    /// @code{.json}
    /// {
    ///   "success": bool,
    ///   "message": string
    /// }
    /// @endcode
    void storageStop(const std::string& payload);

    /// Emitted when connect() has finished connecting to the peer.
    /// @code{.json}
    /// {
    ///   "success": bool,
    ///   "message": string
    /// }
    /// @endcode
    void storageConnect(const std::string& payload);

    /// Emitted as uploadUrl() or uploadChunk() upload data.
    /// @code{.json}
    /// {
    ///   "success":   bool,
    ///   "sessionId": string,
    ///   "bytes":     number,       // present on success
    ///   "error":     string        // present on failure
    /// }
    /// @endcode
    void storageUploadProgress(const std::string& payload);

    /// Emitted when uploadUrl() finishes.
    /// @code{.json}
    /// {
    ///   "success":   bool,
    ///   "sessionId": string,
    ///   "cid":       string,       // present on success
    ///   "error":     string        // present on failure
    /// }
    /// @endcode
    void storageUploadDone(const std::string& payload);

    /// Emitted as downloadToUrl() or downloadChunks() receive data.
    /// @code{.json}
    /// {
    ///   "success":   true,
    ///   "sessionId": string,
    ///   "bytes":     number,       // file download (downloadToUrl)
    ///   "chunk":     string        // base64 chunk, stream download (downloadChunks)
    /// }
    /// @endcode
    void storageDownloadProgress(const std::string& payload);

    /// Emitted when downloadToUrl() or downloadChunks() finishes.
    /// @code{.json}
    /// {
    ///   "success":   bool,
    ///   "sessionId": string,
    ///   "error":     string        // present on failure
    /// }
    /// @endcode
    void storageDownloadDone(const std::string& payload);

    /// Emitted when downloadManifest() finishes.
    /// @code{.json}
    /// {
    ///   "success": bool,
    ///   "cid":     string,
    ///   "manifest": {              // present on success
    ///     "manifestVersion": number,
    ///     "treeCid":     string,
    ///     "datasetSize": number,
    ///     "blockSize":   number,
    ///     "filename":    string,
    ///     "mimetype":    string
    ///   },
    ///   "error":   string          // present on failure
    /// }
    /// @endcode
    void storageDownloadManifestDone(const std::string& payload);

    /// Emitted when remove() finishes.
    /// @code{.json}
    /// {
    ///   "success": bool,
    ///   "cid":     string,
    ///   "error":   string          // present on failure
    /// }
    /// @endcode
    void storageRemoveDone(const std::string& payload);
    /// @}

private:
    void* storageCtx;

    /// Shared internal download helper used by downloadToUrl and downloadChunks.
    /// Returns session ID (= cid) on success, empty string on failure.
    std::string downloadChunksInternal(const std::string& cid,
                                       const std::string& filepath,
                                       bool local, int64_t chunkSize);
};
