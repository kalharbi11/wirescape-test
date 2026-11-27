# SPDX-License-Identifier: GPL-3.0-only
add_executable(abi2_lfo_tests tests.cpp ../../tests/native/abi2/allocation_watch.cpp)
target_link_libraries(abi2_lfo_tests PRIVATE wirescape_sdk2)
add_test(NAME abi2_lfo_tests COMMAND abi2_lfo_tests)
