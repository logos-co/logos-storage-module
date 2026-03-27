// Integration tests for StorageModulePlugin — uses the REAL libstorage library.
// No mocking. These tests start an actual storage node, upload/download real data,
// and verify end-to-end behavior.
//
// Requires libstorage to be available in ../lib at build time.
// Skipped automatically when libstorage is not found.

#include <logos_test.h>
#include "storage_module_plugin.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

static const int DEFAULT_TIMEOUT = 3000;
static const QString LOG_FILENAME = "storage.log";

// ---------------------------------------------------------------------------
// Helper: wait for a specific StorageSignal with timeout
// ---------------------------------------------------------------------------
static LogosResult waitForSignal(StorageModulePlugin* plugin, StorageSignal signal, int timeout) {
    QEventLoop loop;
    LogosResult result = {false, ""};
    QMetaObject::Connection connection;

    auto fn = [&](const StorageSignal& s, int code, const QString& m) {
        if (s != signal) return;
        result.success = code == RET_OK;
        result.value = m;
        QObject::disconnect(connection);
        loop.quit();
    };

    connection = QObject::connect(plugin, &StorageModulePlugin::storageResponse, &loop, fn);

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        result.success = false;
        result.value = QString("Timeout waiting for signal.");
        loop.quit();
    });

    timer.start(timeout);
    loop.exec();
    return result;
}

// ---------------------------------------------------------------------------
// Helper: upload content and return the CID
// ---------------------------------------------------------------------------
static QString uploadFile(StorageModulePlugin* plugin, const QByteArray& content, const QString& filename) {
    QTemporaryDir folder(QDir::currentPath());
    const QString filePath = folder.path() + "/" + filename;
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) return "";
    f.write(content);
    f.close();

    LogosResult startResult = plugin->uploadUrl(QUrl::fromLocalFile(filePath));
    if (!startResult.success) return "";

    LogosResult doneResult = waitForSignal(plugin, StorageSignal::UploadDone, DEFAULT_TIMEOUT);
    if (!doneResult.success) return "";

    return doneResult.getString().section(',', 1);
}

// ---------------------------------------------------------------------------
// Helper: collect download chunks until DownloadDone
// ---------------------------------------------------------------------------
static QByteArray collectDownloadChunks(StorageModulePlugin* plugin, int timeout) {
    QEventLoop loop;
    QByteArray collected;
    bool success = false;
    QMetaObject::Connection connection;

    auto fn = [&](const StorageSignal& s, int code, const QString& m) {
        if (s == StorageSignal::DownloadProgress) {
            collected.append(m.toUtf8());
        } else if (s == StorageSignal::DownloadDone) {
            success = (code == RET_OK);
            QObject::disconnect(connection);
            loop.quit();
        }
    };

    connection = QObject::connect(plugin, &StorageModulePlugin::storageResponse, &loop, fn);

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        QObject::disconnect(connection);
        loop.quit();
    });

    timer.start(timeout);
    loop.exec();
    return success ? collected : QByteArray();
}

// ---------------------------------------------------------------------------
// RAII guard: starts the storage node on construction, stops it on destruction.
// ---------------------------------------------------------------------------
struct PluginGuard {
    StorageModulePlugin* plugin;
    QTemporaryDir dataDir;

    PluginGuard() : dataDir(QDir::tempPath() + "/logos-storage-integration-test") {
        if (!dataDir.isValid())
            throw LogosTestFailure("Failed to create temp directory.");

        QString logFile = dataDir.path() + "/" + LOG_FILENAME;
        plugin = new StorageModulePlugin();

        const QString config =
            QString(R"({"data-dir": "%1", "log-level": "DEBUG", "log-file": "%2"})").arg(dataDir.path(), logFile);

        if (!plugin->init(config))
            throw LogosTestFailure("Failed to init storage plugin.");
        if (!plugin->start())
            throw LogosTestFailure("Failed to start storage plugin.");

        const int START_TIMEOUT = 15000;
        LogosResult result = waitForSignal(plugin, StorageSignal::Start, START_TIMEOUT);
        if (!result.success)
            throw LogosTestFailure("Storage node did not start within timeout.");
    }

    ~PluginGuard() {
        plugin->stop();
        waitForSignal(plugin, StorageSignal::Stop, DEFAULT_TIMEOUT);
        plugin->destroy();
        delete plugin;
    }
};

// ── test_version ────────────────────────────────────────────────────────────

LOGOS_TEST(integration_version) {
    PluginGuard guard;
    LogosResult result = guard.plugin->version();

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_FALSE(result.getString().isEmpty());
}

// ── test_dataDir ────────────────────────────────────────────────────────────

LOGOS_TEST(integration_dataDir) {
    PluginGuard guard;
    LogosResult result = guard.plugin->dataDir();

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_EQ(result.getString().toStdString(), guard.dataDir.path().toStdString());
}

// ── test_peerId ─────────────────────────────────────────────────────────────

LOGOS_TEST(integration_peerId) {
    PluginGuard guard;
    LogosResult result = guard.plugin->peerId();

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_FALSE(result.getString().isEmpty());
}

// ── test_debug ──────────────────────────────────────────────────────────────

LOGOS_TEST(integration_debug) {
    PluginGuard guard;
    LogosResult result = guard.plugin->debug();

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_FALSE(result.getString("id").isEmpty());
    LOGOS_ASSERT(result.getMap().contains("addrs"));
    LOGOS_ASSERT(result.getMap().contains("announceAddresses"));
    LOGOS_ASSERT(result.getMap().contains("table"));
}

// ── test_spr ────────────────────────────────────────────────────────────────

LOGOS_TEST(integration_spr) {
    PluginGuard guard;
    LogosResult result = guard.plugin->spr();

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_FALSE(result.getString().isEmpty());
}

// ── test_uploadFile ─────────────────────────────────────────────────────────

LOGOS_TEST(integration_uploadFile) {
    PluginGuard guard;
    const QString cid = uploadFile(guard.plugin, "Hello, Logos Storage!", "test_upload.txt");
    LOGOS_ASSERT_FALSE(cid.isEmpty());
}

// ── test_uploadWorkflowManual ───────────────────────────────────────────────

LOGOS_TEST(integration_uploadWorkflowManual) {
    PluginGuard guard;

    const QString filePath = guard.dataDir.path() + "/test_manual_upload.txt";
    const QByteArray content = "Hello, Logos Storage! Manual upload test.";
    QFile f(filePath);
    LOGOS_ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(content);
    f.close();

    // Step 1: init upload session
    LogosResult initResult = guard.plugin->uploadInit(filePath);
    LOGOS_ASSERT_TRUE(initResult.success);
    const QString sessionId = initResult.getString();
    LOGOS_ASSERT_FALSE(sessionId.isEmpty());

    // Step 2: upload the content as a single chunk
    LogosResult chunkResult = guard.plugin->uploadChunk(sessionId, content);
    LOGOS_ASSERT_TRUE(chunkResult.success);

    // Step 3: finalize, get the CID
    LogosResult finalResult = guard.plugin->uploadFinalize(sessionId);
    LOGOS_ASSERT_TRUE(finalResult.success);
    const QString cid = finalResult.getString();
    LOGOS_ASSERT_FALSE(cid.isEmpty());
}

// ── test_downloadFile ───────────────────────────────────────────────────────

LOGOS_TEST(integration_downloadFile) {
    PluginGuard guard;

    const QByteArray content = "Hello, Logos Download Test!";
    const QString cid = uploadFile(guard.plugin, content, "test_download.txt");
    LOGOS_ASSERT_FALSE(cid.isEmpty());

    const QString downloadPath = guard.dataDir.path() + "/test_download_result.txt";
    LogosResult downloadStart = guard.plugin->downloadToUrl(cid, QUrl::fromLocalFile(downloadPath));
    LOGOS_ASSERT_TRUE(downloadStart.success);

    LogosResult downloadDone = waitForSignal(guard.plugin, StorageSignal::DownloadDone, DEFAULT_TIMEOUT);
    LOGOS_ASSERT_TRUE(downloadDone.success);

    QFile downloaded(downloadPath);
    LOGOS_ASSERT_TRUE(downloaded.open(QIODevice::ReadOnly));
    const QByteArray downloadedContent = downloaded.readAll();
    downloaded.close();

    LOGOS_ASSERT_EQ(downloadedContent.toStdString(), content.toStdString());
}

// ── test_downloadChunks ─────────────────────────────────────────────────────

LOGOS_TEST(integration_downloadChunks) {
    PluginGuard guard;

    const QByteArray content = "Hello, Logos Chunks Download Test!";
    const QString cid = uploadFile(guard.plugin, content, "test_chunks_src.txt");
    LOGOS_ASSERT_FALSE(cid.isEmpty());

    LogosResult startResult = guard.plugin->downloadChunks(cid);
    LOGOS_ASSERT_TRUE(startResult.success);

    const QByteArray downloaded = collectDownloadChunks(guard.plugin, DEFAULT_TIMEOUT);
    LOGOS_ASSERT_FALSE(downloaded.isEmpty());
    LOGOS_ASSERT_EQ(downloaded.toStdString(), content.toStdString());
}

// ── test_exists ─────────────────────────────────────────────────────────────

LOGOS_TEST(integration_exists) {
    PluginGuard guard;

    const QString cid = uploadFile(guard.plugin, "Hello, Logos Exists Test!", "test_exists_src.txt");
    LOGOS_ASSERT_FALSE(cid.isEmpty());

    LogosResult result = guard.plugin->exists(cid);
    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_TRUE(result.getBool());
}

// ── test_fetch ──────────────────────────────────────────────────────────────

LOGOS_TEST(integration_fetch) {
    PluginGuard guard;

    const QString cid = uploadFile(guard.plugin, "Hello, Logos Fetch Test!", "test_fetch_src.txt");
    LOGOS_ASSERT_FALSE(cid.isEmpty());

    LogosResult result = guard.plugin->fetch(cid);
    LOGOS_ASSERT_TRUE(result.success);
}

// ── test_remove ─────────────────────────────────────────────────────────────

LOGOS_TEST(integration_remove) {
    PluginGuard guard;

    const QString cid = uploadFile(guard.plugin, "Hello, Logos Remove Test!", "test_remove_src.txt");
    LOGOS_ASSERT_FALSE(cid.isEmpty());

    LogosResult existsResult = guard.plugin->exists(cid);
    LOGOS_ASSERT_TRUE(existsResult.success);
    LOGOS_ASSERT_TRUE(existsResult.getBool());

    LogosResult removeResult = guard.plugin->remove(cid);
    LOGOS_ASSERT_TRUE(removeResult.success);

    existsResult = guard.plugin->exists(cid);
    LOGOS_ASSERT_TRUE(existsResult.success);
    LOGOS_ASSERT_FALSE(existsResult.getBool());
}

// ── test_space ──────────────────────────────────────────────────────────────

LOGOS_TEST(integration_space) {
    PluginGuard guard;

    LogosResult result = guard.plugin->space();
    LOGOS_ASSERT_TRUE(result.success);

    const QVariantMap map = result.getMap();
    LOGOS_ASSERT(map.contains("totalBlocks"));
    LOGOS_ASSERT(map.contains("quotaMaxBytes"));
    LOGOS_ASSERT(map.contains("quotaUsedBytes"));
    LOGOS_ASSERT(map.contains("quotaReservedBytes"));
}

// ── test_manifests ──────────────────────────────────────────────────────────

LOGOS_TEST(integration_manifests) {
    PluginGuard guard;

    const QByteArray content = "Hello, Logos Manifests Test!";
    const QString cid = uploadFile(guard.plugin, content, "test_manifests_src.txt");
    LOGOS_ASSERT_FALSE(cid.isEmpty());

    LogosResult result = guard.plugin->manifests();
    LOGOS_ASSERT_TRUE(result.success);

    const QVariantList list = result.getList();
    LOGOS_ASSERT_FALSE(list.isEmpty());

    bool found = false;
    for (const QVariant& v : list) {
        const QVariantMap m = v.toMap();
        if (m["cid"].toString() == cid) {
            found = true;
            LOGOS_ASSERT_FALSE(m["treeCid"].toString().isEmpty());
            LOGOS_ASSERT_EQ(m["datasetSize"].toInt(), static_cast<int>(content.size()));
            break;
        }
    }
    LOGOS_ASSERT_TRUE(found);
}

// ── test_downloadManifest ───────────────────────────────────────────────────

LOGOS_TEST(integration_downloadManifest) {
    PluginGuard guard;

    const QByteArray content = "Hello, Logos DownloadManifest Test!";
    const QString cid = uploadFile(guard.plugin, content, "test_download_manifest_src.txt");
    LOGOS_ASSERT_FALSE(cid.isEmpty());

    LogosResult result = guard.plugin->downloadManifest(cid);
    LOGOS_ASSERT_TRUE(result.success);

    const QVariantMap manifest = result.getMap();
    LOGOS_ASSERT_FALSE(manifest.isEmpty());
    LOGOS_ASSERT_FALSE(manifest["treeCid"].toString().isEmpty());
    LOGOS_ASSERT_EQ(manifest["datasetSize"].toInt(), static_cast<int>(content.size()));
}

// ── test_updateLogLevel ─────────────────────────────────────────────────────

LOGOS_TEST(integration_updateLogLevel) {
    PluginGuard guard;

    LOGOS_ASSERT_TRUE(guard.plugin->updateLogLevel("TRACE").success);

    // Upload a file to generate TRACE logs
    {
        const QByteArray content = "Hello, Logos Log Level Test!";
        uploadFile(guard.plugin, content, "test_loglevel_src.txt");
    }

    QString logFile = guard.dataDir.path() + "/" + LOG_FILENAME;
    QFile file(logFile);
    LOGOS_ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString logContent = QString::fromUtf8(file.readAll());
    file.close();

    LOGOS_ASSERT_TRUE(logContent.contains("TRC"));
}
