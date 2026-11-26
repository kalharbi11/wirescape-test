# SPDX-License-Identifier: GPL-3.0-only
add_library(ws2_random SHARED module.cpp)
target_link_libraries(ws2_random PRIVATE wirescape_sdk2)
add_executable(abi2_random_tests tests.cpp ../../tests/native/abi2/allocation_watch.cpp)
target_link_libraries(abi2_random_tests PRIVATE wirescape_sdk2)
add_dependencies(abi2_random_tests ws2_random)
add_test(NAME abi2_random_tests COMMAND abi2_random_tests)
