#include "test_storage_module_started.h"

#include "storage_module_plugin.h"

#include <QtTest/QtTest>

void TestStorageModuleStarted::init()
{
    QVERIFY(initPlugin());
    QVERIFY(m_plugin->start());

    LogosResult result = waitForSignal(StorageSignal::Start, 10000);

    QVERIFY2(result.success, "Cannot start the plugin.");
    m_started = true;
}

void TestStorageModuleStarted::test_dataDir()
{
    LogosResult result = m_plugin->dataDir();

    QVERIFY2(result.success, "Cannot get data dir.");
    QVERIFY(!result.getString().isEmpty());
}

void TestStorageModuleStarted::test_peerId()
{
    LogosResult result = m_plugin->peerId();

    QVERIFY2(result.success, "Cannot get peer id.");
    QVERIFY(!result.getString().isEmpty());
}

void TestStorageModuleStarted::test_debug()
{
    LogosResult result = m_plugin->debug();

    QVERIFY2(result.success, "Cannot get debug info.");
    QVERIFY(!result.getString().isEmpty());
}

void TestStorageModuleStarted::test_spr()
{
    LogosResult result = m_plugin->spr();

    QVERIFY2(result.success, "Cannot get SPR.");
    QVERIFY(!result.getString().isEmpty());
}
