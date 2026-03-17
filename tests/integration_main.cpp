#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include "LogosAPI.h"
#include "LogosModules.h"

extern "C" {
void logos_core_set_plugins_dir(const char*);
void logos_core_start();
void logos_core_cleanup();
int logos_core_load_plugin(const char*);
}

// ─── Test state ──────────────────────────────────────────────────────────────

static int s_passed = 0, s_failed = 0;
static QStringList s_failures;

static void check(const QString& name, bool ok)
{
    if (ok) {
        qInfo("  PASS  %s", qPrintable(name));
        s_passed++;
    } else {
        qCritical("  FAIL  %s", qPrintable(name));
        s_failed++;
        s_failures << name;
    }
}

// Wait for a storage event, returns the event data or empty list on timeout.
static QVariantList waitForEvent(StorageModule& module,
                                 const QString& eventName,
                                 int timeoutMs = 30000)
{
    QEventLoop loop;
    QTimer timer;
    QVariantList result;
    bool received = false;

    timer.setSingleShot(true);

    module.on(eventName, [&](const QVariantList& data) {
        if (received) return; // guard against duplicate calls
        received = true;
        result = data;
        loop.quit();
    });

    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(timeoutMs);
    loop.exec();

    return result;
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    if (argc < 2) {
        qCritical("Usage: storage_module_integration_tests <modules-dir>");
        return 1;
    }

    QCoreApplication app(argc, argv);

    qInfo("=================================================================");
    qInfo(" Logos Storage Module -- Integration Tests");
    qInfo("=================================================================");
    qInfo("  modules-dir : %s", argv[1]);

    // ── Bootstrap logos_core ─────────────────────────────────────────────────

    logos_core_set_plugins_dir(argv[1]);
    logos_core_start();

    if (!logos_core_load_plugin("storage_module")) {
        qCritical("FATAL: Failed to load storage_module plugin from '%s'", argv[1]);
        logos_core_cleanup();
        return 1;
    }

    LogosAPI     api("integration_tests");
    LogosModules logos(&api);

    QTemporaryDir dataDir;
    if (!dataDir.isValid()) {
        qCritical("FATAL: Could not create temporary data directory");
        logos_core_cleanup();
        return 1;
    }

    const QString config =
        QString(R"({"data-dir": "%1", "log-level": "WARN"})").arg(dataDir.path());

    qInfo("  data-dir    : %s", qPrintable(dataDir.path()));
    qInfo("");

    // ── init ─────────────────────────────────────────────────────────────────

    qInfo("-----------------------------------------------------------------");
    qInfo(" init, start, peerId");
    qInfo("-----------------------------------------------------------------");
    qInfo("");

    check("init()", logos.storage_module.init(config));

    // ── start ─────────────────────────────────────────────────────────────────

    logos.storage_module.start();
    const QVariantList startData = waitForEvent(logos.storage_module, "storageStart", 30000);
    check("start()", !startData.isEmpty() && startData.first().toBool());

    // ── peerId ────────────────────────────────────────────────────────────────

    const LogosResult peerResult = logos.storage_module.peerId();
    check("peerId()", peerResult.success && !peerResult.getString().isEmpty());
    if (peerResult.success)
        qInfo("  peer-id     : %s", qPrintable(peerResult.getString()));

    // ── upload ────────────────────────────────────────────────────────────────

    qInfo("");
    qInfo("-----------------------------------------------------------------");
    qInfo(" upload");
    qInfo("-----------------------------------------------------------------");
    qInfo("");

    const QString testContent = "Hello, Logos Storage Integration Test!";
    const QString uploadPath  = dataDir.path() + "/upload.txt";
    {
        QFile f(uploadPath);
        f.open(QIODevice::WriteOnly);
        f.write(testContent.toUtf8());
    }

    logos.storage_module.uploadUrl(QUrl::fromLocalFile(uploadPath));
    const QVariantList uploadData = waitForEvent(logos.storage_module, "storageUploadDone", 60000);
    const bool uploadOk = !uploadData.isEmpty() && uploadData.first().toBool();
    check("uploadUrl()", uploadOk);

    // UploadDone payload: [success, sessionId, cid]
    QString cid;
    if (uploadOk && uploadData.size() >= 3)
        cid = uploadData.at(2).toString();

    check("uploadUrl() — CID non-empty", !cid.isEmpty());
    if (!cid.isEmpty())
        qInfo("  cid         : %s", qPrintable(cid));

    // ── download ──────────────────────────────────────────────────────────────

    qInfo("");
    qInfo("-----------------------------------------------------------------");
    qInfo(" download");
    qInfo("-----------------------------------------------------------------");
    qInfo("");

    if (!cid.isEmpty()) {
        const QString downloadPath = dataDir.path() + "/download.txt";

        logos.storage_module.downloadToUrl(cid, QUrl::fromLocalFile(downloadPath));
        const QVariantList dlData = waitForEvent(logos.storage_module, "storageDownloadDone", 60000);
        const bool dlOk = !dlData.isEmpty() && dlData.first().toBool();
        check("downloadToUrl()", dlOk);

        if (dlOk) {
            QFile f(downloadPath);
            f.open(QIODevice::ReadOnly);
            const QString downloaded = QString::fromUtf8(f.readAll());
            check("downloadToUrl() — content matches", downloaded == testContent);
        }
    } else {
        qInfo("  SKIP  downloadToUrl()  (no CID from upload)");
    }

    // ── stop ──────────────────────────────────────────────────────────────────

    qInfo("");
    qInfo("-----------------------------------------------------------------");
    qInfo(" stop");
    qInfo("-----------------------------------------------------------------");
    qInfo("");

    logos.storage_module.stop();
    const QVariantList stopData = waitForEvent(logos.storage_module, "storageStop", 15000);
    check("stop()", !stopData.isEmpty() && stopData.first().toBool());

    logos_core_cleanup();

    // ── Summary ───────────────────────────────────────────────────────────────

    qInfo("");
    qInfo("=================================================================");
    qInfo(" Results: %d passed, %d failed", s_passed, s_failed);
    qInfo("=================================================================");

    if (s_failed > 0) {
        qInfo("\nFailures:");
        for (const QString& f : std::as_const(s_failures))
            qInfo("  FAIL  %s", qPrintable(f));
        qInfo("");
        return 1;
    }

    qInfo("\nAll tests passed.");
    return 0;
}
