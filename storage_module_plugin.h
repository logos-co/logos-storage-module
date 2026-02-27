#pragma once

#include "libstorage.h"
#include "logos_api.h"
#include "logos_api_client.h"
#include "storage_module_interface.h"
#include <QCoreApplication>
#include <QMutex>
#include <QWaitCondition>
#include <QtCore/QObject>

// Signals for synchronous behaviour
enum class StorageSignal {
    Init,
    Close,
    Start,
    Stop,
    Version,
    DataDir,
    PeerId,
    Spr,
    Debug,
    LogLevel,
    UploadInit,
    UploadCancel,
    UploadFinalize,
    UploadDone,
    Exists,
    Fetch,
    Remove,
    DownloadInit,
    DownloadCancel,
    DownloadDone,
    Space,
    Manifests,
    DownloadManifest

};

// Event for asynchronous event
enum class StorageEvent { Start, Stop, Connect, UploadProgress, UploadDone, DownloadProgress, DownloadDone };

// Keep the event names in a single place to avoid mistakes
// and make it easier to change in the future if needed.
inline QString eventName(StorageEvent event) {
    switch (event) {
    case StorageEvent::Start:
        return "storageStart";
    case StorageEvent::Stop:
        return "storageStop";
    case StorageEvent::Connect:
        return "storageConnect";
    case StorageEvent::UploadProgress:
        return "storageUploadProgress";
    case StorageEvent::UploadDone:
        return "storageUploadDone";
    case StorageEvent::DownloadProgress:
        return "storageDownloadProgress";
    case StorageEvent::DownloadDone:
        return "storageDownloadDone";
    }
    return "";
}

// After this time, the sync method will timeout,
// and return an error.
// This is to prevent the UI from hanging indefinitely
// in case of an issue with the storage module.
static const int DEFAULT_SYNC_TIMEOUT = 1000;

// Define a type for storage functions that take no arguments.
using StorageNoArgFunction = int (*)(void*, StorageCallback, void*);
// Define a type for storage functions that take one string argument.
using StorageStringArgFunction = int (*)(void*, const char*, StorageCallback, void*);
// Define a type for storage functions that take a string argument and a int argument.
using StorageStringArgAndIntArgFunction = int (*)(void*, const char*, const size_t, StorageCallback, void*);

class StorageModulePlugin : public QObject, public StorageModuleInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID StorageModuleInterface_iid FILE "metadata.json")
    Q_INTERFACES(StorageModuleInterface PluginInterface)

  public:
    StorageModulePlugin();
    ~StorageModulePlugin();

    Q_INVOKABLE bool init(const QString& cfg) override;
    Q_INVOKABLE bool start() override;
    Q_INVOKABLE LogosResult version() override;
    Q_INVOKABLE LogosResult dataDir() override;
    Q_INVOKABLE LogosResult peerId() override;
    Q_INVOKABLE LogosResult debug() override;
    Q_INVOKABLE LogosResult spr() override;
    Q_INVOKABLE LogosResult updateLogLevel(const QString& logLevel) override;
    Q_INVOKABLE LogosResult connect(const QString& peerId, const QStringList& peerAddresses) override;
    Q_INVOKABLE LogosResult uploadUrl(const QUrl& url, const int chunkSize = 1024 * 64) override;
    Q_INVOKABLE LogosResult uploadInit(const QString& filename, const int chunkSize = 1024 * 64) override;
    Q_INVOKABLE LogosResult uploadChunk(const QString& sessionId, const QByteArray& chunk) override;
    Q_INVOKABLE LogosResult uploadFinalize(const QString& sessionId) override;
    Q_INVOKABLE LogosResult uploadCancel(const QString& sessionId) override;
    Q_INVOKABLE LogosResult downloadCancel(const QString& sessionId) override;
    Q_INVOKABLE LogosResult downloadToUrl(const QString& cid, const QUrl& url, const bool local = false,
                                          const int chunkSize = 1024 * 64) override;
    Q_INVOKABLE LogosResult downloadChunks(const QString& cid, const bool local = false,
                                           const int chunkSize = 1024 * 64, const QString& filepath = "") override;
    Q_INVOKABLE LogosResult exists(const QString& cid) override;
    Q_INVOKABLE LogosResult fetch(const QString& cid) override;
    Q_INVOKABLE LogosResult remove(const QString& cid) override;
    Q_INVOKABLE LogosResult space() override;
    Q_INVOKABLE LogosResult manifests() override;
    Q_INVOKABLE LogosResult downloadManifest(const QString& cid) override;
    Q_INVOKABLE LogosResult stop() override;
    Q_INVOKABLE LogosResult destroy() override;
    Q_INVOKABLE void importFiles(const QString& path);

    QString name() const override { return "storage_module"; }
    QString version() const override { return "1.0.0"; }

    // LogosAPI initialization
    Q_INVOKABLE void initLogos(LogosAPI* logosAPIInstance);

  signals:
    // for now this is required for events, later it might not be necessary if using a proxy
    void eventResponse(const QString& eventName, const QVariantList& data);

    // This signal is used when we need a synchronous response.
    // The operation will allow to distinguish which function the response is for.
    void storageResponse(const StorageSignal& signal, int code, const QString& message);

  private:
    void* storageCtx;
    bool isStarted = false;

    // Helper to simulate synchronous calls.
    // It waits for the signal to be emitted with the matching StorageSignal value,
    // and returns the result.
    // If the signal is not received within the timeout, it returns an error.
    LogosResult waitForSignal(const StorageSignal& signal, int timeout);

    // Generic helper that handles all sync call types with optional arguments.
    LogosResult syncCall(StorageSignal signal, StorageNoArgFunction fn, int timeout = DEFAULT_SYNC_TIMEOUT);
    LogosResult syncCall(StorageSignal signal, StorageStringArgFunction fn, const QString& arg,
                         int timeout = DEFAULT_SYNC_TIMEOUT);
    LogosResult syncCall(StorageSignal signal, StorageStringArgAndIntArgFunction fn, const QString& arg1, int arg2,
                         int timeout = DEFAULT_SYNC_TIMEOUT);

    // Callback used by libstorage to pass the data back to the Storage Module.
    static void callback(int callerRet, const char* msg, size_t len, void* userData);
};
