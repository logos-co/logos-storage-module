#pragma once

#include "test_storage_module_base.h"

class TestStorageModuleStarted : public TestStorageModuleBase
{
    Q_OBJECT

private slots:
    // Runs before each test: creates plugin, inits and starts the node.
    void init();

    void test_dataDir();
    void test_peerId();
    void test_debug();
    void test_spr();
};
