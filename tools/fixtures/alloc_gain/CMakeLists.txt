# SPDX-License-Identifier: GPL-3.0-only
add_library(ws2_alloc_gain SHARED module.cpp)
target_link_libraries(ws2_alloc_gain PRIVATE wirescape_sdk2)
add_executable(abi2_alloc_gain_tests tests.cpp ../../tests/native/abi2/allocation_watch.cpp)
target_link_libraries(abi2_alloc_gain_tests PRIVATE wirescape_sdk2)
add_dependencies(abi2_alloc_gain_tests ws2_alloc_gain)
add_test(NAME abi2_alloc_gain_tests COMMAND abi2_alloc_gain_tests)
