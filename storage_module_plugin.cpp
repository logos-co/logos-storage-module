#include "storage_module_plugin.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <csignal>

// Provide signals in order to provide
// a synchronous interface over asynchronous calls
// for certain methods.
enum class StorageSignal {
    StorageClosed,
    StorageVersion,
    StorageDataDir,
    StoragePeerId,
    StorageSpr,
    StorageDebug,
    StorageLogLevel,
};

// The event callback context contains the
// event name to emit upon callback.
struct EventCallbackCtx {
    StorageModulePlugin* plugin;
    QString eventName;
};

// The sync callback context contains the
// signal to emit upon callback.
struct SignalCallbackCtx {
    StorageModulePlugin* plugin;
    StorageSignal signal;
};

void StorageModulePlugin::initLogos(LogosAPI* logosAPIInstance) {
    if (logosAPI) {
        delete logosAPI;
    }
    logosAPI = logosAPIInstance;
}

StorageModulePlugin::StorageModulePlugin() : storageCtx(nullptr) {}

// Destructor implementation
// This is not the best place to put cleanup code, as the destroy method
// should be called before the destructor.
StorageModulePlugin::~StorageModulePlugin() {
    qDebug() << "StorageModulePlugin: Destructor called";

    // Clean up resources
    if (logosAPI) {
        delete logosAPI;
        logosAPI = nullptr;
    }

    // create a variable here

    // Clean up Storage context if it exists
    if (storageCtx) {
        storageCtx = nullptr;

        // The destroy should have been called before destructor
        qWarning() << "StorageModulePlugin: Warning - Storage context was not "
                      "destroyed before plugin destruction";
    }
}

// Basic callback that just logs the message.
// Used for operations that do not require a callback response like init
// function.
void StorageModulePlugin::callback(int callerRet, const char* msg, size_t len, void* userData) {
    qDebug() << "StorageModulePlugin::callback called with ret:" << callerRet;

    if (msg && len > 0) {
        QString message = QString::fromUtf8(msg, len);
        qDebug() << "StorageModulePlugin::callback message:" << message << "is ignored";
    }
}

// Event callback that emits the corresponding event name to the UI.
// The ret code and message are passed as event data.
void StorageModulePlugin::eventCallback(int callerRet, const char* msg, size_t len, void* userData) {
    // Build the context from userData
    auto* ctx = static_cast<EventCallbackCtx*>(userData);
    if (!ctx) {
        qWarning() << "StorageModulePlugin::eventCallback: Invalid userData";
        return;
    }

    // Extract the event name and message
    const QString eventName = ctx->eventName;
    const QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();

    qDebug() << "StorageModulePlugin::eventCallback called with ret:" << callerRet << "for event:" << eventName;

    // Use QPointer to safely reference the plugin instance.
    QPointer<StorageModulePlugin> plugin = ctx->plugin;

    // Delete the context to avoid memory leaks.
    delete ctx;

    // Make sure the plugin is still valid.
    if (!plugin) {
        return;
    }

    // Build the event data to send to the UI.
    QVariantList eventData{callerRet, message};

    // Use invokeMethod to ensure thread-safety when emitting the event.
    QMetaObject::invokeMethod(
        plugin.data(),
        [plugin, eventName, eventData]() {
            // Check if the plugin and logosAPI are still valid.
            if (!plugin || !plugin->logosAPI) {
                return;
            }

            // Emit the event to the core manager.
            plugin->logosAPI->getClient("core_manager")->onEventResponse(plugin.data(), eventName, eventData);
        },
        Qt::QueuedConnection);

    qDebug() << "StorageModulePlugin::onEventResponse scheduled for event:" << eventName << "with message:" << message;
}

// Helper method to wait for a specific signal with a timeout
// Returns the message received with the signal or an empty string on timeout.
QString StorageModulePlugin::waitForSignal(void (StorageModulePlugin::*sig)(int, const QString&), int timeout) {
    QEventLoop loop;
    QString msg;

    // Connect the signal to capture the message.
    // Connection is used to disconnect after receiving the signal.
    QMetaObject::Connection connection;
    connection = QObject::connect(this, sig, &loop, [&](int, const QString& m) {
        // Store the result message to return later.
        msg = m;

        // Disconnect after receiving the signal to avoid multiple triggers.
        QObject::disconnect(connection);

        loop.quit();
    });

    QTimer timer;
    // Just make sure the timer is single shot
    timer.setSingleShot(true);

    bool hasTimedOut = false;

    // Connect the timer to quit the loop on timeout
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        hasTimedOut = true;
        loop.quit();
    });

    timer.start(timeout);

    // Wait for the signal or timeout
    loop.exec();

    if (hasTimedOut) {
        qWarning() << "StorageModulePlugin::wait timed out after" << timeout << "ms";
    }

    return msg;
}

// signalCallback emits the corresponding signal upon callback.
// It is used to provide synchronous behavior for certain methods.
void StorageModulePlugin::signalCallback(int callerRet, const char* msg, size_t len, void* userData) {
    // Build the context from userData
    auto* ctx = static_cast<SignalCallbackCtx*>(userData);
    if (!ctx) {
        qWarning() << "StorageModulePlugin::signalCallback: Invalid userData";
        return;
    }

    // Extract the message
    const QString result = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();

    qDebug() << "StorageModulePlugin::signalCallback ret=" << callerRet << "signal=" << int(ctx->signal)
             << "msg=" << result;

    // Use QPointer to safely reference the plugin instance.
    QPointer<StorageModulePlugin> plugin = ctx->plugin;

    // Store the signal before deleting the context
    const StorageSignal sig = ctx->signal;

    // Delete the context to avoid memory leaks.
    delete ctx;

    // Make sure the plugin is still valid.
    if (!plugin) {
        return;
    }

    // Use invokeMethod to ensure thread-safety when emitting the signal.
    QMetaObject::invokeMethod(
        plugin.data(),
        [plugin, sig, callerRet, result] {
            // Check if the plugin is still valid.
            if (!plugin) {
                return;
            }

            // Emit the corresponding signal based on the stored signal type.
            switch (sig) {
            case StorageSignal::StorageClosed:
                emit plugin->storageClosed(callerRet, QString());
                break;
            case StorageSignal::StorageVersion:
                emit plugin->storageVersion(callerRet, result);
                break;
            case StorageSignal::StorageDataDir:
                emit plugin->storageDataDir(callerRet, result);
                break;
            case StorageSignal::StorageDebug:
                emit plugin->storageDebug(callerRet, result);
                break;
            case StorageSignal::StoragePeerId:
                emit plugin->storagePeerId(callerRet, result);
                break;
            case StorageSignal::StorageSpr:
                emit plugin->storageSpr(callerRet, result);
                break;
            case StorageSignal::StorageLogLevel:
                emit plugin->storageLogLevel(callerRet, result);
                break;
            }
        },
        Qt::QueuedConnection);
}

// Initialize the storage module with the given configuration.
// The method is synchronous.
bool StorageModulePlugin::init(const QString& cfg) {
    qDebug() << "StorageModulePlugin::init called with cfg:" << cfg;

    QByteArray cfgUtf8 = cfg.toUtf8();

    storageCtx = storage_new(cfgUtf8.constData(), callback, this);

    if (storageCtx) {
        qDebug() << "StorageModulePlugin: Storage context created successfully";
        return true;
    }

    qWarning() << "StorageModulePlugin: Failed to create Storage context";

    return false;
}

// Start the storage module
// Emit "storageStart" event on completion.
bool StorageModulePlugin::start() {
    qDebug() << "StorageModulePlugin::start called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::start: Storage context is not initialized";
        return false;
    }

    int ret = storage_start(storageCtx, eventCallback, new EventCallbackCtx{this, "storageStart"});

    qDebug() << "StorageModulePlugin::start: storage_start ret =" << ret;

    return (ret == RET_OK);
}

// Stop the storage module
// Emit "storageStop" event on completion.
bool StorageModulePlugin::stop() {
    qDebug() << "StorageModulePlugin::stop called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::stop: Storage context is not initialized";
        return false;
    }

    int ret = storage_stop(storageCtx, eventCallback, new EventCallbackCtx{this, "storageStop"});

    qDebug() << "StorageModulePlugin::stop: storage_stop ret =" << ret;

    return (ret == RET_OK);
}

// Get the version of the storage module
// The method is synchronous.
QString StorageModulePlugin::version() {
    qDebug() << "StorageModulePlugin::version called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::version: Storage context is not initialized";
        return QString();
    }

    const int ret =
        storage_version(storageCtx, signalCallback, new SignalCallbackCtx{this, StorageSignal::StorageVersion});

    qDebug() << "StorageModulePlugin::version: storage_version ret =" << ret;

    if (ret != RET_OK) {
        return QString();
    }

    int timeout = 1000;
    QString version = waitForSignal(&StorageModulePlugin::storageVersion, timeout);

    qDebug() << "StorageModulePlugin::version: storageVersion event received";

    return version;
}

// Get the data directory used by the node
// The method is synchronous.
QString StorageModulePlugin::dataDir() {
    qDebug() << "StorageModulePlugin::dataDir called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::dataDir: Storage context is not initialized";
        return QString();
    }

    const int ret =
        storage_repo(storageCtx, signalCallback, new SignalCallbackCtx{this, StorageSignal::StorageDataDir});

    qDebug() << "StorageModulePlugin::dataDir: storage_repo ret =" << ret;

    if (ret != RET_OK) {
        return QString();
    }

    int timeout = 1000;
    QString dataDir = waitForSignal(&StorageModulePlugin::storageDataDir, timeout);

    qDebug() << "StorageModulePlugin::dataDir: storageDataDir event received, dataDir=" << dataDir;

    return dataDir;
}

// Get the node peer id
// The method is synchronous.
QString StorageModulePlugin::peerId() {
    qDebug() << "StorageModulePlugin::peerId called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::peerId: Storage context is not initialized";
        return QString();
    }

    const int ret =
        storage_peer_id(storageCtx, signalCallback, new SignalCallbackCtx{this, StorageSignal::StoragePeerId});

    qDebug() << "StorageModulePlugin::peerId: storage_peer_id ret =" << ret;

    if (ret != RET_OK) {
        return QString();
    }

    int timeout = 1000;
    QString peerId = waitForSignal(&StorageModulePlugin::storagePeerId, timeout);

    qDebug() << "StorageModulePlugin::peerId: storagePeerId event received, peerId=" << peerId;

    return peerId;
}

// Get the node's Signed Peer Record (SPR)
// The method is synchronous.
QString StorageModulePlugin::spr() {
    qDebug() << "StorageModulePlugin::spr called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::spr: Storage context is not initialized";
        return QString();
    }

    const int ret = storage_spr(storageCtx, signalCallback, new SignalCallbackCtx{this, StorageSignal::StorageSpr});
    qDebug() << "StorageModulePlugin::spr: storage_spr ret =" << ret;

    if (ret != RET_OK) {
        return QString();
    }

    int timeout = 1000;
    QString spr = waitForSignal(&StorageModulePlugin::storageSpr, timeout);

    qDebug() << "StorageModulePlugin::spr: storageSpr event received, spr=" << spr;

    return spr;
}

// Get the debug info of the node
// The method is synchronous.
QString StorageModulePlugin::debug() {
    qDebug() << "StorageModulePlugin::debug called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::debug: Storage context is not initialized";
        return QString();
    }

    const int ret = storage_debug(storageCtx, signalCallback, new SignalCallbackCtx{this, StorageSignal::StorageDebug});

    qDebug() << "StorageModulePlugin::debug: storage_debug ret =" << ret;

    if (ret != RET_OK) {
        return QString();
    }

    int timeout = 1000;
    QString debugInfo = waitForSignal(&StorageModulePlugin::storageDebug, timeout);

    qDebug() << "StorageModulePlugin::debug: storageDebug event received";

    return debugInfo;
}

// Get the log level of the node
// The method is synchronous.
void StorageModulePlugin::updateLogLevel(const QString& logLevel) {
    qDebug() << "StorageModulePlugin::updateLogLevel called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::updateLogLevel: Storage context is not initialized";
        return;
    }

    std::string levelStr(logLevel.toStdString());
    const int ret = storage_log_level(storageCtx, levelStr.c_str(), signalCallback,
                                      new SignalCallbackCtx{this, StorageSignal::StorageLogLevel});

    qDebug() << "StorageModulePlugin::updateLogLevel: storage_log_level ret =" << ret;

    if (ret != RET_OK) {
        return;
    }

    int timeout = 1000;
    waitForSignal(&StorageModulePlugin::storageLogLevel, timeout);

    qDebug() << "StorageModulePlugin::updateLogLevel: storageLogLevel event received";

    return;
}

// Destroy the storage module.
// The method is synchronous.
// It calls storage_close and storage_destroy internally.
bool StorageModulePlugin::destroy() {
    qDebug() << "StorageModulePlugin::destroy called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::destroy: Storage context is not initialized";
        return true;
    }

    int closeRet = storage_close(storageCtx, signalCallback, new SignalCallbackCtx{this, StorageSignal::StorageClosed});

    qDebug() << "StorageModulePlugin::destroy: storage_close ret =" << closeRet;

    int timeout = 5000;
    waitForSignal(&StorageModulePlugin::storageClosed, timeout);

    qDebug() << "StorageModulePlugin::destroy: storageClosed event received";

    int destroyRet = storage_destroy(storageCtx, callback, this);

    qDebug() << "StorageModulePlugin::destroy: storage_destroy ret =" << destroyRet;

    if (destroyRet == RET_OK) {
        storageCtx = nullptr;
    }

    return closeRet == RET_OK && destroyRet == RET_OK;
}