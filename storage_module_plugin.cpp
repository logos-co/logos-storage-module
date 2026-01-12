#include "storage_module_plugin.h"
#include <QDebug>
#include <QCoreApplication>
#include <QVariantList>
#include <QDateTime>
#include <QMutexLocker>
#include <csignal>

struct StringCallbackCtx {
    StorageModulePlugin* plugin;
    QString eventName;
};

void StorageModulePlugin::initLogos(LogosAPI *logosAPIInstance)
{
    if (logosAPI)
    {
        delete logosAPI;
    }
    logosAPI = logosAPIInstance;
}

StorageModulePlugin::StorageModulePlugin(): storageCtx(nullptr)
{
}

StorageModulePlugin::~StorageModulePlugin()
{
    qDebug() << "StorageModulePlugin: Destructor called";

    // Clean up resources
    if (logosAPI)
    {
        delete logosAPI;
        logosAPI = nullptr;
    }

    // Clean up Storage context if it exists
    if (storageCtx)
    {
        storageCtx = nullptr;
    }
}

void StorageModulePlugin::callback(int callerRet, const char *msg, size_t len, void *userData)
{
    qDebug() << "StorageModulePlugin::callback called with ret:" << callerRet;

    if (msg && len > 0)
    {
        QString message = QString::fromUtf8(msg, len);
        qDebug() << "StorageModulePlugin::callback message:" << message << "is ignored";
    }
}

void StorageModulePlugin::string_callback(int callerRet, const char *msg, size_t len, void *userData)
{
    auto* ctx = static_cast<StringCallbackCtx *>(userData);
    if (!ctx || !ctx->plugin)
    {
        qWarning() << "StorageModulePlugin::string_callback: Invalid userData";
        return;
    }

    QString eventName = ctx->eventName;

    qDebug() << "StorageModulePlugin::string_callback called with ret:" << callerRet << "for event:" << eventName;

    QString message = QString::fromUtf8(msg, len);
    StorageModulePlugin *plugin = ctx->plugin;
    QVariantList eventData;
    eventData << callerRet;
    eventData << message;

    if (plugin->logosAPI)  { 
        plugin->logosAPI->getClient("core_manager")
            ->onEventResponse(plugin, eventName, eventData);
    }

    if (eventName == "storageStop") { 
        emit plugin->storageStopped(callerRet);
    }

    qDebug() << "StorageModulePlugin::onEventResponse called for event:" << eventName << "with message:" << message;
}


bool StorageModulePlugin::init(const QString &cfg)
{
    qDebug() << "StorageModulePlugin::init called with cfg:" << cfg;

    QByteArray cfgUtf8 = cfg.toUtf8();

    storageCtx = storage_new(cfgUtf8.constData(), callback, this);

    if (storageCtx)
    {
        qDebug() << "StorageModulePlugin: Storage context created successfully";
        return true;
    }

    qWarning() << "StorageModulePlugin: Failed to create Storage context";

    return false;
}

bool StorageModulePlugin::start()
{
    qDebug() << "StorageModulePlugin::start called";

    if (!storageCtx)
    {
        qWarning() << "StorageModulePlugin::start: Storage context is not initialized";
        return false;
    }

    if (isRunning) {
        qWarning() << "StorageModulePlugin::start: Storage module is already running";
        return true;
    }

    int ret = storage_start(storageCtx, string_callback, new StringCallbackCtx{this, "storageStart"});

    qDebug() << "StorageModulePlugin::start: storage_start ret =" << ret;

    isRunning = (ret == RET_OK);

    return (ret == RET_OK);
}

bool StorageModulePlugin::stop()
{
    qDebug() << "StorageModulePlugin::stop called";

    if (!storageCtx)
    {
        qWarning() << "StorageModulePlugin::stop: Storage context is not initialized";
        return false;
    }

    if (!isRunning) {
        qWarning() << "StorageModulePlugin::stop: Storage module is not running";
        return true;
    }

    int ret = storage_stop(storageCtx, string_callback, new StringCallbackCtx{this, "storageStop"});

    qDebug() << "StorageModulePlugin::stop: storage_stop ret =" << ret;

    isRunning = !(ret == RET_OK);

    return (ret == RET_OK);
}

bool StorageModulePlugin::version()
{
    qDebug() << "StorageModulePlugin::version called";

    if (!storageCtx)
    {
        qWarning() << "StorageModulePlugin::version: Storage context is not initialized";
        return false;
    }

    int ret = storage_repo(storageCtx, string_callback, new StringCallbackCtx{this, "storageVersion"});

    qDebug() << "StorageModulePlugin::version: storage_repo ret =" << ret;

    return (ret == RET_OK);
}

bool StorageModulePlugin::destroy()
{
    qDebug() << "StorageModulePlugin::destroy called";

    QEventLoop loop;
    
    if (!storageCtx)
    {
        qWarning() << "StorageModulePlugin::destroy: Storage context is not initialized";
        return true;
    }

    QObject::connect(this, &StorageModulePlugin::storageClosed, this, [&](int ret){ 
        if (ret == RET_OK) {
            qDebug() << "StorageModulePlugin::destroy: storageClosed ret =" << ret;
        } else {
            qWarning() << "StorageModulePlugin::destroy: storageClosed failed with ret =" << ret;
        }

        loop.quit(); 
    });

    int closeRet = storage_close(storageCtx, close_callback, this);

    qDebug() << "StorageModulePlugin::destroy: storage_close ret =" << closeRet;

    loop.exec();

    int destroyRet = storage_destroy(storageCtx, callback, this);

    qDebug() << "StorageModulePlugin::destroy: storage_destroy ret =" << destroyRet;

    return closeRet == RET_OK && destroyRet == RET_OK;
}

void StorageModulePlugin::close_callback(int callerRet, const char *msg, size_t len, void *userData)
{
    qDebug() << "StorageModulePlugin::close_callback called with ret:" << callerRet;

    auto* plugin = static_cast<StorageModulePlugin *>(userData);
    if (!plugin) {
        qWarning() << "StorageModulePlugin::close_callback: Invalid userData";
        return;
    }

    emit plugin->storageClosed(callerRet);
}