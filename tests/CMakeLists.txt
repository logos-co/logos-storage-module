cmake_minimum_required(VERSION 3.14)
project(StorageModuleTests LANGUAGES CXX)

include(LogosTest)

# Copy metadata.json so AUTOMOC can find it for Q_PLUGIN_METADATA
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../metadata.json
               ${CMAKE_CURRENT_BINARY_DIR}/metadata.json COPYONLY)

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
