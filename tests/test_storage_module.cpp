#include "test_storage_module.h"

#include "storage_module_plugin.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QtTest/QtTest>

void TestStorageModule::initTestCase()
{
    m_dataDir = QTemporaryDir(QDir::tempPath() + "/logos-storage-test");
    QVERIFY(m_dataDir.isValid());

    QString logFile = m_dataDir.path() + "/" + LOG_FILENAME;

    m_plugin = new StorageModulePlugin();

    const QString config =
        QString(R"({"data-dir": "%1", "log-level": "DEBUG", "log-file": "%2"})").arg(m_dataDir.path(), logFile);
    QVERIFY(m_plugin->init(config));
    QVERIFY(m_plugin->start());

    // start requires more time.
    const int TIMEOUT = 15000;
    LogosResult result = waitForSignal(StorageSignal::Start, TIMEOUT);
    QVERIFY2(result.success, "Cannot start the plugin.");
}

void TestStorageModule::cleanupTestCase()
{
    if (!m_plugin) {
        return;
    }

    m_plugin->stop();
    waitForSignal(StorageSignal::Stop, DEFAULT_TIMEOUT);

    m_plugin->destroy();
    delete m_plugin;
    m_plugin = nullptr;
}

LogosResult TestStorageModule::waitForSignal(StorageSignal signal, int timeout)
{
    QEventLoop loop;
    LogosResult result = {false, ""};

    QMetaObject::Connection connection;

    auto fn = [&](const StorageSignal& s, int code, const QString& m) {
        if (s != signal)
            return;

        result.success = code == RET_OK;
        result.value = m;

        QObject::disconnect(connection);
        loop.quit();
    };

    connection = QObject::connect(m_plugin, &StorageModulePlugin::storageResponse, &loop, fn);

    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        result.success = false;
        result.value = QString("Cannot get response before timeout.");
        loop.quit();
    });

    timer.start(timeout);
    loop.exec();

    return result;
}

QString TestStorageModule::uploadFile(const QByteArray& content, const QString& filename) {
    QTemporaryDir folder = QTemporaryDir(QDir::currentPath());
    const QString filePath = folder.path() + "/" + filename;
    QFile f(filePath);

    if (!f.open(QIODevice::WriteOnly)) {
        return "";
    }

    f.write(content);
    f.close();

    LogosResult startResult = m_plugin->uploadUrl(QUrl::fromLocalFile(filePath));

    if (!startResult.success) {
        return "";
    }

    LogosResult doneResult = waitForSignal(StorageSignal::UploadDone, DEFAULT_TIMEOUT);

    if (!doneResult.success) {
        return "";
    }

    return doneResult.getString().section(',', 1);
}

void TestStorageModule::test_version() {
    LogosResult result = m_plugin->version();

    QVERIFY2(result.success, "Cannot get the plugin version.");
    QVERIFY(!result.getString().isEmpty());
}

void TestStorageModule::test_dataDir() {
    LogosResult result = m_plugin->dataDir();

    QVERIFY2(result.success, "Cannot get data dir.");
    QCOMPARE(result.getString(), m_dataDir.path());
}

void TestStorageModule::test_peerId() {
    LogosResult result = m_plugin->peerId();

    QVERIFY2(result.success, "Cannot get peer id.");
    QVERIFY(!result.getString().isEmpty());
}

void TestStorageModule::test_debug() {
    LogosResult result = m_plugin->debug();

    QVERIFY2(result.success, "Cannot get debug info.");
    QVERIFY(!result.getString("id").isEmpty());
    QVERIFY(result.getMap().contains("addrs"));
    QVERIFY(result.getMap().contains("announceAddresses"));
    QVERIFY(result.getMap().contains("table"));
}

void TestStorageModule::test_spr() {
    LogosResult result = m_plugin->spr();

    QVERIFY2(result.success, "Cannot get SPR.");
    QVERIFY(!result.getString().isEmpty());
}

void TestStorageModule::test_uploadFile() {
    const QString cid = uploadFile("Hello, Logos Storage!", "test_upload.txt");
    QVERIFY2(!cid.isEmpty(), "CID should not be empty after upload.");
}

void TestStorageModule::test_uploadWorkflowManual() {
    const QString filePath = m_dataDir.path() + "/test_manual_upload.txt";
    const QByteArray content = "Hello, Logos Storage! Manual upload test.";
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
    f.close();

    // Step 1: init upload session.
    LogosResult initResult = m_plugin->uploadInit(filePath);
    QVERIFY2(initResult.success, "uploadInit failed.");
    const QString sessionId = initResult.getString();
    QVERIFY2(!sessionId.isEmpty(), "Session ID should not be empty.");

    // Step 2: upload the content as a single chunk.
    LogosResult chunkResult = m_plugin->uploadChunk(sessionId, content);
    QVERIFY2(chunkResult.success, "uploadChunk failed.");

    // Step 3: finalize, get the CID.
    LogosResult finalResult = m_plugin->uploadFinalize(sessionId);
    QVERIFY2(finalResult.success, "uploadFinalize failed.");
    const QString cid = finalResult.getString();
    QVERIFY2(!cid.isEmpty(), "CID should not be empty.");
}

void TestStorageModule::test_downloadFile() {
    const QByteArray content = "Hello, Logos Download Test!";
    const QString cid = uploadFile(content, "test_download.txt");
    QVERIFY2(!cid.isEmpty(), "Upload failed — cannot proceed with download.");

    // Download the file to a local path.
    const QString downloadPath = m_dataDir.path() + "/test_download.txt";
    LogosResult downloadStart = m_plugin->downloadToUrl(cid, QUrl::fromLocalFile(downloadPath));
    QVERIFY2(downloadStart.success, "downloadToUrl failed to start.");

    // Wait for completion
    LogosResult downloadDone = waitForSignal(StorageSignal::DownloadDone, DEFAULT_TIMEOUT);
    QVERIFY2(downloadDone.success, "Download did not complete successfully.");

    // Check downloaded content
    QFile downloaded(downloadPath);
    QVERIFY2(downloaded.open(QIODevice::ReadOnly), "Cannot open downloaded file.");
    const QByteArray downloadedContent = downloaded.readAll();
    downloaded.close();

    QCOMPARE(downloadedContent, content);
}

QByteArray TestStorageModule::collectDownloadChunks(int timeout) {
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

    connection = QObject::connect(m_plugin, &StorageModulePlugin::storageResponse, &loop, fn);

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

void TestStorageModule::test_downloadChunks() {
    const QByteArray content = "Hello, Logos Chunks Download Test!";
    const QString cid = uploadFile(content, "test_chunks_src.txt");
    QVERIFY2(!cid.isEmpty(), "Upload failed — cannot proceed with download.");

    LogosResult startResult = m_plugin->downloadChunks(cid);
    QVERIFY2(startResult.success, "downloadChunks failed to start.");

    const QByteArray downloaded = collectDownloadChunks(DEFAULT_TIMEOUT);
    QVERIFY2(!downloaded.isEmpty(), "No chunks received.");
    QCOMPARE(downloaded, content);
}

void TestStorageModule::test_exists() {
    const QString cid = uploadFile("Hello, Logos Exists Test!", "test_exists_src.txt");
    QVERIFY2(!cid.isEmpty(), "Upload failed.");

    LogosResult result = m_plugin->exists(cid);
    QVERIFY2(result.success, "exists() failed.");
    QVERIFY2(result.getBool(), "CID should exist after upload.");
}

void TestStorageModule::test_fetch() {
    const QString cid = uploadFile("Hello, Logos Fetch Test!", "test_fetch_src.txt");
    QVERIFY2(!cid.isEmpty(), "Upload failed.");

    LogosResult result = m_plugin->fetch(cid);
    QVERIFY2(result.success, "fetch() failed.");

    // Not much to test ! The fetch is done in background.
    // We could wait and check CID exists but it is gonna to
    // be a flaky test.
}

void TestStorageModule::test_remove() {
    const QString cid = uploadFile("Hello, Logos Remove Test!", "test_remove_src.txt");
    QVERIFY2(!cid.isEmpty(), "Upload failed.");

    LogosResult existsResult = m_plugin->exists(cid);
    QVERIFY2(existsResult.success && existsResult.getBool(), "CID should exist before remove.");
    QVERIFY2(existsResult.getBool(), "CID should exist after remove.");

    LogosResult removeResult = m_plugin->remove(cid);
    QVERIFY2(removeResult.success, "remove() failed.");

    existsResult = m_plugin->exists(cid);
    QVERIFY2(existsResult.success, "exists() after remove failed.");
    QVERIFY2(!existsResult.getBool(), "CID should not exist after remove.");
}

void TestStorageModule::test_space() {
    LogosResult result = m_plugin->space();
    QVERIFY2(result.success, "space() failed.");

    const QVariantMap map = result.getMap();
    QVERIFY2(map.contains("totalBlocks"), "Missing field: totalBlocks.");
    QVERIFY2(map.contains("quotaMaxBytes"), "Missing field: quotaMaxBytes.");
    QVERIFY2(map.contains("quotaUsedBytes"), "Missing field: quotaUsedBytes.");
    QVERIFY2(map.contains("quotaReservedBytes"), "Missing field: quotaReservedBytes.");
}

void TestStorageModule::test_manifests() {
    const QByteArray content = "Hello, Logos Manifests Test!";
    const QString cid = uploadFile(content, "test_manifests_src.txt");
    QVERIFY2(!cid.isEmpty(), "Upload failed.");

    LogosResult result = m_plugin->manifests();
    QVERIFY2(result.success, "manifests() failed.");

    const QVariantList list = result.getList();
    QVERIFY2(!list.isEmpty(), "Manifests list should not be empty.");

    bool found = false;
    for (const QVariant& v : list) {
        const QVariantMap m = v.toMap();
        if (m["cid"].toString() == cid) {
            found = true;
            QVERIFY(!m["treeCid"].toString().isEmpty());
            QCOMPARE(m["datasetSize"].toInt(), content.size());
            break;
        }
    }
    QVERIFY2(found, "Uploaded CID not found in manifests list.");
}

void TestStorageModule::test_downloadManifest() {
    const QByteArray content = "Hello, Logos DownloadManifest Test!";
    const QString cid = uploadFile(content, "test_download_manifest_src.txt");
    QVERIFY2(!cid.isEmpty(), "Upload failed.");

    LogosResult result = m_plugin->downloadManifest(cid);
    QVERIFY2(result.success, "downloadManifest() failed.");

    const QVariantMap manifest = result.getMap();
    QVERIFY2(!manifest.isEmpty(), "Manifest should not be empty.");
    QVERIFY2(!manifest["treeCid"].toString().isEmpty(), "treeCid should not be empty.");
    QCOMPARE(manifest["datasetSize"].toInt(), content.size());
}

void TestStorageModule::test_updateLogLevel()
{
    QVERIFY2(m_plugin->updateLogLevel("TRACE").success, "Cannot update log level to TRACE.");

    // Upload a file to generate TRC logs
    {
        const QByteArray content = "Hello, Logos Manifests Test!";
        const QString cid = uploadFile(content, "test_manifests_src.txt");
    }

    QString logFile = m_dataDir.path() + "/" + LOG_FILENAME;
    QFile file(logFile);
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "Cannot open log file.");
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    QVERIFY2(content.contains("TRC"), "No TRACE entries found in log file after level update.");
}

QTEST_MAIN(TestStorageModule)
