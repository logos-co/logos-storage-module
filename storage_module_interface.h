#pragma once

#include "interface.h"
#include <QMetaType>

class StorageModuleInterface : public PluginInterface {
  public:
    virtual ~StorageModuleInterface() {}

    // Create a new instance of a Logos Storage node.
    // `cfg` is a JSON string with the configuration overwriting defaults.
    //
    // Example of JSON config:
    // {
    //     "log-level": "DEBUG",
    //     "log-format": "auto",
    //     "metrics": true,
    //     "metrics-address": "127.0.0.1",
    //     "metrics-port": 8008,
    //     "data-dir": ".cache/storage",
    //     "listen-addrs": [
    //         "/ip4/0.0.0.0/tcp/0"
    //      ],
    //     "nat": "any",
    //     "disc-port": 8090,
    //     "net-privkey": "key",
    //     "bootstrap-node": [
    //         "spr:ABCD1234"
    //      ],
    //     "max-peers": 160,
    //     "num-threads": 0,
    //     "agent-string": "Logos Storage",
    //     "repo-kind": "fs",
    //     "storage-quota": 21474836480,
    //     "block-ttl": "4w2d",
    //     "block-mi": "10m",
    //     "block-mn": 1000,
    //     "block-retries": 3000,
    //     "cache-size": 0,
    //     "log-file": "/tmp/storage-log-624036264.log",
    //     "api-bindaddr": "127.0.0.1",
    //     "api-port": 8080,
    //     "api-cors-origin": "*"
    // }
    //
    // Returns boolean to be compatible with headless mode.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual bool init(const QString& cfg) = 0;

    // Start the Storage node.
    //
    // Returns boolean to be compatible with headless mode.
    //
    // The method is asynchronous; completion is signaled via events.
    // Emit "storageStart" event on completion.
    Q_INVOKABLE virtual bool start() = 0;

    // Get the Logos Storage version string.
    // This call does not require the node to be started.
    //
    // Returns the version string on success.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult version() = 0;

    // Stop the Logos Storage node.
    // The node can be started and stopped multiple times.
    //
    // Returns an empty string on success.
    //
    // The method is asynchronous; completion is signaled via events.
    // Emit "storageStop" event on completion.
    Q_INVOKABLE virtual LogosResult stop() = 0;

    // Destroys an instance of a Logos Storage node.
    // This will free all resources associated with the node.
    // The node must be stopped and closed before calling this function.
    // This method calls internally storage_close and storage_destroy.
    //
    // Returns true if the destroy command was successfully issued.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult destroy() = 0;

    // Get the Logos Storage data directory.
    //
    // Returns the data directory string on success.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult dataDir() = 0;

    // Get the Logos Storage debug information.
    //
    // Returns the debug object or a on success.
    // Usage:
    //  LogosResult result = debug();
    //  QStringList addrs = result.getValue<QStringList>("addrs");
    //  QStringList announceAddresses = result.getValue<QStringList>("announceAddresses");
    //  QVariantMap table = result.getValue<QVariantMap>("table");
    //  QVariantList nodes = table["nodes"].toList();
    //
    //  for (const QVariant& nodeVar : nodes) {
    //    QVariantMap node = nodeVar.toMap();
    //    QString nodeId = node["nodeId"].toString();
    //    QString peerId = node["peerId"].toString();
    //    QString record = node["record"].toString();
    //    bool seen = node["seen"].toBool();
    //  }
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult debug() = 0;

    // Get the Logos Storage node Peer Id.
    // Peer Identity reference as specified at
    // https://docs.libp2p.io/concepts/fundamentals/peers/
    //
    // Returns the peer id string on success.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult peerId() = 0;

    // Get the node's Signed Peer Record (SPR)
    //
    // Returns the signed peer record string on success.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult spr() = 0;

    // Set the log level at run time.
    // `logLevel` can be one of:
    // TRACE, DEBUG, INFO, NOTICE, WARN, ERROR or FATAL
    //
    // Returns an empty string on success.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult updateLogLevel(const QString& logLevel) = 0;

    // Connect to a peer by using `peerAddresses` if provided, otherwise use `peerId`.
    // Note that the `peerId` has to be advertised in the DHT for this to work.
    //
    // Returns an empty string on success.
    //
    // The method is asynchronous, completion is signaled via events.
    //
    // Emit "storageConnect" event on completion with two parameters:
    // 1- success: true if the operation was successful, false otherwise
    // 2- msg: the error message if the connect command failed, or an empty string if it succeeded.
    Q_INVOKABLE virtual LogosResult connect(const QString& peerId, const QStringList& peerAddresses) = 0;

    // Upload file content from a QUrl.
    //
    // Internally, this method first calls `storage_upload_init` to create an upload
    // session.
    //
    // The QUrl can be:
    // - a local file URL (file:///home/...), in which case `storage_update_file` is
    //   used internally.
    // - a non-local URL (e.g. qrc:, content://), in which case `uploadStream` is
    //   called internally.
    //
    // The filename and MIME type metadata are derived from the URL when available
    // and added to the manifest.
    //
    // If the upload session is created but `storage_update_file` fails, the upload
    // is cancelled internally.
    //
    // Returns the session ID as a string if the upload session was created
    // successfully.
    //
    // The method is asynchronous, completion is signaled via events.
    //
    // Emits `storageUploadProgress` on progress with:
    // 1- success: true if the operation was successful, false otherwise
    // 2- sessionId: the upload session ID
    // 3- size: the number of bytes uploaded in the current chunk
    //
    // Emits `storageUploadDone` when the upload completion with:
    // 1- success: true if the operation was successful, false otherwise
    // 2- sessionId: the upload session ID
    // 3- cid: the CID of the uploaded content if the upload succeeded, or the error message if it failed.
    Q_INVOKABLE virtual LogosResult uploadUrl(const QUrl& url, const int chunkSize = 1024 * 64) = 0;

    // Create an upload session.
    //
    // This method should not be called explicitly when using `uploadUrl`.
    // It should be called explicitly only when you want to use the advanced
    // API and upload a chunk manually.
    // In that case, after sending all the chunks you should call explicitly `uploadFinalize`.
    // The filename is used to determine the mimetype.
    //
    // Returns the session id on success.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult uploadInit(const QString& filename, const int chunkSize = 1024 * 64) = 0;

    // Upload a chunk of data.
    //
    // Please note that this is an advanced API and you should use `uploadUrl` if it
    // matches your needs. Because the communication in the SDK is done using Qt Remote Objects,
    // the function cannot handle a QIODevice, which would make the API nicer.
    // So the caller has to handle this manually and pass each chunk using this function.
    //
    // This method requires first to have created a session using `uploadInit`.
    // After all the chunks are uploaded, you need to call explicitly `uploadFinalize` to get the cid.
    //
    // Returns an empty string on success.
    //
    // The method is synchronous.
    //
    // Emits `storageUploadProgress` on progress with:
    // 1- success: true if the operation was successful, false otherwise
    // 2- sessionId: the upload session ID
    // 3- size: the number of bytes uploaded in the current chunk
    Q_INVOKABLE virtual LogosResult uploadChunk(const QString& sessionId, const QByteArray& chunk) = 0;

    // Finalize an upload session.
    //
    // It should be used only when you want to upload a chunk manually.
    // Returns the cid on success.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult uploadFinalize(const QString& sessionId) = 0;

    // Cancel an ongoing upload session.
    //
    // Returns an empty string on success.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult uploadCancel(const QString& sessionId) = 0;

    // Download content identified by a CID to a URL.
    //
    // Internally, this method streams the downloaded data to the destination URL.
    //
    // The URL must refer to a local destination (e.g. file:///home/...). If the URL
    // points to a directory, the filename is derived from the content metadata when
    // available, or from the CID otherwise.
    //
    // The method will first download the manifest in order to retrieve
    // the size of the data and provide throttled download progress. If it fails, the method
    // will return an error.
    //
    // If the download session is created but the download fails,
    // `storage_download_cancel` is called internally to cancel the download.
    //
    // Returns the download session ID as a string if the download session was
    // created successfully.
    //
    // The session ID is actually the CID, meaning that you can only one
    // session for a CID at the same time.
    //
    // The method is asynchronous, completion is signaled via events.
    //
    // Emits `storageDownloadProgress` on progess with:
    // 1- success: true if the operation was successful, false otherwise
    // 2- sessionId: the download session ID
    // 3- bytes: the number of bytes downloaded
    // Note that the callback does not return the chunk to avoid to make copies.
    //
    // Emits storageDownloadDone on completion with:
    // 1- success: true if the operation was successful, false otherwise
    // 2- sessionId: the download session ID
    // 3- message: the error message if the download failed, or an empty string if it succeeded.
    Q_INVOKABLE virtual LogosResult downloadToUrl(const QString& cid, const QUrl& url, const bool local = false,
                                                  const int chunktSize = 1024 * 64) = 0;

    // Download chunks content identified by a CID. Chunks are received through
    // storageDownloadProgress event.
    //
    // Do not use filepath, it is used internally.
    //
    // Internally, this method first calls `storage_download_init` to create a
    // download session. The data is then downloaded in chunks using
    // `storage_download_stream`.
    //
    // If the download session is created but `storage_download_stream` fails,
    // `storage_download_cancel` is called internally to cancel the download.
    //
    // Returns the download session ID as a string if the download session was
    // created successfully.
    //
    // The session ID is actually the CID, meaning that you can only one
    // session for a CID at the same time.
    //
    // The method is asynchronous, completion is signaled via events.
    //
    // Emits `storageDownloadProgress` on progess with:
    // 1- success: true if the operation was successful, false otherwise
    // 2- sessionId: the download session ID
    // 3- chunk: the actual bytes downloaded
    // Note that the callback makes a copy of the chunk because
    // eventResponse uses Qt::QueuedConnection.
    //
    // Emits storageDownloadDone on completion with:
    // 1- success: true if the operation was successful, false otherwise
    // 2- sessionId: the download session ID
    // 3- message: the error message if the download failed, or an empty string if it succeeded.
    Q_INVOKABLE virtual LogosResult downloadChunks(const QString& cid, const bool local = false,
                                                   const int chunkSize = 1024 * 64, const QString& filepath = "") = 0;

    // Cancel an ongoing download session.
    //
    // Returns an empty string on success.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult downloadCancel(const QString& sessionId) = 0;

    // Check whether content identified by a CID exists in local storage.
    //
    // Returns a boolean value indicating that the cid exists in local storage on success.
    //
    // This method is synchronous.
    Q_INVOKABLE virtual LogosResult exists(const QString& cid) = 0;

    // Fetch content identified by a CID from the network and store it in local
    // storage in the background.
    //
    // Returns an empty string on success.
    //
    // This method is synchronous and only indicates whether the fetch request was
    // accepted. The actual download is performed asynchronously and is not tracked
    // by this function.
    Q_INVOKABLE virtual LogosResult fetch(const QString& cid) = 0;

    // Remove content identified by a CID from the local store.
    //
    // Returns an empty string on success.
    //
    // This method is synchronous.
    Q_INVOKABLE virtual LogosResult remove(const QString& cid) = 0;

    // Get storage space information.
    //
    // Returns a StorageSpace struct containing total blocks, quota max bytes,
    // quota used bytes, and quota reserved bytes.
    // Usage:
    //   LogosResult result = space();
    //   if (result.success) {
    //      int totalBlocks = result.getValue<int>("totalBlocks");
    //      int quotaMaxBytes = result.getValue<int>("quotaMaxBytes");
    //      int quotaUsedBytes = result.getValue<int>("quotaUsedBytes");
    //      int quotaReservedBytes = result.getValue<int>("totalBlocks");
    //   }
    //
    // This method is synchronous.
    Q_INVOKABLE virtual LogosResult space() = 0;

    // List all manifests stored locally.
    //
    // Returns a list of manifests stored locally.
    // Usage:
    //   LogosResult result = manifests();
    //   if (result.success) {
    //      // Get first item values
    //      QString cid = result.getValue<QString>(0, "cid");
    //      QString treeCid = result.getValue<QString>(0, "treeCid");
    //      qint64 datasetSize = result.getValue<qint64>(0, "datasetSize");
    //      qint64 blockSize = result.getValue<qint64>(0, "blockSize");
    //      QString filename = result.getValue<QString>(0, "filename");
    //      QString mimetype = result.getValue<QString>(0, "mimetype");
    //   }
    //
    // This method is synchronous.
    Q_INVOKABLE virtual LogosResult manifests() = 0;

    // Download the manifest identified by a CID and store it in the local store.
    //
    // Returns the downloaded Manifest.
    // Usage:
    //   LogosResult result = downloadManifest(cid);
    //   if (result.success) {
    //      QString cid = result.getValue<QString>("cid");
    //      QString treeCid = result.getValue<QString>("treeCid");
    //      qint64 datasetSize = result.getValue<qint64>("datasetSize");
    //      qint64 blockSize = result.getValue<qint64>("blockSize");
    //      QString filename = result.getValue<QString>("filename");
    //      QString mimetype = result.getValue<QString>("mimetype");
    //   }
    //
    // This method is synchronous.
    Q_INVOKABLE virtual LogosResult downloadManifest(const QString& cid) = 0;

  signals:
    // for now this is required for events, later it might not be necessary if using a proxy
    void eventResponse(const QString& eventName, const QVariantList& data);
};

#define StorageModuleInterface_iid "org.logos.StorageModuleInterface"
Q_DECLARE_INTERFACE(StorageModuleInterface, StorageModuleInterface_iid)
