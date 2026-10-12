// Unit tests for StorageModuleImpl.
// All libstorage C functions are mocked at link time via mock_libstorage.cpp.
// Async mocks invoke the callback immediately so the condvar is signalled
// before waitSync's first check.

#include <logos_test.h>
#include "storage_module_plugin.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>
#include <map>
#include <fstream>
#include <filesystem>
extern std::map<std::string, nlohmann::json> storageMockArgs;
using json = nlohmann::json;
namespace fs = std::filesystem;

extern "C" {
void mock_fire_upload_progress(const char* sessionId, int64_t storedBytes);
void mock_fire_download_chunk(const char* cid, const uint8_t* data, size_t len);
void mock_fire_download_done(const char* cid, const char* error);
void mock_hold_transfer_replies(bool hold);
void mock_release_transfer_reply(void);
void mock_release_create(int rc);
}

// Helper: create an impl with a mocked, successfully initialized storage context.
static StorageModuleImpl* createInitializedImpl(LogosTestContext& t) {
    t.mockCFunction("storage_ctx_create").returns(RET_OK);
    auto* impl = new StorageModuleImpl();
    LOGOS_ASSERT_TRUE(impl->init("{\"data-dir\":\"/tmp/test\"}"));
    return impl;
}

// init

LOGOS_TEST(init_succeeds_when_storage_ctx_create_succeeds) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create").returns(RET_OK);

    StorageModuleImpl impl;
    LOGOS_ASSERT_TRUE(impl.init("{\"data-dir\":\"/tmp/test\"}"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_create"));
}

LOGOS_TEST(init_registers_the_transfer_listeners) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_add_on_upload_progress_listener"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_add_on_download_chunk_listener"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_add_on_download_done_listener"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(init_fails_when_storage_ctx_create_fails) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create").returns(RET_ERR);

    StorageModuleImpl impl;
    LOGOS_ASSERT_FALSE(impl.init("{\"data-dir\":\"/tmp/test\"}"));
}

LOGOS_TEST(isRunning_is_false_before_the_node_starts) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_FALSE(impl->isRunning());

    impl->destroy();
    delete impl;
}

LOGOS_TEST(start_on_a_running_node_is_accepted_and_reported) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    impl->start();
    LOGOS_ASSERT_TRUE(impl->start());
    LOGOS_ASSERT_TRUE(impl->isRunning());
    LOGOS_ASSERT_EQ(events.all("storageStart").size(), static_cast<size_t>(2));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(start_while_starting_is_refused) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);
    t.mockCFunction("storage_ctx_start_pending").returns(1);

    impl->start();

    LOGOS_ASSERT_FALSE(impl->start());
    delete impl;
}

LOGOS_TEST(isRunning_follows_the_start_and_the_stop) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    impl->start();
    LOGOS_ASSERT_TRUE(impl->isRunning());

    impl->stop();
    LOGOS_ASSERT_FALSE(impl->isRunning());

    impl->destroy();
    delete impl;
}

// version

LOGOS_TEST(libstorageVersion_returns_mocked_string) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_version").returns("1.2.3-test");
    StdLogosResult vr = impl->libstorageVersion();

    LOGOS_ASSERT_TRUE(vr.success);
    LOGOS_ASSERT_EQ(vr.value.get<std::string>(), std::string("1.2.3-test"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_version"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(libstorageVersion_needs_no_context) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_version").returns("9.9.9-test");

    StorageModuleImpl impl;
    StdLogosResult vr = impl.libstorageVersion();

    LOGOS_ASSERT_TRUE(vr.success);
    LOGOS_ASSERT_EQ(vr.value.get<std::string>(), std::string("9.9.9-test"));
}

// The value is injected at build time from metadata.json; the fallback
// "0.0.0-dev" only survives when the build failed to pass the define, so
// asserting against it verifies the injection pipeline actually ran.
LOGOS_TEST(moduleVersion_is_injected_from_build) {
    StorageModuleImpl impl;
    std::string v = impl.moduleVersion();

    LOGOS_ASSERT_FALSE(v.empty());
    LOGOS_ASSERT(v != std::string("0.0.0-dev"));
}

// start / stop

LOGOS_TEST(start_returns_true_after_init) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->start());
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_start"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(start_returns_false_without_init) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;
    LOGOS_ASSERT_FALSE(impl.start());
}

LOGOS_TEST(stop_fails_without_init) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;
    LOGOS_ASSERT_FALSE(impl.stop().success);
}

LOGOS_TEST(stop_succeeds_after_init) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->stop().success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_stop"));

    impl->destroy();
    delete impl;
}

// destroy

LOGOS_TEST(destroy_without_init_returns_error) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;
    // destroy() must not touch the C API when there is no context
    LOGOS_ASSERT_FALSE(impl.destroy().success);
    LOGOS_ASSERT(!t.cFunctionCalled("storage_ctx_destroy"));
}

LOGOS_TEST(destroy_succeeds_after_init) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->destroy().success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_destroy"));

    delete impl;
}

LOGOS_TEST(failed_destroy_drops_the_freed_context) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_destroy").returns(RET_ERR);
    LOGOS_ASSERT_FALSE(impl->destroy().success);
    LOGOS_ASSERT_FALSE(impl->destroy().success);
    LOGOS_ASSERT_EQ(t.cFunctionCallCount("storage_ctx_destroy"), 1);

    delete impl;
}

// aboutToUnload

LOGOS_TEST(aboutToUnload_stops_a_running_node) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    impl->start();
    impl->_logosCoreAboutToUnload_();

    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_stop"));
    LOGOS_ASSERT_FALSE(impl->isRunning());

    delete impl;
}

LOGOS_TEST(aboutToUnload_destroys_the_context) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    impl->_logosCoreAboutToUnload_();

    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_destroy"));
    // The context is gone, so there is nothing left for destroy() to take.
    LOGOS_ASSERT_FALSE(impl->destroy().success);

    delete impl;
}

LOGOS_TEST(aboutToUnload_without_a_context_touches_nothing) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    impl._logosCoreAboutToUnload_();

    LOGOS_ASSERT(!t.cFunctionCalled("storage_ctx_destroy"));
}

// peerId / spr / dataDir

LOGOS_TEST(peerId_returns_mocked_value) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_peer_id").returns("QmTestPeerId123");
    StdLogosResult r = impl->peerId();

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), std::string("QmTestPeerId123"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(spr_returns_mocked_value) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_spr").returns("spr:ABCD1234");
    StdLogosResult r = impl->spr();

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), std::string("spr:ABCD1234"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(dataDir_returns_mocked_value) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_repo").returns("/tmp/test-data");
    StdLogosResult r = impl->dataDir();

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), std::string("/tmp/test-data"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(network_returns_mocked_value) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_network").returns("logos.test");
    StdLogosResult r = impl->network();

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), std::string("logos.test"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(peerId_returns_failure_without_init) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;
    LOGOS_ASSERT_FALSE(impl.peerId().success);
}

// debug

LOGOS_TEST(debug_returns_parsed_map) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_debug")
        .returns(R"({"id":"QmNode","addrs":[],"spr":"spr:AAAA","table":{}})");
    StdLogosResult r = impl->debug();

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.is_object());
    LOGOS_ASSERT_FALSE(r.value.empty());
    LOGOS_ASSERT_TRUE(r.value.contains("id"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(debug_fails_on_invalid_json) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_debug").returns("not json");
    StdLogosResult r = impl->debug();
    LOGOS_ASSERT_FALSE(r.success);

    impl->destroy();
    delete impl;
}

// collectMetrics

LOGOS_TEST(collectMetrics_returns_parsed_metrics) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_get_metrics")
        .returns(R"({"metrics":[{"name":"test_metric","type":"gauge","help":"Test metric","value":1.0,"labels":{}}]})");
    LogosMap r = impl->collectMetrics();

    LOGOS_ASSERT_TRUE(r.is_object());
    LOGOS_ASSERT_TRUE(r.contains("metrics"));
    LOGOS_ASSERT_TRUE(r["metrics"].is_array());
    LOGOS_ASSERT_EQ(static_cast<int>(r["metrics"].size()), 1);
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_get_metrics"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(collectMetrics_returns_empty_metrics_on_libstorage_error) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_get_metrics").returns(RET_ERR);
    LogosMap r = impl->collectMetrics();

    LOGOS_ASSERT_TRUE(r.is_object());
    LOGOS_ASSERT_TRUE(r.contains("metrics"));
    LOGOS_ASSERT_TRUE(r["metrics"].is_array());
    LOGOS_ASSERT_TRUE(r["metrics"].empty());

    impl->destroy();
    delete impl;
}

LOGOS_TEST(collectMetrics_returns_empty_metrics_on_invalid_json) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_get_metrics").returns("not json");
    LogosMap r = impl->collectMetrics();

    LOGOS_ASSERT_TRUE(r.is_object());
    LOGOS_ASSERT_TRUE(r.contains("metrics"));
    LOGOS_ASSERT_TRUE(r["metrics"].is_array());
    LOGOS_ASSERT_TRUE(r["metrics"].empty());

    impl->destroy();
    delete impl;
}

LOGOS_TEST(collectMetrics_returns_empty_metrics_when_payload_is_array) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_get_metrics").returns(R"([])");
    LogosMap r = impl->collectMetrics();

    LOGOS_ASSERT_TRUE(r.is_object());
    LOGOS_ASSERT_TRUE(r.contains("metrics"));
    LOGOS_ASSERT_TRUE(r["metrics"].is_array());
    LOGOS_ASSERT_TRUE(r["metrics"].empty());

    impl->destroy();
    delete impl;
}

LOGOS_TEST(collectMetrics_returns_empty_metrics_when_metrics_missing) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_get_metrics").returns(R"({"other":[]})");
    LogosMap r = impl->collectMetrics();

    LOGOS_ASSERT_TRUE(r.is_object());
    LOGOS_ASSERT_TRUE(r.contains("metrics"));
    LOGOS_ASSERT_TRUE(r["metrics"].is_array());
    LOGOS_ASSERT_TRUE(r["metrics"].empty());

    impl->destroy();
    delete impl;
}

LOGOS_TEST(collectMetrics_returns_empty_metrics_when_metrics_is_not_array) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_get_metrics").returns(R"({"metrics":{}})");
    LogosMap r = impl->collectMetrics();

    LOGOS_ASSERT_TRUE(r.is_object());
    LOGOS_ASSERT_TRUE(r.contains("metrics"));
    LOGOS_ASSERT_TRUE(r["metrics"].is_array());
    LOGOS_ASSERT_TRUE(r["metrics"].empty());

    impl->destroy();
    delete impl;
}

// updateLogLevel

LOGOS_TEST(updateLogLevel_returns_true_on_success) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->updateLogLevel("DEBUG").success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_log_level"));

    impl->destroy();
    delete impl;
}

// exists

LOGOS_TEST(exists_returns_true_when_cid_found) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_exists").returns("true");
    StdLogosResult r = impl->exists("QmSomeCid");
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.get<bool>());

    impl->destroy();
    delete impl;
}

LOGOS_TEST(exists_returns_false_when_cid_not_found) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_exists").returns("false");
    StdLogosResult r = impl->exists("QmMissingCid");
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_FALSE(r.value.get<bool>());

    impl->destroy();
    delete impl;
}

// getAdvertise

LOGOS_TEST(getAdvertise_returns_false) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_get_advertise").returns("false");
    StdLogosResult r = impl->getAdvertise("QmSomeCid");
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_FALSE(r.value.get<bool>());
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_get_advertise"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(getAdvertise_returns_true) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_get_advertise").returns("true");
    StdLogosResult r = impl->getAdvertise("QmSomeCid");
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.get<bool>());

    impl->destroy();
    delete impl;
}

// fetch / remove

LOGOS_TEST(fetch_calls_storage_fetch) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->fetch("QmSomeCid", false, true).success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_fetch"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(remove_dispatches_and_emits_event) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->remove("QmSomeCid").success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_delete"));
    LOGOS_ASSERT_TRUE(events.has("storageRemoveDone"));

    impl->destroy();
    delete impl;
}

// space

LOGOS_TEST(space_returns_parsed_map) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_space")
        .returns(R"({"totalBlocks":100,"quotaMaxBytes":1000,"quotaUsedBytes":50,"quotaReservedBytes":10})");
    StdLogosResult r = impl->space();

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.is_object());
    LOGOS_ASSERT_FALSE(r.value.empty());
    LOGOS_ASSERT_TRUE(r.value.contains("totalBlocks"));
    LOGOS_ASSERT_TRUE(r.value.contains("quotaMaxBytes"));

    impl->destroy();
    delete impl;
}

// manifests

LOGOS_TEST(manifests_returns_parsed_list) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_list")
        .returns(R"([{"cid":"QmABC","manifest":{"treeCid":"QmTree","datasetSize":1024,"blockSize":64,"filename":"test.txt","mimetype":"text/plain"}}])");
    StdLogosResult r = impl->manifests();

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.is_array());
    LOGOS_ASSERT_EQ(static_cast<int>(r.value.size()), 1);

    impl->destroy();
    delete impl;
}

// downloadManifest

LOGOS_TEST(downloadManifest_dispatches_and_emits_event) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_download_manifest")
        .returns(R"({"treeCid":"QmTree","datasetSize":2048,"blockSize":64,"filename":"data.bin","mimetype":"application/octet-stream"})");
    StdLogosResult r = impl->downloadManifest("QmSomeCid", false, true);

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(events.has("storageDownloadManifestDone"));

    impl->destroy();
    delete impl;
}

// uploadInit / uploadFinalize / uploadCancel

LOGOS_TEST(uploadInit_returns_session_id) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_upload_init").returns("session-abc-123");
    StdLogosResult r = impl->uploadInit("test.txt", 65536, true);

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), std::string("session-abc-123"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(uploadFinalize_returns_cid) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_upload_finalize").returns("QmFinalCid");
    StdLogosResult r = impl->uploadFinalize("session-abc-123");

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), std::string("QmFinalCid"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_upload_finalize"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(uploadCancel_returns_true) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->uploadCancel("session-abc-123").success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_upload_cancel"));

    impl->destroy();
    delete impl;
}

// uploadUrl input validation

LOGOS_TEST(uploadUrl_fails_with_nonexistent_file) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    StdLogosResult r = impl->uploadUrl("/nonexistent/path/file.txt", 65536, true);
    LOGOS_ASSERT_FALSE(r.success);

    impl->destroy();
    delete impl;
}

LOGOS_TEST(uploadUrl_fails_with_zero_chunk_size) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    StdLogosResult r = impl->uploadUrl("/tmp/test.txt", 0, true);
    LOGOS_ASSERT_FALSE(r.success);

    impl->destroy();
    delete impl;
}

// downloadCancel

LOGOS_TEST(downloadCancel_returns_true) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->downloadCancel("QmSomeCid").success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_download_cancel"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(downloadToUrl_progress_reports_total_bytes) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_download_manifest").returns(R"({"datasetSize":2048})");

    mock_hold_transfer_replies(true);
    StdLogosResult result = impl->downloadToUrl("QmSomeCid", "/tmp/logos-storage-test.bin",
                                          false, 65536, false, true);
    LOGOS_ASSERT_TRUE(result.success);

    const uint8_t block[64] = {};
    mock_fire_download_chunk("QmSomeCid", block, sizeof(block));
    mock_release_transfer_reply();
    mock_hold_transfer_replies(false);

    auto progress = events.all("storageDownloadProgress");
    LOGOS_ASSERT_EQ(progress.size(), static_cast<size_t>(1));

    // The total expected is the manifest datasetSize.
    json payload = json::parse(progress[0].data);
    LOGOS_ASSERT_EQ(payload["total"].get<int>(), 2048);

    impl->destroy();
    delete impl;
}

LOGOS_TEST(downloadChunks_cancels_and_emits_done_when_stream_dispatch_fails) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_download_stream").returns(RET_ERR);
    LOGOS_ASSERT_FALSE(impl->downloadChunks("QmSomeCid", false, 65536, false, true).success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_download_cancel"));

    auto done = events.all("storageDownloadDone");
    LOGOS_ASSERT_EQ(done.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_FALSE(json::parse(done[0].data)["success"].get<bool>());

    impl->destroy();
    delete impl;
}

LOGOS_TEST(download_chunk_after_reply_is_delivered_before_done) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    mock_hold_transfer_replies(true);
    LOGOS_ASSERT_TRUE(impl->downloadChunks("QmSomeCid", false, 65536, false, true).success);
    mock_release_transfer_reply();
    mock_hold_transfer_replies(false);
    LOGOS_ASSERT_FALSE(events.has("storageDownloadDone"));

    const uint8_t payload[] = {'a', 'b', 'c'};
    mock_fire_download_chunk("QmSomeCid", payload, sizeof(payload));
    LOGOS_ASSERT_TRUE(events.has("storageDownloadProgress"));

    mock_fire_download_done("QmSomeCid", "");
    auto done = events.all("storageDownloadDone");
    LOGOS_ASSERT_EQ(done.size(), static_cast<size_t>(1));
    json result = json::parse(done[0].data);
    LOGOS_ASSERT_TRUE(result["success"].get<bool>());
    LOGOS_ASSERT_EQ(result["sessionId"].get<std::string>(), std::string("QmSomeCid"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(download_done_reports_stream_error) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    mock_hold_transfer_replies(true);
    LOGOS_ASSERT_TRUE(impl->downloadChunks("QmSomeCid", false, 65536, false, true).success);
    mock_hold_transfer_replies(false);

    mock_fire_download_done("QmSomeCid", "stream failed");
    auto done = events.all("storageDownloadDone");
    LOGOS_ASSERT_EQ(done.size(), static_cast<size_t>(1));
    json result = json::parse(done[0].data);
    LOGOS_ASSERT_FALSE(result["success"].get<bool>());
    LOGOS_ASSERT_EQ(result["error"].get<std::string>(), std::string("stream failed"));

    impl->destroy();
    delete impl;
}

// connect

LOGOS_TEST(connect_fails_without_init) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;
    LOGOS_ASSERT_FALSE(impl.connect("QmPeer", {"addr1"}).success);
}

LOGOS_TEST(connect_succeeds_after_init) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->connect("QmPeer", {"/ip4/127.0.0.1/tcp/1234"}).success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_connect"));

    impl->destroy();
    delete impl;
}

// Event wiring — typed `logos_events:` methods are forwarded to
// logos_test::recordEvent by tests/storage_events_test.cpp, so tests observe
// them via EventCapture. Construct the capture before the impl so it outlives
// any background thread the impl's destructor joins.

LOGOS_TEST(start_emits_storageStart_event) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->start());
    LOGOS_ASSERT_TRUE(events.has("storageStart"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(stop_emits_storageStop_event) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->stop().success);
    LOGOS_ASSERT_TRUE(events.has("storageStop"));

    impl->destroy();
    delete impl;
}

// Points HOME at an empty directory, so the persisted config is the one the test writes.
struct TempHome {
    fs::path dir = fs::temp_directory_path() /
                   ("logos-storage-home-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string previous;
    bool hadPrevious = false;

    TempHome() {
        const char* home = std::getenv("HOME");
        hadPrevious = home != nullptr;
        previous = hadPrevious ? home : "";
        fs::create_directories(dir);
        setenv("HOME", dir.c_str(), 1);
    }

    ~TempHome() {
        if (hadPrevious) {
            setenv("HOME", previous.c_str(), 1);
        } else {
            unsetenv("HOME");
        }
        fs::remove_all(dir);
    }

    void writeConfig(const std::string& content) {
        fs::create_directories(dir / ".logos_storage");
        std::ofstream(dir / ".logos_storage" / "config.json") << content;
    }

    json persistedConfig() {
        std::ifstream file(dir / ".logos_storage" / "config.json");
        return json::parse(file);
    }
};

// Saves `saved` as the persisted config, then loads it back.
static json migrated(StorageModuleImpl& impl, const json& saved) {
    TempHome home;
    home.writeConfig(saved.dump());

    const StdLogosResult r = impl.loadConfigOrDefault();

    if (!r.success || !r.value.is_string()) {
        // An empty object on failure, so the assertion that follows fails on the field
        // it was actually checking.
        return json::object();
    }

    return json::parse(r.value.get<std::string>());
}

LOGOS_TEST(loadConfigOrDefault_fills_an_absent_data_dir) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json::object());

    LOGOS_ASSERT_TRUE(out.contains("data-dir"));
    LOGOS_ASSERT_TRUE(out["data-dir"].get<std::string>().find(".logos_storage") != std::string::npos);
}

LOGOS_TEST(loadConfigOrDefault_keeps_the_present_data_dir) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"data-dir", "/somewhere/else"}});

    LOGOS_ASSERT_EQ(out["data-dir"].get<std::string>(), std::string("/somewhere/else"));
}

LOGOS_TEST(loadConfigOrDefault_stamps_the_schema_version) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    LOGOS_ASSERT_EQ(migrated(impl, json::object())["config-version"].get<int>(), 3);
}

LOGOS_TEST(loadConfigOrDefault_leaves_a_current_config_alone) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"config-version", 3}, {"nat", "extip:1.2.3.4"}});

    LOGOS_ASSERT_EQ(out["nat"].get<std::string>(), std::string("extip:1.2.3.4"));
}

LOGOS_TEST(loadConfigOrDefault_drops_a_legacy_bootstrap_list) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"bootstrap-node", json::array({"spr:CiUIAhIhA-VlcoiRm02KyIzrcTP-ljFpzTljfBRRKTIvhMIwqBqWEgIDARpJCicAJQgCEiED5WVyiJGbTYrIjOtxM_6WMWnNOWN8FFEpMi-EwjCoGpYQs8n8wQYaCwoJBHTKubmRAnU6GgsKCQR0yrm5kQJ1OipHMEUCIQDwUNsfReB4ty7JFS5WVQ6n1fcko89qVAOfQEHixa03rgIgan2-uFNDT-r4s9TOkLe9YBkCbsRWYCHGGVJ25rLj0QE"})}});

    LOGOS_ASSERT_FALSE(out.contains("bootstrap-node"));
}

LOGOS_TEST(loadConfigOrDefault_drops_an_empty_bootstrap_list) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"bootstrap-node", json::array()}});

    LOGOS_ASSERT_FALSE(out.contains("bootstrap-node"));
}

LOGOS_TEST(loadConfigOrDefault_keeps_the_present_bootstrap_config_not_legacy) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"bootstrap-node", json::array({"spr:MINE"})}});

    LOGOS_ASSERT_TRUE(out.contains("bootstrap-node"));
    LOGOS_ASSERT_EQ(out["bootstrap-node"][0].get<std::string>(), std::string("spr:MINE"));
}

LOGOS_TEST(loadConfigOrDefault_drops_only_the_legacy_bootstrap_node) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl,
        json{{"bootstrap-node", json::array({"spr:CiUIAhIhA-VlcoiRm02KyIzrcTP-ljFpzTljfBRRKTIvhMIwqBqWEgIDARpJCicAJQgCEiED5WVyiJGbTYrIjOtxM_6WMWnNOWN8FFEpMi-EwjCoGpYQs8n8wQYaCwoJBHTKubmRAnU6GgsKCQR0yrm5kQJ1OipHMEUCIQDwUNsfReB4ty7JFS5WVQ6n1fcko89qVAOfQEHixa03rgIgan2-uFNDT-r4s9TOkLe9YBkCbsRWYCHGGVJ25rLj0QE", "spr:MINE"})}});

    LOGOS_ASSERT_EQ(out["bootstrap-node"].size(), 1u);
    LOGOS_ASSERT_EQ(out["bootstrap-node"][0].get<std::string>(), std::string("spr:MINE"));
}

LOGOS_TEST(loadConfigOrDefault_turns_mix_on_when_the_mix_config_is_absent) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    LOGOS_ASSERT_TRUE(migrated(impl, json::object())["mix-enabled"].get<bool>());
}

LOGOS_TEST(loadConfigOrDefault_leaves_mix_off_on_a_private_network) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"bootstrap-node", json::array({"spr:MINE"})}});

    LOGOS_ASSERT_FALSE(out["mix-enabled"].get<bool>());
}

LOGOS_TEST(loadConfigOrDefault_leaves_mix_value_when_the_mix_config_is_present) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    LOGOS_ASSERT_FALSE(migrated(impl, json{{"mix-enabled", false}})["mix-enabled"].get<bool>());
}

LOGOS_TEST(loadConfigOrDefault_drops_an_invalid_nat_value) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"nat", "any"}});

    LOGOS_ASSERT_FALSE(out.contains("nat"));
}

LOGOS_TEST(loadConfigOrDefault_keeps_a_valid_nat) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    LOGOS_ASSERT_EQ(migrated(impl, json{{"nat", "auto"}})["nat"].get<std::string>(),
                    std::string("auto"));
    LOGOS_ASSERT_EQ(migrated(impl, json{{"nat", "extip:1.2.3.4"}})["nat"].get<std::string>(),
                    std::string("extip:1.2.3.4"));
}

LOGOS_TEST(loadConfigOrDefault_drops_the_disc_port) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"config-version", 2}, {"disc-port", 8090}});

    LOGOS_ASSERT_FALSE(out.contains("disc-port"));
}

LOGOS_TEST(loadConfigOrDefault_fills_the_mix_configuration_of_the_network) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"network", "logos.dev"}});

    LOGOS_ASSERT_FALSE(out["dht-mix-proxy"].empty());
    LOGOS_ASSERT_FALSE(out["mix-pool-json"].get<std::string>().empty());
}

LOGOS_TEST(loadConfigOrDefault_replaces_stale_mix_configuration) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"dht-mix-proxy", json::array({"stale"})}});

    LOGOS_ASSERT_TRUE(out["dht-mix-proxy"] != json::array({"stale"}));
}

LOGOS_TEST(loadConfigOrDefault_leaves_mix_alone_when_mix_is_off) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"config-version", 3}, {"mix-enabled", false}});

    LOGOS_ASSERT_FALSE(out.contains("dht-mix-proxy"));
}

LOGOS_TEST(loadConfigOrDefault_leaves_mix_alone_on_a_private_network) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"bootstrap-node", json::array({"spr:MINE"})},
                                          {"dht-mix-proxy", json::array({"theirs"})}});

    LOGOS_ASSERT_EQ(out["dht-mix-proxy"], json::array({"theirs"}));
}

LOGOS_TEST(loadConfigOrDefault_leaves_mix_alone_on_an_unknown_network) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"network", "logos.nowhere"}});

    LOGOS_ASSERT_FALSE(out.contains("dht-mix-proxy"));
}

LOGOS_TEST(loadConfigOrDefault_produces_same_output_on_repeated_calls) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json once = migrated(impl, json{{"nat", "any"}});
    const json twice = migrated(impl, once);

    LOGOS_ASSERT_TRUE(once == twice);
}

LOGOS_TEST(loadConfigOrDefault_leaves_mix_off_without_bootstrap_nodes) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"no-bootstrap-node", true}});

    LOGOS_ASSERT_FALSE(out["mix-enabled"].get<bool>());
}

LOGOS_TEST(loadConfigOrDefault_leaves_mix_alone_without_bootstrap_nodes) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;

    const json out = migrated(impl, json{{"config-version", 3},
                                          {"mix-enabled", true},
                                          {"no-bootstrap-node", true},
                                          {"dht-mix-proxy", json::array({"theirs"})}});

    LOGOS_ASSERT_EQ(out["dht-mix-proxy"], json::array({"theirs"}));
}

LOGOS_TEST(loadConfigOrDefault_reports_a_mistyped_config_version) {
    auto t = LogosTestContext("storage_module");
    TempHome home;
    home.writeConfig(json{{"config-version", "2"}}.dump());
    StorageModuleImpl impl;

    StdLogosResult r = impl.loadConfigOrDefault();

    LOGOS_ASSERT_FALSE(r.success);
}

LOGOS_TEST(loadConfigOrDefault_uses_the_defaults_when_there_is_no_persisted_config) {
    auto t = LogosTestContext("storage_module");
    TempHome home;
    StorageModuleImpl impl;

    const StdLogosResult r = impl.loadConfigOrDefault();
    const json out = json::parse(r.value.get<std::string>());

    LOGOS_ASSERT_EQ(out["data-dir"].get<std::string>(),
                    (home.dir / ".logos_storage" / "data").string());
}

LOGOS_TEST(loadConfigOrDefault_reports_an_invalid_persisted_config) {
    auto t = LogosTestContext("storage_module");
    TempHome home;
    home.writeConfig("{ not json");
    StorageModuleImpl impl;

    StdLogosResult r = impl.loadConfigOrDefault();

    LOGOS_ASSERT_FALSE(r.success);
}

LOGOS_TEST(init_does_not_migrate_the_config) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create").returns(RET_OK);
    TempHome home;
    StorageModuleImpl impl;

    LOGOS_ASSERT_TRUE(impl.init(json{{"data-dir", "/tmp/test"}, {"nat", "any"}}.dump()));

    LOGOS_ASSERT_EQ(home.persistedConfig()["nat"].get<std::string>(), std::string("any"));
}

LOGOS_TEST(init_leaves_an_absent_mix_enabled_absent) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create").returns(RET_OK);
    TempHome home;
    StorageModuleImpl impl;

    LOGOS_ASSERT_TRUE(impl.init(json{{"data-dir", "/tmp/test"}}.dump()));

    LOGOS_ASSERT_FALSE(home.persistedConfig().contains("mix-enabled"));
}

LOGOS_TEST(init_fills_the_mix_configuration_of_the_network) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create").returns(RET_OK);
    TempHome home;
    StorageModuleImpl impl;

    LOGOS_ASSERT_TRUE(impl.init(
        json{{"data-dir", "/tmp/test"}, {"mix-enabled", true}, {"network", "logos.dev"}}.dump()));

    LOGOS_ASSERT_FALSE(home.persistedConfig()["dht-mix-proxy"].empty());
}

LOGOS_TEST(init_fills_an_absent_data_dir) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create").returns(RET_OK);
    TempHome home;
    StorageModuleImpl impl;

    LOGOS_ASSERT_TRUE(impl.init(json::object().dump()));

    LOGOS_ASSERT_EQ(home.persistedConfig()["data-dir"].get<std::string>(),
                    (home.dir / ".logos_storage" / "data").string());
}

LOGOS_TEST(init_config_is_not_migrated_again_on_load) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create").returns(RET_OK);
    TempHome home;
    StorageModuleImpl impl;

    LOGOS_ASSERT_TRUE(impl.init(json{{"data-dir", "/tmp/test"}}.dump()));

    const json loaded = json::parse(impl.loadConfigOrDefault().value.get<std::string>());
    LOGOS_ASSERT_FALSE(loaded.contains("mix-enabled"));
}

LOGOS_TEST(init_fails_on_a_mistyped_mix_enabled) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create").returns(RET_OK);
    TempHome home;
    StorageModuleImpl impl;

    LOGOS_ASSERT_FALSE(impl.init(json{{"data-dir", "/tmp/test"}, {"mix-enabled", "yes"}}.dump()));
}

LOGOS_TEST(init_persists_the_config_it_was_given) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create").returns(RET_OK);
    TempHome home;
    const std::string config = json{{"config-version", 3}, {"data-dir", "/tmp/test"}}.dump();
    StorageModuleImpl impl;

    LOGOS_ASSERT_TRUE(impl.init(config));

    std::ifstream file(home.dir / ".logos_storage" / "config.json");
    std::string persisted((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    LOGOS_ASSERT_EQ(persisted, config);
}

LOGOS_TEST(init_does_not_persist_a_config_that_is_not_json) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create").returns(RET_OK);
    TempHome home;
    StorageModuleImpl impl;

    LOGOS_ASSERT_TRUE(impl.init(""));

    LOGOS_ASSERT_FALSE(fs::exists(home.dir / ".logos_storage" / "config.json"));
}

LOGOS_TEST(loadConfigOrDefault_reports_an_unreadable_storage_home) {
    auto t = LogosTestContext("storage_module");
    TempHome home;
    home.writeConfig(json{{"config-version", 3}}.dump());
    const fs::path storageHome = home.dir / ".logos_storage";
    fs::permissions(storageHome, fs::perms::none);
    StorageModuleImpl impl;

    StdLogosResult r = impl.loadConfigOrDefault();

    fs::permissions(storageHome, fs::perms::owner_all);
    LOGOS_ASSERT_FALSE(r.success);
}

LOGOS_TEST(downloads_forward_privacy_and_advertisement_flags) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);
    t.mockCFunction("storage_ctx_download_manifest").returns(R"({"datasetSize":2048})");
    // Tries all combinations to rule out hardcoded values. Not much more we can do here anyway.
    for (bool isPrivate : {false, true}) {
        for (bool advertise : {false, true}) {
            LOGOS_ASSERT_TRUE(impl->downloadManifest("cid", isPrivate, advertise).success);
            LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_download_manifest"]["isPrivate"].get<bool>(), isPrivate);
            LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_download_manifest"]["advertise"].get<bool>(), advertise);
            storageMockArgs.clear();
            LOGOS_ASSERT_TRUE(impl->fetch("cid", isPrivate, advertise).success);
            LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_fetch"]["isPrivate"].get<bool>(), isPrivate);
            LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_fetch"]["advertise"].get<bool>(), advertise);
            for (bool local : {false, true}) {
                storageMockArgs.clear();
                LOGOS_ASSERT_TRUE(impl->downloadToUrl("cid", "/tmp/download.bin", local, 65536, isPrivate, advertise).success);
                LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_download_manifest"]["isPrivate"].get<bool>(), isPrivate);
                LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_download_manifest"]["advertise"].get<bool>(), advertise);
                LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_download_init"]["isPrivate"].get<bool>(), isPrivate);
                LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_download_init"]["advertise"].get<bool>(), advertise);
                LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_download_init"]["local"].get<bool>(), local);
                
                storageMockArgs.clear();
                LOGOS_ASSERT_TRUE(impl->downloadChunks("cid", local, 65536, isPrivate, advertise).success);
                LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_download_init"]["isPrivate"].get<bool>(), isPrivate);
                LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_download_init"]["advertise"].get<bool>(), advertise);
                LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_download_init"]["local"].get<bool>(), local);
            }
        }
    }
    impl->destroy();
    delete impl;
}

LOGOS_TEST(uploads_fetch_and_setAdvertise_forward_advertisement_flags) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);
    auto path = std::filesystem::temp_directory_path() / "storage-advertise-test.txt";
    std::ofstream(path) << "advertisement test";
    t.mockCFunction("storage_ctx_upload_init").returns("session");
    for (bool advertise : {false, true}) {
        LOGOS_ASSERT_TRUE(impl->uploadInit("file.txt", 65536, advertise).success);
        LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_upload_init"]["advertise"].get<bool>(), advertise);
        storageMockArgs.clear();
        LOGOS_ASSERT_TRUE(impl->uploadUrl(path.string(), 65536, advertise).success);
        LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_upload_init"]["advertise"].get<bool>(), advertise);
        LOGOS_ASSERT_TRUE(impl->fetch("cid", false, advertise).success);
        LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_fetch"]["advertise"].get<bool>(), advertise);
        LOGOS_ASSERT_TRUE(impl->setAdvertise("cid", advertise).success);
        LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_set_advertise"]["advertise"].get<bool>(), advertise);
        LOGOS_ASSERT_EQ(storageMockArgs["storage_ctx_set_advertise"]["cid"].get<std::string>(), std::string("cid"));
    }
    std::filesystem::remove(path);
    impl->destroy();
    delete impl;
}

LOGOS_TEST(upload_progress_listener_emits_storageUploadProgress_event) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    std::string path =
        (std::filesystem::temp_directory_path() / "storage_module_upload_progress").string();
    {
        std::ofstream f(path, std::ios::binary);
        f << std::string(1000, 'x');
    }

    // The terminal reply would forget the transfer before the event arrives.
    mock_hold_transfer_replies(true);
    t.mockCFunction("storage_ctx_upload_init").returns("0");
    LOGOS_ASSERT_TRUE(impl->uploadUrl(path, 65536, true).success);

    mock_fire_upload_progress("0", 500);
    LOGOS_ASSERT_TRUE(events.has("storageUploadProgress"));

    mock_release_transfer_reply();
    LOGOS_ASSERT_TRUE(events.has("storageUploadDone"));
    mock_hold_transfer_replies(false);

    std::filesystem::remove(path);
    impl->destroy();
    delete impl;
}

LOGOS_TEST(advertisement_operations_report_errors) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl uninitialized;
    LOGOS_ASSERT_FALSE(uninitialized.getAdvertise("cid").success);
    LOGOS_ASSERT_FALSE(uninitialized.setAdvertise("cid", true).success);
    auto* impl = createInitializedImpl(t);
    t.mockCFunction("storage_ctx_get_advertise").returns(RET_ERR);
    t.mockCFunction("storage_ctx_set_advertise").returns(RET_ERR);
    LOGOS_ASSERT_FALSE(impl->getAdvertise("cid").success);
    LOGOS_ASSERT_FALSE(impl->setAdvertise("cid", false).success);
    impl->destroy();
    delete impl;
}

LOGOS_TEST(init_does_not_persist_a_rejected_config) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create").returns(RET_ERR);
    TempHome home;
    StorageModuleImpl impl;

    LOGOS_ASSERT_FALSE(impl.init("{\"data-dir\":\"/tmp/test\"}"));

    LOGOS_ASSERT_FALSE(fs::exists(home.dir / ".logos_storage" / "config.json"));
}

LOGOS_TEST(download_chunk_listener_emits_base64_chunk) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    mock_hold_transfer_replies(true);
    LOGOS_ASSERT_TRUE(impl->downloadChunks("QmSomeCid", false, 65536, false, true).success);

    const uint8_t payload[] = {'a', 'b', 'c'};
    mock_fire_download_chunk("QmSomeCid", payload, sizeof(payload));
    LOGOS_ASSERT_TRUE(events.has("storageDownloadProgress"));

    mock_release_transfer_reply();
    mock_hold_transfer_replies(false);

    impl->destroy();
    delete impl;
}

static std::string writeTempFile(const std::string& name, size_t size) {
    std::string path = (std::filesystem::temp_directory_path() / name).string();
    std::ofstream f(path, std::ios::binary);
    f << std::string(size, 'x');
    return path;
}

LOGOS_TEST(download_done_for_an_untracked_cid_is_reported) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    mock_fire_download_done("QmOtherCid", "");

    auto done = events.all("storageDownloadDone");
    LOGOS_ASSERT_EQ(done.size(), static_cast<size_t>(1));
    json result = json::parse(done[0].data);
    LOGOS_ASSERT_TRUE(result["success"].get<bool>());
    LOGOS_ASSERT_EQ(result["sessionId"].get<std::string>(), std::string("QmOtherCid"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(download_chunk_after_done_is_dropped) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    mock_hold_transfer_replies(true);
    LOGOS_ASSERT_TRUE(impl->downloadChunks("QmSomeCid", false, 65536, false, true).success);
    mock_hold_transfer_replies(false);
    mock_fire_download_done("QmSomeCid", "");

    const uint8_t payload[] = {'a'};
    mock_fire_download_chunk("QmSomeCid", payload, sizeof(payload));
    LOGOS_ASSERT_FALSE(events.has("storageDownloadProgress"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(upload_progress_after_the_reply_is_dropped) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);
    std::string path = writeTempFile("storage_module_upload_after_reply", 1000);

    mock_hold_transfer_replies(true);
    t.mockCFunction("storage_ctx_upload_init").returns("0");
    LOGOS_ASSERT_TRUE(impl->uploadUrl(path, 65536, true).success);
    mock_release_transfer_reply();
    mock_hold_transfer_replies(false);
    LOGOS_ASSERT_TRUE(events.has("storageUploadDone"));

    mock_fire_upload_progress("0", 500);
    LOGOS_ASSERT_FALSE(events.has("storageUploadProgress"));

    std::filesystem::remove(path);
    impl->destroy();
    delete impl;
}

LOGOS_TEST(uploadUrl_cancels_and_reports_failure_when_dispatch_fails) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);
    std::string path = writeTempFile("storage_module_upload_dispatch_fails", 1000);

    t.mockCFunction("storage_ctx_upload_init").returns("0");
    t.mockCFunction("storage_ctx_upload_file").returns(RET_ERR);
    LOGOS_ASSERT_FALSE(impl->uploadUrl(path, 65536, true).success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_ctx_upload_cancel"));

    auto done = events.all("storageUploadDone");
    LOGOS_ASSERT_EQ(done.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_FALSE(json::parse(done[0].data)["success"].get<bool>());

    mock_fire_upload_progress("0", 500);
    LOGOS_ASSERT_FALSE(events.has("storageUploadProgress"));

    std::filesystem::remove(path);
    impl->destroy();
    delete impl;
}

LOGOS_TEST(upload_progress_of_an_empty_file_is_not_throttled) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);
    std::string path = writeTempFile("storage_module_upload_empty", 0);

    mock_hold_transfer_replies(true);
    t.mockCFunction("storage_ctx_upload_init").returns("0");
    LOGOS_ASSERT_TRUE(impl->uploadUrl(path, 65536, true).success);

    for (int i = 0; i < 3; i++) mock_fire_upload_progress("0", 5);
    mock_release_transfer_reply();
    mock_hold_transfer_replies(false);

    auto progress = events.all("storageUploadProgress");
    LOGOS_ASSERT_EQ(progress.size(), static_cast<size_t>(3));
    json last = json::parse(progress[2].data);
    LOGOS_ASSERT_EQ(last["bytes"].get<int>(), 5);
    LOGOS_ASSERT_EQ(last["total"].get<int>(), 0);

    std::filesystem::remove(path);
    impl->destroy();
    delete impl;
}

LOGOS_TEST(upload_progress_emits_once_per_percent_with_the_bytes_since_the_last_event) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);
    std::string path = writeTempFile("storage_module_upload_throttle", 1000);

    mock_hold_transfer_replies(true);
    t.mockCFunction("storage_ctx_upload_init").returns("0");
    LOGOS_ASSERT_TRUE(impl->uploadUrl(path, 65536, true).success);

    mock_fire_upload_progress("0", 1);
    mock_fire_upload_progress("0", 2);
    mock_fire_upload_progress("0", 20);
    mock_release_transfer_reply();
    mock_hold_transfer_replies(false);

    auto progress = events.all("storageUploadProgress");
    LOGOS_ASSERT_EQ(progress.size(), static_cast<size_t>(2));
    LOGOS_ASSERT_EQ(json::parse(progress[0].data)["bytes"].get<int>(), 1);
    LOGOS_ASSERT_EQ(json::parse(progress[1].data)["bytes"].get<int>(), 22);

    std::filesystem::remove(path);
    impl->destroy();
    delete impl;
}

LOGOS_TEST(downloadToUrl_fails_on_an_unusable_manifest_datasetSize) {
    const char* manifests[] = {
        R"({"datasetSize":-5})",
        R"({"datasetSize":0})",
        R"({"datasetSize":"abc"})",
        R"({"datasetSize":"-7"})",
        R"({"datasetSize":"99999999999999999999"})",
        R"({"datasetSize":1.5})",
        R"({"datasetSize":null})",
        R"({})",
        "not json",
    };
    for (const char* manifest : manifests) {
        auto t = LogosTestContext("storage_module");
        auto* impl = createInitializedImpl(t);

        t.mockCFunction("storage_ctx_download_manifest").returns(manifest);
        LOGOS_ASSERT_FALSE(impl->downloadToUrl("QmSomeCid", "/tmp/logos-storage-test.bin",
                                               false, 65536, false, true).success);
        LOGOS_ASSERT_FALSE(t.cFunctionCalled("storage_ctx_download_init"));

        impl->destroy();
        delete impl;
    }
}

LOGOS_TEST(downloadToUrl_reads_a_datasetSize_written_as_a_string) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_download_manifest").returns(R"({"datasetSize":"2048"})");
    mock_hold_transfer_replies(true);
    LOGOS_ASSERT_TRUE(impl->downloadToUrl("QmSomeCid", "/tmp/logos-storage-test.bin",
                                          false, 65536, false, true).success);

    const uint8_t block[64] = {};
    mock_fire_download_chunk("QmSomeCid", block, sizeof(block));
    mock_release_transfer_reply();
    mock_hold_transfer_replies(false);

    auto progress = events.all("storageDownloadProgress");
    LOGOS_ASSERT_EQ(progress.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(json::parse(progress[0].data)["total"].get<int>(), 2048);

    impl->destroy();
    delete impl;
}

LOGOS_TEST(init_destroys_the_node_when_a_listener_fails_to_register) {
    const char* listeners[] = {
        "storage_ctx_add_on_upload_progress_listener",
        "storage_ctx_add_on_download_chunk_listener",
        "storage_ctx_add_on_download_done_listener",
    };
    for (const char* listener : listeners) {
        auto t = LogosTestContext("storage_module");
        t.mockCFunction(std::string(listener) + "_fails").returns(1);
        TempHome home;
        StorageModuleImpl impl;

        LOGOS_ASSERT_FALSE(impl.init("{\"data-dir\":\"/tmp/test\"}"));
        LOGOS_ASSERT_EQ(t.cFunctionCallCount("storage_ctx_destroy"), 1);
        LOGOS_ASSERT_FALSE(impl.start());
        LOGOS_ASSERT_FALSE(fs::exists(home.dir / ".logos_storage" / "config.json"));
    }
}

LOGOS_TEST(aboutToUnload_keeps_the_context_when_stop_fails) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    impl->start();
    t.mockCFunction("storage_ctx_stop_reply_err").returns(1);
    impl->_logosCoreAboutToUnload_();

    LOGOS_ASSERT_FALSE(t.cFunctionCalled("storage_ctx_destroy"));

    delete impl;
}

LOGOS_TEST(aboutToUnload_keeps_the_context_when_the_node_stays_busy) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_ctx_start_pending").returns(1);
    LOGOS_ASSERT_TRUE(impl->start());
    impl->_logosCoreAboutToUnload_();

    LOGOS_ASSERT_FALSE(t.cFunctionCalled("storage_ctx_destroy"));
    LOGOS_ASSERT_FALSE(t.cFunctionCalled("storage_ctx_stop"));

    delete impl;
}

LOGOS_TEST(init_destroys_a_node_created_after_the_timeout) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_ctx_create_pending").returns(1);
    StorageModuleImpl impl;

    LOGOS_ASSERT_FALSE(impl.init("{\"data-dir\":\"/tmp/test\"}"));
    LOGOS_ASSERT_FALSE(t.cFunctionCalled("storage_ctx_destroy"));

    mock_release_create(RET_OK);
    LOGOS_ASSERT_EQ(t.cFunctionCallCount("storage_ctx_destroy"), 1);
    LOGOS_ASSERT_FALSE(impl.destroy().success);
}

