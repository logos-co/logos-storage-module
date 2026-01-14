#pragma once

#include <QtCore/QObject>
#include "interface.h"

class StorageModuleInterface : public PluginInterface
{
public:
    virtual ~StorageModuleInterface() {}

    // Create a new instance of a Logos Storage node.
    // `cfg` is a JSON string with the configuration overwriting defaults.
    // Returns true if initialization was successful.
    Q_INVOKABLE virtual bool init(const QString &cfg) = 0;
    Q_INVOKABLE virtual bool start() = 0;
    Q_INVOKABLE virtual QString version() = 0;
    Q_INVOKABLE virtual bool destroy() = 0;
    Q_INVOKABLE virtual bool stop() = 0;

signals:
    // for now this is required for events, later it might not be necessary if using a proxy
    void eventResponse(const QString &eventName, const QVariantList &data);
};

#define StorageModuleInterface_iid "org.logos.StorageModuleInterface"
Q_DECLARE_INTERFACE(StorageModuleInterface, StorageModuleInterface_iid)
