#pragma once

#include "logos_types.h"

#include <QObject>
#include <QTemporaryDir>

const int DEFAULT_TIMEOUT = 3000;

class StorageModulePlugin;
enum class StorageSignal;

class TestStorageModule : public QObject
{
    Q_OBJECT

private:
    StorageModulePlugin* m_plugin = nullptr;
    QTemporaryDir m_dataDir;
    QString m_logFile;
    LogosResult waitForSignal(StorageSignal signal, int timeout);
    QString uploadFile(const QByteArray& content, const QString& filename);
    QByteArray collectDownloadChunks(int timeout);

private slots:
    // Runs once before all tests.
    void initTestCase();
    // Runs once after all tests.
    void cleanupTestCase();

    void test_version();
    void test_dataDir();
    void test_peerId();
    void test_debug();
    void test_spr();
    void test_uploadFile();
    void test_uploadWorkflowManual();
    void test_downloadFile();
    void test_downloadChunks();
    void test_exists();
    void test_fetch();
    void test_remove();
    void test_space();
    void test_manifests();
    void test_downloadManifest();
    // Must be last: stops the node to flush TRACE entries to the log file.
    void test_updateLogLevel();
};
