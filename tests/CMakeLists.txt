cmake_minimum_required(VERSION 3.14)
project(StorageModuleTests LANGUAGES CXX)

include(LogosTest)

# Mirror the module build: inject the version from metadata.json.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../metadata.json" _storage_metadata_json)
string(JSON STORAGE_MODULE_VERSION GET "${_storage_metadata_json}" version)

# Unit tests (mocked libstorage)

logos_test(
    NAME storage_module_tests
    MODULE_SOURCES
        ../src/storage_module_plugin.cpp
    TEST_SOURCES
        main.cpp
        test_storage.cpp
        storage_events_test.cpp
    MOCK_C_SOURCES
        mocks/mock_libstorage.cpp
    EXTRA_INCLUDES
        stubs
)
target_compile_definitions(storage_module_tests PRIVATE
    STORAGE_MODULE_VERSION="${STORAGE_MODULE_VERSION}")

# Integration tests (real libstorage)

find_library(LIBSTORAGE_PATH
    NAMES libstorage.so libstorage.dylib
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../lib
    NO_DEFAULT_PATH)

if(LIBSTORAGE_PATH)
    message(STATUS "[StorageTests] libstorage found: ${LIBSTORAGE_PATH} - building integration tests")

    # The generated bindings encode their requests with TinyCBOR.
    find_library(TINYCBOR_LIBRARY
        NAMES libtinycbor.a tinycbor
        PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../lib
        NO_DEFAULT_PATH
        REQUIRED)

    logos_test(
        NAME storage_module_integration_tests
        MODULE_SOURCES
            ../src/storage_module_plugin.cpp
        TEST_SOURCES
            main.cpp
            test_storage_module.cpp
            storage_events_test.cpp
        EXTRA_INCLUDES
            ../lib
        EXTRA_LINK_LIBS
            ${LIBSTORAGE_PATH}
            ${TINYCBOR_LIBRARY}
    )

    target_compile_definitions(storage_module_integration_tests PRIVATE
        STORAGE_MODULE_VERSION="${STORAGE_MODULE_VERSION}")

    # Ensure the integration test can find libstorage at runtime
    get_filename_component(LIBSTORAGE_DIR "${LIBSTORAGE_PATH}" DIRECTORY)
    set_target_properties(storage_module_integration_tests PROPERTIES
        BUILD_RPATH "${LIBSTORAGE_DIR}"
    )
else()
    message(STATUS "[StorageTests] libstorage not found in ../lib - skipping integration tests")
endif()
