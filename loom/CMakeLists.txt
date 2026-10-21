cmake_minimum_required(VERSION 3.24)

# The one source of the version (R36): loom/VERSION, "x.yy.zz" (zero-padded, so versions sort as text). CMake wants
# plain integers, so the project version drops the padding; LOOM_VERSION_STRING keeps the text as written.
file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" LOOM_VERSION_TEXT)
string(STRIP "${LOOM_VERSION_TEXT}" LOOM_VERSION_TEXT)
if(NOT LOOM_VERSION_TEXT MATCHES "^([0-9]+)\\.([0-9][0-9])\\.([0-9][0-9])$")
  message(FATAL_ERROR "loom/VERSION must read x.yy.zz (got '${LOOM_VERSION_TEXT}')")
endif()
math(EXPR LOOM_VERSION_MINOR_N "${CMAKE_MATCH_2}")
math(EXPR LOOM_VERSION_PATCH_N "${CMAKE_MATCH_3}")
set(LOOM_VERSION_NUMERIC "${CMAKE_MATCH_1}.${LOOM_VERSION_MINOR_N}.${LOOM_VERSION_PATCH_N}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/VERSION")

project(loom VERSION ${LOOM_VERSION_NUMERIC} DESCRIPTION "Loom kernel library (ChatADHD engine)" LANGUAGES C CXX)

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
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # GCC 12-14 at -O2 reports false positives whose location is inside
      # system headers (nlohmann/json.hpp, <functional>, <string>, <optional>)
      # after inlining into our code, so SYSTEM include dirs cannot hide them.
      # Keep every other warning fatal; these two stay visible as warnings.
      target_compile_options(loom_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=null-dereference -Wno-error=maybe-uninitialized>)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      # nlohmann/json 3.11.3 (pinned) probes is_comparable<std::equal_to<>, string, json_pointer> for
      # at()/contains()/operator[] with a json_pointer. libc++ evaluates equal_to<>'s noexcept-spec there, which selects
      # json_pointer's own deprecated operator==(json_pointer, string) overload and reports it at <__functional/operations.h>
      # (libstdc++ never does). The warning is inside the library's trait probe, not in our code, and the vendored
      # header is pinned by sha256, so it stays a visible warning instead of a hard error. Every other warning stays fatal.
      include(CheckCXXSourceCompiles)
      check_cxx_source_compiles("#include <cstddef>\n#ifndef _LIBCPP_VERSION\n#error not libc++\n#endif\nint main(){}" LOOM_USING_LIBCXX)
      if(LOOM_USING_LIBCXX)
        target_compile_options(loom_warnings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=deprecated-declarations>)
      endif()
    endif()
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

# ── Post-quantum / password-hash / curve25519 (vendored, portable C) ──
# mlkem-native, mldsa-native (FIPS 203/204), phc-winner-argon2 (RFC 9106) and
# Monocypher (X25519/Ed25519). No assembly or arch backends: portable C only.
# Warnings are relaxed for this target only (vendored code is not edited).
set(LOOM_PQC_SOURCES
  ${LOOM_TP}/mlkem-native/mlkem/mlkem_native.c
  ${LOOM_TP}/mldsa-native/mldsa/mldsa_native.c
  ${LOOM_TP}/argon2/src/argon2.c ${LOOM_TP}/argon2/src/core.c
  ${LOOM_TP}/argon2/src/encoding.c ${LOOM_TP}/argon2/src/ref.c
  ${LOOM_TP}/argon2/src/blake2/blake2b.c
  ${LOOM_TP}/monocypher/monocypher.c ${LOOM_TP}/monocypher/monocypher-ed25519.c
  ${LOOM_ROOT}/src/crypto/vendor/pq_glue.c)
add_library(loom_pqc STATIC ${LOOM_PQC_SOURCES})
set_source_files_properties(${LOOM_TP}/mlkem-native/mlkem/mlkem_native.c PROPERTIES
  COMPILE_DEFINITIONS "MLK_CONFIG_PARAMETER_SET=768")
set_source_files_properties(${LOOM_TP}/mldsa-native/mldsa/mldsa_native.c PROPERTIES
  COMPILE_DEFINITIONS "MLD_CONFIG_PARAMETER_SET=65")
set_source_files_properties(${LOOM_ROOT}/src/crypto/vendor/pq_glue.c PROPERTIES
  COMPILE_DEFINITIONS "MLK_CONFIG_PARAMETER_SET=768;MLD_CONFIG_PARAMETER_SET=65")
target_compile_definitions(loom_pqc PRIVATE ARGON2_NO_THREADS=1)
target_include_directories(loom_pqc PRIVATE
  ${LOOM_TP}/mlkem-native/mlkem ${LOOM_TP}/mldsa-native/mldsa
  ${LOOM_TP}/argon2 ${LOOM_TP}/argon2/src ${LOOM_TP}/monocypher)
target_include_directories(loom_pqc SYSTEM PUBLIC ${LOOM_TP}/argon2 ${LOOM_TP}/monocypher)
set_target_properties(loom_pqc PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(loom_pqc PRIVATE -w)
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
  PRIVATE loom_warnings loom_sqlite loom_miniz loom_pqc)
target_compile_definitions(loom_core PUBLIC
  LOOM_VERSION_STRING="${LOOM_VERSION_TEXT}"
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

  # C-ABI lifecycle probe (init/subscribe/shutdown loop) in plain C against the public header only. Runs in every
  # preset: against libloom.so when it is built, else against loom_core (same code, static). It exists because a
  # destruction-order bug (R30) crashed only under libc++, the Android standard library; see the `libcxx` preset.
  add_executable(loom_shutdown_probe ${LOOM_ROOT}/tests/shutdown_cabi_probe.c)
  target_include_directories(loom_shutdown_probe PRIVATE ${LOOM_ROOT}/include)
  set_target_properties(loom_shutdown_probe PROPERTIES LINKER_LANGUAGE CXX)
  if(LOOM_SHARED)
    target_link_libraries(loom_shutdown_probe PRIVATE loom)
  else()
    target_link_libraries(loom_shutdown_probe PRIVATE loom_core loom_sqlite)
  endif()
  add_test(NAME capi.shutdown_cycle
    COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:loom_shutdown_probe> -DDIR=${CMAKE_CURRENT_BINARY_DIR}/capi_shutdown_data
      -P ${LOOM_ROOT}/tests/run_shutdown_probe.cmake)
  set_tests_properties(capi.shutdown_cycle PROPERTIES TIMEOUT 120)

  file(GLOB LOOM_COMPAT_SOURCES CONFIGURE_DEPENDS ${LOOM_ROOT}/tests/compat/*.cpp)
  list(FILTER LOOM_COMPAT_SOURCES EXCLUDE REGEX "/candidate_graph_native_tool\\.cpp$")
  add_executable(loom_compat_tool ${LOOM_COMPAT_SOURCES})
  target_include_directories(loom_compat_tool PRIVATE ${LOOM_ROOT}/src ${LOOM_ROOT}/tests/compat)
  target_link_libraries(loom_compat_tool PRIVATE loom_core loom_sqlite loom_warnings)

  add_executable(loom_candidate_graph_native_tool ${LOOM_ROOT}/tests/compat/candidate_graph_native_tool.cpp)
  target_link_libraries(loom_candidate_graph_native_tool PRIVATE loom_core loom_sqlite loom_warnings)

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    # Test imports must not depend on the directory from which ctest is run or
    # on a caller-provided PYTHONPATH (notably for namespace package `loom`).
    set(_loom_python_env
      "PYTHONPATH=${LOOM_REPO_ROOT}"
      "PYTHONDONTWRITEBYTECODE=1")
    set(_loom_compat_env ${_loom_python_env}
      "LOOM_COMPAT_TOOL=$<TARGET_FILE:loom_compat_tool>"
      "LOOM_CANDIDATE_GRAPH_NATIVE_TOOL=$<TARGET_FILE:loom_candidate_graph_native_tool>")
    if(LOOM_SHARED)
      list(APPEND _loom_compat_env "LOOM_LIBRARY=$<TARGET_FILE:loom>")
    endif()
    file(GLOB LOOM_COMPAT_PY CONFIGURE_DEPENDS ${LOOM_ROOT}/tests/compat/test_*.py)
    foreach(_py ${LOOM_COMPAT_PY})
      get_filename_component(_name ${_py} NAME_WE)
      add_test(NAME compat.${_name} COMMAND ${Python3_EXECUTABLE} ${_py})
      set_tests_properties(compat.${_name} PROPERTIES ENVIRONMENT "${_loom_compat_env}" TIMEOUT 600)
    endforeach()
    # External-resource mechanisms use the public packet/native-store boundary.
    if(LOOM_SHARED)
      add_test(NAME resource_graph.contract
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
          -s ${LOOM_ROOT}/tests/resource_graph -p "test_*.py")
      set_tests_properties(resource_graph.contract PROPERTIES
        ENVIRONMENT "${_loom_python_env};LOOM_LIBRARY=$<TARGET_FILE:loom>"
        TIMEOUT 600)
    endif()
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
    add_test(NAME research.contracts
      COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s ${LOOM_ROOT}/tools/contracts -p "test_*.py")
    add_test(NAME research.seeding
      COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s ${LOOM_ROOT}/tools/seeding -p "test_*.py")
    # M1-D1: the settings each flow declares it reads (actions.pack reads) are the settings its executor files read,
    # and settings_fields.pack human.flows agree with them (exceptions with reasons in tools/check_action_reads_allow.json).
    add_test(NAME data.check_action_reads
      COMMAND ${Python3_EXECUTABLE} -I ${LOOM_ROOT}/tools/check_action_reads.py)
    set_tests_properties(data.check_action_reads PROPERTIES TIMEOUT 60)
    # The check itself fails on mutated copies of the data (a lost read, a false role, a non-read literal ...).
    add_test(NAME data.check_action_reads_selftest
      COMMAND ${Python3_EXECUTABLE} -I ${LOOM_ROOT}/tools/check_action_reads.py --self-test)
    set_tests_properties(data.check_action_reads_selftest PROPERTIES TIMEOUT 120)
    set_tests_properties(research.independent_protocol research.graph_native_protocol research.candidate_graph_protocol research.seeding PROPERTIES
      ENVIRONMENT "${_loom_python_env}" TIMEOUT 60)
    # The contracts suite (~210 cases) takes ~31 s alone on a dev build; under ASan with the other
    # suites running in parallel it reached the 60 s hang timeout on CI (run 38788701397). Its hang
    # timeout is wider, like research.structure's; no case, threshold or assertion changes.
    set_tests_properties(research.contracts PROPERTIES
      ENVIRONMENT "${_loom_python_env}" TIMEOUT 300)
    # The structure suite also runs the research programme's native-execution
    # cases (otherwise skipped) and has grown to ~1300 cases (~70 s alone), so
    # its hang timeout is wider; no case, threshold or assertion changes.
    set_tests_properties(research.structure PROPERTIES
      ENVIRONMENT "${_loom_python_env};LOOM_CANDIDATE_GRAPH_NATIVE_TOOL=$<TARGET_FILE:loom_candidate_graph_native_tool>"
      TIMEOUT 300)
    # X-Port archive-extract tool (loom/tools/extract): stdlib-only unittest suite on synthetic
    # fixtures (loom/tests/fixtures/extract, exports, import); no network (socket guard in setUp);
    # optional deps (curses, libloom) make their tests SKIP with a visible reason, never fail.
    add_test(NAME tools.extract
      COMMAND ${Python3_EXECUTABLE} -I -m unittest discover
        -s ${LOOM_ROOT}/tools/extract/tests -p "test_*.py")
    # libloom exists only with LOOM_SHARED (not in asan/tsan/vendored): without it the libloom tests SKIP, as documented.
    set(_loom_extract_env ${_loom_python_env} "TERM=xterm-256color")
    if(LOOM_SHARED)
      list(APPEND _loom_extract_env "LOOM_LIBRARY=$<TARGET_FILE:loom>")
    endif()
    set_tests_properties(tools.extract PROPERTIES ENVIRONMENT "${_loom_extract_env}" TIMEOUT 600)
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
      # Slow, not hung: measured 35 s under ASan+UBSan on a shared 4-core box (dev: a few s); CI asan on 2
      # shared cores timed out at 60 s. Timeout widened, no assertion changed.
      TIMEOUT 300)
    add_test(NAME server.conversation_view
      COMMAND ${Python3_EXECUTABLE} ${LOOM_ROOT}/server/tests/test_conversation_view_server.py)
    set_tests_properties(server.conversation_view PROPERTIES
      ENVIRONMENT "LOOM_SERVER_BIN=$<TARGET_FILE:loom-server>;PYTHONDONTWRITEBYTECODE=1"
      TIMEOUT 90)
    add_test(NAME server.chat_usage_guard
      COMMAND ${Python3_EXECUTABLE} ${LOOM_ROOT}/server/tests/test_chat_usage_guard_server.py)
    set_tests_properties(server.chat_usage_guard PROPERTIES
      ENVIRONMENT "LOOM_SERVER_BIN=$<TARGET_FILE:loom-server>;PYTHONDONTWRITEBYTECODE=1"
      # Slow, not hung: measured 77 s under ASan+UBSan on a shared 4-core box (CI asan timed out at 90 s).
      TIMEOUT 600)
    add_test(NAME server.accounts
      COMMAND ${Python3_EXECUTABLE} ${LOOM_ROOT}/server/tests/test_server_accounts.py)
    set_tests_properties(server.accounts PROPERTIES
      ENVIRONMENT "LOOM_SERVER_BIN=$<TARGET_FILE:loom-server>;PYTHONDONTWRITEBYTECODE=1"
      TIMEOUT 240)
    add_test(NAME server.security
      COMMAND ${Python3_EXECUTABLE} ${LOOM_ROOT}/server/tests/test_server_security.py)
    set_tests_properties(server.security PROPERTIES
      ENVIRONMENT "LOOM_SERVER_BIN=$<TARGET_FILE:loom-server>;PYTHONDONTWRITEBYTECODE=1"
      TIMEOUT 300)
    add_test(NAME server.sse
      COMMAND ${Python3_EXECUTABLE} ${LOOM_ROOT}/server/tests/test_server_sse.py)
    set_tests_properties(server.sse PROPERTIES
      ENVIRONMENT "LOOM_SERVER_BIN=$<TARGET_FILE:loom-server>;PYTHONDONTWRITEBYTECODE=1"
      TIMEOUT 120)
    add_test(NAME server.chat_active_task
      COMMAND ${Python3_EXECUTABLE} ${LOOM_ROOT}/server/tests/test_chat_active_task_server.py)
    set_tests_properties(server.chat_active_task PROPERTIES
      ENVIRONMENT "LOOM_SERVER_BIN=$<TARGET_FILE:loom-server>;PYTHONDONTWRITEBYTECODE=1"
      TIMEOUT 90)
  endif()
endif()
