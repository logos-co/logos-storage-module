cmake_minimum_required(VERSION 3.14)
project(StorageModuleTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ── SDK ─────────────────────────────────────────────────────────────────────
if(NOT DEFINED LOGOS_CPP_SDK_ROOT AND DEFINED ENV{LOGOS_CPP_SDK_ROOT})
    set(LOGOS_CPP_SDK_ROOT "$ENV{LOGOS_CPP_SDK_ROOT}")
endif()

# ── Check for libstorage (integration tests require it) ────────────────────
find_library(LIBSTORAGE_PATH
    NAMES libstorage.so libstorage.dylib
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../lib
    NO_DEFAULT_PATH)

if(NOT LIBSTORAGE_PATH)
    message(STATUS "libstorage not found in ../lib — skipping integration tests")
    enable_testing()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/skip_test.cpp
        "int main() { return 0; }\n")
    add_executable(storage_module_tests ${CMAKE_CURRENT_BINARY_DIR}/skip_test.cpp)
    add_test(NAME StorageModuleTests COMMAND storage_module_tests)
    return()
endif()

# ── Qt ──────────────────────────────────────────────────────────────────────
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core RemoteObjects Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core RemoteObjects Test)

# ── Sources ─────────────────────────────────────────────────────────────────
enable_testing()

add_executable(storage_module_tests
    test_storage_module.h
    test_storage_module.cpp
    ../src/storage_module_plugin.cpp
    ../src/storage_module_plugin.h
    ../src/storage_module_interface.h
)

# ── Includes ────────────────────────────────────────────────────────────────
target_include_directories(storage_module_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib
)

# SDK includes (installed layout)
if(LOGOS_CPP_SDK_ROOT)
    target_include_directories(storage_module_tests PRIVATE
        ${LOGOS_CPP_SDK_ROOT}/include
        ${LOGOS_CPP_SDK_ROOT}/include/cpp
        ${LOGOS_CPP_SDK_ROOT}/include/core
    )
endif()

# logos-module interface.h (check SDK core include first)
if(EXISTS "${LOGOS_CPP_SDK_ROOT}/include/core/interface.h")
    # Already included via SDK
elseif(DEFINED ENV{LOGOS_MODULE_ROOT})
    target_include_directories(storage_module_tests PRIVATE
        $ENV{LOGOS_MODULE_ROOT}/include/module_lib
    )
endif()

# ── Link ────────────────────────────────────────────────────────────────────
target_link_libraries(storage_module_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::RemoteObjects
    Qt${QT_VERSION_MAJOR}::Test
)

# SDK library
if(LOGOS_CPP_SDK_ROOT)
    find_library(LOGOS_SDK_LIB logos_sdk PATHS ${LOGOS_CPP_SDK_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(storage_module_tests PRIVATE ${LOGOS_SDK_LIB})
endif()

# libstorage
target_link_libraries(storage_module_tests PRIVATE ${LIBSTORAGE_PATH})

add_test(NAME StorageModuleTests COMMAND storage_module_tests)
