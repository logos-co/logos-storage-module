// Test bodies for StorageModuleImpl's `logos_events:` methods.
//
// Production builds get these from the codegen-emitted
// `storage_module_events_cdylib.cpp`, which marshals through Qt; unit/
// integration tests construct the impl directly without that layer, so they
// link these one-line forwarders instead. Observe with
// logos_test::EventCapture / ScopedEventSink.

#include <logos_test.h>
#include "storage_module_plugin.h"

using logos_test::recordEvent;

void StorageModuleImpl::storageStart(const std::string& payload)            { recordEvent("storageStart", payload); }
void StorageModuleImpl::storageStop(const std::string& payload)             { recordEvent("storageStop", payload); }
void StorageModuleImpl::storageConnect(const std::string& payload)          { recordEvent("storageConnect", payload); }
void StorageModuleImpl::storageUploadProgress(const std::string& payload)   { recordEvent("storageUploadProgress", payload); }
void StorageModuleImpl::storageUploadDone(const std::string& payload)       { recordEvent("storageUploadDone", payload); }
void StorageModuleImpl::storageDownloadProgress(const std::string& payload) { recordEvent("storageDownloadProgress", payload); }
void StorageModuleImpl::storageDownloadDone(const std::string& payload)     { recordEvent("storageDownloadDone", payload); }
