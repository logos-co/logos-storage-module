#include "storage_module_plugin.h"
#include <QDebug>
#include <QCoreApplication>
#include <QVariantList>
#include <QDateTime>

StorageModulePlugin::StorageModulePlugin() : storageCtx(nullptr)
{
    qDebug() << "StorageModulePlugin: Initializing...";
    qDebug() << "StorageModulePlugin: Initialized successfully";
}

StorageModulePlugin::~StorageModulePlugin()
{
    // Clean up resources
    if (logosAPI)
    {
        delete logosAPI;
        logosAPI = nullptr;
    }

    // Clean up Waku context if it exists
    if (storageCtx)
    {
        // TODO: Call waku_destroy when needed
        storageCtx = nullptr;
    }
}

bool StorageModulePlugin::foo(const QString &bar)
{
    qDebug() << "StorageModulePlugin::foo called with:" << bar;


    // Create event data with the bar parameter
    QVariantList eventData;
    eventData << bar;                                                // Add the bar parameter to the event data
    eventData << QDateTime::currentDateTime().toString(Qt::ISODate); // Add timestamp

    // Trigger the event using LogosAPI client (like chat module does)
    if (logosAPI)
    {
        // print triggering signal
        qDebug() << "StorageModulePlugin: Triggering event 'fooTriggered' with data:" << eventData;
        logosAPI->getClient("core_manager")->onEventResponse(this, "fooTriggered", eventData);
        qDebug() << "StorageModulePlugin: Event 'fooTriggered' triggered with data:" << eventData;
    }
    else
    {
        qWarning() << "StorageModulePlugin: LogosAPI not available, cannot trigger event";
    }

    return true;
}

void StorageModulePlugin::init_callback(int callerRet, const char *msg, size_t len, void *userData)
{
    qDebug() << "StorageModulePlugin::init_callback called with ret:" << callerRet;
    if (msg && len > 0)
    {
        QString message = QString::fromUtf8(msg, len);
        qDebug() << "StorageModulePlugin::init_callback message:" << message;
    }
}

// void StorageModulePlugin::start_callback(int callerRet, const char* msg, size_t len, void* userData)
// {
//     qDebug() << "StorageModulePlugin::start_callback called with ret:" << callerRet;
//     if (msg && len > 0) {
//         QString message = QString::fromUtf8(msg, len);
//         qDebug() << "StorageModulePlugin::start_callback message:" << message;
//     }
// }

void StorageModulePlugin::initLogos(LogosAPI *logosAPIInstance)
{
    if (logosAPI)
    {
        delete logosAPI;
    }
    logosAPI = logosAPIInstance;
}

bool StorageModulePlugin::initStorage(const QString &cfg)
{
    qDebug() << "StorageModulePlugin::initStorage called with cfg:" << cfg;

    // Convert QString to UTF-8 byte array
    QByteArray cfgUtf8 = cfg.toUtf8();

    // Call storage_new with the configuration
    storageCtx = storage_new(cfgUtf8.constData(), init_callback, this);

    if (storageCtx)
    {
        qDebug() << "StorageModulePlugin: Storage context created successfully";
        return true;
    }
    else
    {
        qWarning() << "StorageModulePlugin: Failed to create Storage context";
        return false;
    }
}

QString StorageModulePlugin::storageVersion()
{
    qDebug() << "StorageModulePlugin::repo called";

    if (storageCtx)
    {
        storage_repo(storageCtx, init_callback, this);
    }

    return "";
}
