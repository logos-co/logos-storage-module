// Unit tests for StorageModulePlugin.
// All libstorage C functions are mocked at link time via mock_libstorage.cpp.
// Async mocks invoke the callback immediately so Qt can process them.
//
// Note: We don't call initLegacy() because StorageModulePlugin's destructor
// deletes logosAPI, which would conflict with LogosTestContext's ownership.
// The plugin's callback handlers gracefully skip LogosAPIClient when
// logosAPI is null, and still emit the storageResponse signal that drives
// all sync/async flows.

#include <logos_test.h>
#include "storage_module_plugin.h"

// Helper: create a plugin with a mocked, successfully initialized storage context.
static StorageModulePlugin* createInitializedPlugin(LogosTestContext& t) {
    t.mockCFunction("storage_new").returns(1);
    auto* plugin = new StorageModulePlugin();
    plugin->init(R"({"data-dir": "/tmp/test"})");
    return plugin;
}

// ── init ────────────────────────────────────────────────────────────────────

LOGOS_TEST(init_succeeds_when_storage_new_returns_context) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_new").returns(1);

    StorageModulePlugin plugin;
    LOGOS_ASSERT_TRUE(plugin.init(R"({"data-dir": "/tmp/test"})"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_new"));
}

LOGOS_TEST(init_fails_when_storage_new_returns_null) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_new").returns(0);

    StorageModulePlugin plugin;
    LOGOS_ASSERT_FALSE(plugin.init(R"({"data-dir": "/tmp/test"})"));
}

// ── version ─────────────────────────────────────────────────────────────────

LOGOS_TEST(version_returns_mocked_string) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_version").returns("1.2.3-test");
    LogosResult result = plugin->version();

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_EQ(result.getString().toStdString(), std::string("1.2.3-test"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_version"));

    plugin->destroy();
    delete plugin;
}

// ── start / stop ────────────────────────────────────────────────────────────

LOGOS_TEST(start_returns_true_after_init) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LOGOS_ASSERT_TRUE(plugin->start());
    LOGOS_ASSERT(t.cFunctionCalled("storage_start"));

    plugin->destroy();
    delete plugin;
}

LOGOS_TEST(start_returns_false_without_init) {
    auto t = LogosTestContext("storage_module");
    StorageModulePlugin plugin;
    LOGOS_ASSERT_FALSE(plugin.start());
}

LOGOS_TEST(stop_fails_without_init) {
    auto t = LogosTestContext("storage_module");
    StorageModulePlugin plugin;
    LogosResult result = plugin.stop();
    LOGOS_ASSERT_FALSE(result.success);
}

LOGOS_TEST(stop_succeeds_after_init) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->stop();
    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_stop"));

    plugin->destroy();
    delete plugin;
}

// ── destroy ─────────────────────────────────────────────────────────────────

LOGOS_TEST(destroy_without_init_still_calls_storage_destroy) {
    auto t = LogosTestContext("storage_module");
    StorageModulePlugin plugin;
    LogosResult result = plugin.destroy();
    // destroy() proceeds even if close fails; storage_destroy(nullptr) returns OK
    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_destroy"));
}

LOGOS_TEST(destroy_succeeds_after_init) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->destroy();
    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_close"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_destroy"));

    delete plugin;
}

// ── peerId / spr / dataDir ──────────────────────────────────────────────────

LOGOS_TEST(peerId_returns_mocked_value) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_peer_id").returns("QmTestPeerId123");
    LogosResult result = plugin->peerId();

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_EQ(result.getString().toStdString(), std::string("QmTestPeerId123"));

    plugin->destroy();
    delete plugin;
}

LOGOS_TEST(spr_returns_mocked_value) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_spr").returns("spr:ABCD1234");
    LogosResult result = plugin->spr();

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_EQ(result.getString().toStdString(), std::string("spr:ABCD1234"));

    plugin->destroy();
    delete plugin;
}

LOGOS_TEST(dataDir_returns_mocked_value) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_repo").returns("/tmp/test-data");
    LogosResult result = plugin->dataDir();

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_EQ(result.getString().toStdString(), std::string("/tmp/test-data"));

    plugin->destroy();
    delete plugin;
}

LOGOS_TEST(peerId_fails_without_init) {
    auto t = LogosTestContext("storage_module");
    StorageModulePlugin plugin;
    LogosResult result = plugin.peerId();
    LOGOS_ASSERT_FALSE(result.success);
}

// ── debug ───────────────────────────────────────────────────────────────────

LOGOS_TEST(debug_returns_parsed_json) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_debug")
        .returns(R"({"id":"QmNode","addrs":[],"announceAddresses":[],"table":{}})");
    LogosResult result = plugin->debug();

    LOGOS_ASSERT_TRUE(result.success);
    QVariantMap map = result.getMap();
    LOGOS_ASSERT_EQ(map["id"].toString().toStdString(), std::string("QmNode"));

    plugin->destroy();
    delete plugin;
}

// ── updateLogLevel ──────────────────────────────────────────────────────────

LOGOS_TEST(updateLogLevel_calls_storage_log_level) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->updateLogLevel("DEBUG");
    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_log_level"));

    plugin->destroy();
    delete plugin;
}

// ── exists ──────────────────────────────────────────────────────────────────

LOGOS_TEST(exists_returns_true_when_cid_found) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_exists").returns("true");
    LogosResult result = plugin->exists("QmSomeCid");

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_TRUE(result.getBool());

    plugin->destroy();
    delete plugin;
}

LOGOS_TEST(exists_returns_false_when_cid_not_found) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_exists").returns("false");
    LogosResult result = plugin->exists("QmMissingCid");

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_FALSE(result.getBool());

    plugin->destroy();
    delete plugin;
}

// ── fetch / remove ──────────────────────────────────────────────────────────

LOGOS_TEST(fetch_calls_storage_fetch) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->fetch("QmSomeCid");
    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_fetch"));

    plugin->destroy();
    delete plugin;
}

LOGOS_TEST(remove_calls_storage_delete) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->remove("QmSomeCid");
    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_delete"));

    plugin->destroy();
    delete plugin;
}

// ── space ───────────────────────────────────────────────────────────────────

LOGOS_TEST(space_returns_parsed_json) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_space")
        .returns(R"({"totalBlocks":100,"quotaMaxBytes":1000,"quotaUsedBytes":50,"quotaReservedBytes":10})");
    LogosResult result = plugin->space();

    LOGOS_ASSERT_TRUE(result.success);
    QVariantMap map = result.getMap();
    LOGOS_ASSERT_EQ(map["totalBlocks"].toInt(), 100);
    LOGOS_ASSERT_EQ(map["quotaMaxBytes"].toInt(), 1000);
    LOGOS_ASSERT_EQ(map["quotaUsedBytes"].toInt(), 50);

    plugin->destroy();
    delete plugin;
}

// ── manifests ───────────────────────────────────────────────────────────────

LOGOS_TEST(manifests_returns_parsed_list) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_list")
        .returns(R"([{"cid":"QmABC","manifest":{"treeCid":"QmTree","datasetSize":1024,"blockSize":64,"filename":"test.txt","mimetype":"text/plain"}}])");
    LogosResult result = plugin->manifests();

    LOGOS_ASSERT_TRUE(result.success);
    QVariantList list = result.getList();
    LOGOS_ASSERT_EQ(static_cast<int>(list.size()), 1);
    QVariantMap m = list[0].toMap();
    LOGOS_ASSERT_EQ(m["cid"].toString().toStdString(), std::string("QmABC"));
    LOGOS_ASSERT_EQ(m["treeCid"].toString().toStdString(), std::string("QmTree"));
    LOGOS_ASSERT_EQ(m["datasetSize"].toInt(), 1024);

    plugin->destroy();
    delete plugin;
}

// ── downloadManifest ────────────────────────────────────────────────────────

LOGOS_TEST(downloadManifest_returns_parsed_json) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_download_manifest")
        .returns(R"({"treeCid":"QmTree","datasetSize":2048,"blockSize":64,"filename":"data.bin","mimetype":"application/octet-stream"})");
    LogosResult result = plugin->downloadManifest("QmSomeCid");

    LOGOS_ASSERT_TRUE(result.success);
    QVariantMap map = result.getMap();
    LOGOS_ASSERT_EQ(map["treeCid"].toString().toStdString(), std::string("QmTree"));
    LOGOS_ASSERT_EQ(map["datasetSize"].toInt(), 2048);

    plugin->destroy();
    delete plugin;
}

// ── uploadUrl input validation ──────────────────────────────────────────────

LOGOS_TEST(uploadUrl_fails_with_invalid_url) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->uploadUrl(QUrl(""));
    LOGOS_ASSERT_FALSE(result.success);

    plugin->destroy();
    delete plugin;
}

LOGOS_TEST(uploadUrl_fails_with_non_local_url) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->uploadUrl(QUrl("https://example.com/file.txt"));
    LOGOS_ASSERT_FALSE(result.success);

    plugin->destroy();
    delete plugin;
}

LOGOS_TEST(uploadUrl_fails_with_zero_chunk_size) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->uploadUrl(QUrl::fromLocalFile("/tmp/test.txt"), 0);
    LOGOS_ASSERT_FALSE(result.success);

    plugin->destroy();
    delete plugin;
}

LOGOS_TEST(uploadUrl_fails_with_nonexistent_file) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->uploadUrl(QUrl::fromLocalFile("/nonexistent/path/file.txt"));
    LOGOS_ASSERT_FALSE(result.success);

    plugin->destroy();
    delete plugin;
}

// ── connect ─────────────────────────────────────────────────────────────────

LOGOS_TEST(connect_fails_without_init) {
    auto t = LogosTestContext("storage_module");
    StorageModulePlugin plugin;
    LogosResult result = plugin.connect("QmPeer", QStringList{"addr1"});
    LOGOS_ASSERT_FALSE(result.success);
}

LOGOS_TEST(connect_succeeds_after_init) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->connect("QmPeer", QStringList{"/ip4/127.0.0.1/tcp/1234"});
    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_connect"));

    plugin->destroy();
    delete plugin;
}

// ── upload workflow ─────────────────────────────────────────────────────────

LOGOS_TEST(uploadInit_returns_session_id) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_upload_init").returns("session-abc-123");
    LogosResult result = plugin->uploadInit("test.txt");

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_EQ(result.getString().toStdString(), std::string("session-abc-123"));

    plugin->destroy();
    delete plugin;
}

LOGOS_TEST(uploadFinalize_returns_cid) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    t.mockCFunction("storage_upload_finalize").returns("QmFinalCid");
    LogosResult result = plugin->uploadFinalize("session-abc-123");

    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT_EQ(result.getString().toStdString(), std::string("QmFinalCid"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_upload_finalize"));

    plugin->destroy();
    delete plugin;
}

LOGOS_TEST(uploadCancel_succeeds) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->uploadCancel("session-abc-123");
    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_upload_cancel"));

    plugin->destroy();
    delete plugin;
}

// ── download ────────────────────────────────────────────────────────────────

LOGOS_TEST(downloadCancel_succeeds) {
    auto t = LogosTestContext("storage_module");
    auto* plugin = createInitializedPlugin(t);

    LogosResult result = plugin->downloadCancel("QmSomeCid");
    LOGOS_ASSERT_TRUE(result.success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_download_cancel"));

    plugin->destroy();
    delete plugin;
}

// ── name / version metadata ─────────────────────────────────────────────────

LOGOS_TEST(name_returns_storage_module) {
    auto t = LogosTestContext("storage_module");
    StorageModulePlugin plugin;
    LOGOS_ASSERT_EQ(plugin.name().toStdString(), std::string("storage_module"));
}
