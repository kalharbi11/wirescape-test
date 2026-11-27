# SPDX-License-Identifier: GPL-3.0-only
add_library(ws2_guttr SHARED module.cpp)
target_link_libraries(ws2_guttr PRIVATE wirescape_sdk2)
add_executable(abi2_guttr_tests tests.cpp ../../tests/native/abi2/allocation_watch.cpp)
target_link_libraries(abi2_guttr_tests PRIVATE wirescape_sdk2)
add_dependencies(abi2_guttr_tests ws2_guttr)
add_test(NAME abi2_guttr_tests COMMAND abi2_guttr_tests)
