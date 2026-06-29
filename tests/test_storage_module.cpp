// Integration tests for StorageModuleImpl - uses the REAL libstorage library.
// No mocking. These tests start an actual storage node, upload/download real data,
// and verify end-to-end behavior.
//
// Requires libstorage to be available in ../lib at build time.
// Skipped automatically when libstorage is not found.

#include <logos_test.h>
#include "storage_module_plugin.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// base64 decode — mirrors the base64Encode helper in storage_module_plugin.cpp.
// Chunk data arriving in storageDownloadProgress events is base64-encoded so
// that arbitrary binary bytes can be safely embedded in a JSON string.
// ---------------------------------------------------------------------------
static std::string base64Decode(const std::string& in) {
    static const int kDecTable[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    std::string out;
    out.reserve((in.size() / 4) * 3);
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (kDecTable[c] == -1) break;
        val = (val << 6) + kDecTable[c];
        bits += 6;
        if (bits >= 0) {
            out += static_cast<char>((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return out;
}

static const int DEFAULT_TIMEOUT_MS = 3000;
static const int START_TIMEOUT_MS   = 5000;
static const std::string LOG_FILENAME = "storage.log";

// ---------------------------------------------------------------------------
// EventWaiter - replaces QEventLoop + storageResponse signal.
// Collects events emitted via the typed `logos_events:` methods on
// StorageModuleImpl. The Qt-free test forwarders in
// `tests/storage_events_test.cpp` route each event through
// logos_test::recordEvent(name, payload); here we install a ScopedEventSink
// that fans them into the same (name, data) pair the existing assertions read.
// ---------------------------------------------------------------------------

// Events are identified by the typed `logos_events:` method, exactly as the
// plugin emits them in storage_module_plugin.cpp (e.g.
// `&StorageModuleImpl::storageUploadDone`). The Qt-free forwarders in
// storage_events_test.cpp record each one under its method name, so this is
// the single place that maps the typed identity back to that name.
using StorageEvent = void (StorageModuleImpl::*)(const std::string&);

static const char* eventName(StorageEvent e) {
    if (e == &StorageModuleImpl::storageStart)                return "storageStart";
    if (e == &StorageModuleImpl::storageStop)                 return "storageStop";
    if (e == &StorageModuleImpl::storageConnect)              return "storageConnect";
    if (e == &StorageModuleImpl::storageUploadProgress)       return "storageUploadProgress";
    if (e == &StorageModuleImpl::storageUploadDone)           return "storageUploadDone";
    if (e == &StorageModuleImpl::storageDownloadProgress)     return "storageDownloadProgress";
    if (e == &StorageModuleImpl::storageDownloadDone)         return "storageDownloadDone";
    return "";
}

struct EventWaiter {
    std::mutex mtx;
    std::condition_variable cv;
    std::string lastEventName;
    std::string lastEventData;
    bool received = false;
    std::unique_ptr<logos_test::ScopedEventSink> sink;

    // Install — must be called once per impl instance. Replaces any previous
    // sink installed by an earlier EventWaiter / collectDownloadChunks call.
    // Destroy the old sink *before* creating the new one: ScopedEventSink is
    // RAII over a single global slot, so constructing the replacement first
    // and tearing the old one down after would leave the old destructor
    // clearing the slot we just registered — silently dropping every event of
    // the next node (the cause of restart timeouts).
    void install(StorageModuleImpl* /*impl*/) {
        sink.reset();
        sink = std::make_unique<logos_test::ScopedEventSink>(
            [this](const std::string& name, const std::string& data) {
                std::unique_lock<std::mutex> lock(mtx);
                lastEventName = name;
                lastEventData = data;
                received = true;
                cv.notify_all();
            });
    }

    // Reset before waiting for the next event.
    void reset() {
        std::unique_lock<std::mutex> lock(mtx);
        received = false;
        lastEventName.clear();
        lastEventData.clear();
    }

    // Wait for the given typed event within timeoutMs.
    bool waitFor(StorageEvent event, int timeoutMs) {
        const std::string name = eventName(event);
        std::unique_lock<std::mutex> lock(mtx);
        bool ok = cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                              [&] { return received && lastEventName == name; });
        return ok;
    }

    // Non-blocking: returns the last event data string (must be called under lock or after wait).
    std::string data() {
        std::unique_lock<std::mutex> lock(mtx);
        return lastEventData;
    }
};

// ---------------------------------------------------------------------------
// Shared impl instance - restarted before each test.
// ---------------------------------------------------------------------------

static StorageModuleImpl* g_impl = nullptr;
static fs::path g_dataDir;
static EventWaiter g_waiter;

static void ensureRestarted(const json& extraConfig = json::object()) {
    if (g_impl) {
        g_impl->stop();
        g_waiter.reset();
        g_waiter.waitFor(&StorageModuleImpl::storageStop, DEFAULT_TIMEOUT_MS);
        g_impl->destroy();
        delete g_impl;
        g_impl = nullptr;
        g_dataDir.clear();
    }

    g_dataDir = fs::temp_directory_path() /
                ("logos-storage-integration-test-" +
                 std::to_string(
                     std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(g_dataDir);

    std::string logFile = (g_dataDir / LOG_FILENAME).string();

    g_impl = new StorageModuleImpl();
    g_waiter.install(g_impl);

    json cfg = {
        {"data-dir", g_dataDir.string()},
        {"log-level", "DEBUG"},
        {"nat", "none"},
        {"log-file", logFile},
    };
    cfg.update(extraConfig);
    std::string config = cfg.dump();

    if (!g_impl->init(config)) {
        throw LogosTestFailure("Failed to init storage impl.");
    }

    g_waiter.reset();
    if (!g_impl->start()) {
        throw LogosTestFailure("Failed to start storage impl.");
    }

    if (!g_waiter.waitFor(&StorageModuleImpl::storageStart, START_TIMEOUT_MS)) {
        throw LogosTestFailure("Storage node did not start within timeout.");
    }
}

// ---------------------------------------------------------------------------
// Helper: write a file and upload it, returning the CID.
// ---------------------------------------------------------------------------

static std::string uploadContent(const std::string& content,
                                  const std::string& filename) {
    fs::path filePath = g_dataDir / filename;
    std::ofstream f(filePath, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    f.close();

    g_waiter.reset();
    StdLogosResult sr = g_impl->uploadUrl(filePath.string(), 65536);
    if (!sr.success) return {};

    if (!g_waiter.waitFor(&StorageModuleImpl::storageUploadDone, DEFAULT_TIMEOUT_MS)) return {};

    // Extract CID from JSON payload: {"success":true,"sessionId":"...","cid":"..."}
    std::string d = g_waiter.data();
    auto cidPos = d.find("\"cid\":\"");
    if (cidPos == std::string::npos) return {};
    cidPos += 7;
    auto cidEnd = d.find('"', cidPos);
    if (cidEnd == std::string::npos) return {};
    return d.substr(cidPos, cidEnd - cidPos);
}

// ---------------------------------------------------------------------------
// Helper: collect download chunks until storageDownloadDone.
// ---------------------------------------------------------------------------

static std::string collectDownloadChunks(int timeoutMs) {
    std::string collected;
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool success = false;

    // Replace the global EventWaiter sink for this call only; restored at
    // the end via g_waiter.install(g_impl).
    logos_test::ScopedEventSink localSink(
        [&](const std::string& name, const std::string& data) {
            if (name == "storageDownloadProgress") {
                // Extract chunk field from JSON payload.
                // The chunk is base64-encoded; decode it before accumulating.
                auto pos = data.find("\"chunk\":\"");
                if (pos != std::string::npos) {
                    pos += 9;
                    auto end = data.find('"', pos);
                    if (end != std::string::npos) {
                        collected += base64Decode(data.substr(pos, end - pos));
                    }
                }
            } else if (name == "storageDownloadDone") {
                success = data.find("\"success\":true") != std::string::npos;
                std::unique_lock<std::mutex> lock(m);
                done = true;
                cv.notify_all();
            }
        });

    std::unique_lock<std::mutex> lock(m);
    cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                [&] { return done; });

    // Restore normal waiter.
    g_waiter.install(g_impl);

    return success ? collected : std::string();
}

// integration_version

LOGOS_TEST(init_multiple_times) {
    std::string g_dataDir = fs::temp_directory_path() /
                ("logos-storage-integration-test-" +
                 std::to_string(
                     std::chrono::steady_clock::now().time_since_epoch().count()));

    g_impl = new StorageModuleImpl();
    g_waiter.install(g_impl);

    json cfg = {
        {"data-dir", g_dataDir},
        {"log-level", "DEBUG"},
        {"nat", "none"},
    };

    std::string config = cfg.dump();

    if (!g_impl->init(config)) {
        throw LogosTestFailure("Failed to init storage impl.");
    }

    // It will not re-initialize if already initialized.
    // So the init function should return false to indicate that
    // no init was done but the call itself should not fail.
    if (g_impl->init(config)) {
        throw LogosTestFailure("Failed to init storage impl.");
    }

    // The call to destroy should succeed.
    StdLogosResult result = g_impl->destroy();
    if (!result.success) {
        throw LogosTestFailure("Failed to destroy storage impl.");
    }

    delete g_impl;
    g_impl = nullptr;
}

// integration_version

LOGOS_TEST(integration_version) {
    ensureRestarted();
    StdLogosResult r = g_impl->version();
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_FALSE(r.value.get<std::string>().empty());
}

// integration_dataDir

LOGOS_TEST(integration_dataDir) {
    ensureRestarted();
    StdLogosResult r = g_impl->dataDir();
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), g_dataDir.string());
}

// integration_peerId

LOGOS_TEST(integration_peerId) {
    ensureRestarted();
    StdLogosResult r = g_impl->peerId();
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_FALSE(r.value.get<std::string>().empty());
}

// integration_debug

LOGOS_TEST(integration_debug) {
    ensureRestarted();
    StdLogosResult r = g_impl->debug();
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.is_object());
    LOGOS_ASSERT_FALSE(r.value.empty());
    LOGOS_ASSERT_TRUE(r.value.contains("id"));
    LOGOS_ASSERT_TRUE(r.value.contains("addrs"));
    LOGOS_ASSERT_TRUE(r.value.contains("announceAddresses"));
    LOGOS_ASSERT_TRUE(r.value.contains("table"));
}

// integration_collectMetrics

LOGOS_TEST(integration_collectMetrics) {
    ensureRestarted();
    LogosMap r = g_impl->collectMetrics();
    LOGOS_ASSERT_TRUE(r.is_object());
    LOGOS_ASSERT_TRUE(r.contains("metrics"));
    LOGOS_ASSERT_TRUE(r["metrics"].is_array());
    if (!r["metrics"].empty()) {
        const auto& metric = r["metrics"][0];
        LOGOS_ASSERT_TRUE(metric.is_object());
        LOGOS_ASSERT_TRUE(metric.contains("name"));
        LOGOS_ASSERT_TRUE(metric.contains("type"));
        LOGOS_ASSERT_TRUE(metric.contains("help"));
        LOGOS_ASSERT_TRUE(metric.contains("value"));
        LOGOS_ASSERT_TRUE(metric.contains("labels"));
        LOGOS_ASSERT_TRUE(metric["name"].is_string());
        LOGOS_ASSERT_TRUE(metric["type"].is_string());
        LOGOS_ASSERT_TRUE(metric["help"].is_string());
        LOGOS_ASSERT_TRUE(metric["value"].is_number());
        LOGOS_ASSERT_TRUE(metric["labels"].is_object());
    }
}

// integration_spr

LOGOS_TEST(integration_spr) {
    ensureRestarted();
    StdLogosResult r = g_impl->spr();
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_FALSE(r.value.get<std::string>().empty());
}

// integration_uploadFile

LOGOS_TEST(integration_uploadFile) {
    ensureRestarted();
    std::string cid = uploadContent("Hello, Logos Storage!", "test_upload.txt");
    LOGOS_ASSERT_FALSE(cid.empty());
}

// integration_uploadWorkflowManual

LOGOS_TEST(integration_uploadWorkflowManual) {
    ensureRestarted();

    fs::path filePath = g_dataDir / "test_manual_upload.txt";
    std::string content = "Hello, Logos Storage! Manual upload test.";
    std::ofstream f(filePath, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    f.close();

    StdLogosResult initR = g_impl->uploadInit(filePath.string(), 65536);
    LOGOS_ASSERT_TRUE(initR.success);
    std::string sid = initR.value.get<std::string>();
    LOGOS_ASSERT_FALSE(sid.empty());

    LOGOS_ASSERT_TRUE(g_impl->uploadChunk(sid, content).success);

    StdLogosResult finalizeR = g_impl->uploadFinalize(sid);
    LOGOS_ASSERT_TRUE(finalizeR.success);
    LOGOS_ASSERT_FALSE(finalizeR.value.get<std::string>().empty());
}

// integration_downloadFile

LOGOS_TEST(integration_downloadFile) {
    ensureRestarted();

    std::string content = "Hello, Logos Download Test!";
    std::string cid = uploadContent(content, "test_download.txt");
    LOGOS_ASSERT_FALSE(cid.empty());

    fs::path downloadPath = g_dataDir / "test_download_result.txt";

    g_waiter.reset();
    StdLogosResult dlR = g_impl->downloadToUrl(cid, downloadPath.string(), false, 65536);
    LOGOS_ASSERT_TRUE(dlR.success);

    LOGOS_ASSERT_TRUE(g_waiter.waitFor(&StorageModuleImpl::storageDownloadDone, DEFAULT_TIMEOUT_MS));

    std::ifstream in(downloadPath, std::ios::binary);
    std::string downloaded((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    LOGOS_ASSERT_EQ(downloaded, content);
}

// integration_downloadChunks

LOGOS_TEST(integration_downloadChunks) {
    ensureRestarted();

    std::string content = "Hello, Logos Chunks Download Test!";
    std::string cid = uploadContent(content, "test_chunks_src.txt");
    LOGOS_ASSERT_FALSE(cid.empty());

    // collectDownloadChunks installs a local ScopedEventSink for the duration
    // of the call and restores the global EventWaiter sink before returning.
    StdLogosResult dlR = g_impl->downloadChunks(cid, false, 65536);
    LOGOS_ASSERT_TRUE(dlR.success);

    std::string downloaded = collectDownloadChunks(DEFAULT_TIMEOUT_MS);
    LOGOS_ASSERT_FALSE(downloaded.empty());
    LOGOS_ASSERT_EQ(downloaded, content);
}

// integration_exists

LOGOS_TEST(integration_exists) {
    ensureRestarted();

    std::string cid = uploadContent("Hello, Logos Exists Test!", "test_exists_src.txt");
    LOGOS_ASSERT_FALSE(cid.empty());

    StdLogosResult r = g_impl->exists(cid);
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.get<bool>());
}

// integration_fetch

LOGOS_TEST(integration_fetch) {
    ensureRestarted();

    std::string cid = uploadContent("Hello, Logos Fetch Test!", "test_fetch_src.txt");
    LOGOS_ASSERT_FALSE(cid.empty());

    LOGOS_ASSERT_TRUE(g_impl->fetch(cid).success);
}

// integration_remove

LOGOS_TEST(integration_remove) {
    ensureRestarted();

    std::string cid = uploadContent("Hello, Logos Remove Test!", "test_remove_src.txt");
    LOGOS_ASSERT_FALSE(cid.empty());

    StdLogosResult e1 = g_impl->exists(cid);
    LOGOS_ASSERT_TRUE(e1.success);
    LOGOS_ASSERT_TRUE(e1.value.get<bool>());

    LOGOS_ASSERT_TRUE(g_impl->remove(cid).success);

    StdLogosResult e2 = g_impl->exists(cid);
    LOGOS_ASSERT_TRUE(e2.success);
    LOGOS_ASSERT_FALSE(e2.value.get<bool>());
}

// integration_space

LOGOS_TEST(integration_space) {
    ensureRestarted();

    StdLogosResult r = g_impl->space();
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.is_object());
    LOGOS_ASSERT_FALSE(r.value.empty());
    LOGOS_ASSERT_TRUE(r.value.contains("totalBlocks"));
    LOGOS_ASSERT_TRUE(r.value.contains("quotaMaxBytes"));
    LOGOS_ASSERT_TRUE(r.value.contains("quotaUsedBytes"));
    LOGOS_ASSERT_TRUE(r.value.contains("quotaReservedBytes"));
}

// integration_manifests

LOGOS_TEST(integration_manifests) {
    ensureRestarted();

    std::string content = "Hello, Logos Manifests Test!";
    std::string cid = uploadContent(content, "test_manifests_src.txt");
    LOGOS_ASSERT_FALSE(cid.empty());

    StdLogosResult lr = g_impl->manifests();
    LOGOS_ASSERT_TRUE(lr.success);
    LOGOS_ASSERT_TRUE(lr.value.is_array());
    LOGOS_ASSERT_FALSE(lr.value.empty());

    bool found = false;
    for (const auto& entry : lr.value) {
        if (!entry.contains("cid") || entry["cid"].get<std::string>() != cid) continue;
        found = true;
        LOGOS_ASSERT_TRUE(entry.contains("treeCid"));
        LOGOS_ASSERT_FALSE(entry["treeCid"].get<std::string>().empty());
        break;
    }
    LOGOS_ASSERT_TRUE(found);
}

// integration_downloadManifest

LOGOS_TEST(integration_downloadManifest) {
    ensureRestarted();

    std::string content = "Hello, Logos DownloadManifest Test!";
    std::string cid = uploadContent(content, "test_download_manifest_src.txt");
    LOGOS_ASSERT_FALSE(cid.empty());

    StdLogosResult mr = g_impl->downloadManifest(cid);
    LOGOS_ASSERT_TRUE(mr.success);
    LOGOS_ASSERT_TRUE(mr.value.is_object());
    LOGOS_ASSERT_FALSE(mr.value.empty());
    LOGOS_ASSERT_TRUE(mr.value.contains("treeCid"));
    LOGOS_ASSERT_TRUE(mr.value.contains("datasetSize"));
}

// integration_updateLogLevel

LOGOS_TEST(integration_updateLogLevel) {
    ensureRestarted();

    LOGOS_ASSERT_TRUE(g_impl->updateLogLevel("TRACE").success);

    // Upload a file to generate TRACE logs.
    uploadContent("Hello, Logos Log Level Test!", "test_loglevel_src.txt");

    fs::path logFile = g_dataDir / LOG_FILENAME;
    std::ifstream in(logFile);
    std::string logContent((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

    LOGOS_ASSERT_FALSE(logContent.empty());
    LOGOS_ASSERT_TRUE(logContent.find("TRC") != std::string::npos);
}

// integration_togglePrivateQueries_withoutMix
//
// This test verifies that private queries cannot be enabled without Mix being
// configured.

LOGOS_TEST(integration_togglePrivateQueries_withoutMix) {
    ensureRestarted();

    StdLogosResult sprRes = g_impl->spr();
    LOGOS_ASSERT_TRUE(sprRes.success);
    std::string proxySpr = sprRes.value.get<std::string>();

    ensureRestarted({
        {"mix-enabled", false},
        {"dht-mix-proxy", json::array({proxySpr})},
    });

    // Enabling fails: Mix is not configured.
    StdLogosResult on = g_impl->togglePrivateQueries(true);
    LOGOS_ASSERT_FALSE(on.success);

    // Disabling is always allowed and reports the previous state (off).
    StdLogosResult off = g_impl->togglePrivateQueries(false);
    LOGOS_ASSERT_TRUE(off.success);
    LOGOS_ASSERT_FALSE(off.value.get<bool>());
}

// integration_togglePrivateQueries_withMixEnabled
//
// This test verifies that private queries can be enabled and disabled when Mix
// is configured.

LOGOS_TEST(integration_togglePrivateQueries_withMixEnabled) {
    ensureRestarted();

    StdLogosResult sprRes = g_impl->spr();
    LOGOS_ASSERT_TRUE(sprRes.success);
    std::string proxySpr = sprRes.value.get<std::string>();

    ensureRestarted({
        {"mix-enabled", true},
        {"dht-mix-proxy", json::array({proxySpr})},
    });

    // Mix configured: private queries default on, so the first disable reports
    // the previous state as on.
    StdLogosResult off = g_impl->togglePrivateQueries(false);
    LOGOS_ASSERT_TRUE(off.success);
    LOGOS_ASSERT_TRUE(off.value.get<bool>());

    // Re-enabling now succeeds (Mix is configured) and reports previous = off.
    StdLogosResult on = g_impl->togglePrivateQueries(true);
    LOGOS_ASSERT_TRUE(on.success);
    LOGOS_ASSERT_FALSE(on.value.get<bool>());
}
