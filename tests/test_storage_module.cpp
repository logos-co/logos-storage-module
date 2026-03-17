#include "test_storage_module.h"

#include "storage_module_plugin.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QtTest/QtTest>

void TestStorageModule::initTestCase()
{
    m_dataDir = QTemporaryDir(QDir::currentPath());
    QVERIFY(m_dataDir.isValid());

    m_logFile = m_dataDir.path() + "/storage.log";

    m_plugin = new StorageModulePlugin();

    const QString config = QString(R"({"data-dir": "%1", "log-level": "DEBUG", "log-file": "%2"})")
                               .arg(m_dataDir.path(), m_logFile);
    QVERIFY(m_plugin->init(config));
    QVERIFY(m_plugin->start());

    LogosResult result = waitForSignal(StorageSignal::Start, 10000);
    QVERIFY2(result.success, "Cannot start the plugin.");
}

void TestStorageModule::cleanupTestCase()
{
    if (!m_plugin)
        return;

    m_plugin->stop();
    waitForSignal(StorageSignal::Stop, 5000);

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

void TestStorageModule::test_version()
{
    LogosResult result = m_plugin->version();

    QVERIFY2(result.success, "Cannot get the plugin version.");
    QVERIFY(!result.getString().isEmpty());
}

void TestStorageModule::test_dataDir()
{
    LogosResult result = m_plugin->dataDir();

    QVERIFY2(result.success, "Cannot get data dir.");
    QCOMPARE(result.getString(), m_dataDir.path());
}

void TestStorageModule::test_peerId()
{
    LogosResult result = m_plugin->peerId();

    QVERIFY2(result.success, "Cannot get peer id.");
    QVERIFY(!result.getString().isEmpty());
}

void TestStorageModule::test_debug()
{
    LogosResult result = m_plugin->debug();

    QVERIFY2(result.success, "Cannot get debug info.");
    QVERIFY(!result.getString("id").isEmpty());
    QVERIFY(result.getMap().contains("addrs"));
    QVERIFY(result.getMap().contains("announceAddresses"));
    QVERIFY(result.getMap().contains("table"));
}

void TestStorageModule::test_spr()
{
    LogosResult result = m_plugin->spr();

    QVERIFY2(result.success, "Cannot get SPR.");
    QVERIFY(!result.getString().isEmpty());
}

void TestStorageModule::test_uploadFile()
{
    const QString filePath = m_dataDir.path() + "/test_upload.txt";
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("Hello, Logos Storage!");
    f.close();

    // Run the upload command
    {
        LogosResult result = m_plugin->uploadUrl(QUrl::fromLocalFile(filePath));
        QVERIFY2(result.success, "uploadUrl failed to start.");

        const QString sessionId = result.getString();
        QVERIFY2(!sessionId.isEmpty(), "Session ID should not be empty.");
    }

    LogosResult result = waitForSignal(StorageSignal::UploadDone, 3000);
    QVERIFY2(result.success, "Upload did not complete successfully.");

    // The result comes with sessionId,cid
    const QString cid = result.getString().section(',', 1);
    QVERIFY2(!cid.isEmpty(), "CID should not be empty after upload.");
}

void TestStorageModule::test_uploadWorkflowManual()
{
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

void TestStorageModule::test_updateLogLevel()
{
    QVERIFY2(m_plugin->updateLogLevel("TRACE").success, "Cannot update log level to TRACE.");

    // Stop the plugin to produce TRC logs
    m_plugin->stop();
    waitForSignal(StorageSignal::Stop, 5000);

    QTest::qWait(500);

    QFile file(m_logFile);
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "Cannot open log file.");
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    QVERIFY2(content.contains("TRC"), "No TRACE entries found in log file after level update.");
}

QTEST_MAIN(TestStorageModule)
