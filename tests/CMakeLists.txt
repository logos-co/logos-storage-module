cmake_minimum_required(VERSION 3.14)
project(StorageModuleTests LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(STORAGE_MODULE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

########### DEPENDENCIES SECTION ###########
if(NOT DEFINED LOGOS_LIBLOGOS_ROOT)
    set(_parent_liblogos "${STORAGE_MODULE_ROOT}/../logos-liblogos")
    set(_use_vendor ${LOGOS_STORAGE_MODULE_USE_VENDOR})
    if(NOT _use_vendor)
        if(NOT EXISTS "${_parent_liblogos}/src/common/interface.h")
            set(_use_vendor ON)
        endif()
    endif()
    if(_use_vendor)
        set(LOGOS_LIBLOGOS_ROOT "${STORAGE_MODULE_ROOT}/vendor/logos-liblogos")
    else()
        set(LOGOS_LIBLOGOS_ROOT "${_parent_liblogos}")
    endif()
endif()

if(NOT DEFINED LOGOS_CPP_SDK_ROOT)
    set(_parent_cpp_sdk "${STORAGE_MODULE_ROOT}/../logos-cpp-sdk")
    set(_use_vendor ${LOGOS_STORAGE_MODULE_USE_VENDOR})
    if(NOT _use_vendor)
        if(NOT EXISTS "${_parent_cpp_sdk}/cpp/logos_api.h")
            set(_use_vendor ON)
        endif()
    endif()
    if(_use_vendor)
        set(LOGOS_CPP_SDK_ROOT "${STORAGE_MODULE_ROOT}/vendor/logos-cpp-sdk")
    else()
        set(LOGOS_CPP_SDK_ROOT "${_parent_cpp_sdk}")
    endif()
endif()

if(NOT DEFINED LOGOS_STORAGE_NIM_ROOT)
    set(_parent_logos_storage_nim "${STORAGE_MODULE_ROOT}/../logos-storage-nim")
    set(_use_vendor ${LOGOS_STORAGE_MODULE_USE_VENDOR})
    if(NOT _use_vendor)
        if(NOT EXISTS "${_parent_logos_storage_nim}/library/libstorage.h")
            set(_use_vendor ON)
        endif()
    endif()
    if(_use_vendor)
        set(LOGOS_STORAGE_NIM_ROOT "${STORAGE_MODULE_ROOT}/vendor/logos-storage-nim")
    else()
        set(LOGOS_STORAGE_NIM_ROOT "${_parent_logos_storage_nim}")
    endif()
endif()

if(NOT DEFINED _liblogos_is_source)
    if(EXISTS "${LOGOS_LIBLOGOS_ROOT}/src/common/interface.h")
        set(_liblogos_is_source TRUE)
    else()
        set(_liblogos_is_source FALSE)
    endif()
endif()

if(NOT DEFINED _cpp_sdk_is_source)
    if(EXISTS "${LOGOS_CPP_SDK_ROOT}/cpp/logos_api.h")
        set(_cpp_sdk_is_source TRUE)
    else()
        set(_cpp_sdk_is_source FALSE)
    endif()
endif()

if(NOT DEFINED _logos_storage_nim_is_source)
    if(EXISTS "${LOGOS_STORAGE_NIM_ROOT}/library/libstorage.h")
        set(_logos_storage_nim_is_source TRUE)
    else()
        set(_logos_storage_nim_is_source FALSE)
    endif()
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

if(_liblogos_is_source)
    list(APPEND PLUGIN_SOURCES ${LOGOS_LIBLOGOS_ROOT}/src/common/interface.h)
else()
    list(APPEND PLUGIN_SOURCES ${LOGOS_LIBLOGOS_ROOT}/include/interface.h)
endif()

if(_cpp_sdk_is_source)
    list(APPEND PLUGIN_SOURCES
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_types.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_types.h
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api.h
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api_client.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api_client.h
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api_consumer.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api_consumer.h
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api_provider.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/logos_api_provider.h
        ${LOGOS_CPP_SDK_ROOT}/cpp/token_manager.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/token_manager.h
        ${LOGOS_CPP_SDK_ROOT}/cpp/module_proxy.cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/module_proxy.h
    )
endif()

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

if(NOT _cpp_sdk_is_source)
    find_library(LOGOS_SDK_LIB logos_sdk PATHS ${LOGOS_CPP_SDK_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(storage_module_tests PRIVATE ${LOGOS_SDK_LIB})
endif()

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

if(LIBSTORAGE_PATH)
    target_link_libraries(storage_module_tests PRIVATE ${LIBSTORAGE_PATH})
else()
    message(WARNING "libstorage not found — tests will not link correctly.")
endif()

########### HEADERS ###########

target_include_directories(storage_module_tests PRIVATE
    ${STORAGE_MODULE_ROOT}
    ${LOGOS_STORAGE_NIM_ROOT}/include
)

if(_liblogos_is_source)
    target_include_directories(storage_module_tests PRIVATE
        ${LOGOS_LIBLOGOS_ROOT}/src
        ${LOGOS_LIBLOGOS_ROOT}/src/common
    )
else()
    target_include_directories(storage_module_tests PRIVATE
        ${LOGOS_LIBLOGOS_ROOT}/include
    )
endif()

if(_cpp_sdk_is_source)
    target_include_directories(storage_module_tests PRIVATE
        ${LOGOS_CPP_SDK_ROOT}/cpp
        ${LOGOS_CPP_SDK_ROOT}/cpp/generated
    )
else()
    target_include_directories(storage_module_tests PRIVATE
        ${LOGOS_CPP_SDK_ROOT}/include
        ${LOGOS_CPP_SDK_ROOT}/include/cpp
        ${LOGOS_CPP_SDK_ROOT}/include/core
    )
endif()

if(_logos_storage_nim_is_source)
    target_include_directories(storage_module_tests PRIVATE
        ${LOGOS_STORAGE_NIM_ROOT}/library
    )
else()
    target_include_directories(storage_module_tests PRIVATE
        ${LOGOS_STORAGE_NIM_ROOT}/include
    )
endif()

########### HEADERS END ###########

add_test(NAME StorageModuleTests COMMAND storage_module_tests)

########### TEST EXECUTABLE END ###########
