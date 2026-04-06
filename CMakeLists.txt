cmake_minimum_required(VERSION 3.14)
project(StorageModulePlugin LANGUAGES CXX)

# Include the Logos Module CMake helper
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# Ensure metadata.json is in the build directory so AUTOMOC embeds it
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/metadata.json ${CMAKE_CURRENT_BINARY_DIR}/metadata.json COPYONLY)

# Define the module
logos_module(
    NAME storage_module
    SOURCES
        src/storage_module_interface.h
        src/storage_module_plugin.h
        src/storage_module_plugin.cpp
    EXTERNAL_LIBS
        storage
)
