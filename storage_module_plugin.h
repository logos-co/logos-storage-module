#pragma once

#include "libstorage.h"
#include "logos_api.h"
#include "logos_api_client.h"
#include "storage_module_interface.h"
#include <QCoreApplication>
#include <QMutex>
#include <QWaitCondition>
#include <QtCore/QObject>

class StorageModulePlugin : public QObject, public StorageModuleInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID StorageModuleInterface_iid FILE "metadata.json")
    Q_INTERFACES(StorageModuleInterface PluginInterface)

  public:
    StorageModulePlugin();
    ~StorageModulePlugin();

    Q_INVOKABLE bool init(const QString& cfg) override;
    Q_INVOKABLE bool start() override;
    Q_INVOKABLE QString version() override;
    Q_INVOKABLE QString dataDir() override;
    Q_INVOKABLE QString peerId() override;
    Q_INVOKABLE QString debug() override;
    Q_INVOKABLE QString spr() override;
    Q_INVOKABLE bool updateLogLevel(const QString& logLevel) override;
    Q_INVOKABLE bool connect(const QString& peerId, const QStringList& peerAddresses) override;
    Q_INVOKABLE QString uploadInit(const QString& filename, const int chunkSize = 1024 * 64) override;
    Q_INVOKABLE bool uploadCancel(const QString& sessionId) override;
    Q_INVOKABLE QString uploadFinalize(const QString& sessionId) override;
    Q_INVOKABLE bool uploadChunk(const QString& sessionId, const QByteArray& chunk) override;
    Q_INVOKABLE bool uploadFile(const QString& sessionId) override;
    Q_INVOKABLE QString uploadFromPath(const QUrl& url, const int chunkSize = 1024 * 64) override;
    QString uploadFromIO(std::unique_ptr<QIODevice> device, const int chunkSize = 1024 * 64) override;

    Q_INVOKABLE bool stop() override;
    Q_INVOKABLE bool destroy() override;
    QString name() const override { return "storage_module"; }
    QString version() const override { return "1.0.0"; }

    // LogosAPI initialization
    Q_INVOKABLE void initLogos(LogosAPI* logosAPIInstance);

  signals:
    // for now this is required for events, later it might not be necessary if using a proxy
    void eventResponse(const QString& eventName, const QVariantList& data);

    void storageClosed(int code, const QString& message);
    void storageStopped(int code);
    void storageVersion(int code, const QString& message);
    void storageDebug(int code, const QString& message);
    void storageDataDir(int code, const QString& message);
    void storagePeerId(int code, const QString& message);
    void storageSpr(int code, const QString& message);
    void storageLogLevel(int code, const QString& message);
    void storageConnect(int code, const QString& message);
    void storageUploadInit(int code, const QString& message);
    void storageUploadCancel(int code, const QString& message);
    void storageUploadFinalize(int code, const QString& message);

  private:
    void* storageCtx;

    QString waitForSignal(void (StorageModulePlugin::*s)(int, const QString&), int timeout);

    // Static callback functions for storage
    static void callback(int callerRet, const char* msg, size_t len, void* userData);
    static void eventCallback(int callerRet, const char* msg, size_t len, void* userData);
    static void signalCallback(int callerRet, const char* msg, size_t len, void* userData);
    static void uploadChunkCallback(int callerRet, const char* msg, size_t len, void* userData);
    static void uploadFileCallback(int callerRet, const char* msg, size_t len, void* userData);
};
