#pragma once

#include "interface.h"
#include <QMetaType>

class StorageModuleInterface : public PluginInterface {
  public:
    virtual ~StorageModuleInterface() {}

    // Create a new instance of a Logos Storage node.
    // `cfg` is a JSON string with the configuration overwriting defaults.
    //
    // Returns an empty string on success.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual LogosResult init(const QString& cfg) = 0;

    // Start the Storage node.
    //
    // Returns the error message on failure, or an empty string on success.
    //
    // The method is asynchronous; completion is signaled via events.
    // Emit "storageStart" event on completion.
    Q_INVOKABLE virtual LogosResult start() = 0;

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
    // Returns the debug string or a on success.
    //
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

    // Upload data from a QIODevice stream.
    //
    // Internally, this method first calls `storage_upload_init` to create an upload
    // session. The stream is then read and uploaded in chunks using
    // `storage_upload_chunk`. Once all chunks have been uploaded,
    // `storage_upload_finalize` is called to complete the upload.
    //
    // If the upload session is created but `storage_upload_chunk` fails, the upload
    // is cancelled internally.
    //
    // The filename cannot be inferred from the stream and must be provided
    // explicitly if needed. If the filename is empty, the uploaded file will have
    // no extension and will appear as a binary file.
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
    Q_INVOKABLE virtual LogosResult uploadStream(QIODevice* device, const QString& filename = "",
                                                 const int chunkSize = 1024 * 64) = 0;

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
    // If the download session is created but the download fails,
    // `storage_download_cancel` is called internally to cancel the download.
    //
    // Returns the download session ID as a string if the download session was
    // created successfully.
    //
    // The method is asynchronous, completion is signaled via events.
    //
    // Emits `storageDownloadProgress` on progess with:
    // 1- success: true if the operation was successful, false otherwise
    // 2- sessionId: the download session ID
    // 3- chunk: the actual bytes downloaded
    //
    // Emits storageDownloadDone on completion with:
    // 1- success: true if the operation was successful, false otherwise
    // 2- sessionId: the download session ID
    // 3- message: the error message if the download failed, or an empty string if it succeeded.
    Q_INVOKABLE virtual LogosResult downloadToUrl(const QString& cid, const QUrl& url) = 0;

    // Download content identified by a CID to a QIODevice stream.
    //
    // Internally, this method first calls `storage_download_init` to create a
    // download session. The data is then downloaded in chunks using
    // `storage_download_stream`.
    //
    // If the download session is created but `storage_download_stream` fails,
    // `storage_download_cancel` is called internally to cancel the download.
    //
    // This function is intentionally not marked as Q_INVOKABLE and therefore cannot
    // be called directly from QML. It is intended for C++ use only.
    //
    // Returns the download session ID as a string if the download session was
    // created successfully.
    //
    // The method is asynchronous, completion is signaled via events.
    //
    // Emits `storageDownloadProgress` on progess with:
    // 1- success: true if the operation was successful, false otherwise
    // 2- sessionId: the download session ID
    // 3- chunk: the actual bytes downloaded
    //
    // Emits storageDownloadDone on completion with:
    // 1- success: true if the operation was successful, false otherwise
    // 2- sessionId: the download session ID
    // 3- message: the error message if the download failed, or an empty string if it succeeded.
    virtual LogosResult downloadToStream(const QString& cid, QIODevice* device) = 0;

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
