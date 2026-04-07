cmake_minimum_required(VERSION 3.14)
project(StorageModuleTests LANGUAGES CXX)

include(LogosTest)

# Copy metadata.json so AUTOMOC can find it for Q_PLUGIN_METADATA
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../metadata.json
               ${CMAKE_CURRENT_BINARY_DIR}/metadata.json COPYONLY)

# ── Unit tests (mocked libstorage) ──────────────────────────────────────────

logos_test(
    NAME storage_module_tests
    MODULE_SOURCES
        ../src/storage_module_plugin.cpp
    TEST_SOURCES
        main.cpp
        test_storage.cpp
    MOCK_C_SOURCES
        mocks/mock_libstorage.cpp
    EXTRA_INCLUDES
        stubs
)

# ── Integration tests (real libstorage) ─────────────────────────────────────

find_library(LIBSTORAGE_PATH
    NAMES libstorage.so libstorage.dylib
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../lib
    NO_DEFAULT_PATH)

if(LIBSTORAGE_PATH)
    message(STATUS "[StorageTests] libstorage found: ${LIBSTORAGE_PATH} — building integration tests")

    logos_test(
        NAME storage_module_integration_tests
        MODULE_SOURCES
            ../src/storage_module_plugin.cpp
        TEST_SOURCES
            main.cpp
            test_storage_module.cpp
        EXTRA_INCLUDES
            ../lib
        EXTRA_LINK_LIBS
            ${LIBSTORAGE_PATH}
    )

    # Ensure the integration test can find libstorage at runtime
    get_filename_component(LIBSTORAGE_DIR "${LIBSTORAGE_PATH}" DIRECTORY)
    set_target_properties(storage_module_integration_tests PROPERTIES
        BUILD_RPATH "${LIBSTORAGE_DIR}"
    )
else()
    message(STATUS "[StorageTests] libstorage not found in ../lib — skipping integration tests")
endif()
