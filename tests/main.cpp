#include "test_storage_module.h"
#include "test_storage_module_started.h"

#include <QCoreApplication>
#include <QtTest/QtTest>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    int result = 0;
    result |= QTest::qExec(new TestStorageModule, argc, argv);
    result |= QTest::qExec(new TestStorageModuleStarted, argc, argv);

    return result;
}
