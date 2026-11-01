cmake_minimum_required(VERSION 3.24)
project(docx_parts_native LANGUAGES CXX)
# Configure ONLY after fresh heavy-slot reservation and incremental own core build.
if(NOT DEFINED LOOM_SOURCE OR NOT DEFINED LOOM_CORE_BUILD)
  message(FATAL_ERROR "Explicit own LOOM_SOURCE and freshly built LOOM_CORE_BUILD required")
endif()
set(CMAKE_CXX_STANDARD 20)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)
add_executable(docx_parts_native
  "${LOOM_SOURCE}/tests/main.cpp" "${LOOM_SOURCE}/tests/test_docx_source_parts.cpp" "${LOOM_SOURCE}/tests/test_text_source_parts.cpp")
target_include_directories(docx_parts_native PRIVATE
  "${LOOM_SOURCE}/include" "${LOOM_SOURCE}/src" "${LOOM_SOURCE}/tests"
  "${LOOM_SOURCE}/third_party/doctest" "${LOOM_SOURCE}/third_party/nlohmann")
target_compile_definitions(docx_parts_native PRIVATE
  JSON_USE_IMPLICIT_CONVERSIONS=1 LOOM_TEST_FIXTURES="${LOOM_SOURCE}/tests/fixtures")
target_link_libraries(docx_parts_native PRIVATE
  "${LOOM_CORE_BUILD}/libloom_core.a" "${LOOM_CORE_BUILD}/libloom_miniz.a"
  "${LOOM_CORE_BUILD}/libloom_pqc.a" SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto
  Threads::Threads ${CMAKE_DL_LIBS})
enable_testing()
add_test(NAME docx.parts.native9 COMMAND docx_parts_native --test-suite=docx_source_parts --no-intro=true)
add_test(NAME g1.text.regression COMMAND docx_parts_native --test-suite=text_source_parts --no-intro=true)
set_tests_properties(docx.parts.native9 g1.text.regression PROPERTIES TIMEOUT 180)
