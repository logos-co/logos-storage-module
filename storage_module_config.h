#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

enum class LogFormat {
    Auto,
    Text,
    Json
};

enum class RepoKind {
    Fs,
    Sqlite,
    Leveldb
};

QString toString(LogFormat f);
QString toString(RepoKind k);

struct StorageConfig
{
    // Default: INFO
    QString logLevel;

	// Specifies what kind of logs should be written to stdout
	// Default: auto
    LogFormat logFormat;

	// Enable the metrics server
	// Default: false
    bool metricsEnabled = false;

	// Listening address of the metrics server
	// Default: 127.0.0.1
    QString metricsAddress;

	// Listening HTTP port of the metrics server
	// Default: 8008
    int metricsPort = 0;

	// The directory where codex will store configuration and data
	// Default:
	// $HOME\AppData\Roaming\Storage on Windows
	// $HOME/Library/Application Support/Storage on macOS
	// $HOME/.cache/storage on Linux
    QString dataDir;

	// Multi Addresses to listen on
	// Default: ["/ip4/0.0.0.0/tcp/0"]
    QStringList listenAddrs;

	// Specify method to use for determining public address.
	// Must be one of: any, none, upnp, pmp, extip:<IP>
	// Default: any
    QString nat;

	// Discovery (UDP) port
	// Default: 8090
    int discoveryPort = 0;

	// Source of network (secp256k1) private key file path or name
	// Default: "key"
    QString netPrivKeyFile;

	// Specifies one or more bootstrap nodes to use when connecting to the net
    QStringList bootstrapNodes;

	// The maximum number of peers to connect to.
	// Default: 160
    int maxPeers = 0;

	// Number of worker threads (\"0\" = use as many threads as there are CPU cores available)
	// Default: 0
    int numThreads = 0;

	// Node agent string which is used as identifier in network
	// Default: "Logos Storage"
    QString agentString;

	// Backend for main repo store (fs, sqlite, leveldb)
	// Default: fs
    RepoKind repoKind;

	// The size of the total storage quota dedicated to the node
	// Default: 20 GiBs
    int storageQuota = 0;

	// Default block timeout in seconds - 0 disables the ttl
	// Default: 30 days
    QString blockTtl;

	// Time interval in seconds - determines frequency of block
	// maintenance cycle: how often blocks are checked for expiration and cleanup
	// Default: 10 minutes
    QString blockMaintenanceInterval;

	// Number of blocks to check every maintenance cycle
	// Default: 1000
    int blockMaintenanceNumberOfBlocks = 0;

	// Number of times to retry fetching a block before giving up
	// Default: 3000
    int blockRetries = 0;

	// The size of the block cache, 0 disables the cache -
	// might help on slow hardrives
	// Default: 0
    int cacheSize = 0;

	// Default: "" (no log file)
    QString logFile;

    QJsonObject toJson() const;
    static StorageConfig fromJson(const QJsonObject&);
    QString toJsonString(QJsonDocument::JsonFormat fmt = QJsonDocument::Compact) const;
};