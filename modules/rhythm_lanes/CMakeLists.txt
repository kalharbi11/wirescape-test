# SPDX-License-Identifier: GPL-3.0-only
add_library(ws2_rhythm_lanes SHARED module.cpp)
target_link_libraries(ws2_rhythm_lanes PRIVATE wirescape_sdk2)
add_executable(abi2_rhythm_lanes_tests tests.cpp ../../tests/native/abi2/allocation_watch.cpp)
target_link_libraries(abi2_rhythm_lanes_tests PRIVATE wirescape_sdk2)
add_dependencies(abi2_rhythm_lanes_tests ws2_rhythm_lanes)
add_test(NAME abi2_rhythm_lanes_tests COMMAND abi2_rhythm_lanes_tests)
