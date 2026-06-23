// Unit tests for StorageModuleImpl.
// All libstorage C functions are mocked at link time via mock_libstorage.cpp.
// Async mocks invoke the callback immediately so the condvar is signalled
// before waitSync's first check.

#include <logos_test.h>
#include "storage_module_plugin.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Helper: create an impl with a mocked, successfully initialized storage context.
static StorageModuleImpl* createInitializedImpl(LogosTestContext& t) {
    t.mockCFunction("storage_new").returns(1);
    auto* impl = new StorageModuleImpl();
    LOGOS_ASSERT_TRUE(impl->init("{\"data-dir\":\"/tmp/test\"}"));
    return impl;
}

// init

LOGOS_TEST(init_succeeds_when_storage_new_returns_context) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_new").returns(1);

    StorageModuleImpl impl;
    LOGOS_ASSERT_TRUE(impl.init("{\"data-dir\":\"/tmp/test\"}"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_new"));
}

LOGOS_TEST(init_fails_when_storage_new_returns_null) {
    auto t = LogosTestContext("storage_module");
    t.mockCFunction("storage_new").returns(0);

    StorageModuleImpl impl;
    LOGOS_ASSERT_FALSE(impl.init("{\"data-dir\":\"/tmp/test\"}"));
}

// version

LOGOS_TEST(version_returns_mocked_string) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_version").returns("1.2.3-test");
    StdLogosResult vr = impl->version();

    LOGOS_ASSERT_TRUE(vr.success);
    LOGOS_ASSERT_EQ(vr.value.get<std::string>(), std::string("1.2.3-test"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_version"));

    impl->destroy();
    delete impl;
}

// start / stop

LOGOS_TEST(start_returns_true_after_init) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->start());
    LOGOS_ASSERT(t.cFunctionCalled("storage_start"));

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
    LOGOS_ASSERT(t.cFunctionCalled("storage_stop"));

    impl->destroy();
    delete impl;
}

// destroy

LOGOS_TEST(destroy_without_init_returns_error) {
    auto t = LogosTestContext("storage_module");
    StorageModuleImpl impl;
    // destroy() must not touch the C API when there is no context
    LOGOS_ASSERT_FALSE(impl.destroy().success);
    LOGOS_ASSERT(!t.cFunctionCalled("storage_destroy"));
}

LOGOS_TEST(destroy_succeeds_after_init) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->destroy().success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_close"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_destroy"));

    delete impl;
}

// peerId / spr / dataDir

LOGOS_TEST(peerId_returns_mocked_value) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_peer_id").returns("QmTestPeerId123");
    StdLogosResult r = impl->peerId();

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), std::string("QmTestPeerId123"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(spr_returns_mocked_value) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_spr").returns("spr:ABCD1234");
    StdLogosResult r = impl->spr();

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), std::string("spr:ABCD1234"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(dataDir_returns_mocked_value) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_repo").returns("/tmp/test-data");
    StdLogosResult r = impl->dataDir();

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), std::string("/tmp/test-data"));

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

    t.mockCFunction("storage_debug")
        .returns(R"({"id":"QmNode","addrs":[],"announceAddresses":[],"table":{}})");
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

    t.mockCFunction("storage_debug").returns("not json");
    StdLogosResult r = impl->debug();
    LOGOS_ASSERT_FALSE(r.success);

    impl->destroy();
    delete impl;
}

// updateLogLevel

LOGOS_TEST(updateLogLevel_returns_true_on_success) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->updateLogLevel("DEBUG").success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_log_level"));

    impl->destroy();
    delete impl;
}

// exists

LOGOS_TEST(exists_returns_true_when_cid_found) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_exists").returns("true");
    StdLogosResult r = impl->exists("QmSomeCid");
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.get<bool>());

    impl->destroy();
    delete impl;
}

LOGOS_TEST(exists_returns_false_when_cid_not_found) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_exists").returns("false");
    StdLogosResult r = impl->exists("QmMissingCid");
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_FALSE(r.value.get<bool>());

    impl->destroy();
    delete impl;
}

// togglePrivateQueries

LOGOS_TEST(togglePrivateQueries_returns_previous_state) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_toggle_private_queries").returns("false");
    StdLogosResult r = impl->togglePrivateQueries(true);
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_FALSE(r.value.get<bool>());
    LOGOS_ASSERT(t.cFunctionCalled("storage_toggle_private_queries"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(togglePrivateQueries_maps_true_previous_state) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_toggle_private_queries").returns("true");
    StdLogosResult r = impl->togglePrivateQueries(false);
    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.get<bool>());

    impl->destroy();
    delete impl;
}

// fetch / remove

LOGOS_TEST(fetch_calls_storage_fetch) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->fetch("QmSomeCid").success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_fetch"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(remove_calls_storage_delete) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->remove("QmSomeCid").success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_delete"));

    impl->destroy();
    delete impl;
}

// space

LOGOS_TEST(space_returns_parsed_map) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_space")
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

    t.mockCFunction("storage_list")
        .returns(R"([{"cid":"QmABC","manifest":{"treeCid":"QmTree","datasetSize":1024,"blockSize":64,"filename":"test.txt","mimetype":"text/plain"}}])");
    StdLogosResult r = impl->manifests();

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.is_array());
    LOGOS_ASSERT_EQ(static_cast<int>(r.value.size()), 1);

    impl->destroy();
    delete impl;
}

// downloadManifest

LOGOS_TEST(downloadManifest_returns_parsed_map) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_download_manifest")
        .returns(R"({"treeCid":"QmTree","datasetSize":2048,"blockSize":64,"filename":"data.bin","mimetype":"application/octet-stream"})");
    StdLogosResult r = impl->downloadManifest("QmSomeCid");

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_TRUE(r.value.is_object());
    LOGOS_ASSERT_FALSE(r.value.empty());
    LOGOS_ASSERT_TRUE(r.value.contains("treeCid"));

    impl->destroy();
    delete impl;
}

// uploadInit / uploadFinalize / uploadCancel

LOGOS_TEST(uploadInit_returns_session_id) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_upload_init").returns("session-abc-123");
    StdLogosResult r = impl->uploadInit("test.txt", 65536);

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), std::string("session-abc-123"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(uploadFinalize_returns_cid) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    t.mockCFunction("storage_upload_finalize").returns("QmFinalCid");
    StdLogosResult r = impl->uploadFinalize("session-abc-123");

    LOGOS_ASSERT_TRUE(r.success);
    LOGOS_ASSERT_EQ(r.value.get<std::string>(), std::string("QmFinalCid"));
    LOGOS_ASSERT(t.cFunctionCalled("storage_upload_finalize"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(uploadCancel_returns_true) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->uploadCancel("session-abc-123").success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_upload_cancel"));

    impl->destroy();
    delete impl;
}

// uploadUrl input validation

LOGOS_TEST(uploadUrl_fails_with_nonexistent_file) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    StdLogosResult r = impl->uploadUrl("/nonexistent/path/file.txt", 65536);
    LOGOS_ASSERT_FALSE(r.success);

    impl->destroy();
    delete impl;
}

LOGOS_TEST(uploadUrl_fails_with_zero_chunk_size) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    StdLogosResult r = impl->uploadUrl("/tmp/test.txt", 0);
    LOGOS_ASSERT_FALSE(r.success);

    impl->destroy();
    delete impl;
}

// downloadCancel

LOGOS_TEST(downloadCancel_returns_true) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    LOGOS_ASSERT_TRUE(impl->downloadCancel("QmSomeCid").success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_download_cancel"));

    impl->destroy();
    delete impl;
}

LOGOS_TEST(downloadChunks_cancels_session_when_stream_fails) {
    auto t = LogosTestContext("storage_module");
    auto* impl = createInitializedImpl(t);

    // storage_download_init succeeds, but the stream dispatch fails: the
    // already-open session must be cancelled and the call must report failure.
    t.mockCFunction("storage_download_stream").returns(1);
    StdLogosResult r = impl->downloadChunks("QmSomeCid", false, 65536);
    LOGOS_ASSERT_FALSE(r.success);
    LOGOS_ASSERT(t.cFunctionCalled("storage_download_cancel"));

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
    LOGOS_ASSERT(t.cFunctionCalled("storage_connect"));

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
