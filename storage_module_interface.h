#pragma once

#include <QtCore/QObject>
#include "interface.h"

class StorageModuleInterface : public PluginInterface
{
public:
    virtual ~StorageModuleInterface() {}
    Q_INVOKABLE virtual bool foo(const QString &bar) = 0;
    Q_INVOKABLE virtual bool initStorage(const QString &cfg) = 0;
    Q_INVOKABLE virtual QString storageVersion() = 0;
};

#define StorageModuleInterface_iid "org.logos.StorageModuleInterface"
Q_DECLARE_INTERFACE(StorageModuleInterface, StorageModuleInterface_iid)
