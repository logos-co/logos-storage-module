#include "storage_config.h"

QString toString(LogFormat f)
{
    switch (f)
    {
        case LogFormat::Auto:
        {
            return "auto";
        }

        case LogFormat::Text:
        {
            return "text";
        }

        case LogFormat::Json:
        {
            return "json";
        }
    }

    return {};
}

QString toString(RepoKind k)
{
    switch (k)
    {
        case RepoKind::Fs:
        {
            return "fs";
        }

        case RepoKind::Sqlite:
        {
            return "sqlite";
        }

        case RepoKind::Leveldb:
        {
            return "leveldb";
        }
    }

    return {};
}

QJsonObject StorageConfig::toJson() const
{
    QJsonObject o;

    auto put = [&](const char* key, const QJsonValue& value)
    {
        if (!value.isNull())
        {
            o[key] = value;
        }
    };

    if (!logLevel.isEmpty())
    {
        put("log-level", logLevel);
    }

    put("log-format", toString(logFormat));

    if (metricsEnabled)
    {
        put("metrics", metricsEnabled);
    }

    if (!metricsAddress.isEmpty())
    {
        put("metrics-address", metricsAddress);
    }

    if (metricsPort > 0)
    {
        put("metrics-port", metricsPort);
    }

    if (!dataDir.isEmpty())
    {
        put("data-dir", dataDir);
    }

    if (!listenAddrs.isEmpty())
    {
        QJsonArray array;

        for (const auto& addr : listenAddrs)
        {
            array.append(addr);
        }

        put("listen-addrs", array);
    }

    if (!nat.isEmpty())
    {
        put("nat", nat);
    }

    if (discoveryPort > 0)
    {
        put("disc-port", discoveryPort);
    }

    if (!netPrivKeyFile.isEmpty())
    {
        put("net-privkey", netPrivKeyFile);
    }

    if (!bootstrapNodes.isEmpty())
    {
        QJsonArray array;

        for (const auto& node : bootstrapNodes)
        {
            array.append(node);
        }

        put("bootstrap-node", array);
    }

    if (maxPeers > 0)
    {
        put("max-peers", maxPeers);
    }

    if (numThreads >= 0)
    {
        put("num-threads", numThreads);
    }

    if (!agentString.isEmpty())
    {
        put("agent-string", agentString);
    }

    put("repo-kind", toString(repoKind));

    if (storageQuota > 0)
    {
        put("storage-quota", storageQuota);
    }

    if (!blockTtl.isEmpty())
    {
        put("block-ttl", blockTtl);
    }

    if (!blockMaintenanceInterval.isEmpty())
    {
        put("block-mi", blockMaintenanceInterval);
    }

    if (blockMaintenanceNumberOfBlocks > 0)
    {
        put("block-mn", blockMaintenanceNumberOfBlocks);
    }

    if (blockRetries > 0)
    {
        put("block-retries", blockRetries);
    }

    if (cacheSize > 0)
    {
        put("cache-size", cacheSize);
    }

    if (!logFile.isEmpty())
    {
        put("log-file", logFile);
    }

    return o;
}


QString StorageConfig::toJsonString(QJsonDocument::JsonFormat fmt) const
{
    return QString::fromUtf8(
        QJsonDocument(toJson()).toJson(fmt)
    );
}
