#include <QtTest/QtTest>
#include <QString>

// NOTE: The StorageEvent enum and eventName() function are duplicated here
// from storage_module_plugin.h to keep these tests free of libstorage/logos-sdk
// dependencies. They must stay in sync with the plugin header.
//
// TODO: extract StorageEvent + eventName() into a standalone header
//       (e.g. storage_module_types.h) so both the plugin and the tests
//       can share the same definition.

enum class StorageEvent {
    Start,
    Stop,
    Connect,
    UploadProgress,
    UploadDone,
    DownloadProgress,
    DownloadDone
};

inline QString eventName(StorageEvent event)
{
    switch (event) {
    case StorageEvent::Start:
        return "storageStart";
    case StorageEvent::Stop:
        return "storageStop";
    case StorageEvent::Connect:
        return "storageConnect";
    case StorageEvent::UploadProgress:
        return "storageUploadProgress";
    case StorageEvent::UploadDone:
        return "storageUploadDone";
    case StorageEvent::DownloadProgress:
        return "storageDownloadProgress";
    case StorageEvent::DownloadDone:
        return "storageDownloadDone";
    }
    return "";
}

class TestStorageModule : public QObject
{
    Q_OBJECT

private slots:
    // eventName() — maps StorageEvent values to their wire-protocol string names.
    // These strings are used by the Logos SDK event bus, so correctness is critical.
    void test_eventName_start();
    void test_eventName_stop();
    void test_eventName_connect();
    void test_eventName_uploadProgress();
    void test_eventName_uploadDone();
    void test_eventName_downloadProgress();
    void test_eventName_downloadDone();
};

void TestStorageModule::test_eventName_start()
{
    QCOMPARE(eventName(StorageEvent::Start), QString("storageStart"));
}

void TestStorageModule::test_eventName_stop()
{
    QCOMPARE(eventName(StorageEvent::Stop), QString("storageStop"));
}

void TestStorageModule::test_eventName_connect()
{
    QCOMPARE(eventName(StorageEvent::Connect), QString("storageConnect"));
}

void TestStorageModule::test_eventName_uploadProgress()
{
    QCOMPARE(eventName(StorageEvent::UploadProgress), QString("storageUploadProgress"));
}

void TestStorageModule::test_eventName_uploadDone()
{
    QCOMPARE(eventName(StorageEvent::UploadDone), QString("storageUploadDone"));
}

void TestStorageModule::test_eventName_downloadProgress()
{
    QCOMPARE(eventName(StorageEvent::DownloadProgress), QString("storageDownloadProgress"));
}

void TestStorageModule::test_eventName_downloadDone()
{
    QCOMPARE(eventName(StorageEvent::DownloadDone), QString("storageDownloadDone"));
}

QTEST_MAIN(TestStorageModule)
#include "test_storage_module.moc"
