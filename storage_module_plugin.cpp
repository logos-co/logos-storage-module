#include "storage_module_plugin.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QMutexLocker>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

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
    StorageUploadInit,
    StorageUploadCancel,
    StorageUploadFinalize
};

// The event callback context contains the
// event name to emit upon callback.
// Data vector is used to keep data alive during the async call.
// They are freed after the callback is done.
struct EventCallbackCtx {
    StorageModulePlugin* plugin;
    QString eventName;
};

// The sync callback context contains the
// signal to emit upon callback.
struct SignalCallbackCtx {
    StorageModulePlugin* plugin;
    StorageSignal signal;

    // Extra data to ensure that args are still valid
    // during the async call.
    QByteArray lifetimeUtf8;
};

struct ConnectCallbackCtx : EventCallbackCtx {
    char* peerId;
    QVector<char*> addrs;
};

struct UploadChunkCallbackCtx {
    StorageModulePlugin* plugin;
    QByteArray sessionIdUtf8;
    QByteArray chunk;
};

struct UploadFileCallbackCtx {
    StorageModulePlugin* plugin;
    QByteArray sessionIdUtf8;
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
    const QPointer<StorageModulePlugin> plugin = ctx->plugin;

    // Delete the context to avoid memory leaks.
    delete ctx;

    // Make sure the plugin is still valid.
    if (!plugin) {
        return;
    }

    // Build the event data to send to the UI.
    const QVariantList eventData{callerRet, message};

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

void StorageModulePlugin::uploadChunkCallback(int callerRet, const char* msg, size_t len, void* userData) {
    // Build the context from userData
    auto* ctx = static_cast<UploadChunkCallbackCtx*>(userData);
    if (!ctx) {
        qWarning() << "StorageModulePlugin::uploadChunkCallback: Invalid userData";
        return;
    }

    qDebug() << "StorageModulePlugin::uploadChunkCallback called with ret:" << callerRet;

    // Use QPointer to safely reference the plugin instance.
    const QPointer<StorageModulePlugin> plugin = ctx->plugin;
    const QString sessionId = ctx->sessionIdUtf8;
    const int size = ctx->chunk.size();

    // Delete the context to avoid memory leaks.
    delete ctx;

    // Make sure the plugin is still valid.
    if (!plugin) {
        return;
    }

    const QVariantList eventData{callerRet, sessionId, size};
    const QString eventName = "storageUploadProgress";

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

    qDebug() << "StorageModulePlugin::uploadChunkCallback scheduled for event:" << eventName;
}

void StorageModulePlugin::uploadFileCallback(int callerRet, const char* msg, size_t len, void* userData) {
    // Build the context from userData
    auto* ctx = static_cast<UploadFileCallbackCtx*>(userData);
    if (!ctx) {
        qWarning() << "StorageModulePlugin::uploadFileCallback: Invalid userData";
        return;
    }

    qDebug() << "StorageModulePlugin::uploadFileCallback called with ret:" << callerRet;

    // Use QPointer to safely reference the plugin instance.
    QPointer<StorageModulePlugin> plugin = ctx->plugin;
    QString sessionId = QString::fromUtf8(ctx->sessionIdUtf8);

    if (callerRet != RET_PROGRESS) {
        qDebug() << "StorageModulePlugin::uploadFileCallback will deleting context...";
        // Delete the context to avoid memory leaks.
        delete ctx;
    }

    // Make sure the plugin is still valid.
    if (!plugin) {
        return;
    }

    qDebug() << "StorageModulePlugin::uploadFileCallback: Preparing event data";

    QVariantList eventData{callerRet, sessionId};
    QString eventName;

    if (callerRet == RET_PROGRESS) {
        const int size = static_cast<int>(len);
        eventName = "storageUploadProgress";
        eventData.push_back(size);
    } else if (callerRet == RET_OK) {
        eventName = "storageUploadDone";
        if (len > 0) {
            const int size = static_cast<int>(len);
            eventData.push_back(QString::fromUtf8(msg, size));
        }
    }

    qDebug() << "StorageModulePlugin::uploadFileCallback: event data ready with eventName=" << eventName;

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

    qDebug() << "StorageModulePlugin::uploadChunkCallback scheduled for event:" << eventName;
}

// Helper method to wait for a specific signal with a timeout
// Returns the message received with the signal or an empty string on timeout.
QString StorageModulePlugin::waitForSignal(void (StorageModulePlugin::*sig)(int, const QString&), int timeout) {
    QEventLoop loop;
    QString msg;

    qDebug() << "StorageModulePlugin::waitForSignal: Waiting for signal with timeout" << timeout << "ms";

    // Connect the signal to capture the message.
    // Connection is used to disconnect after receiving the signal.
    QMetaObject::Connection connection;
    connection = QObject::connect(this, sig, &loop, [&](int recode, const QString& m) {
        if (recode != RET_OK) {
            qWarning() << "StorageModulePlugin::waitForSignal: Received error code" << recode << "with message:" << m;
            if (msg.isEmpty()) {
                msg = QString("Unknown error received.");
            }
        } else {
            // Store the result message to return later.
            msg = m;
        }

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
        msg = QString("Cannot get response before timeout.");
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
    const QPointer<StorageModulePlugin> plugin = ctx->plugin;

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
            case StorageSignal::StorageUploadInit:
                emit plugin->storageUploadInit(callerRet, result);
                break;
            case StorageSignal::StorageUploadCancel:
                emit plugin->storageUploadCancel(callerRet, result);
                break;
            case StorageSignal::StorageUploadFinalize:
                emit plugin->storageUploadFinalize(callerRet, result);
                break;
            }
        },
        Qt::QueuedConnection);
}

// Initialize the storage module with the given configuration.
// The method is synchronous.
bool StorageModulePlugin::init(const QString& cfg) {
    qDebug() << "StorageModulePlugin::init called with cfg:" << cfg;

    const QByteArray cfgUtf8 = cfg.toUtf8();

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

    const int ret = storage_start(storageCtx, eventCallback, new EventCallbackCtx{this, "storageStart"});

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

    const int ret = storage_stop(storageCtx, eventCallback, new EventCallbackCtx{this, "storageStop"});

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

    const int timeout = 1000;
    const QString version = waitForSignal(&StorageModulePlugin::storageVersion, timeout);

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

    const int timeout = 1000;
    const QString dataDir = waitForSignal(&StorageModulePlugin::storageDataDir, timeout);

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

    const int timeout = 1000;
    const QString peerId = waitForSignal(&StorageModulePlugin::storagePeerId, timeout);

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

    const int timeout = 1000;
    const QString spr = waitForSignal(&StorageModulePlugin::storageSpr, timeout);

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

    const int timeout = 1000;
    const QString debugInfo = waitForSignal(&StorageModulePlugin::storageDebug, timeout);

    qDebug() << "StorageModulePlugin::debug: storageDebug event received";

    return debugInfo;
}

// Get the log level of the node
// The method is synchronous.
bool StorageModulePlugin::updateLogLevel(const QString& logLevel) {
    qDebug() << "StorageModulePlugin::updateLogLevel called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::updateLogLevel: Storage context is not initialized";
        return false;
    }

    const std::string levelStr(logLevel.toStdString());
    const int ret = storage_log_level(storageCtx, levelStr.c_str(), signalCallback,
                                      new SignalCallbackCtx{this, StorageSignal::StorageLogLevel});

    qDebug() << "StorageModulePlugin::updateLogLevel: storage_log_level ret =" << ret;

    if (ret != RET_OK) {
        return false;
    }

    const int timeout = 1000;
    const QString result = waitForSignal(&StorageModulePlugin::storageLogLevel, timeout);

    qDebug() << "StorageModulePlugin::updateLogLevel: storageLogLevel event received";

    // Success response is an empty string for log level update
    return result.isEmpty();
}

// Connect to a peer by its peer id
// Emit "storageConnect" event on completion.
bool StorageModulePlugin::connect(const QString& peerId, const QStringList& peerAddresses) {
    qDebug() << "StorageModulePlugin::connect called with peerId << " << peerId
             << "and peerAddresses =" << peerAddresses;

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::connect: Storage context is not initialized";
        return false;
    }

    auto* ctx = new ConnectCallbackCtx{this, "storageConnect"};

    ctx->addrs.reserve(peerAddresses.size());
    for (const auto& addr : peerAddresses) {
        ctx->addrs.append(strdup(addr.toUtf8().constData()));
    }

    const int ret = storage_connect(storageCtx, ctx->peerId, const_cast<const char**>(ctx->addrs.data()),
                                    static_cast<size_t>(ctx->addrs.size()), eventCallback, ctx);

    qDebug() << "StorageModulePlugin::connect: storage_connect ret =" << ret;

    if (ret != RET_OK) {
        delete ctx;
        return false;
    }

    return true;
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

    const int closeRet =
        storage_close(storageCtx, signalCallback, new SignalCallbackCtx{this, StorageSignal::StorageClosed});

    qDebug() << "StorageModulePlugin::destroy: storage_close ret =" << closeRet;

    const int timeout = 5000;
    waitForSignal(&StorageModulePlugin::storageClosed, timeout);

    qDebug() << "StorageModulePlugin::destroy: storageClosed event received";

    const int destroyRet = storage_destroy(storageCtx, callback, this);

    qDebug() << "StorageModulePlugin::destroy: storage_destroy ret =" << destroyRet;

    if (destroyRet == RET_OK) {
        storageCtx = nullptr;
    }

    return closeRet == RET_OK && destroyRet == RET_OK;
}

// Initialise an upload session.
// The method is synchronous.
// Return the session id as QString when successfull.
QString StorageModulePlugin::uploadInit(const QString& filename, const int chunkSize) {
    qDebug() << "StorageModulePlugin::uploadInit called with filename:" << filename << "chunkSize:" << chunkSize;

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::uploadInit: Storage context is not initialized";
        return QString();
    }

    // Create a context for the signal and pass the filename to ensure that the
    // filename is valid in the callback.
    auto* ctx = new SignalCallbackCtx{this, StorageSignal::StorageUploadInit, filename.toUtf8()};

    const size_t chunkSizeC = static_cast<size_t>(chunkSize);
    const int ret = storage_upload_init(storageCtx, ctx->lifetimeUtf8.constData(), chunkSizeC, signalCallback, ctx);

    if (ret != RET_OK) {
        qDebug() << "StorageModulePlugin::uploadInit failed with ret =" << ret;
        delete ctx;
        return QString();
    }

    qDebug() << "StorageModulePlugin::uploadInit: storage_upload_init ret =" << ret;

    const int timeout = 1000;
    const QString sessionId = waitForSignal(&StorageModulePlugin::storageUploadInit, timeout);

    qDebug() << "StorageModulePlugin::uploadInit: storageUploadInit event received, sessionId=" << sessionId;

    return sessionId;
}

// Cancel an upload session.
// The method is synchronous.
// Return true if the session was cancelled successfully.
bool StorageModulePlugin::uploadCancel(const QString& sessionId) {
    qDebug() << "StorageModulePlugin::uploadCancel called with sessionId:" << sessionId;

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::uploadCancel: Storage context is not initialized";
        return false;
    }

    // Create a context for the signal and pass the filename to ensure that the
    // sessionId is valid in the callback.
    auto* ctx = new SignalCallbackCtx{this, StorageSignal::StorageUploadCancel, sessionId.toUtf8()};

    const int ret = storage_upload_cancel(storageCtx, ctx->lifetimeUtf8.constData(), signalCallback, ctx);

    if (ret != RET_OK) {
        qDebug() << "StorageModulePlugin::uploadCancel failed with ret =" << ret;
        delete ctx;
        return false;
    }

    qDebug() << "StorageModulePlugin::uploadCancel: storage_upload_cancel ret =" << ret;

    const int timeout = 1000;
    const QString error = waitForSignal(&StorageModulePlugin::storageUploadCancel, timeout);

    qDebug() << "StorageModulePlugin::uploadCancel: storageUploadCancel event received, error=" << error;

    return error == "";
}

// Finalize an upload session.
// The method is synchronous.
// Return the CID if the upload was successful.
QString StorageModulePlugin::uploadFinalize(const QString& sessionId) {
    qDebug() << "StorageModulePlugin::uploadFinalize called with sessionId:" << sessionId;

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::uploadFinalize: Storage context is not initialized";
        return "";
    }

    // Create a context for the signal and pass the session id to ensure that the
    // sessionId is valid in the callback.
    auto* ctx = new SignalCallbackCtx{this, StorageSignal::StorageUploadFinalize, sessionId.toUtf8()};

    const int ret = storage_upload_finalize(storageCtx, ctx->lifetimeUtf8.constData(), signalCallback, ctx);

    if (ret != RET_OK) {
        qDebug() << "StorageModulePlugin::uploadFinalize failed with ret =" << ret;
        delete ctx;
        return "";
    }

    qDebug() << "StorageModulePlugin::uploadFinalize: storage_upload_finalize ret =" << ret;

    const int timeout = 1000;
    const QString cid = waitForSignal(&StorageModulePlugin::storageUploadFinalize, timeout);

    qDebug() << "StorageModulePlugin::uploadFinalize: storageuploadFinalize event received, cid=" << cid;

    return cid;
}

// Upload a chunk.
// This method is asynchronous.
// Emit "storageUploadProgress" event on completion.
bool StorageModulePlugin::uploadChunk(const QString& sessionId, const QByteArray& chunk) {
    qDebug() << "StorageModulePlugin::uploadChunk called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::uploadChunk: Storage context is not initialized";
        return false;
    }

    // Create a context for the signal and pass the session id to ensure that the
    // sessionId is valid in the callback.
    // The chunk data is also stored in the context to ensure its validity.
    // That should not make a copy of the data, because of a Qt rule called
    //
    // Implicit Sharing
    //
    // As long as we only read only and nobody modifies the shared array,
    // no deep copy of the chunk bytes happens. A deep copy would only occur if a write
    // is attempted.
    auto* ctx = new UploadChunkCallbackCtx{
        this,
        sessionId.toUtf8(),
        chunk,
    };

    const uint8_t* ptr = ctx->chunk.isEmpty() ? nullptr : reinterpret_cast<const uint8_t*>(ctx->chunk.constData());
    const size_t len = static_cast<size_t>(ctx->chunk.size());

    const int ret =
        storage_upload_chunk(storageCtx, ctx->sessionIdUtf8.constData(), ptr, len, uploadChunkCallback, ctx);

    qDebug() << "StorageModulePlugin::uploadChunk: storage_upload_chunk ret =" << ret;

    if (ret != RET_OK) {
        delete ctx;
        return false;
    }

    return true;
}

// Upload a file.
// This method is asynchronous.
// Emit "storageUploadProgress" event on progress.
// Emit "storageUploadDone" event on completion.
bool StorageModulePlugin::uploadFile(const QString& sessionId) {
    qDebug() << "StorageModulePlugin::uploadChunk called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::uploadFile: Storage context is not initialized";
        return false;
    }

    // Create a context for the signal and pass the session id to ensure that the
    // sessionId is valid in the callback.
    auto* ctx = new UploadFileCallbackCtx{
        this,
        sessionId.toUtf8(),
    };

    const int ret = storage_upload_file(storageCtx, ctx->sessionIdUtf8.constData(), uploadFileCallback, ctx);

    qDebug() << "StorageModulePlugin::uploadFile: storage_upload_file ret =" << ret;

    if (ret != RET_OK) {
        delete ctx;
        return false;
    }

    return true;
}

QString StorageModulePlugin::uploadFromPath(const QUrl& url, const int chunkSize) {
    qDebug() << "StorageModulePlugin::uploadFromPath called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::uploadFromPath: Storage context is not initialized";
        return "";
    }

    if (!url.isValid()) {
        qWarning() << "StorageModulePlugin::uploadFromPath: QUrl is not valid.";
        return "";
    }

    if (!url.isLocalFile()) {
        qWarning() << "StorageModulePlugin::uploadFromPath: QUrl is not a local file.";
        return "";
    }

    QString path = url.toLocalFile();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        qWarning() << "StorageModulePlugin::uploadFromPath: path empty or does not exist path=" << path;
        return "";
    }

    QString filename = QFileInfo(path).fileName();

    // Create a context for the signal and pass the filename to ensure that the
    // filename is valid in the callback.
    auto* ctx = new SignalCallbackCtx{this, StorageSignal::StorageUploadInit, filename.toUtf8()};

    const size_t chunkSizeC = static_cast<size_t>(chunkSize);
    const int ret = storage_upload_init(storageCtx, ctx->lifetimeUtf8.constData(), chunkSizeC, signalCallback, ctx);

    if (ret != RET_OK) {
        qDebug() << "StorageModulePlugin::uploadFromPath storage_upload_init failed with ret =" << ret;
        delete ctx;
        return "";
    }

    qDebug() << "StorageModulePlugin::uploadFromPath: storage_upload_init ret =" << ret;

    const int timeout = 1000;
    const QString sessionId = waitForSignal(&StorageModulePlugin::storageUploadInit, timeout);

    qDebug() << "StorageModulePlugin::uploadFromPath: storageUploadInit event received, sessionId=" << sessionId;

    if (sessionId.isEmpty()) {
        qWarning() << "StorageModulePlugin::uploadFromPath: Failed to get sessionId.";
        delete ctx;
        return "";
    }

    // Create a context for the signal and pass the session id to ensure that the
    // sessionId is valid in the callback.
    auto* uploadFileCtx = new UploadFileCallbackCtx{
        this,
        sessionId.toUtf8(),
    };

    const int uploadFileRet =
        storage_upload_file(storageCtx, uploadFileCtx->sessionIdUtf8.constData(), uploadFileCallback, uploadFileCtx);

    qDebug() << "StorageModulePlugin::uploadFromPath: storage_upload_file ret =" << uploadFileRet;

    if (uploadFileRet != RET_OK) {
        qDebug() << "StorageModulePlugin::uploadFromPath: storage_upload_file failed with ret =" << uploadFileRet;
        // TODO cancel the session
        delete ctx;
        delete uploadFileCtx;
        return "";
    }

    return sessionId;
}

QString StorageModulePlugin::uploadFromIO(std::unique_ptr<QIODevice> device, const int chunkSize) {
    qDebug() << "StorageModulePlugin::uploadFromIO is not implemented";
    return "";
}

// todo check uploadLoglevel and string parameter
// todo check connect callback
