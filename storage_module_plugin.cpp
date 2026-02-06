#include "storage_module_plugin.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QList>
#include <QMutexLocker>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

StorageModulePlugin::StorageModulePlugin() : storageCtx(nullptr) {}

// Destructor implementation
StorageModulePlugin::~StorageModulePlugin() {
    qDebug() << "StorageModulePlugin: Destructor called";

    // Clean up resources
    if (logosAPI) {
        delete logosAPI;
        logosAPI = nullptr;
    }

    // Clean up Storage context if it exists
    if (storageCtx) {
        storageCtx = nullptr;

        // The destroy should have been called before destructor
        qWarning() << "StorageModulePlugin: Warning - Storage context was not "
                      "destroyed before plugin destruction";
    }
}

// Bind the LogosApi instance
void StorageModulePlugin::initLogos(LogosAPI* logosAPIInstance) {
    if (logosAPI) {
        delete logosAPI;
    }
    logosAPI = logosAPIInstance;
}

// Define a generic callback ctx.
struct CallbackCtx {
    // QPointer provides safe weak reference to the plugin.
    // If the plugin is destroyed before the callback executes,
    // the pointer becomes null automatically, preventing crashes.
    // This is critical because callbacks are invoked asynchronously
    // from the storage module thread.
    QPointer<StorageModulePlugin> plugin;

    CallbackCtx(QPointer<StorageModulePlugin> p) : plugin(p) {}

    virtual ~CallbackCtx() = default;

    virtual void handleResponse(int callerRet, const char* msg, size_t len) const = 0;
};

// Send an event to the UI.
// The response values are:
// 1- ret: the return code of the command (0 for success, non-zero for failure)
// 2- msg: the return string message.
struct EventCallbackCtx : CallbackCtx {
    StorageEvent event;

    EventCallbackCtx(QPointer<StorageModulePlugin> p, StorageEvent e) : CallbackCtx(p), event(e) {}

    void handleResponse(int ret, const char* msg, size_t len) const override {
        // Making sure that plugin is alive
        if (!plugin || !plugin->logosAPI) {
            qWarning() << "EventCallbackCtx::handleResponse: Invalid plugin or logosAPI";
            return;
        }

        LogosAPIClient* client = plugin->logosAPI->getClient("core_manager");
        // Making really really sure that everything is in place
        if (!client) {
            qWarning() << "EventCallbackCtx::handleResponse: core_manager client is null";
            return;
        }

        // Get the message reponse from the callback
        const QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();
        // Construct the response data to send to the UI.
        const QVariantList eventData{ret == RET_OK, message};

        client->onEventResponse(plugin.data(), eventName(event), eventData);
    }
};

// Send an internal signal for sync calls.
struct SignalCallbackCtx : CallbackCtx {
    StorageSyncSignal signal;
    // Extra data to ensure that args are still valid
    // during the async call.
    QByteArray lifetimeUtf8;

    SignalCallbackCtx(QPointer<StorageModulePlugin> p, StorageSyncSignal s, QByteArray l = QByteArray())
        : CallbackCtx(p), signal(s), lifetimeUtf8(std::move(l)) {}

    void handleResponse(int ret, const char* msg, size_t len) const override {
        // Making sure that plugin is alive
        if (!plugin || !plugin->logosAPI) {
            qWarning() << "SignalCallbackCtx::handleResponse: Invalid plugin or logosAPI";
            return;
        }

        // Get the message reponse from the callback
        const QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();

        emit plugin->storageResponse(signal, ret, message);
    }
};

// Connect callback context that holds the peerId and peerAddresses for the connect event.
// It frees the peer addresses when destroyed to avoid memory leaks.
struct ConnectCallbackCtx : EventCallbackCtx {
    QByteArray peerId;
    QVector<char*> addrs;

    ConnectCallbackCtx(QPointer<StorageModulePlugin> p, QByteArray pid, QVector<char*> a)
        : EventCallbackCtx(p, StorageEvent::Connect), peerId(pid), addrs(a) {}

    ~ConnectCallbackCtx() {
        for (char* addr : addrs) {
            if (addr) {
                free(addr);
            }
        }

        addrs.clear();
    }
};

// struct UploadChunkCallbackCtx {
//     StorageModulePlugin* plugin;
//     QByteArray sessionIdUtf8;
//     QByteArray chunk;
// };

// Send a a progress event on RET_PROGRESS,
// otherwise send completion with CID is success or error message on failure.
struct UploadFileCallbackCtx : CallbackCtx {
    QByteArray sessionIdUtf8;

    UploadFileCallbackCtx(QPointer<StorageModulePlugin> p, QByteArray s)
        : CallbackCtx(p), sessionIdUtf8(std::move(s)) {}

    void handleResponse(int ret, const char* msg, size_t len) const override {
        // Making sure that plugin is alive
        if (!plugin || !plugin->logosAPI) {
            qWarning() << "EventCallbackCtx::handleResponse: Invalid plugin or logosAPI";
            return;
        }

        LogosAPIClient* client = plugin->logosAPI->getClient("core_manager");
        // Making really really sure that everything is in place
        if (!client) {
            qWarning() << "EventCallbackCtx::handleResponse: core_manager client is null";
            return;
        }

        const QString sessionId = QString::fromUtf8(sessionIdUtf8, sessionIdUtf8.size());

        if (ret == RET_PROGRESS) {
            const int size = static_cast<int>(len);
            QVariantList eventData{true, sessionId, size};
            client->onEventResponse(plugin.data(), eventName(StorageEvent::UploadProgress), eventData);
            return;
        }

        const QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();
        QVariantList eventData{ret == RET_OK, sessionId, message};
        client->onEventResponse(plugin.data(), eventName(StorageEvent::UploadDone), eventData);
    }
};

// Generic callback to pass data back from libstorage.
void StorageModulePlugin::callback(int ret, const char* msg, size_t len, void* userData) {
    qDebug() << "StorageModulePlugin::callback called with ret=" << ret;

    // Build the context from userData
    auto* ctx = static_cast<CallbackCtx*>(userData);
    if (!ctx) {
        qWarning() << "StorageModulePlugin::eventCallback: Invalid userData";
        return;
    }

    // Make sure the plugin is still valid.
    if (!ctx->plugin) {
        // Delete the context to avoid memory leaks.
        delete ctx;
        return;
    }

    const QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();
    // Copy the message to a QByteArray to extend its lifetime.
    const QByteArray messageUtf8 = message.toUtf8();

    // Use invokeMethod to ensure thread-safety when emitting the event.
    QMetaObject::invokeMethod(
        ctx->plugin.data(),
        [ctx, ret, messageUtf8]() {
            // Call constData to satisfy the callback signature
            ctx->handleResponse(ret, messageUtf8.constData(), messageUtf8.size());

            if (ret != RET_PROGRESS) {
                delete ctx;
            }
        },
        Qt::QueuedConnection);
}

// Event callback that emits the corresponding event name to the UI.
// The ret code and message are passed as event data.
// void StorageModulePlugin::eventCallback(int callerRet, const char* msg, size_t len, void* userData) {
//     // Build the context from userData
//     auto* ctx = static_cast<EventCallbackCtx*>(userData);
//     if (!ctx) {
//         qWarning() << "StorageModulePlugin::eventCallback: Invalid userData";
//         return;
//     }

//     // Extract the event name and message
//     const StorageEvent event = ctx->event;
//     const QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();

//     qDebug() << "StorageModulePlugin::eventCallback called with ret:" << callerRet << "for event:" <<
//     eventName(event);

//     // Use QPointer to safely reference the plugin instance.
//     const QPointer<StorageModulePlugin> plugin = ctx->plugin;

//     // Delete the context to avoid memory leaks.
//     delete ctx;

//     // Make sure the plugin is still valid.
//     if (!plugin) {
//         return;
//     }

//     // Build the event data to send to the UI.
//     const QVariantList eventData{callerRet, message};

//     // Use invokeMethod to ensure thread-safety when emitting the event.
//     QMetaObject::invokeMethod(
//         plugin.data(),
//         [plugin, event, eventData]() {
//             // Check if the plugin and logosAPI are still valid.
//             if (!plugin || !plugin->logosAPI) {
//                 return;
//             }

//             // Emit the event to the core manager.
//             plugin->logosAPI->getClient("core_manager")->onEventResponse(plugin.data(), eventName(event), eventData);
//         },
//         Qt::QueuedConnection);

//     qDebug() << "StorageModulePlugin::onEventResponse scheduled for event:" << eventName(event)
//              << "with message:" << message;
// }

// Helper method to wait for a specific signal with a timeout.
LogosResult StorageModulePlugin::waitForSignal(const StorageSyncSignal& signal, int timeout) {
    QEventLoop loop;
    QString msg;
    LogosResult result = {false, ""};

    qDebug() << "StorageModulePlugin::waitForSignal: Waiting for signal with timeout" << timeout << "ms";

    // Connect the signal to capture the message.
    // Connection is used to disconnect after receiving the signal.
    QMetaObject::Connection connection;

    // Create a callback that will assign the result and
    // quit the loop when the signal is received.
    auto fn = [&](const StorageSyncSignal& s, int code, const QString& m) {
        if (s != signal) {
            // We are looking for another signal, ignore this one.
            return;
        }

        result.success = code == RET_OK;
        result.value = m;

        // Disconnect after receiving the signal to avoid multiple triggers.
        QObject::disconnect(connection);

        // Quit the loop to unblock the waiting function.
        loop.quit();
    };

    connection = QObject::connect(this, &StorageModulePlugin::storageResponse, &loop, fn);

    QTimer timer;
    // Just make sure the timer is single shot
    timer.setSingleShot(true);

    // Connect the timer to quit the loop on timeout.
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        result.success = false;
        result.value = QString("Cannot get response before timeout.");
        loop.quit();
    });

    timer.start(timeout);

    // Wait for the signal or timeout
    loop.exec();

    return result;
}

// signalCallback emits the corresponding signal upon callback.
// It is used to provide synchronous behavior for certain methods.
// void StorageModulePlugin::signalCallback(int callerRet, const char* msg, size_t len, void* userData) {
//     // Build the context from userData
//     auto* ctx = static_cast<SignalCallbackCtx*>(userData);
//     if (!ctx) {
//         qWarning() << "StorageModulePlugin::signalCallback: Invalid userData";
//         return;
//     }

//     // Extract the message
//     const QString result = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();

//     qDebug() << "StorageModulePlugin::signalCallback ret=" << callerRet << "signal=" << int(ctx->signal)
//              << "msg=" << result;

//     // Use QPointer to safely reference the plugin instance.
//     const QPointer<StorageModulePlugin> plugin = ctx->plugin;

//     // Store the operation before deleting the context
//     const StorageSyncSignal signal = ctx->signal;

//     // Delete the context to avoid memory leaks.
//     delete ctx;

//     // Make sure the plugin is still valid.
//     if (!plugin) {
//         return;
//     }

//     // Use invokeMethod to ensure thread-safety when emitting the signal.
//     QMetaObject::invokeMethod(
//         plugin.data(),
//         [plugin, signal, callerRet, result] {
//             // Check if the plugin is still valid.
//             if (!plugin) {
//                 return;
//             }

//             emit plugin->storageResponse(signal, callerRet, result);
//         },
//         Qt::QueuedConnection);
// }

// Basic callback that just logs the message.
// Used for operations that do not require a callback response like init
// function.
// void StorageModulePlugin::callback(int callerRet, const char* msg, size_t len, void* userData) {
//     qDebug() << "StorageModulePlugin::callback called with ret:" << callerRet;

//     if (msg && len > 0) {
//         QString message = QString::fromUtf8(msg, len);
//         qDebug() << "StorageModulePlugin::callback message:" << message << "is ignored";
//     }
// }

// Helper to reduce redundant code for sync calls without arguments.
LogosResult StorageModulePlugin::syncCall(StorageSyncSignal signal, StorageNoArgFunction storageFn) {
    qDebug() << "StorageModulePlugin:: syncCall called";

    if (!storageCtx) {
        return {false, "Storage context is not initialized."};
    }

    const int ret = storageFn(storageCtx, callback, new SignalCallbackCtx{this, signal});

    if (ret != RET_OK) {
        return {false, QString("Failed to send command.")};
    }

    return waitForSignal(signal, DEFAULT_SYNC_TIMEOUT);
}

// Helper to reduce redundant code for sync calls with a string argument.
LogosResult StorageModulePlugin::syncCall(StorageSyncSignal signal, StorageStringArgFunction storageFn,
                                          const QString& arg) {
    if (!storageCtx) {
        return {false, "Storage context is not initialized."};
    }

    // Create a QByteArray to ensure that the data is valid during the async call.
    auto* ctx = new SignalCallbackCtx{this, signal, arg.toUtf8()};

    const int ret = storageFn(storageCtx, ctx->lifetimeUtf8, callback, ctx);

    if (ret != RET_OK) {
        // Delete the context because the callback won't be called because it failed.
        delete ctx;
        return {false, QString("Failed to send command.")};
    }

    return waitForSignal(signal, DEFAULT_SYNC_TIMEOUT);
}

// Initialize the storage module with the given configuration.
// The method is synchronous.
LogosResult StorageModulePlugin::init(const QString& cfg) {
    qDebug() << "StorageModulePlugin::init called with cfg:" << cfg;

    // Create a QByteArray to ensure that the data is valid during the async call.
    const QByteArray cfgUtf8 = cfg.toUtf8();

    storageCtx = storage_new(cfgUtf8.constData(), callback, new SignalCallbackCtx(this, StorageSyncSignal::Init));

    LogosResult result = waitForSignal(StorageSyncSignal::Init, DEFAULT_SYNC_TIMEOUT);

    if (!result.success) {
        return {false, result.getValue<QString>()};
    }

    if (storageCtx) {
        return {true, "Storage context created successfully."};
    }

    return {false, "Failed to create Storage context."};
}

// The method is asynchronous.
LogosResult StorageModulePlugin::start() {
    qDebug() << "StorageModulePlugin::start called";

    if (!storageCtx) {
        return {false, "Storage context is not initialized."};
    }

    const int ret = storage_start(storageCtx, callback, new EventCallbackCtx{this, StorageEvent::Start});

    if (ret != RET_OK) {
        return {false, "Failed to send start command to Storage module."};
    }

    return {true, ""};
}

// The method is asynchronous.
LogosResult StorageModulePlugin::stop() {
    qDebug() << "StorageModulePlugin::stop called";

    if (!storageCtx) {
        return {false, "Storage context is not initialized."};
    }

    const int ret = storage_stop(storageCtx, callback, new EventCallbackCtx{this, StorageEvent::Stop});

    if (ret != RET_OK) {
        return {false, "Failed to send stop command to Storage module."};
    }

    return {true, ""};
}

// The method is synchronous.
// It calls storage_close and storage_destroy internally.
LogosResult StorageModulePlugin::destroy() {
    qDebug() << "StorageModulePlugin::destroy called";

    LogosResult result = syncCall(StorageSyncSignal::Close, storage_close);

    if (!result.success) {
        qWarning() << "StorageModulePlugin::destroy failed to close with error " << result.value
                   << ". Let's try to destroy anyway.";
    }

    // callback is actually not called, it should be removed from the api.
    const int destroyRet = storage_destroy(storageCtx, callback, nullptr);

    if (destroyRet == RET_OK) {
        storageCtx = nullptr;
        return {true, ""};
    }

    return {false, "Failed to destroy Storage."};
}

// Connect to a peer by its peer id
// The method is asynchronous.
LogosResult StorageModulePlugin::connect(const QString& peerId, const QStringList& peerAddresses) {
    qDebug() << "StorageModulePlugin::connect called with peerId << " << peerId
             << "and peerAddresses =" << peerAddresses;

    if (!storageCtx) {
        return {false, " Storage context is not initialized"};
    }

    // Copy the addresses to ensure validity in the callback.
    QVector<char*> addrs;
    addrs.reserve(peerAddresses.size());
    for (const auto& addr : peerAddresses) {
        // Use constData to satisfy libstorage C api.
        addrs.append(strdup(addr.toUtf8().constData()));
    }

    // Create a QByteArray to ensure that the data is valid during the async call.
    auto* ctx = new ConnectCallbackCtx(this, peerId.toUtf8(), addrs);

    // Use constData to satisfy libstorage C api.
    const int ret = storage_connect(storageCtx, ctx->peerId.constData(), const_cast<const char**>(ctx->addrs.data()),
                                    static_cast<size_t>(ctx->addrs.size()), callback, ctx);

    if (ret != RET_OK) {
        // Delete the context because the callback won't be called it because it failed.
        delete ctx;
        return {false, "Failed to send the connect command."};
    }

    return {true, ""};
}

// The method is synchronous.
LogosResult StorageModulePlugin::version() {
    qDebug() << "StorageModulePlugin::version called";
    return syncCall(StorageSyncSignal::Version, storage_version);
}

// The method is synchronous.
LogosResult StorageModulePlugin::dataDir() {
    qDebug() << "StorageModulePlugin::dataDir called";
    return syncCall(StorageSyncSignal::DataDir, storage_repo);
}

// The method is synchronous.
LogosResult StorageModulePlugin::peerId() {
    qDebug() << "StorageModulePlugin::peerId called";
    return syncCall(StorageSyncSignal::PeerId, storage_peer_id);
}

// Get the node's Signed Peer Record (SPR)
// The method is synchronous.
LogosResult StorageModulePlugin::spr() {
    qDebug() << "StorageModulePlugin::spr called";
    return syncCall(StorageSyncSignal::Spr, storage_spr);
}

// Get the debug info of the node
// The method is synchronous.
LogosResult StorageModulePlugin::debug() {
    qDebug() << "StorageModulePlugin::debug called";
    return syncCall(StorageSyncSignal::Debug, storage_debug);
}

// The method is synchronous.
LogosResult StorageModulePlugin::updateLogLevel(const QString& logLevel) {
    qDebug() << "StorageModulePlugin::updateLogLevel called";
    return syncCall(StorageSyncSignal::LogLevel, storage_log_level, logLevel);
}

// The method is synchronous.
LogosResult StorageModulePlugin::exists(const QString& cid) {
    qDebug() << "StorageModulePlugin::exists called";
    LogosResult result = syncCall(StorageSyncSignal::Exists, storage_exists, cid);

    if (result.success) {
        return {true, result.value == "true"};
    }

    return result;
}

// The method is synchronous.
LogosResult StorageModulePlugin::fetch(const QString& cid) {
    qDebug() << "StorageModulePlugin::fetch called";
    return syncCall(StorageSyncSignal::Fetch, storage_fetch, cid);
}

// The method is synchronous.
LogosResult StorageModulePlugin::remove(const QString& cid) {
    qDebug() << "StorageModulePlugin::remove called";
    return syncCall(StorageSyncSignal::Remove, storage_delete, cid);
}

// The method is synchronous.
LogosResult StorageModulePlugin::space() {
    qDebug() << "StorageModulePlugin::space called";
    LogosResult result = syncCall(StorageSyncSignal::Space, storage_space);

    if (!result.success) {
        return result;
    }

    QString jsonString = result.value.toString();
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8());

    if (doc.isNull()) {
        return {false, "Failed to parse the JSON document."};
    }

    QJsonObject obj = doc.object();

    // Use a primitive type because of SDK's constraints.
    QVariantMap spaceMap;
    spaceMap["totalBlocks"] = obj["totalBlocks"].toInteger();
    spaceMap["quotaMaxBytes"] = obj["quotaMaxBytes"].toInteger();
    spaceMap["quotaUsedBytes"] = obj["quotaUsedBytes"].toInteger();
    spaceMap["quotaReservedBytes"] = obj["quotaReservedBytes"].toInteger();

    return {true, spaceMap};
}

// The method is synchronous.
LogosResult StorageModulePlugin::manifests() {
    qDebug() << "StorageModulePlugin::manifests called";

    LogosResult result = syncCall(StorageSyncSignal::Manifests, storage_list);

    if (!result.success) {
        return result;
    }

    QString jsonString = result.value.toString();
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8());

    if (!doc.isArray()) {
        return {false, "Failed to parse json array."};
    }

    QJsonArray arr = doc.array();
    QVariantList manifestsList;

    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();

        // Use a primitive type because of SDK's constraints.
        QVariantMap manifestMap;
        manifestMap["cid"] = obj["cid"].toString();
        manifestMap["treeCid"] = obj["treeCid"].toString();
        manifestMap["datasetSize"] = obj["datasetSize"].toInteger();
        manifestMap["blockSize"] = obj["blockSize"].toInteger();
        manifestMap["filename"] = obj["filename"].toString();
        manifestMap["mimetype"] = obj["mimetype"].toString();

        manifestsList.append(manifestMap);
    }

    return {true, manifestsList};
}

// The method is synchronous.
LogosResult StorageModulePlugin::downloadManifest(const QString& cid) {
    qDebug() << "StorageModulePlugin::space called";

    LogosResult result = syncCall(StorageSyncSignal::DownloadManifest, storage_download_manifest, cid);

    if (!result.success) {
        return result;
    }

    QString jsonString = result.value.toString();
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8());

    if (!doc.isObject()) {
        return {false, "Failed to parse JSON object."};
    }

    QJsonObject obj = doc.object();

    // Use a primitive type because of SDK's constraints.
    QVariantMap manifestMap;
    manifestMap["cid"] = obj["cid"].toString();
    manifestMap["treeCid"] = obj["treeCid"].toString();
    manifestMap["datasetSize"] = obj["datasetSize"].toInteger();
    manifestMap["blockSize"] = obj["blockSize"].toInteger();
    manifestMap["filename"] = obj["filename"].toString();
    manifestMap["mimetype"] = obj["mimetype"].toString();

    return {true, manifestMap};
}

// The method is synchronous.
LogosResult StorageModulePlugin::uploadCancel(const QString& sessionId) {
    qDebug() << "StorageModulePlugin::uploadCancel called with sessionId:" << sessionId;
    return syncCall(StorageSyncSignal::UploadCancel, storage_upload_cancel, sessionId);
}

// Upload a chunk.
// This method is asynchronous.
// Emit "storageUploadProgress" event on completion.
// bool StorageModulePlugin::uploadChunk(const QString& sessionId, const QByteArray& chunk) {
//     qDebug() << "StorageModulePlugin::uploadChunk called";

//     if (!storageCtx) {
//         qWarning() << "StorageModulePlugin::uploadChunk: Storage context is not initialized";
//         return false;
//     }

//     // Create a context for the signal and pass the session id to ensure that the
//     // sessionId is valid in the callback.
//     // The chunk data is also stored in the context to ensure its validity.
//     // That should not make a copy of the data, because of a Qt rule called
//     //
//     // Implicit Sharing
//     //
//     // As long as we only read only and nobody modifies the shared array,
//     // no deep copy of the chunk bytes happens. A deep copy would only occur if a write
//     // is attempted.
//     auto* ctx = new UploadChunkCallbackCtx{
//         this,
//         sessionId.toUtf8(),
//         chunk,
//     };

//     const uint8_t* ptr = ctx->chunk.isEmpty() ? nullptr : reinterpret_cast<const uint8_t*>(ctx->chunk.constData());
//     const size_t len = static_cast<size_t>(ctx->chunk.size());

//     const int ret =
//         storage_upload_chunk(storageCtx, ctx->sessionIdUtf8.constData(), ptr, len, uploadChunkCallback, ctx);

//     qDebug() << "StorageModulePlugin::uploadChunk: storage_upload_chunk ret =" << ret;

//     if (ret != RET_OK) {
//         delete ctx;
//         return false;
//     }

//     return true;
// }

// void StorageModulePlugin::uploadChunkCallback(int callerRet, const char* msg, size_t len, void* userData) {
//     // Build the context from userData
//     auto* ctx = static_cast<UploadChunkCallbackCtx*>(userData);
//     if (!ctx) {
//         qWarning() << "StorageModulePlugin::uploadChunkCallback: Invalid userData";
//         return;
//     }

//     qDebug() << "StorageModulePlugin::uploadChunkCallback called with ret:" << callerRet;

//     // Use QPointer to safely reference the plugin instance.
//     const QPointer<StorageModulePlugin> plugin = ctx->plugin;
//     const QString sessionId = ctx->sessionIdUtf8;
//     const int size = ctx->chunk.size();

//     // Delete the context to avoid memory leaks.
//     delete ctx;

//     // Make sure the plugin is still valid.
//     if (!plugin) {
//         return;
//     }

//     const QVariantList eventData{callerRet, sessionId, size};
//     const QString eventName = "storageUploadProgress";

//     // Use invokeMethod to ensure thread-safety when emitting the event.
//     QMetaObject::invokeMethod(
//         plugin.data(),
//         [plugin, eventName, eventData]() {
//             // Check if the plugin and logosAPI are still valid.
//             if (!plugin || !plugin->logosAPI) {
//                 return;
//             }

//             // Emit the event to the core manager.
//             plugin->logosAPI->getClient("core_manager")->onEventResponse(plugin.data(), eventName, eventData);
//         },
//         Qt::QueuedConnection);

//     qDebug() << "StorageModulePlugin::uploadChunkCallback scheduled for event:" << eventName;
// }

// void StorageModulePlugin::uploadFileCallback(int callerRet, const char* msg, size_t len, void* userData) {
//     // Build the context from userData
//     auto* ctx = static_cast<UploadFileCallbackCtx*>(userData);
//     if (!ctx) {
//         qWarning() << "StorageModulePlugin::uploadFileCallback: Invalid userData";
//         return;
//     }

//     qDebug() << "StorageModulePlugin::uploadFileCallback called with ret:" << callerRet;

//     // Use QPointer to safely reference the plugin instance.
//     QPointer<StorageModulePlugin> plugin = ctx->plugin;
//     QString sessionId = QString::fromUtf8(ctx->sessionIdUtf8);

//     if (callerRet != RET_PROGRESS) {
//         qDebug() << "StorageModulePlugin::uploadFileCallback will deleting context...";
//         // Delete the context to avoid memory leaks.
//         delete ctx;
//     }

//     // Make sure the plugin is still valid.
//     if (!plugin) {
//         return;
//     }

//     qDebug() << "StorageModulePlugin::uploadFileCallback: Preparing event data";

//     QVariantList eventData{callerRet, sessionId};
//     QString eventName;

//     if (callerRet == RET_PROGRESS) {
//         const int size = static_cast<int>(len);
//         eventName = "storageUploadProgress";
//         eventData.push_back(size);
//     } else if (callerRet == RET_OK) {
//         eventName = "storageUploadDone";
//         if (len > 0) {
//             const int size = static_cast<int>(len);
//             eventData.push_back(QString::fromUtf8(msg, size));
//         }
//     }

//     qDebug() << "StorageModulePlugin::uploadFileCallback: event data ready with eventName=" << eventName;

//     // Use invokeMethod to ensure thread-safety when emitting the event.
//     QMetaObject::invokeMethod(
//         plugin.data(),
//         [plugin, eventName, eventData]() {
//             // Check if the plugin and logosAPI are still valid.
//             if (!plugin || !plugin->logosAPI) {
//                 return;
//             }

//             auto* client = plugin->logosAPI->getClient("core_manager");
//             if (!client) {
//                 return;
//             }

//             client->onEventResponse(plugin.data(), eventName, eventData);
//         },
//         Qt::QueuedConnection);

//     qDebug() << "StorageModulePlugin::uploadChunkCallback scheduled for event:" << eventName;
// }

// The method is asynchronous.
LogosResult StorageModulePlugin::uploadUrl(const QUrl& url, const int chunkSize) {
    qDebug() << "StorageModulePlugin::uploadFromPath called";

    if (!storageCtx) {
        return {false, "Storage context is not initialized;"};
    }

    if (!url.isValid()) {
        return {false, "The URL is not valid."};
    }

    if (!url.isLocalFile()) {
        // TODO: we should handle case like
        // - qrc:/resources/file.txt (ressources Qt)
        // - data:text/plain;base64,SGVsbG8= (data URLs)
        // - content:// (Android content providers)
        // We should retrive the stream and use uploadStream
        return {false, "Non local file is not supported yet."};
    }

    if (chunkSize <= 0) {
        return {false, "Chunk size cannot be 0 or less."};
    }

    QString path = url.toLocalFile();
    QFileInfo info(path);

    if (!info.exists()) {
        return {false, "The file does not exist."};
    }

    if (!info.isFile()) {
        return {false, "The file is not a regular file (folder ?)."};
    }

    if (!info.isReadable()) {
        return {false, "The file is not readable"};
    }

    QString filename = info.fileName();

    // Create a QByteArray to ensure that the data is valid during the async call.
    auto* ctx = new SignalCallbackCtx{this, StorageSyncSignal::UploadInit, filename.toUtf8()};

    const size_t chunkSizeC = static_cast<size_t>(chunkSize);
    const int ret = storage_upload_init(storageCtx, ctx->lifetimeUtf8.constData(), chunkSizeC, callback, ctx);

    if (ret != RET_OK) {
        // Delete the context because the callback won't be called it.
        delete ctx;
        return {false, "Failed to send upload init command"};
    }

    LogosResult result = waitForSignal(StorageSyncSignal::UploadInit, DEFAULT_SYNC_TIMEOUT);

    if (!result.success) {
        // No need to delete the context, it was deleted in the callback.
        return result;
    }

    QString sessionId = result.getValue<QString>();

    // Create a QByteArray to ensure that the data is valid during the async call.
    auto* uploadFileCtx = new UploadFileCallbackCtx{
        this,
        sessionId.toUtf8(),
    };

    const int uploadFileRet =
        storage_upload_file(storageCtx, uploadFileCtx->sessionIdUtf8.constData(), callback, uploadFileCtx);

    if (uploadFileRet != RET_OK) {
        result = uploadCancel(sessionId);

        if (!result.success) {
            qWarning() << "StorageModulePlugin:: uploadUrl Failed to cancel the session.";
            // Continue on fails to cleanup the context
        }

        // Delete the context because the callback won't be called it.
        delete uploadFileCtx;
        return {false, "Failed to send the upload file command"};
    }

    return {true, sessionId};
}

LogosResult StorageModulePlugin::uploadStream(QIODevice* device, const QString& filename, const int chunkSize) {
    qDebug() << "StorageModulePlugin::uploadStream called";
    return {false, "uploadStream is not implemented yet."};
}

LogosResult StorageModulePlugin::downloadToUrl(const QString& cid, const QUrl& url) {
    qDebug() << "StorageModulePlugin::downloadToUrl called";
    return {false, "downloadToUrl is not implemented yet."};
}

LogosResult StorageModulePlugin::downloadToStream(const QString& cid, QIODevice* device) {
    qDebug() << "StorageModulePlugin::downloadToStream called";
    return {false, "downloadToStream is not implemented yet."};
}

