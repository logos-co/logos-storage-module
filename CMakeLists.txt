cmake_minimum_required(VERSION 3.14)
project(StorageModulePlugin LANGUAGES CXX)

# Load GNUInstallDirs for standard install directories
include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
option(LOGOS_STORAGE_MODULE_USE_VENDOR "Force use of vendored Logos dependencies" OFF)

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

#get_filename_component(LOGOS_DEPS_ROOT "${LOGOS_CPP_SDK_ROOT}" DIRECTORY)

########### DEPENDENCIES SECTION END ###########

########### QT SECTION ###########

# Find Qt RemoteObjects (needed for LogosAPI).
if(NOT DEFINED QT_VERSION_MAJOR)
    # Discover the Qt version
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core RemoteObjects)
    if(Qt6_FOUND)
        set(QT_VERSION_MAJOR 6)
    else()
        set(QT_VERSION_MAJOR 5)
    endif()
endif()

# Use the Qt version discovered
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core RemoteObjects)

########### QT SECTION END ###########

########### CPP GENERATOR ###########

# Set the metadata json path
set(METADATA_JSON "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json")

# Define the folder for generated sources
set(PLUGINS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/modules")

# Use the generator from the install path
find_program(CPP_GENERATOR logos-cpp-generator PATHS ${LOGOS_CPP_SDK_ROOT}/bin NO_DEFAULT_PATH REQUIRED)

add_custom_target(run_cpp_generator_logos_storage
    COMMAND "${CPP_GENERATOR}" --metadata "${METADATA_JSON}" --module-dir "${PLUGINS_OUTPUT_DIR}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Running logos-cpp-generator on ${METADATA_JSON} with module-dir ${PLUGINS_OUTPUT_DIR}"
    VERBATIM
)

########### CPP GENERATOR END ###########

########### SOURCES ###########

# Plugin sources
set(PLUGIN_SOURCES
    storage_module_plugin.cpp
    storage_module_plugin.h
    storage_module_interface.h
)

# Add liblogos interface header
list(APPEND PLUGIN_SOURCES ${LOGOS_LIBLOGOS_ROOT}/include/core/interface.h)

########### SOURCES END ###########

########### LIBRARY DEFINITION ###########

# Create the plugin library
add_library(storage_module_plugin SHARED ${PLUGIN_SOURCES})

set_target_properties(storage_module_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modules"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modules"  # For Windows .dll
    BUILD_WITH_INSTALL_RPATH TRUE
    PREFIX ""
    SKIP_BUILD_RPATH FALSE)

# Ensure generator runs before building the plugin
add_dependencies(storage_module_plugin run_cpp_generator_logos_storage)

########### LIBRARY DEFINITION END ###########

########### LIBRARY LINKS ###########

# Link Qt libraries
target_link_libraries(storage_module_plugin PRIVATE 
    Qt${QT_VERSION_MAJOR}::Core 
    Qt${QT_VERSION_MAJOR}::RemoteObjects
)

# Link SDK library
find_library(LOGOS_SDK_LIB logos_sdk PATHS ${LOGOS_CPP_SDK_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
target_link_libraries(storage_module_plugin PRIVATE ${LOGOS_SDK_LIB})

########### LIBRARY LINKS END ###########

########### LIBSTORAGE ###########

# Prioritize platform-specific library names
if(APPLE)
    set(LIBSTORAGE_NAMES libstorage.dylib)
elseif(WIN32)
    set(LIBSTORAGE_NAMES libstorage.dll)
else()
    set(LIBSTORAGE_NAMES libstorage.so)
endif()

# Always add lib directory and try to link -lstorage; also keep optional direct path if present
# LIBSTORAGE_PATH will be defined with the path to the library if found
find_library(LIBSTORAGE_PATH NAMES ${LIBSTORAGE_NAMES} PATHS ${LOGOS_STORAGE_NIM_ROOT}/lib ${LOGOS_STORAGE_NIM_ROOT}/build NO_DEFAULT_PATH)

# Link to libstorage on all platforms
target_link_libraries(storage_module_plugin PRIVATE ${LIBSTORAGE_PATH})

########### LIBSTORAGE END ###########

########### HEADERS ###########

# Include directories
target_include_directories(storage_module_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LOGOS_STORAGE_NIM_ROOT}/include
)

# Add include directories based on layout type
target_include_directories(storage_module_plugin PRIVATE
    "${LOGOS_LIBLOGOS_ROOT}/include"
)

target_include_directories(storage_module_plugin PRIVATE
    ${LOGOS_CPP_SDK_ROOT}/include
    ${LOGOS_CPP_SDK_ROOT}/include/cpp
    ${LOGOS_CPP_SDK_ROOT}/include/core
)

# if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs")
#     target_include_directories(storage_module_plugin
#         PRIVATE
#             ${CMAKE_CURRENT_SOURCE_DIR}/libs
#     )
# endif()

target_include_directories(storage_module_plugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/result/include
)

# libstorage headers (from logos-storage-nim submodule)
# This is required only for vendored builds where libstorage.h is not in a standard include path.
# For Nix, it is available from the LOGOS_STORAGE_NIM_ROOT/include path.
target_include_directories(storage_module_plugin PRIVATE
  "${LOGOS_STORAGE_NIM_ROOT}/include"
)

########### HEADERS END ###########

########### RUNTIME LINK ###########

if(APPLE)
    # Allow unresolved symbols at link time; libstorage will be provided at runtime
    target_link_options(storage_module_plugin PRIVATE -undefined dynamic_lookup)
    set_target_properties(storage_module_plugin PROPERTIES
        INSTALL_RPATH "@loader_path"
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_NAME_DIR TRUE)

    if(LIBSTORAGE_PATH)
        get_filename_component(LIBSTORAGE_FILENAME "${LIBSTORAGE_PATH}" NAME)
        add_custom_command(TARGET storage_module_plugin PRE_LINK
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${LIBSTORAGE_PATH}
                ${CMAKE_BINARY_DIR}/modules/${LIBSTORAGE_FILENAME}
            COMMENT "Copying ${LIBSTORAGE_FILENAME} to modules directory"
        )

        add_custom_command(TARGET storage_module_plugin POST_BUILD
            COMMAND install_name_tool -id "@rpath/storage_module_plugin.dylib" $<TARGET_FILE:storage_module_plugin>
            COMMAND install_name_tool -id "@rpath/${LIBSTORAGE_FILENAME}" "${CMAKE_BINARY_DIR}/modules/${LIBSTORAGE_FILENAME}"
            COMMAND install_name_tool -change "${LIBSTORAGE_PATH}" "@rpath/${LIBSTORAGE_FILENAME}" $<TARGET_FILE:storage_module_plugin>
            COMMENT "Updating library paths for macOS"
        )
    else()
        add_custom_command(TARGET storage_module_plugin POST_BUILD
            COMMAND install_name_tool -id "@rpath/storage_module_plugin.dylib" $<TARGET_FILE:storage_module_plugin>
            COMMENT "Updating library paths for macOS (libstorage not found)"
        )
    endif()
else()
    set_target_properties(storage_module_plugin PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        INSTALL_RPATH_USE_LINK_PATH FALSE)

    if(LIBSTORAGE_PATH)
        get_filename_component(LIBSTORAGE_FILENAME "${LIBSTORAGE_PATH}" NAME)
        add_custom_command(TARGET storage_module_plugin PRE_LINK
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${LIBSTORAGE_PATH}
                ${CMAKE_BINARY_DIR}/modules/${LIBSTORAGE_FILENAME}
            COMMENT "Copying ${LIBSTORAGE_FILENAME} to modules directory"
        )
    endif()
endif()

########### RUNTIME LINK END ###########

########### INSTALL ###########

install(TARGETS storage_module_plugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/logos/modules
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/logos/modules
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/logos/modules
)

install(FILES ${METADATA_JSON}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/logos-storage-module
)

install(DIRECTORY "${PLUGINS_OUTPUT_DIR}/"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/logos-storage-module/generated
    OPTIONAL
)

########### INSTALL END ###########
