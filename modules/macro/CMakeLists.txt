# SPDX-License-Identifier: GPL-3.0-only
add_library(ws2_macro SHARED module.cpp)
target_link_libraries(ws2_macro PRIVATE wirescape_sdk2)
# The tests pack the labels slot with the lab's own engine/data_pack.h (JSON -> bytes).
add_executable(abi2_macro_tests tests.cpp ../../tests/native/abi2/allocation_watch.cpp)
target_include_directories(abi2_macro_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../engine)
target_link_libraries(abi2_macro_tests PRIVATE wirescape_sdk2 nlohmann_json::nlohmann_json)
add_dependencies(abi2_macro_tests ws2_macro)
add_test(NAME abi2_macro_tests COMMAND abi2_macro_tests)
