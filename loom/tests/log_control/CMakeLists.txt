cmake_minimum_required(VERSION 3.20)
project(loom_log_control_probe LANGUAGES CXX)
# Focused real logger/profile test; no server, monolith, network or old objects.
get_filename_component(LOOM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(log_control_probe probe.cpp
  ${LOOM_ROOT}/src/util/log.cpp
  ${LOOM_ROOT}/src/model/runtime_profile.cpp
  ${LOOM_ROOT}/src/util/random_ids_time.cpp
  ${LOOM_ROOT}/src/util/json.cpp
  ${LOOM_ROOT}/src/util/utf8.cpp
  ${LOOM_ROOT}/src/util/sha256.cpp
  ${LOOM_ROOT}/src/util/result.cpp)
target_compile_features(log_control_probe PRIVATE cxx_std_23)
target_include_directories(log_control_probe PRIVATE ${LOOM_ROOT}/include)
target_include_directories(log_control_probe SYSTEM PRIVATE ${LOOM_ROOT}/third_party/nlohmann)
# Keep the real called code; discard unrelated fs/unicode functions in shared TUs.
target_compile_options(log_control_probe PRIVATE -ffunction-sections -fdata-sections)
target_link_options(log_control_probe PRIVATE -Wl,--gc-sections)
target_link_libraries(log_control_probe PRIVATE Threads::Threads)
enable_testing()
add_test(NAME log_control_sinks_and_startup COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_LIST_DIR}/run.py $<TARGET_FILE:log_control_probe>)
set_tests_properties(log_control_sinks_and_startup PROPERTIES TIMEOUT 45)
add_test(NAME log_control_symbols COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_LIST_DIR}/check_symbols.py ${CMAKE_NM} $<TARGET_FILE:log_control_probe>)
