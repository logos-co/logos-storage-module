#pragma once

#include "interface.h"
#include <QtCore/QObject>

class StorageModuleInterface : public PluginInterface {
  public:
    virtual ~StorageModuleInterface() {}

    // Create a new instance of a Logos Storage node.
    // `cfg` is a JSON string with the configuration overwriting defaults.
    //
    // Returns true if initialization was successful.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual bool init(const QString& cfg) = 0;

    // Start starts the Codex node.
    //
    // Returns true if the start command was successfully issued.
    //
    // The method is asynchronous; completion is signaled via events.
    // Emit "storageStart" event on completion.
    Q_INVOKABLE virtual bool start() = 0;

    // Get the Logos Storage version string.
    // This call does not require the node to be started.
    //
    // Return the version string or an empty string on error.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString version() = 0;

    // Stop the Logos Storage node.
    // The node can be started and stopped multiple times.
    //
    // Returns true if the stop command was successfully issued.
    //
    // The method is asynchronous; completion is signaled via events.
    // Emit "storageStop" event on completion.
    Q_INVOKABLE virtual bool stop() = 0;

    // Destroys an instance of a Logos Storage node.
    // This will free all resources associated with the node.
    // The node must be stopped and closed before calling this function.
    // This method calls internally storage_close and storage_destroy.
    //
    // Returns true if the destroy command was successfully issued.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual bool destroy() = 0;

    // Get the Logos Storage data directory.
    //
    // Return the data directory string or an empty string on error.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString dataDir() = 0;

    // Get the Logos Storage debug information.
    //
    // Return the debug string or an empty string on error.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString debug() = 0;

    // Get the Logos Storage node Peer Id.
    // Peer Identity reference as specified at
    // https://docs.libp2p.io/concepts/fundamentals/peers/
    //
    // Return the peer id string or an empty string on error.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString peerId() = 0;

    // Get the node's Signed Peer Record (SPR)
    //
    // Return the signed peer record string or an empty string on error.
    //
    // The method is synchronous.
    Q_INVOKABLE virtual QString spr() = 0;

    // Set the log level at run time.
    // `logLevel` can be one of:
    // TRACE, DEBUG, INFO, NOTICE, WARN, ERROR or FATAL
    Q_INVOKABLE virtual void updateLogLevel(const QString& logLevel) = 0;

  signals:
    // for now this is required for events, later it might not be necessary if using a proxy
    void eventResponse(const QString& eventName, const QVariantList& data);
};

#define StorageModuleInterface_iid "org.logos.StorageModuleInterface"
Q_DECLARE_INTERFACE(StorageModuleInterface, StorageModuleInterface_iid)
