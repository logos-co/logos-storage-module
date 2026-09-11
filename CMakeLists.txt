cmake_minimum_required(VERSION 3.14)
project(StorageModulePlugin LANGUAGES CXX)

# Include the Logos Module CMake helper
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# Universal module — generated_code/ is picked up automatically by LogosModule.cmake
logos_module(
    NAME storage_module
    SOURCES
        src/storage_module_plugin.h
        src/storage_module_plugin.cpp
    EXTERNAL_LIBS
        storage
)

# Use a generated Mix config from mix-config.json
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/mix-config.json" MIX_CONFIG_JSON)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/mix-config.json")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/MixConfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/MixConfig.h"
    @ONLY)

target_include_directories(storage_module_module_plugin
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Inject the module version from metadata.jsonso
# moduleVersion() reports it without parsing JSON at runtime.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" _storage_metadata_json)
string(JSON STORAGE_MODULE_VERSION GET "${_storage_metadata_json}" version)
target_compile_definitions(storage_module_module_plugin
    PRIVATE STORAGE_MODULE_VERSION="${STORAGE_MODULE_VERSION}")
