#pragma once

#include "logos_types.h"

#include <QObject>
#include <QTemporaryDir>

class StorageModulePlugin;
enum class StorageSignal;

class TestStorageModuleBase : public QObject
{
    Q_OBJECT

protected:
    StorageModulePlugin* m_plugin = nullptr;
    QTemporaryDir m_dataDir;
    bool m_started = false;

    // Creates a fresh plugin instance and initialises it with a temp data dir.
    // Returns false if the plugin fails to initialise.
    // Intended to be called from init() in derived classes.
    bool initPlugin();

    // Mirrors StorageModulePlugin::waitForSignal — same pattern, usable from tests.
    LogosResult waitForSignal(StorageSignal signal, int timeout);

protected slots:
    // Runs after each test: stops the module if running, then destroys it.
    void cleanup();
};
