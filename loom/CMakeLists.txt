cmake_minimum_required(VERSION 3.24)

project(loom VERSION 0.1.0 DESCRIPTION "Loom kernel library (ChatADHD engine)" LANGUAGES C CXX)

# ── Language / global settings ───────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_program(LOOM_CCACHE ccache)
if(LOOM_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  set(CMAKE_C_COMPILER_LAUNCHER ${LOOM_CCACHE})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${LOOM_CCACHE})
  message(STATUS "loom: using ccache (${LOOM_CCACHE})")
endif()

# ── Options ─────────────────────────────────────────────────────────
find_package(SQLite3 QUIET)
set(_loom_sqlite_default OFF)
if(SQLite3_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
  set(_loom_sqlite_default ON)
endif()
option(LOOM_USE_SYSTEM_SQLITE "Link the system libsqlite3 instead of the vendored amalgamation" ${_loom_sqlite_default})
set(LOOM_WITH_OPENSSL "AUTO" CACHE STRING "Use OpenSSL (AES-GCM, HTTPS): ON, OFF or AUTO")
set_property(CACHE LOOM_WITH_OPENSSL PROPERTY STRINGS ON OFF AUTO)
option(LOOM_BUILD_TESTS "Build unit + compat tests" ON)
option(LOOM_BUILD_CLI "Build the loom CLI" OFF)
option(LOOM_BUILD_SERVER "Build the loom HTTP server (wave 3)" OFF)
option(LOOM_SHARED "Build libloom shared library exporting only the C API" OFF)
option(LOOM_WERROR "Treat warnings in Loom sources as errors" OFF)

set(LOOM_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(LOOM_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(LOOM_TP ${LOOM_ROOT}/third_party)

find_package(Threads REQUIRED)

# ── Warnings (Loom code only, never third_party) ─────────────────────
add_library(loom_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(loom_warnings INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
      -Wold-style-cast -Wcast-align -Woverloaded-virtual -Wnull-dereference
      -Wimplicit-fallthrough -Wno-unused-parameter>)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Wave-2 stubs keep their wiring references (cfg_, db_, http_, ...) before
    # the implementations use them. Remove this once no "STUB: wave2" is left.
    target_compile_options(loom_warnings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-private-field>)
  endif()
  if(LOOM_WERROR)
    target_compile_options(loom_warnings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-Werror>)
  endif()
endif()

# ── SQLite ──────────────────────────────────────────────────────────
if(LOOM_USE_SYSTEM_SQLITE)
  find_package(SQLite3 REQUIRED)
  add_library(loom_sqlite INTERFACE)
  target_link_libraries(loom_sqlite INTERFACE SQLite::SQLite3)
  message(STATUS "loom: SQLite = system ${SQLite3_VERSION}")
else()
  add_library(loom_sqlite3_amalgamation STATIC ${LOOM_TP}/sqlite/sqlite3.c)
  set_target_properties(loom_sqlite3_amalgamation PROPERTIES LINKER_LANGUAGE C)
  target_include_directories(loom_sqlite3_amalgamation SYSTEM PUBLIC ${LOOM_TP}/sqlite)
  target_compile_definitions(loom_sqlite3_amalgamation PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_ENABLE_MATH_FUNCTIONS
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    HAVE_USLEEP=1)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(loom_sqlite3_amalgamation PRIVATE -w)
  endif()
  target_link_libraries(loom_sqlite3_amalgamation PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
  if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(loom_sqlite3_amalgamation PUBLIC m)
  endif()
  add_library(loom_sqlite INTERFACE)
  target_link_libraries(loom_sqlite INTERFACE loom_sqlite3_amalgamation)
  target_compile_definitions(loom_sqlite INTERFACE LOOM_VENDORED_SQLITE=1)
  message(STATUS "loom: SQLite = vendored amalgamation 3.47.2")
endif()

# ── miniz (ZIP) ─────────────────────────────────────────────────────
add_library(loom_miniz STATIC ${LOOM_TP}/miniz/miniz.c)
target_include_directories(loom_miniz SYSTEM PUBLIC ${LOOM_TP}/miniz)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(loom_miniz PRIVATE -w)
endif()

# ── OpenSSL ─────────────────────────────────────────────────────────
set(LOOM_HAVE_OPENSSL OFF)
if(NOT LOOM_WITH_OPENSSL STREQUAL "OFF")
  if(LOOM_WITH_OPENSSL STREQUAL "ON")
    find_package(OpenSSL REQUIRED)
  else()
    find_package(OpenSSL QUIET)
  endif()
  if(OpenSSL_FOUND)
    set(LOOM_HAVE_OPENSSL ON)
  endif()
endif()
message(STATUS "loom: OpenSSL = ${LOOM_HAVE_OPENSSL}")

# ── loom_core (static) ──────────────────────────────────────────────
file(GLOB_RECURSE LOOM_CORE_SOURCES CONFIGURE_DEPENDS ${LOOM_ROOT}/src/*.cpp)
add_library(loom_core STATIC ${LOOM_CORE_SOURCES})
target_include_directories(loom_core
  PUBLIC ${LOOM_ROOT}/include
  PRIVATE ${LOOM_ROOT}/src)
target_include_directories(loom_core SYSTEM PUBLIC ${LOOM_TP}/nlohmann)
target_include_directories(loom_core SYSTEM PRIVATE ${LOOM_TP}/cpp-httplib)
target_link_libraries(loom_core
  PUBLIC Threads::Threads
  PRIVATE loom_warnings loom_sqlite loom_miniz)
target_compile_definitions(loom_core PUBLIC
  LOOM_VERSION_STRING="${PROJECT_VERSION}"
  JSON_USE_IMPLICIT_CONVERSIONS=1)
target_compile_definitions(loom_core PRIVATE LOOM_BUILDING=1)
if(LOOM_HAVE_OPENSSL)
  target_link_libraries(loom_core PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  target_compile_definitions(loom_core PRIVATE LOOM_HAVE_OPENSSL=1 CPPHTTPLIB_OPENSSL_SUPPORT=1)
endif()
if(ANDROID)
  target_link_libraries(loom_core PRIVATE log)
endif()
# Tests and tools that include sqlite3.h directly need the same headers.
add_library(loom::core ALIAS loom_core)

# ── libloom (shared, C API only) ────────────────────────────────────
if(LOOM_SHARED)
  add_library(loom SHARED ${LOOM_ROOT}/src/capi/shared_anchor.c)
  target_link_libraries(loom PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,loom_core>")
  target_include_directories(loom PUBLIC ${LOOM_ROOT}/include)
  set_target_properties(loom PROPERTIES
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
    target_link_options(loom PRIVATE -Wl,--no-undefined)
  endif()
endif()

# ── Wave 3 entry points (only if present) ───────────────────────────
if(LOOM_BUILD_CLI AND EXISTS ${LOOM_ROOT}/cli/CMakeLists.txt)
  add_subdirectory(cli)
endif()
if(LOOM_BUILD_SERVER AND EXISTS ${LOOM_ROOT}/server/CMakeLists.txt)
  add_subdirectory(server)
endif()

# ── Tests ───────────────────────────────────────────────────────────
if(LOOM_BUILD_TESTS)
  enable_testing()

  file(GLOB LOOM_TEST_SOURCES CONFIGURE_DEPENDS ${LOOM_ROOT}/tests/*.cpp)
  add_executable(loom_tests ${LOOM_TEST_SOURCES})
  target_include_directories(loom_tests SYSTEM PRIVATE ${LOOM_TP}/doctest)
  target_include_directories(loom_tests PRIVATE ${LOOM_ROOT}/src ${LOOM_ROOT}/tests)
  target_link_libraries(loom_tests PRIVATE loom_core loom_sqlite loom_warnings)
  target_compile_definitions(loom_tests PRIVATE
    LOOM_TEST_FIXTURES="${LOOM_ROOT}/tests/fixtures")

  # One ctest entry per test source file so `ctest -j` parallelises and
  # new wave-2 test files show up without editing this file.
  foreach(_src ${LOOM_TEST_SOURCES})
    get_filename_component(_name ${_src} NAME_WE)
    if(_name STREQUAL "main")
      continue()
    endif()
    add_test(NAME unit.${_name} COMMAND loom_tests --source-file=*/${_name}.cpp --no-intro=true)
  endforeach()

  file(GLOB LOOM_COMPAT_SOURCES CONFIGURE_DEPENDS ${LOOM_ROOT}/tests/compat/*.cpp)
  list(FILTER LOOM_COMPAT_SOURCES EXCLUDE REGEX "/candidate_graph_native_tool\\.cpp$")
  add_executable(loom_compat_tool ${LOOM_COMPAT_SOURCES})
  target_include_directories(loom_compat_tool PRIVATE ${LOOM_ROOT}/src ${LOOM_ROOT}/tests/compat)
  target_link_libraries(loom_compat_tool PRIVATE loom_core loom_sqlite loom_warnings)

  add_executable(loom_candidate_graph_native_tool ${LOOM_ROOT}/tests/compat/candidate_graph_native_tool.cpp)
  target_link_libraries(loom_candidate_graph_native_tool PRIVATE loom_core loom_sqlite loom_warnings)

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    set(_loom_compat_env
      "LOOM_COMPAT_TOOL=$<TARGET_FILE:loom_compat_tool>"
      "LOOM_CANDIDATE_GRAPH_NATIVE_TOOL=$<TARGET_FILE:loom_candidate_graph_native_tool>"
      "PYTHONPATH=${LOOM_REPO_ROOT}"
      "PYTHONDONTWRITEBYTECODE=1")
    if(LOOM_SHARED)
      list(APPEND _loom_compat_env "LOOM_LIBRARY=$<TARGET_FILE:loom>")
    endif()
    file(GLOB LOOM_COMPAT_PY CONFIGURE_DEPENDS ${LOOM_ROOT}/tests/compat/test_*.py)
    foreach(_py ${LOOM_COMPAT_PY})
      get_filename_component(_name ${_py} NAME_WE)
      add_test(NAME compat.${_name} COMMAND ${Python3_EXECUTABLE} ${_py})
      set_tests_properties(compat.${_name} PROPERTIES ENVIRONMENT "${_loom_compat_env}" TIMEOUT 600)
    endforeach()
    # Offline research mechanism/protocol checks, separate from quality gates.
    # These exercise no model service and never open the real holdout key.
    add_test(NAME research.structure
      COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s ${LOOM_ROOT}/tools/structure -p "test_*.py")
    add_test(NAME research.independent_protocol
      COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s ${LOOM_ROOT}/tools/eval -p "test_independent_cases.py")
    add_test(NAME research.graph_native_protocol
      COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s ${LOOM_ROOT}/tools/eval -p "test_graph_native_eval.py")
    add_test(NAME research.candidate_graph_protocol
      COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s ${LOOM_ROOT}/tools/eval -p "test_candidate_graph_eval.py")
    set_tests_properties(research.structure research.independent_protocol research.graph_native_protocol research.candidate_graph_protocol PROPERTIES
      ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1" TIMEOUT 60)
  else()
    message(WARNING "loom: Python3 not found - compat tests disabled")
  endif()

  # loom-server API smoke test (server/tests/test_server_smoke.py): only
  # registered when the server is actually built and Python's `requests` is
  # available at ctest time; the script itself SKIPs (exit 0) otherwise.
  if(LOOM_BUILD_SERVER AND TARGET loom-server AND Python3_Interpreter_FOUND)
    add_test(NAME server.smoke COMMAND ${Python3_EXECUTABLE} ${LOOM_ROOT}/server/tests/test_server_smoke.py)
    set_tests_properties(server.smoke PROPERTIES
      ENVIRONMENT "LOOM_SERVER_BIN=$<TARGET_FILE:loom-server>"
      TIMEOUT 60)
  endif()
endif()
