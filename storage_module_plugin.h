#pragma once

#include <QtCore/QObject>
#include "storage_module_interface.h"
#include "logos_api.h"
#include "logos_api_client.h"
#include "libstorage.h"

class StorageModulePlugin : public QObject, public StorageModuleInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID StorageModuleInterface_iid FILE "metadata.json")
    Q_INTERFACES(StorageModuleInterface PluginInterface)

public:
    StorageModulePlugin();
    ~StorageModulePlugin();

    Q_INVOKABLE bool foo(const QString &bar) override;
    Q_INVOKABLE QString storageVersion() override;
    Q_INVOKABLE bool initStorage(const QString &cfg) override;
    QString name() const override { return "storage_module"; }
    QString version() const override { return "1.0.0"; }

    // LogosAPI initialization
    Q_INVOKABLE void initLogos(LogosAPI *logosAPIInstance);

private:
    void *storageCtx;

    // Static callback functions for storage
    static void init_callback(int callerRet, const char *msg, size_t len, void *userData);
    // static void start_callback(int callerRet, const char* msg, size_t len, void* userData);
};