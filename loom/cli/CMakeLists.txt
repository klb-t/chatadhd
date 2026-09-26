# loom CLI (LOOM_BUILD_CLI): subcommands over the C++ API.
add_executable(loom_cli main.cpp)
set_target_properties(loom_cli PROPERTIES OUTPUT_NAME loom)
target_link_libraries(loom_cli PRIVATE loom_core loom_warnings)
target_include_directories(loom_cli PRIVATE ${LOOM_ROOT}/src)

if(LOOM_BUILD_TESTS)
  enable_testing()
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME cli.smoke COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_smoke.py)
    set_tests_properties(cli.smoke PROPERTIES
      ENVIRONMENT "LOOM_CLI=$<TARGET_FILE:loom_cli>;LOOM_FIXTURES=${LOOM_ROOT}/tests/fixtures;PYTHONDONTWRITEBYTECODE=1"
      TIMEOUT 300)
  endif()
endif()
