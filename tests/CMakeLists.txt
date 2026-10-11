# SPDX-License-Identifier: BSL-1.0
# Copyright 2026 Steven Festenstein

project(CommonUtilsTests)

include(GoogleTest)

file(GLOB UNIT_TEST_SOURCE CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

# Add the source files
add_executable(${PROJECT_NAME} ${UNIT_TEST_SOURCE})

target_link_libraries(${PROJECT_NAME}
   PRIVATE
      CommonUtils
      CommonUtils_warnings
      GTest::gtest
      GTest::gmock
)

# In some Linux container hosts, TSan binaries need ASLR disabled at launch.
# TEST_LAUNCHER is honored by gtest_discover_tests() for both discovery and
# test execution, so this keeps tsan presets usable without manual wrappers.
if (CMAKE_CXX_FLAGS MATCHES "(^| )-fsanitize=thread(,| |$)")
   find_program(SETARCH_EXE NAMES setarch)
   if (SETARCH_EXE)
      execute_process(
         COMMAND ${SETARCH_EXE} ${CMAKE_SYSTEM_PROCESSOR} -R ${CMAKE_COMMAND} -E true
         RESULT_VARIABLE SETARCH_DISABLE_ASLR_RESULT
         OUTPUT_QUIET
         ERROR_QUIET
      )
      if (SETARCH_DISABLE_ASLR_RESULT EQUAL 0)
         set_property(TARGET ${PROJECT_NAME} PROPERTY TEST_LAUNCHER "${SETARCH_EXE};${CMAKE_SYSTEM_PROCESSOR};-R")
      else()
         message(STATUS "setarch -R is unavailable in this environment; tsan tests may require manual ASLR-disabled execution outside this container.")
      endif()
   endif()
endif()

# Discover tests for CTest
gtest_discover_tests(${PROJECT_NAME} 
   WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
   PROPERTIES
      LABELS "unit"
)


# =============================================================================
# Coverage Target
# =============================================================================

# include(Coverage)
# add_coverage_target(
#    NAME    CommonUtilsCoverage
#    TITLE   "CommonUtils Coverage Report"
#    DEPENDS ${PROJECT_NAME}
# )