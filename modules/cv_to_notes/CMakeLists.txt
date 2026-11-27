# SPDX-License-Identifier: GPL-3.0-only
add_library(ws2_cv_to_notes SHARED module.cpp)
target_link_libraries(ws2_cv_to_notes PRIVATE wirescape_sdk2)
add_executable(abi2_cv_to_notes_tests tests.cpp ../../tests/native/abi2/allocation_watch.cpp)
target_link_libraries(abi2_cv_to_notes_tests PRIVATE wirescape_engine2)
add_test(NAME abi2_cv_to_notes_tests COMMAND abi2_cv_to_notes_tests)
