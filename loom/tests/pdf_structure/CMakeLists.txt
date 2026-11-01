cmake_minimum_required(VERSION 3.24)
project(pdf_structure_native LANGUAGES CXX)
# Sole-integrator runner only, after its own incremental core build.
if(NOT DEFINED LOOM_SOURCE OR NOT DEFINED LOOM_CORE_BUILD)
  message(FATAL_ERROR "Explicit LOOM_SOURCE and freshly rebuilt LOOM_CORE_BUILD required")
endif()
set(CMAKE_CXX_STANDARD 20)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)
add_executable(pdf_structure_native
  "${LOOM_SOURCE}/tests/main.cpp"
  "${LOOM_SOURCE}/tests/pdf_structure/test_pdf_structure.cpp"
  "${LOOM_SOURCE}/tests/image_region_candidate/native.verify.cc"
  "${LOOM_SOURCE}/src/context/unified_context.verify.cc"
  "${LOOM_SOURCE}/tests/test_text_source_parts.cpp"
  "${LOOM_SOURCE}/tests/test_docx_source_parts.cpp")
target_include_directories(pdf_structure_native PRIVATE
  "${LOOM_SOURCE}/include" "${LOOM_SOURCE}/src" "${LOOM_SOURCE}/tests"
  "${LOOM_SOURCE}/third_party/doctest" "${LOOM_SOURCE}/third_party/nlohmann")
target_compile_definitions(pdf_structure_native PRIVATE
  JSON_USE_IMPLICIT_CONVERSIONS=1 LOOM_TEST_FIXTURES="${LOOM_SOURCE}/tests/fixtures")
target_link_libraries(pdf_structure_native PRIVATE
  "${LOOM_CORE_BUILD}/libloom_core.a" "${LOOM_CORE_BUILD}/libloom_miniz.a"
  "${LOOM_CORE_BUILD}/libloom_pqc.a" SQLite::SQLite3 OpenSSL::SSL OpenSSL::Crypto
  Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(pdf_structure_native PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
enable_testing()
# One combined run. Shared sentinels selected for changed traversal and document
# adapter; this is not the historical full P0 or per-author suite matrix.
add_test(NAME fleet.ocr_pdf.common COMMAND pdf_structure_native --no-intro=true
  "--test-case=existing producer*,denied and missing*,tampered locator*,resealed malformed*,unified adapter*,review exclusion*,saved projection*,shared page*,existing Poppler*,invalid PDF*,changed saved*,unified selector counts*,unified selector uses raw*,selected recognized paragraph*,explicit upload bridge*,selected graph occurrence*,UTF-8 tiles*")
