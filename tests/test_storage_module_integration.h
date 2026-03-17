#pragma once

#include "logos_api.h"
#include "logos_sdk.h"
#include <QObject>
#include <QTemporaryDir>
#include <QVariantList>

const int DEFAULT_TIMEOUT = 60000;

class LogosAPI;
struct LogosModules;

class TestStorageModuleIntegration : public QObject
{
    Q_OBJECT

private:
    LogosAPI*     m_api   = nullptr;
    LogosModules* m_logos = nullptr;
    QTemporaryDir m_dataDir;

    QVariantList waitForEvent(const QString& eventName, int timeout);
    QString      uploadFile(const QByteArray& content, const QString& filename);

private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_peerId();
    void test_upload();
    void test_download();
};
