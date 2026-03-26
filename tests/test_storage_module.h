#pragma once

#include "logos_types.h"

#include <QObject>
#include <QTemporaryDir>

const int DEFAULT_TIMEOUT = 3000;
const QString LOG_FILENAME = "storage.log";

class StorageModulePlugin;
enum class StorageSignal;

class TestStorageModule : public QObject
{
    Q_OBJECT

private:
    StorageModulePlugin* m_plugin = nullptr;
    QTemporaryDir m_dataDir;
    LogosResult waitForSignal(StorageSignal signal, int timeout);
    QString uploadFile(const QByteArray& content, const QString& filename);
    QByteArray collectDownloadChunks(int timeout);

private slots:
  // Runs before each test.
  void initTestCase();
  // Runs after each test.
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
  void test_updateLogLevel();
};
