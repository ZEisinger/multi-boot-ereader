# Copyright (c) multi-boot-ereader contributors. MIT licensed.
#
# Host build of the multiboot_core unit tests:
#   cmake -S test/native -B build/native
#   cmake --build build/native
#   ctest --test-dir build/native --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(multiboot_core_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
  add_compile_options(-Wall -Wextra -Werror)
endif()

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/multiboot_core)
file(GLOB CORE_SOURCES ${CORE_DIR}/src/*.cpp)
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(multiboot_core_tests ${CORE_SOURCES} ${TEST_SOURCES})
target_include_directories(multiboot_core_tests PRIVATE ${CORE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
add_test(NAME multiboot_core COMMAND multiboot_core_tests)
