# SPDX-License-Identifier: GPL-3.0-only
# OB-g5 standalone exact-source/device-off test, separate from shared native build.
# Execute: cmake -S native/modules2/adsr -B /tmp/adsr-ob-g5 &&
#          cmake --build /tmp/adsr-ob-g5 && ctest --test-dir /tmp/adsr-ob-g5 --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(ADSR_OB_G5_Tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()
add_executable(adsr_ob_g5_tests tests.cpp)
target_include_directories(adsr_ob_g5_tests PRIVATE ../../sdk ../../sdk/include ../../engine/include ../../host/include)
add_test(NAME adsr_ob_g5_tests COMMAND adsr_ob_g5_tests)
