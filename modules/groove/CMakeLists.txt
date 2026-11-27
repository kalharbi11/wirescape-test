# SPDX-License-Identifier: GPL-3.0-only
# Standalone regression target; see DESIGN.md for the pinned harness registration gap.
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(GrooveMod7Tests LANGUAGES CXX)
    enable_testing()
endif()
add_executable(abi2_groove_tests tests.cpp)
target_compile_features(abi2_groove_tests PRIVATE cxx_std_17)
target_include_directories(abi2_groove_tests PRIVATE ../../sdk ../../sdk/include)
add_test(NAME abi2_groove_tests COMMAND abi2_groove_tests)
