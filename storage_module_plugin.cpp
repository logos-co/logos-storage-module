#include "storage_module_plugin.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QList>
#include <QMutexLocker>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <variant>

// Storage Module C++ wrapper based on libstorage C bindings.
//
// Most of the C bindings functions are asynchronous: you call a function
// that sends the job to a worker and you receive the result in a callback.
//
// Based on that, the Storage Module defines different types of callbacks
// (like strategies) to handle data.
// The generic callback will receive the data and calls `handleResponse`
// that varies for each callback type.
// The types of callback are:
//
// 1- EventCallbackCtx: It is used for asynchronous functions in order to send
// an event to the caller on callback completion. This is typically used for
// start / stop methods for example.
// The caller has to subscribe to the event to receive the data.
//
// 2- ConnectCallbackCtx: Wrapper on top of EventCallbackCtx in order to free
// the peer addresses in the destructor.
//
// 3- SignalCallbackCtx: This is very easy to understand. Several APIs
// are sync-like, because they retrieve data. Example: peerId, debug, manifests...
// SignalCallbackCtx provides a mechanism that mimics the sync behaviour by defining
// a signal, and waiting that the callback fires this signal on data receive.
// A timeout is defined to not block too long.
// Because this pattern is widely used, syncCall is a shorthand that reduces the
// amount of code.
//
// 4- UploadFileCallbackCtx: When the callback receives RET_PROGRESS, it will
// notify the caller by sending the size of the data uploaded in order to reflect it
// with an indicator like a progress bar.
//
// 5- UploadChunkCallbackCtx: The callback notifies on success the caller by sending
// the size of the data uploaded.
//
// 6- DownloadStreamCallbackCtx: It is a bit complex because it can handle 2 cases:
//  * Streaming into a file
//  * Streaming by providing the chunks
// In the first case, the data is written into the provided filepath and the progress
// is notified by providing the number of bytes.
// In the second case, the data is not written but provided through the callback. Note
// that the chunk is COPIED because the Logos SDK uses Qt::QueuedConnection with remote objects.

StorageModulePlugin::StorageModulePlugin() : storageCtx(nullptr) {
    // Track storage start/stop state
    QObject::connect(this, &StorageModulePlugin::storageResponse, this,
        [this](const StorageSignal& signal, int code, const QString&) {
            if (signal == StorageSignal::Start) {
                isStarted = (code == RET_OK);
            } else if (signal == StorageSignal::Stop) {
                isStarted = false;
            }
        });
}

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

    LogosAPIClient* client() const {
        if (!plugin || !plugin->logosAPI) {
            qWarning() << "CallbackCtx::handleResponse: Invalid plugin or logosAPI";
            return nullptr;
        }

        LogosAPIClient* client = plugin->logosAPI->getClient("core_manager");
        if (!client) {
            qWarning() << "CallbackCtx::handleResponse: core_manager client is null";
            return nullptr;
        }

        return client;
    }

    virtual ~CallbackCtx() = default;

    virtual void handleResponse(int callerRet, const char* msg, size_t len) const = 0;
};

// Send an event to the caller on completion.
// The response values are:
// 1- ret: the return code of the command (0 for success, non-zero for failure)
// 2- msg: the return string message.
struct EventCallbackCtx : CallbackCtx {
    StorageEvent event;

    EventCallbackCtx(QPointer<StorageModulePlugin> p, StorageEvent e) : CallbackCtx(p), event(e) {}

    void handleResponse(int ret, const char* msg, size_t len) const override {
        LogosAPIClient* client = CallbackCtx::client();
        if (client == nullptr) {
            return;
        }

        // Get the message reponse from the callback
        const QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();
        // Construct the response data to send to the UI.
        const QVariantList eventData{ret == RET_OK, message};

        client->onEventResponse(plugin.data(), eventName(event), eventData);

        // Also emit storageResponse for Start/Stop events to allow using waitForSignal
        if (event == StorageEvent::Start) {
            emit plugin->storageResponse(StorageSignal::Start, ret, message);
        } else if (event == StorageEvent::Stop) {
            emit plugin->storageResponse(StorageSignal::Stop, ret, message);
        }
    }
};

// Connect callback context that holds the peerId and peerAddresses for the connect event.
// It frees the peer addresses when destroyed to avoid memory leaks.
// The response values are:
// 1- ret: the return code of the command (0 for success, non-zero for failure)
// 2- msg: the return string message.
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

// Send an internal signal for sync calls.
// The response values are:
// 1- ret: the return code of the command (0 for success, non-zero for failure)
// 2- msg: the return string message.
struct SyncCallbackCtx : CallbackCtx {
    StorageSignal signal;
    // Extra data to ensure that args are still valid
    // during the async call.
    QByteArray lifetimeUtf8;

    SyncCallbackCtx(QPointer<StorageModulePlugin> p, StorageSignal s, QByteArray l = QByteArray())
        : CallbackCtx(p), signal(s), lifetimeUtf8(std::move(l)) {}

    void handleResponse(int ret, const char* msg, size_t len) const override {
        // Make sure that we have a valid environment
        if (CallbackCtx::client() == nullptr) {
            return;
        }

        // Making sure that plugin is alive
        if (!plugin || !plugin->logosAPI) {
            qWarning() << "SyncCallbackCtx::handleResponse: Invalid plugin or logosAPI";
            return;
        }

        // Get the message reponse from the callback
        const QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();

        emit plugin->storageResponse(signal, ret, message);
    }
};

// Callback use for file upload.
//
// When it receives RET_PROGRESS, the response values are:
// 1- ret: the return code of the command (0 for success, non-zero for failure)
// 2- sessionId: the upload sessionId.
// 3- size: the number of bytes uploaded.
//
// When it receives RET_OK or RET_ERROR, the response values are:
// 1- ret: the return code of the command (0 for success, non-zero for failure)
// 2- sessionId: the upload sessionId.
// 3- message: the CID is success of the error message on error.
struct UploadFileCallbackCtx : CallbackCtx {
    QByteArray sessionIdUtf8;

    UploadFileCallbackCtx(QPointer<StorageModulePlugin> p, QByteArray s)
        : CallbackCtx(p), sessionIdUtf8(std::move(s)) {}

    void handleResponse(int ret, const char* msg, size_t len) const override {
        LogosAPIClient* client = CallbackCtx::client();
        if (client == nullptr) {
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

        // Also emit storageResponse with sessionId and cid
        emit plugin->storageResponse(StorageSignal::UploadDone, ret, sessionId + "," + message);
    }
};

// Callback for a single chunk upload.
//
// When it receives RET_ERROR, the response values are:
// 1- ret: the return code of the command (0 for success, non-zero for failure)
// 2- sessionId: the upload sessionId.
// 3- message: the error message.
//
// When it receives RET_OK, the response values are:
// 1- ret: the return code of the command (0 for success, non-zero for failure)
// 2- sessionId: the upload sessionId.
// 3- size: the number of bytes uploaded.
struct UploadChunkCallbackCtx : CallbackCtx {
    QByteArray sessionIdUtf8;
    QByteArray chunk;

    UploadChunkCallbackCtx(QPointer<StorageModulePlugin> p, QByteArray s, QByteArray c)
        : CallbackCtx(p), sessionIdUtf8(std::move(s)), chunk(std::move(c)) {}

    void handleResponse(int ret, const char* msg, size_t len) const override {
        LogosAPIClient* client = CallbackCtx::client();
        if (client == nullptr) {
            return;
        }

        const QString sessionId = QString::fromUtf8(sessionIdUtf8);

        if (ret != RET_OK) {
            const QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();

            qWarning() << "UploadChunkCallbackCtx: Chunk upload failed, message=" << message;

            QVariantList progressData{true, sessionId, message};
            client->onEventResponse(plugin.data(), eventName(StorageEvent::UploadProgress), progressData);

            // Note that we do not want to cancel the upload because the caller may handle the
            // error properly.

            return;
        }

        QVariantList progressData{true, sessionId, chunk.size()};
        client->onEventResponse(plugin.data(), eventName(StorageEvent::UploadProgress), progressData);
    }
};

// Callback for streaming download data.
//
// When the streaming is done in a file, the progress will be reported with those values:
// 1- ret: the return code of the command (0 for success, non-zero for failure)
// 2- sessionId: the upload sessionId.
// 3- size: the number of bytes downloaded.
//
// When the streaming is not done in a file, the progress will be reported with those values:
// 1- ret: the return code of the command (0 for success, non-zero for failure)
// 2- sessionId: the upload sessionId.
// 3- chunk: the chunk of data downloaded.
//
// When the streaming is done, the response values are:
// 1- ret: the return code of the command (0 for success, non-zero for failure)
// 2- sessionId: the upload sessionId.
// 3- message: empty on success.
struct DownloadStreamCallbackCtx : CallbackCtx {
    QByteArray cidUtf8;
    QByteArray filepathUtf8;

    DownloadStreamCallbackCtx(QPointer<StorageModulePlugin> p, QByteArray c, QByteArray f)
        : CallbackCtx(p), cidUtf8(std::move(c)), filepathUtf8(std::move(f)) {}

    void handleResponse(int ret, const char* msg, size_t len) const override {
        LogosAPIClient* client = CallbackCtx::client();
        if (client == nullptr) {
            return;
        }

        const QString cid = QString::fromUtf8(cidUtf8, cidUtf8.size());

        if (ret == RET_PROGRESS) {
            if (filepathUtf8.isEmpty()) {
                // Here we MUST make a copy of the chunk.
                // LogosAPIClient::onEventResponse uses Qt::QueuedConnection,
                // which queues the event instead of calling immediately. By the time the
                // handler executes, `msg` (pointing to messageUtf8 in the callback lambda)
                // will be freed. Using QByteArray:fomRawData() would be unsafe.
                QByteArray chunk(msg, len);
                QVariantList eventData{true, cid, chunk};
                client->onEventResponse(plugin.data(), eventName(StorageEvent::DownloadProgress), eventData);
            } else {
                const int size = static_cast<int>(len);
                QVariantList eventData{true, cid, size};
                client->onEventResponse(plugin.data(), eventName(StorageEvent::DownloadProgress), eventData);
            }
            return;
        }

        const QString message = (msg && len > 0) ? QString::fromUtf8(msg, len) : QString();
        QVariantList eventData{ret == RET_OK, cid, message};
        client->onEventResponse(plugin.data(), eventName(StorageEvent::DownloadDone), eventData);
    }
};

// Generic callback to pass data back from libstorage.
// Ensure to NOT DELETE the ctx on RET_PROGRESS.
void StorageModulePlugin::callback(int ret, const char* msg, size_t len, void* userData) {
    // Be careful when logging here.
    // It can slow down the performance.
    // qDebug() << "StorageModulePlugin::callback called with ret=" << ret << "and len=" << len;

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
        [ctx, ret, messageUtf8, len]() {
            // Call constData to satisfy the callback signature
            ctx->handleResponse(ret, messageUtf8.constData(), len);

            if (ret != RET_PROGRESS) {
                delete ctx;
            }
        },
        Qt::QueuedConnection);
}

// Helper method to wait for a specific signal with a timeout.
LogosResult StorageModulePlugin::waitForSignal(const StorageSignal& signal, int timeout) {
    QEventLoop loop;
    QString msg;
    LogosResult result = {false, ""};

    qDebug() << "StorageModulePlugin::waitForSignal: Waiting for signal with timeout" << timeout << "ms";

    // Connect the signal to capture the message.
    // Connection is used to disconnect after receiving the signal.
    QMetaObject::Connection connection;

    // Create a callback that will assign the result and
    // quit the loop when the signal is received.
    auto fn = [&](const StorageSignal& s, int code, const QString& m) {
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

// Generic helper that handles all sync call types with optional arguments.
// It is just a shorthand because the pattern is widely used in the code.
LogosResult StorageModulePlugin::syncCall(StorageSignal signal, StorageFunctionVariant fn, const QString& arg1, int arg2) {
    if (!storageCtx) {
        return {false, "", "Storage context is not initialized."};
    }

    auto* ctx = new SyncCallbackCtx{this, signal};

    int ret;

    if (std::holds_alternative<StorageNoArgFunction>(fn)) {
        auto storageFn = std::get<StorageNoArgFunction>(fn);
        ret = storageFn(storageCtx, callback, ctx);
    } else if (std::holds_alternative<StorageStringArgFunction>(fn)) {
        auto storageFn = std::get<StorageStringArgFunction>(fn);
        ctx->lifetimeUtf8 = arg1.toUtf8();
        ret = storageFn(storageCtx, ctx->lifetimeUtf8, callback, ctx);
    } else if (std::holds_alternative<StorageStringArgAndIntArgFunction>(fn)) {
        auto storageFn = std::get<StorageStringArgAndIntArgFunction>(fn);
        ctx->lifetimeUtf8 = arg1.toUtf8();
        ret = storageFn(storageCtx, ctx->lifetimeUtf8, static_cast<size_t>(arg2), callback, ctx);
    } else {
        return {false, "", "Failed to send command."};
    }

    if (ret != RET_OK) {
        delete ctx;
        return {false, "", "Failed to send command."};
    }

    return waitForSignal(signal, DEFAULT_SYNC_TIMEOUT);
}

// Initialize the storage module with the given configuration.
// The method is synchronous.
bool StorageModulePlugin::init(const QString& cfg) {
    qDebug() << "StorageModulePlugin::init called with cfg:" << cfg;

    // Create a QByteArray to ensure that the data is valid during the async call.
    const QByteArray cfgUtf8 = cfg.toUtf8();

    storageCtx = storage_new(cfgUtf8.constData(), callback, new SyncCallbackCtx(this, StorageSignal::Init));

    LogosResult result = waitForSignal(StorageSignal::Init, DEFAULT_SYNC_TIMEOUT);

    if (!result.success) {
        qWarning() << "StorageModulePlugin::init Failed to create context error=" << result.getError();
        return false;
    }

    if (storageCtx) {
        return true;
    }

    qWarning() << "StorageModulePlugin::init Failed to create context.";
    return false;
}

// The method is asynchronous.
bool StorageModulePlugin::start() {
    qDebug() << "StorageModulePlugin::start called";

    if (!storageCtx) {
        qWarning() << "StorageModulePlugin::start Storage context is not initialized.";
        return false;
    }

    const int ret = storage_start(storageCtx, callback, new EventCallbackCtx{this, StorageEvent::Start});

    if (ret != RET_OK) {
        qWarning() << "StorageModulePlugin::start Failed to send start command.";
        return false;
    }

    return true;
}

// The method is asynchronous.
LogosResult StorageModulePlugin::stop() {
    qDebug() << "StorageModulePlugin::stop called";

    if (!storageCtx) {
        return {false, "", "Storage context is not initialized."};
    }

    const int ret = storage_stop(storageCtx, callback, new EventCallbackCtx{this, StorageEvent::Stop});

    if (ret != RET_OK) {
        return {false, "", "Failed to send stop command to Storage module."};
    }

    return {true, ""};
}

// The method is synchronous.
// It calls storage_close and storage_destroy internally.
LogosResult StorageModulePlugin::destroy() {
    qDebug() << "StorageModulePlugin::destroy called";

    LogosResult result = syncCall(StorageSignal::Close, storage_close);

    if (!result.success) {
        qWarning() << "StorageModulePlugin::destroy failed to close with error " << result.value
                   << ". Let's try to destroy anyway.";
    }

    // callback is actually not called, it should be removed from the api.
    const int destroyRet = storage_destroy(storageCtx);

    if (destroyRet == RET_OK) {
        storageCtx = nullptr;
        return {true, ""};
    }

    return {false, "", "Failed to destroy Storage."};
}

// Connect to a peer by its peer id
// The method is asynchronous.
LogosResult StorageModulePlugin::connect(const QString& peerId, const QStringList& peerAddresses) {
    qDebug() << "StorageModulePlugin::connect called with peerId=" << peerId << "and peerAddresses =" << peerAddresses;

    if (!storageCtx) {
        return {false, "", " Storage context is not initialized"};
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
        return {false, "", "Failed to send the connect command."};
    }

    return {true, ""};
}

// The method is synchronous.
LogosResult StorageModulePlugin::version() {
    qDebug() << "StorageModulePlugin::version called";

    auto version = storage_version(storageCtx);
    return {true, QString(version)};
}

// The method is synchronous.
LogosResult StorageModulePlugin::dataDir() {
    qDebug() << "StorageModulePlugin::dataDir called";
    return syncCall(StorageSignal::DataDir, storage_repo);
}

// The method is synchronous.
LogosResult StorageModulePlugin::peerId() {
    qDebug() << "StorageModulePlugin::peerId called";
    return syncCall(StorageSignal::PeerId, storage_peer_id);
}

// Get the node's Signed Peer Record (SPR)
// The method is synchronous.
LogosResult StorageModulePlugin::spr() {
    qDebug() << "StorageModulePlugin::spr called";
    return syncCall(StorageSignal::Spr, storage_spr);
}

// Get the debug info of the node
// The method is synchronous.
LogosResult StorageModulePlugin::debug() {
    qDebug() << "StorageModulePlugin::debug called";

    LogosResult result = syncCall(StorageSignal::Debug, storage_debug);
    if (!result.success) {
        return result;
    }

    QString jsonString = result.value.toString();
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8());

    // Return the whole JSON as QVariant structure
    return {true, doc.toVariant()};
}

// The method is synchronous.
LogosResult StorageModulePlugin::updateLogLevel(const QString& logLevel) {
    qDebug() << "StorageModulePlugin::updateLogLevel called";
    return syncCall(StorageSignal::LogLevel, storage_log_level, logLevel);
}

// The method is synchronous.
LogosResult StorageModulePlugin::exists(const QString& cid) {
    qDebug() << "StorageModulePlugin::exists called";
    LogosResult result = syncCall(StorageSignal::Exists, storage_exists, cid);

    if (result.success) {
        return {true, result.getString() == "true"};
    }

    return {false, false, result.getError()};
}

// The method is synchronous.
LogosResult StorageModulePlugin::fetch(const QString& cid) {
    qDebug() << "StorageModulePlugin::fetch called";
    return syncCall(StorageSignal::Fetch, storage_fetch, cid);
}

// The method is synchronous.
LogosResult StorageModulePlugin::remove(const QString& cid) {
    qDebug() << "StorageModulePlugin::remove called";
    return syncCall(StorageSignal::Remove, storage_delete, cid);
}

// The method is synchronous.
LogosResult StorageModulePlugin::space() {
    qDebug() << "StorageModulePlugin::space called";
    LogosResult result = syncCall(StorageSignal::Space, storage_space);

    if (!result.success) {
        return {false, QVariant(), result.getError()};
    }

    QString jsonString = result.value.toString();
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8());

    if (doc.isNull()) {
        return {false, QVariant(), "Failed to parse the JSON document."};
    }

    return {true, doc.toVariant()};
}

// The method is synchronous.
LogosResult StorageModulePlugin::manifests() {
    qDebug() << "StorageModulePlugin::manifests called";

    LogosResult result = syncCall(StorageSignal::Manifests, storage_list);

    if (!result.success) {
        return {false, QVariantList(), result.getError()};
    }

    QString jsonString = result.value.toString();
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8());

    if (!doc.isArray()) {
        return {false, QVariantList(), "Failed to parse json array."};
    }

    QJsonArray arr = doc.array();
    QVariantList manifestsList;

    // The manifest structure comes like this:
    // {
    //  "cid": "..",
    //  "manifest": {
    //  }
    // So ww will just flat everything.
    for (const QJsonValue& val : arr) {
        QJsonObject item = val.toObject();
        QJsonObject manifestObj = item["manifest"].toObject();

        QVariantMap manifest;
        manifest["cid"] = item["cid"].toString();
        manifest["treeCid"] = manifestObj["treeCid"].toString();
        manifest["datasetSize"] = manifestObj["datasetSize"].toVariant();
        manifest["blockSize"] = manifestObj["blockSize"].toVariant();
        manifest["filename"] = manifestObj["filename"].toString();
        manifest["mimetype"] = manifestObj["mimetype"].toVariant();

        manifestsList.append(manifest);
    }

    return {true, manifestsList};
}

// The method is synchronous.
LogosResult StorageModulePlugin::downloadManifest(const QString& cid) {
    qDebug() << "StorageModulePlugin::downloadManifest called";

    LogosResult result = syncCall(StorageSignal::DownloadManifest, storage_download_manifest, cid);

    if (!result.success) {
        return {false, QVariant(), result.getError()};
    }

    QString jsonString = result.value.toString();
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8());

    if (!doc.isObject()) {
        return {false, QVariant(), "Failed to parse JSON object."};
    }

    return {true, doc.toVariant()};
}

// The method is asynchronous.
LogosResult StorageModulePlugin::uploadUrl(const QUrl& url, const int chunkSize) {
    qDebug() << "StorageModulePlugin::uploadUrl called with url=" << url << " and chunkSize=" << chunkSize;

    if (!storageCtx) {
        return {false, "", "Storage context is not initialized;"};
    }

    if (!url.isValid()) {
        return {false, "", "The URL is not valid."};
    }

    if (!url.isLocalFile()) {
        // TODO: we should handle case like
        // - qrc:/resources/file.txt (ressources Qt)
        // - data:text/plain;base64,SGVsbG8= (data URLs)
        // - content:// (Android content providers)
        // We should retrive the stream and use uploadStream
        return {false, "", "Non local file is not supported yet."};
    }

    if (chunkSize <= 0) {
        return {false, "", "Chunk size cannot be 0 or less."};
    }

    QString path = url.toLocalFile();
    QFileInfo info(path);

    if (!info.exists()) {
        return {false, "", "The file does not exist."};
    }

    if (!info.isFile()) {
        return {false, "", "The file is not a regular file (folder ?)."};
    }

    if (!info.isReadable()) {
        return {false, "", "The file is not readable"};
    }

    // QString filename = info.fileName();

    LogosResult result = syncCall(StorageSignal::UploadInit, storage_upload_init, path, chunkSize);

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
        return {false, "", "Failed to send the upload file command"};
    }

    return {true, sessionId};
}

// The method is synchronous.
LogosResult StorageModulePlugin::uploadInit(const QString& filename, const int chunkSize) {
    qDebug() << "StorageModulePlugin::uploadInit called with filename:" << filename;
    return syncCall(StorageSignal::UploadInit, storage_upload_init, filename, chunkSize);
}

// The method is asynchronous.
LogosResult StorageModulePlugin::uploadChunk(const QString& sessionId, const QByteArray& chunk) {
    qDebug() << "StorageModulePlugin::uploadChunk called with sessionId:" << sessionId;

    auto* ctx = new UploadChunkCallbackCtx{this, sessionId.toUtf8(), chunk};

    const uint8_t* chunkC = reinterpret_cast<const uint8_t*>(ctx->chunk.constData());
    const size_t sizeC = static_cast<size_t>(ctx->chunk.size());

    const int ret = storage_upload_chunk(storageCtx, ctx->sessionIdUtf8.constData(), chunkC, sizeC, callback, ctx);

    if (ret != RET_OK) {
        // Delete the context because the callback won't be called it because it failed.
        // We do not cancel the upload on failure, it does not corrupt the upload session.
        delete ctx;
        return {false, "", "Failed to send command."};
    }

    return {true, ""};
}

// The method is synchronous.
LogosResult StorageModulePlugin::uploadFinalize(const QString& sessionId) {
    qDebug() << "StorageModulePlugin::uploadFinalize called with sessionId:" << sessionId;
    return syncCall(StorageSignal::UploadFinalize, storage_upload_finalize, sessionId);
}

// The method is synchronous.
LogosResult StorageModulePlugin::uploadCancel(const QString& sessionId) {
    qDebug() << "StorageModulePlugin::uploadCancel called with sessionId:" << sessionId;
    return syncCall(StorageSignal::UploadCancel, storage_upload_cancel, sessionId);
}

// The method is asynchronous.
LogosResult StorageModulePlugin::downloadToUrl(const QString& cid, const QUrl& url, const bool local,
                                               const int chunkSize) {
    qDebug() << "StorageModulePlugin::downloadToUrl called";

    if (!url.isValid()) {
        return {false, "", "The URL is not valid"};
    }

    if (!url.isLocalFile()) {
        return {false, "", "Non local file is not supported yet"};
    }

    QString path = url.toLocalFile();
    //  QFileInfo info(path);

    return downloadChunks(cid, local, chunkSize, path);
}

// The method is synchronous.
LogosResult StorageModulePlugin::downloadChunks(const QString& cid, const bool local, const int chunkSize,
                                                const QString& filepath) {
    qDebug() << "StorageModulePlugin::downloadChunks called";

    if (!storageCtx) {
        return {false, "", "Storage context is not initialized"};
    }

    if (chunkSize <= 0) {
        return {false, "", "Chunk size cannot be zero or negative."};
    }

    // Create a QByteArray to ensure that the data is valid during the async call.
    auto* initCtx = new SyncCallbackCtx{this, StorageSignal::DownloadInit, cid.toUtf8()};

    const size_t chunkSizeC = static_cast<size_t>(chunkSize);
    const int initRet =
        storage_download_init(storageCtx, initCtx->lifetimeUtf8.constData(), chunkSizeC, local, callback, initCtx);

    if (initRet != RET_OK) {
        // Delete the context because the callback won't be called it.
        delete initCtx;
        return {false, "", "Failed to send download init command"};
    }

    LogosResult result = waitForSignal(StorageSignal::DownloadInit, DEFAULT_SYNC_TIMEOUT);

    if (!result.success) {
        // No need to delete the context, it was deleted in the callback.
        return result;
    }

    // Create a QByteArray to ensure that the data is valid during the async call
    auto* ctx = new DownloadStreamCallbackCtx{this, cid.toUtf8(), filepath.toUtf8()};

    const int ret = storage_download_stream(storageCtx, ctx->cidUtf8.constData(), static_cast<size_t>(chunkSize), local,
                                            ctx->filepathUtf8.constData(), callback, ctx);

    if (ret != RET_OK) {
        delete ctx;
        return {false, "", "Failed to send download stream command"};
    }

    // The cid is actually the session ID.
    return {true, cid};
}

// The method is synchronous.
LogosResult StorageModulePlugin::downloadCancel(const QString& sessionId) {
    qDebug() << "StorageModulePlugin::downloadCancel called with sessionId:" << sessionId;
    return syncCall(StorageSignal::DownloadCancel, storage_download_cancel, sessionId);
}

// This function is intented to be used internally for the headless mode.
void StorageModulePlugin::importFiles(const QString& path) {
    qDebug() << "StorageModulePlugin::importFiles from path=" << path;

    // Wait for storage to start if not already started
    if (!isStarted) {
        qDebug() << "Storage not started, waiting for start signal (timeout: 60s)";

        const int oneMinute = 60000;
        LogosResult result = waitForSignal(StorageSignal::Start, oneMinute);
        if (!result.success) {
            qWarning() << "Timeout or failure waiting for storage to start:" << result.value;
            return;
        }

        qDebug() << "Storage started successfully";
    }

    QDir dir(path);
    if (!dir.exists()) {
        qWarning() << "Directory does not exist:" << path;
        return;
    }

    QFileInfoList fileList = dir.entryInfoList(QDir::Files);
    if (fileList.isEmpty()) {
        qDebug() << "No files found in directory:" << path;
        return;
    }

    qDebug() << "Found" << fileList.size() << "file(s) to upload";

    // Keep a map of sessionId / filename to display the combinaison
    // filename / cid in logs.
    QMap<QString, QString> sessions;

    for (const QFileInfo& fileInfo : fileList) {
        QUrl fileUrl = QUrl::fromLocalFile(fileInfo.absoluteFilePath());
        qDebug() << "Uploading file:" << fileInfo.fileName();

        LogosResult result = uploadUrl(fileUrl);
        if (!result.success) {
            qWarning() << "Failed to start upload for" << fileInfo.fileName() << ":" << result.value;
        } else {
            QString sessionId = result.getValue<QString>();
            qDebug() << "Upload started for" << fileInfo.fileName() << "with session ID:" << sessionId;
            sessions[sessionId] = fileInfo.fileName();
        }
    }

    if (sessions.isEmpty()) {
        qDebug() << "No uploads started";
        return;
    }

    // Wait for all uploads to complete
    qDebug() << "Waiting for" << sessions.size() << "upload(s) to complete...";
    QEventLoop loop;
    int receivedCount = 0;

    auto fn = [&, sessions](const StorageSignal& signal, int code, const QString& message) {
        QStringList parts = message.split(",");
        QString sessionId = parts[0];
        QString cid = parts[1];

        if (signal == StorageSignal::UploadDone && sessions.contains(sessionId)) {
            QString filename = sessions[sessionId];
            if (code == RET_OK) {
                qDebug() << "File" << filename << "uploaded successfully, session:" << sessionId << "cid=" << cid;
            } else {
                qWarning() << "File" << filename << "upload failed, session:" << sessionId;
            }

            receivedCount++;
            if (receivedCount >= sessions.size()) {
                loop.quit();
            }
        }
    };

    QMetaObject::Connection conn = QObject::connect(this, &StorageModulePlugin::storageResponse, fn);

    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(&timer, &QTimer::timeout, [&]() {
        qWarning() << "Timeout: received" << receivedCount << "/" << sessions.size() << "uploads";
        loop.quit();
    });

    const int fiveMinutes = 300000;
    timer.start(fiveMinutes);

    loop.exec();

    QObject::disconnect(conn);

    qDebug() << "importFiles completed:" << receivedCount << "/" << sessions.size() << "files uploaded";
}
