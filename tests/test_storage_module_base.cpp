#include "test_storage_module_base.h"

#include "storage_module_plugin.h"

#include <QDir>
#include <QEventLoop>
#include <QTimer>

bool TestStorageModuleBase::initPlugin()
{
    m_dataDir = QTemporaryDir(QDir::currentPath() + "/storage_test_XXXXXX");
    if (!m_dataDir.isValid())
        return false;

    m_plugin = new StorageModulePlugin();

    const QString config = QString("{\"data-dir\": \"%1\", \"log-level\": \"DEBUG\"}").arg(m_dataDir.path());
    return m_plugin->init(config);
}

LogosResult TestStorageModuleBase::waitForSignal(StorageSignal signal, int timeout)
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

void TestStorageModuleBase::cleanup()
{
    if (!m_plugin)
        return;

    if (m_started) {
        m_plugin->stop();
        waitForSignal(StorageSignal::Stop, 5000);
        m_started = false;
    }

    m_plugin->destroy();
    delete m_plugin;
    m_plugin = nullptr;

    m_dataDir = QTemporaryDir();
}
