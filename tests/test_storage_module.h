#pragma once

#include "logos_types.h"

#include <QObject>
#include <QTemporaryDir>

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
    void test_updateLogLevel();
};
