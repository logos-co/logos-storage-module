#include "storage_module_plugin.h"
#include <QDebug>
#include <QCoreApplication>
#include <QVariantList>
#include <QDateTime>
#include <QMutexLocker>
#include <csignal>
#include <QTimer>
#include <QPointer>

enum class StorageSignal {
    StorageClosed,
    StorageVersion,
};

struct StringCallbackCtx {
    StorageModulePlugin* plugin;
    QString eventName;
};

struct EventCallbackCtx {
    StorageModulePlugin* plugin;
    StorageSignal signal;
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

        // The destroy should have been called before destructor
        qWarning() << "StorageModulePlugin: Warning - Storage context was not destroyed before plugin destruction";
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
    if (!ctx)
    {
        qWarning() << "StorageModulePlugin::string_callback: Invalid userData";
        return;
    }

    const QString eventName = ctx->eventName;
    const QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();

    qDebug() << "StorageModulePlugin::string_callback called with ret:" << callerRet << "for event:" << eventName;

    QPointer<StorageModulePlugin> plugin = ctx->plugin;

    delete ctx;

    QVariantList eventData{ callerRet, message };

    if (!plugin) {
        return;
    }

    QMetaObject::invokeMethod(plugin.data(), [plugin, eventName, eventData]() {
        if (!plugin || !plugin->logosAPI) {
            return;
        }
        plugin->logosAPI->getClient("core_manager")->onEventResponse(plugin.data(), eventName, eventData);
    }, Qt::QueuedConnection);

    qDebug() << "StorageModulePlugin::onEventResponse scheduled for event:" << eventName << "with message:" << message;
}

QString StorageModulePlugin::wait(void (StorageModulePlugin::*sig)(int, const QString&), int timeout)
{
    QEventLoop loop;
    QTimer timer;
    QString msg;

    QMetaObject::Connection connection;
    connection = QObject::connect(this, sig, &loop, [&](int, const QString& m){
        msg = m;
        QObject::disconnect(connection);
        loop.quit();
    });

    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&](){
        loop.quit();
    });

    timer.start(timeout);
    loop.exec();

    if (timer.isActive() == false && msg.isEmpty()) {
        qWarning() << "StorageModulePlugin::wait timed out after" << timeout << "ms";
    }

    return msg;
}

void StorageModulePlugin::event_callback(int callerRet, const char *msg, size_t len, void *userData)
{
    auto* ctx = static_cast<EventCallbackCtx*>(userData);
    if (!ctx) {
        qWarning() << "StorageModulePlugin::event_callback: Invalid userData";
        return;
    }

    const QString result = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();

    qDebug() << "StorageModulePlugin::event_callback ret=" << callerRet << "signal=" << int(ctx->signal) << "msg=" << result;

    QPointer<StorageModulePlugin> plugin = ctx->plugin;
    const StorageSignal sig = ctx->signal;

    delete ctx;

    if (!plugin) {
        return;
    }

    QMetaObject::invokeMethod(plugin.data(), [plugin, sig, callerRet, result] {
        if (plugin) {
            switch (sig) {
                case StorageSignal::StorageClosed: emit plugin->storageClosed(callerRet, QString()); break;
                case StorageSignal::StorageVersion: emit plugin->storageVersion(callerRet, result); break;
            }
        }
    }, Qt::QueuedConnection);
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

    int ret = storage_start(storageCtx, string_callback, new StringCallbackCtx{this, "storageStart"});

    qDebug() << "StorageModulePlugin::start: storage_start ret =" << ret;

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

    int ret = storage_stop(storageCtx, string_callback, new StringCallbackCtx{this, "storageStop"});

    qDebug() << "StorageModulePlugin::stop: storage_stop ret =" << ret;

    return (ret == RET_OK);
}

QString StorageModulePlugin::version()
{
    qDebug() << "StorageModulePlugin::version called";

    if (!storageCtx)
    {
        qWarning() << "StorageModulePlugin::version: Storage context is not initialized";
        return QString();
    }

    const int ret = storage_version(storageCtx, event_callback, new EventCallbackCtx{this, StorageSignal::StorageVersion});

    qDebug() << "StorageModulePlugin::version: storage_version ret =" << ret;

    if (ret != RET_OK) {
        return QString();
    }

    QString version = wait(&StorageModulePlugin::storageVersion, 1000);

    qDebug() << "StorageModulePlugin::version: storageVersion event received";

    return version;
}

bool StorageModulePlugin::destroy()
{
    qDebug() << "StorageModulePlugin::destroy called";

    if (!storageCtx)
    {
        qWarning() << "StorageModulePlugin::destroy: Storage context is not initialized";
        return true;
    }

    int closeRet = storage_close(storageCtx, event_callback, new EventCallbackCtx{this, StorageSignal::StorageClosed});

    qDebug() << "StorageModulePlugin::destroy: storage_close ret =" << closeRet;

    wait(&StorageModulePlugin::storageClosed, 5000);

    qDebug() << "StorageModulePlugin::destroy: storageClosed event received";

    int destroyRet = storage_destroy(storageCtx, callback, this);

    qDebug() << "StorageModulePlugin::destroy: storage_destroy ret =" << destroyRet;

    if (destroyRet == RET_OK) {
        storageCtx = nullptr;
    }

    return closeRet == RET_OK && destroyRet == RET_OK;
}