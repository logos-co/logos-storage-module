#pragma once

#include "interface.h"
#include <QtCore/QObject>

class StorageModuleInterface : public PluginInterface {
  public:
    virtual ~StorageModuleInterface() {}

    // Create a new instance of a Logos Storage node.
    // `cfg` is a JSON string with the configuration overwriting defaults.
    //
    // Returns true if initialization was successful.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual bool init(const QString& cfg) = 0;

    // Start starts the Codex node.
    //
    // Returns true if the start command was successfully issued.
    //
    // The method is asynchronous; completion is signaled via events.
    // Emit "storageStart" event on completion.
    Q_INVOKABLE virtual bool start() = 0;

    // Get the Logos Storage version string.
    // This call does not require the node to be started.
    //
    // Return the version string or an empty string on error.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString version() = 0;

    // Stop the Logos Storage node.
    // The node can be started and stopped multiple times.
    //
    // Returns true if the stop command was successfully issued.
    //
    // The method is asynchronous; completion is signaled via events.
    // Emit "storageStop" event on completion.
    Q_INVOKABLE virtual bool stop() = 0;

    // Destroys an instance of a Logos Storage node.
    // This will free all resources associated with the node.
    // The node must be stopped and closed before calling this function.
    // This method calls internally storage_close and storage_destroy.
    //
    // Returns true if the destroy command was successfully issued.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual bool destroy() = 0;

    // Get the Logos Storage data directory.
    //
    // Return the data directory string or an empty string on error.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString dataDir() = 0;

    // Get the Logos Storage debug information.
    //
    // Return the debug string or an empty string on error.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString debug() = 0;

    // Get the Logos Storage node Peer Id.
    // Peer Identity reference as specified at
    // https://docs.libp2p.io/concepts/fundamentals/peers/
    //
    // Return the peer id string or an empty string on error.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString peerId() = 0;

    // Get the node's Signed Peer Record (SPR)
    //
    // Return the signed peer record string or an empty string on error.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString spr() = 0;

    // Set the log level at run time.
    // `logLevel` can be one of:
    // TRACE, DEBUG, INFO, NOTICE, WARN, ERROR or FATAL
    //
    // Returns true if the log level was successfully updated.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual bool updateLogLevel(const QString& logLevel) = 0;

    // Connect to a peer by using `peerAddresses` if provided, otherwise use `peerId`.
    // Note that the `peerId` has to be advertised in the DHT for this to work.
    //
    // Returns true if the connect command was successfully issued.
    //
    // The method is asynchronous; completion is signaled via events.
    // Emit "storageConnect" event on completion.
    Q_INVOKABLE virtual bool connect(const QString& peerId, const QStringList& peerAddresses) = 0;

    // TODO write definition
    Q_INVOKABLE virtual QString uploadFromPath(const QUrl& url, const int chunkSize = 1024 * 64) = 0;

    // TODO write definition
    virtual QString uploadFromIO(std::unique_ptr<QIODevice> device, const int chunkSize = 1024 * 64) = 0;

    // Initialize an upload session for a file.
    //
    // The method is part of the upload flow which can be:
    //
    // For chunks:
    // 1- uploadInit: Create a session
    // 2- uploadChunk: Upload individual chunks
    // 3a- uploadCancel: Cancel an upload and destroy the session
    // 3b- uploadFinalize: Finalize an upload and returns its cid
    //
    // For file:
    // 1- uploadInit: Create a session
    // 2- uploadFile: Upload and file and returns its cid
    //
    // `filepath` for a file upload, this is the absolute path to the file
    // to be uploaded. For an upload using chunks, this is the name of the file.
    // The metadata filename and mime type are derived from this value.
    //
    // `chunkSize` defines the size of each chunk to be used during upload.
    // The default value is the default block size 1024 * 64 bytes.
    //
    // Returns the session id as string if the upload initialization was successful.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString uploadInit(const QString& filepath, const int chunkSize = 1024 * 64) = 0;

    // Upload a chunk for an ongoing upload session.
    // This requires that uploadInit has been called first.
    // After all chunks have been uploaded, uploadFinalize has to be called
    // EXPLICITLY.
    //
    // Returns true if the chunk upload command was sent.
    //
    // The method is asynchronous.
    // Emit "storageUploadProgress" event on completion. It will
    // provide the sessionId and the size of the chunk.
    Q_INVOKABLE virtual bool uploadChunk(const QString& sessionId, const QByteArray& chunk) = 0;

    // Upload the file defined as `filepath` in the init method.
    // The callback will be called with RET_PROGRESS updates during the upload,
    // if the chunk size is equal or greater than the session chunkSize.
    // After all chunks have been uploaded, uploadFinalize is called
    // IMPLICITLY, you DO NOT HAVE to call it.
    //
    // The callback returns the `cid` of the uploaded content.
    //
    // Returns the `cid` of the uploaded content at the end of the upload.
    //
    // The method is asynchronous.
    // Emit "storageUploadProgres" event on progress update. It will
    // provide the sessionId, the chunk and the size of the chunk.
    // Emit "storageUploadDone" event on completion.
    Q_INVOKABLE virtual bool uploadFile(const QString& sessionId) = 0;

    // Cancel an ongoing upload session.
    //
    // Returns true if the upload session was successfully canceled.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual bool uploadCancel(const QString& sessionId) = 0;

    // Finalize an ongoing upload session.
    // This function has to be called explictly after all chunks have been
    // uploaded using uploadChunk.
    // It is called implictly when using uploadFile.
    //
    // Returns the CID string of the uploaded file if successful.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString uploadFinalize(const QString& sessionId) = 0;
  signals:
    // for now this is required for events, later it might not be necessary if using a proxy
    void eventResponse(const QString& eventName, const QVariantList& data);
};

#define StorageModuleInterface_iid "org.logos.StorageModuleInterface"
Q_DECLARE_INTERFACE(StorageModuleInterface, StorageModuleInterface_iid)
