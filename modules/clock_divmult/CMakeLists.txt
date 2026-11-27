# SPDX-License-Identifier: GPL-3.0-only
add_library(ws2_clock_divmult SHARED module.cpp)
target_link_libraries(ws2_clock_divmult PRIVATE wirescape_sdk2)
add_executable(abi2_clock_divmult_tests tests.cpp ../../tests/native/abi2/allocation_watch.cpp)
target_link_libraries(abi2_clock_divmult_tests PRIVATE wirescape_sdk2)
# The test reads this folder's beat_clock.h to check it is the same as modules/random's copy.
target_compile_definitions(abi2_clock_divmult_tests PRIVATE WSDM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(abi2_clock_divmult_tests ws2_clock_divmult)
add_test(NAME abi2_clock_divmult_tests COMMAND abi2_clock_divmult_tests)
