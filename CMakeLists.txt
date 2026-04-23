cmake_minimum_required(VERSION 3.14)
project(StorageModulePlugin LANGUAGES CXX)

# Include the Logos Module CMake helper
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

logos_module(
    NAME storage_module
    SOURCES
        src/storage_module_interface.h
        src/storage_module_plugin.h
        src/storage_module_plugin.cpp
    EXTERNAL_LIBS
        storage
)
