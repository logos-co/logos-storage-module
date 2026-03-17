#include "test_storage_module_integration.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QtTest/QtTest>

extern "C" {
void logos_core_set_plugins_dir(const char*);
void logos_core_start();
void logos_core_cleanup();
int logos_core_load_plugin(const char*);
}

void TestStorageModuleIntegration::initTestCase()
{
    logos_core_set_plugins_dir(INTEGRATION_MODULES_DIR);
    logos_core_start();
    QVERIFY2(logos_core_load_plugin("storage_module"), "Failed to load storage_module plugin");

    m_api   = new LogosAPI("integration_tests", this);
    m_logos = new LogosModules(m_api);

    m_dataDir = QTemporaryDir(QDir::currentPath());
    QVERIFY(m_dataDir.isValid());

    const QString config =
        QString(R"({"data-dir": "%1", "log-level": "WARN"})").arg(m_dataDir.path());

    QVERIFY(m_logos->storage_module.init(config));
    m_logos->storage_module.start();

    const QVariantList startData = waitForEvent("storageStart", 30000);
    QVERIFY2(!startData.isEmpty() && startData.first().toBool(), "Cannot start the storage module.");
}

void TestStorageModuleIntegration::cleanupTestCase()
{
    if (!m_logos)
        return;

    m_logos->storage_module.stop();
    waitForEvent("storageStop", DEFAULT_TIMEOUT);

    logos_core_cleanup();

    delete m_logos;
    m_logos = nullptr;
    delete m_api;
    m_api = nullptr;
}

QVariantList TestStorageModuleIntegration::waitForEvent(const QString& eventName, int timeout)
{
    QEventLoop loop;
    QVariantList result;
    bool received = false;

    m_logos->storage_module.on(eventName, [&](const QVariantList& data) {
        if (received) return;
        received = true;
        result = data;
        loop.quit();
    });

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeout);
    loop.exec();

    return result;
}

QString TestStorageModuleIntegration::uploadFile(const QByteArray& content, const QString& filename)
{
    const QString filePath = m_dataDir.path() + "/" + filename;
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(content);
    f.close();

    m_logos->storage_module.uploadUrl(QUrl::fromLocalFile(filePath));
    const QVariantList data = waitForEvent("storageUploadDone", DEFAULT_TIMEOUT);

    if (data.isEmpty() || !data.first().toBool())
        return {};

    // payload: [success, sessionId, cid]
    return data.size() >= 3 ? data.at(2).toString() : QString();
}

void TestStorageModuleIntegration::test_peerId()
{
    const LogosResult result = m_logos->storage_module.peerId();
    QVERIFY2(result.success, "Cannot get peer id.");
    QVERIFY(!result.getString().isEmpty());
}

void TestStorageModuleIntegration::test_upload()
{
    const QString cid = uploadFile("Hello, Logos Integration Upload!", "test_upload.txt");
    QVERIFY2(!cid.isEmpty(), "Upload failed — CID is empty.");
}

void TestStorageModuleIntegration::test_download()
{
    const QByteArray content = "Hello, Logos Integration Download!";
    const QString cid = uploadFile(content, "test_download_src.txt");
    QVERIFY2(!cid.isEmpty(), "Upload failed — cannot proceed with download.");

    const QString downloadPath = m_dataDir.path() + "/test_download.txt";
    m_logos->storage_module.downloadToUrl(cid, QUrl::fromLocalFile(downloadPath));

    const QVariantList data = waitForEvent("storageDownloadDone", DEFAULT_TIMEOUT);
    QVERIFY2(!data.isEmpty() && data.first().toBool(), "Download did not complete successfully.");

    QFile downloaded(downloadPath);
    QVERIFY2(downloaded.open(QIODevice::ReadOnly), "Cannot open downloaded file.");
    QCOMPARE(downloaded.readAll(), content);
}

QTEST_MAIN(TestStorageModuleIntegration)
