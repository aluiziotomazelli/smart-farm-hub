cmake_minimum_required(VERSION 3.16)
project(ui_inputs_host_tests NONE)

# Enable CTest for this project
include(CTest)

# Define the tests for CTest.
# These commands assume that each project has been built using 'idf.py build'
# and that CTest is being run from a 'build' subdirectory within 'host_test'.
add_test(NAME test_hub COMMAND ../test_hub/build/test_hub.elf)

# Unified Coverage Configuration
find_program(LCOV_PATH lcov REQUIRED)
find_program(GENHTML_PATH genhtml REQUIRED)

# The unified coverage report will be placed in host_test/coverage
set(UNIFIED_COVERAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/coverage")

# This target aggregates coverage data (.gcda files) from all test build directories
add_custom_target(unified_coverage
    COMMAND ${LCOV_PATH} --capture --directory .. --output-file unified_coverage.info --rc branch_coverage=1 --rc geninfo_unexecuted_blocks=1 --ignore-errors mismatch,inconsistent
    COMMAND ${LCOV_PATH} --extract unified_coverage.info "*/ui_inputs/src/*" --output-file unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty
    COMMAND ${LCOV_PATH} --remove unified_coverage_filtered.info "*/ui_inputs/include/interfaces/*" --output-file unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty
    COMMAND ${GENHTML_PATH} unified_coverage_filtered.info --output-directory ${UNIFIED_COVERAGE_DIR} --title "UI Inputs Unified Coverage" --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused
    COMMAND ${LCOV_PATH} --list unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating unified coverage report from all host tests"
)

# Added cleanup target to manually clear previous results if needed
add_custom_target(coverage_clean
    COMMAND find . -name "*.gcda" -delete
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${UNIFIED_COVERAGE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Cleaning up coverage data"
)

# Helper target to build all tests at once (requires idf.py to be in your PATH)
add_custom_target(build_all_tests
    COMMAND idf.py -C ../test_hub build
    COMMENT "Building all test projects using idf.py"
)

# Unified target to build, clean, run all tests and generate coverage
add_custom_target(run_all_tests
    COMMAND ${CMAKE_COMMAND} --build . --target build_all_tests
    COMMAND ${CMAKE_COMMAND} --build . --target coverage_clean
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND ${CMAKE_COMMAND} --build . --target unified_coverage
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Full test cycle: build, clean, run, and coverage"
)
