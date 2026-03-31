cmake_minimum_required(VERSION 3.14)
project(StorageModuleTests LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(STORAGE_MODULE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

########### DEPENDENCIES SECTION ###########


if(NOT DEFINED LOGOS_LIBLOGOS_ROOT AND DEFINED ENV{LOGOS_LIBLOGOS_ROOT})
    set(LOGOS_LIBLOGOS_ROOT "$ENV{LOGOS_LIBLOGOS_ROOT}")
endif()

if(NOT DEFINED LOGOS_CPP_SDK_ROOT AND DEFINED ENV{LOGOS_CPP_SDK_ROOT})
    set(LOGOS_CPP_SDK_ROOT "$ENV{LOGOS_CPP_SDK_ROOT}")
endif()

if(NOT DEFINED LOGOS_STORAGE_NIM_ROOT AND DEFINED ENV{LOGOS_STORAGE_NIM_ROOT})
    set(LOGOS_STORAGE_NIM_ROOT "$ENV{LOGOS_STORAGE_NIM_ROOT}")
endif()

if(NOT EXISTS "${LOGOS_LIBLOGOS_ROOT}/include/core/interface.h")
    message(FATAL_ERROR "logos-liblogos not found at ${LOGOS_LIBLOGOS_ROOT}. "
                        "Set LOGOS_LIBLOGOS_ROOT.")
endif()

if(NOT EXISTS "${LOGOS_CPP_SDK_ROOT}/include/cpp/logos_api.h")
    message(FATAL_ERROR "logos-cpp-sdk not found at ${LOGOS_CPP_SDK_ROOT}. "
                        "Set LOGOS_CPP_SDK_ROOT.")
endif()

if(NOT EXISTS "${LOGOS_STORAGE_NIM_ROOT}/include/libstorage.h")
    message(FATAL_ERROR "logos-storage-nim not found at ${LOGOS_STORAGE_NIM_ROOT}. "
                        "Set LOGOS_STORAGE_NIM_ROOT.")
endif()

########### DEPENDENCIES SECTION END ###########

########### QT SECTION ###########

if(NOT DEFINED QT_VERSION_MAJOR)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core RemoteObjects)
    if(Qt6_FOUND)
        set(QT_VERSION_MAJOR 6)
    else()
        set(QT_VERSION_MAJOR 5)
    endif()
endif()

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core RemoteObjects Test)

########### QT SECTION END ###########

########### SOURCES ###########

set(PLUGIN_SOURCES
    ${STORAGE_MODULE_ROOT}/storage_module_plugin.cpp
    ${STORAGE_MODULE_ROOT}/storage_module_plugin.h
    ${STORAGE_MODULE_ROOT}/storage_module_interface.h
)

list(APPEND PLUGIN_SOURCES ${LOGOS_LIBLOGOS_ROOT}/include/core/interface.h)

########### SOURCES END ###########

########### TEST EXECUTABLE ###########

enable_testing()

add_executable(storage_module_tests
    test_storage_module.h
    test_storage_module.cpp
    ${PLUGIN_SOURCES}
)

target_link_libraries(storage_module_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::RemoteObjects
    Qt${QT_VERSION_MAJOR}::Test
)

find_library(LOGOS_SDK_LIB logos_sdk PATHS ${LOGOS_CPP_SDK_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
target_link_libraries(storage_module_tests PRIVATE ${LOGOS_SDK_LIB})

if(APPLE)
    set(LIBSTORAGE_NAMES libstorage.dylib)
elseif(WIN32)
    set(LIBSTORAGE_NAMES libstorage.dll)
else()
    set(LIBSTORAGE_NAMES libstorage.so)
endif()

find_library(LIBSTORAGE_PATH NAMES ${LIBSTORAGE_NAMES}
    PATHS ${LOGOS_STORAGE_NIM_ROOT}/lib ${LOGOS_STORAGE_NIM_ROOT}/build
    NO_DEFAULT_PATH)

target_link_libraries(storage_module_tests PRIVATE ${LIBSTORAGE_PATH})

########### HEADERS ###########

target_include_directories(storage_module_tests PRIVATE
    ${STORAGE_MODULE_ROOT}
    ${LOGOS_STORAGE_NIM_ROOT}/include
)

target_include_directories(storage_module_tests PRIVATE
    ${LOGOS_LIBLOGOS_ROOT}/include
)

target_include_directories(storage_module_tests PRIVATE
    ${LOGOS_CPP_SDK_ROOT}/include
    ${LOGOS_CPP_SDK_ROOT}/include/cpp
    ${LOGOS_CPP_SDK_ROOT}/include/core
)

target_include_directories(storage_module_tests PRIVATE
    ${LOGOS_STORAGE_NIM_ROOT}/include
)

########### HEADERS END ###########

add_test(NAME StorageModuleTests COMMAND storage_module_tests)

########### TEST EXECUTABLE END ###########
