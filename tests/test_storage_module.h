#pragma once

#include <QObject>

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
