# SPDX-License-Identifier: GPL-3.0-only
add_library(ws2_gain SHARED module.cpp)
target_link_libraries(ws2_gain PRIVATE wirescape_sdk2)
add_executable(abi2_gain_tests tests.cpp ../../tests/native/abi2/allocation_watch.cpp)
target_link_libraries(abi2_gain_tests PRIVATE wirescape_sdk2)
add_dependencies(abi2_gain_tests ws2_gain)
add_test(NAME abi2_gain_tests COMMAND abi2_gain_tests)
