cmake_minimum_required(VERSION 3.24)
project(external_share_native LANGUAGES CXX C)
# Configure ONLY after fresh heavy-slot reservation and incremental own core build.
if(NOT DEFINED LOOM_SOURCE OR NOT DEFINED LOOM_CORE_BUILD)
  message(FATAL_ERROR "Explicit own LOOM_SOURCE and freshly built LOOM_CORE_BUILD required")
endif()
set(CMAKE_CXX_STANDARD 20)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)
add_executable(external_share_native
  "${LOOM_SOURCE}/tests/main.cpp" "${LOOM_SOURCE}/tests/test_url_share_import.cpp")
target_include_directories(external_share_native PRIVATE
  "${LOOM_SOURCE}/include" "${LOOM_SOURCE}/src" "${LOOM_SOURCE}/tests"
  "${LOOM_SOURCE}/third_party/doctest" "${LOOM_SOURCE}/third_party/nlohmann")
target_compile_definitions(external_share_native PRIVATE
  JSON_USE_IMPLICIT_CONVERSIONS=1 LOOM_TEST_FIXTURES="${LOOM_SOURCE}/tests/fixtures")
target_link_libraries(external_share_native PRIVATE
  "${LOOM_CORE_BUILD}/libloom_core.a" "${LOOM_CORE_BUILD}/libloom_miniz.a"
  "${LOOM_CORE_BUILD}/libloom_pqc.a" SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto
  Threads::Threads ${CMAKE_DL_LIBS})
add_library(external_share_network_fixture SHARED "${LOOM_SOURCE}/tests/url_share/external_share_network_fixture.c")
target_link_libraries(external_share_network_fixture PRIVATE ${CMAKE_DL_LIBS})
enable_testing()
add_test(NAME share.native10 COMMAND external_share_native --test-suite=url_share_import --no-intro=true)
set_tests_properties(share.native10 PROPERTIES TIMEOUT 180)
