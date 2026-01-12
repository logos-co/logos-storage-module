#pragma once

#include <QtCore/QObject>
#include "storage_module_interface.h"
#include "logos_api.h"
#include "logos_api_client.h"
#include "libstorage.h"
#include <QMutex>
#include <QWaitCondition>
#include <QCoreApplication>

class StorageModulePlugin : public QObject, public StorageModuleInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID StorageModuleInterface_iid FILE "metadata.json")
    Q_INTERFACES(StorageModuleInterface PluginInterface)

public:
    StorageModulePlugin();
    ~StorageModulePlugin();

    Q_INVOKABLE bool init(const QString &cfg) override;
    Q_INVOKABLE bool start() override;
    Q_INVOKABLE bool version() override;
    Q_INVOKABLE bool stop() override;
    Q_INVOKABLE bool destroy() override;
    QString name() const override { return "storage_module"; }
    QString version() const override { return "1.0.0"; }

    // LogosAPI initialization
    Q_INVOKABLE void initLogos(LogosAPI *logosAPIInstance);

signals:
    // for now this is required for events, later it might not be necessary if using a proxy
    void eventResponse(const QString& eventName, const QVariantList& data);
    void storageClosed(int code);
    void storageStopped(int code);
    
private:
    void *storageCtx;
    bool isRunning = false;

    // Static callback functions for storage
    static void callback(int callerRet, const char *msg, size_t len, void *userData);
    static void string_callback(int callerRet, const char *msg, size_t len, void *userData);
    static void close_callback(int callerRet, const char *msg, size_t len, void *userData);
};
