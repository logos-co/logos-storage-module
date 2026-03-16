cmake_minimum_required(VERSION 3.14)
project(StorageModuleTests LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

enable_testing()

add_executable(storage_module_tests
    test_storage_module.cpp
)

target_link_libraries(storage_module_tests PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME StorageModuleTests COMMAND storage_module_tests)
