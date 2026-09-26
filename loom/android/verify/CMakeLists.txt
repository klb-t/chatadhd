# loom/android/verify/CMakeLists.txt
#
# Host-only verification for the Android JNI glue, usable without an Android
# SDK/NDK: compiles the real app/src/main/cpp/loom_jni.cpp against the JDK's
# own jni.h and a prebuilt libloom.so, then runs a plain-`javac` Java smoke
# test (loomverify.HostSmokeTest) against it as one CTest.
#
# This is a separate, standalone CMake project (its own configure/build
# tree) so it never touches loom/CMakeLists.txt, which wave-2 areas own.
#
#   cmake -S loom/android/verify -B loom/android/verify/build \
#         -DLOOM_LIBRARY=loom/build/dev/libloom.so
#   cmake --build loom/android/verify/build
#   ctest --test-dir loom/android/verify/build --output-on-failure
#
# Silently configures with no test added (BUILD_SUCCESSFUL, zero tests) when
# no JDK or no libloom.so is found, so it never breaks a machine that has
# neither — see the two `return()`s below.
cmake_minimum_required(VERSION 3.20)
project(loom_jni_host_verify LANGUAGES CXX)

get_filename_component(LOOM_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LOOM_JNI_CPP "${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/loom_jni.cpp")

find_package(JNI QUIET)
find_program(LOOM_JAVAC_EXE javac)
find_program(LOOM_JAVA_EXE java)

if(NOT JNI_INCLUDE_DIRS OR NOT LOOM_JAVAC_EXE OR NOT LOOM_JAVA_EXE)
  message(STATUS "loom_jni_host_verify: no usable JDK found (JNI=${JNI_FOUND}, javac=${LOOM_JAVAC_EXE}, "
    "java=${LOOM_JAVA_EXE}) - host JNI smoke test disabled. Set JAVA_HOME to enable it.")
  return()
endif()

# Prefer an explicit -DLOOM_LIBRARY=..., else look for a libloom.so this repo
# already built (LOOM_SHARED=ON: the dev/release/vendored presets all can
# produce one).
if(NOT LOOM_LIBRARY)
  foreach(_preset dev release vendored)
    set(_candidate "${LOOM_ROOT_DIR}/build/${_preset}/libloom.so")
    if(EXISTS "${_candidate}")
      set(LOOM_LIBRARY "${_candidate}")
      break()
    endif()
  endforeach()
endif()
if(NOT LOOM_LIBRARY OR NOT EXISTS "${LOOM_LIBRARY}")
  message(STATUS "loom_jni_host_verify: no libloom.so found (looked for -DLOOM_LIBRARY=... and "
    "${LOOM_ROOT_DIR}/build/{dev,release,vendored}/libloom.so) - host JNI smoke test disabled. "
    "Build loom with -DLOOM_SHARED=ON first (see loom/README.md).")
  return()
endif()
get_filename_component(LOOM_LIBRARY_DIR "${LOOM_LIBRARY}" DIRECTORY)
message(STATUS "loom_jni_host_verify: using ${LOOM_LIBRARY}")

# ── the JNI glue itself, unmodified, linked against libloom.so ──────────
add_library(loom_jni_host SHARED ${LOOM_JNI_CPP})
target_include_directories(loom_jni_host PRIVATE ${JNI_INCLUDE_DIRS} "${LOOM_ROOT_DIR}/include")
target_link_libraries(loom_jni_host PRIVATE "${LOOM_LIBRARY}")
set_target_properties(loom_jni_host PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  BUILD_RPATH "${LOOM_LIBRARY_DIR}"
  INSTALL_RPATH "${LOOM_LIBRARY_DIR}")
# __ANDROID__ is not defined here, so loom_jni.cpp's log_line() falls back
# to stderr and no liblog is needed (see the #if in loom_jni.cpp).

# ── plain-javac Java sources (LoomNative/LoomCallbacks/LoomHttp mirrors + the test) ──
file(GLOB_RECURSE LOOM_VERIFY_JAVA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/java/*.java")
set(LOOM_VERIFY_CLASSES_DIR "${CMAKE_CURRENT_BINARY_DIR}/classes")
add_custom_command(
  OUTPUT "${LOOM_VERIFY_CLASSES_DIR}/.built"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${LOOM_VERIFY_CLASSES_DIR}"
  COMMAND ${LOOM_JAVAC_EXE} -encoding UTF-8 -d "${LOOM_VERIFY_CLASSES_DIR}" ${LOOM_VERIFY_JAVA_SOURCES}
  COMMAND ${CMAKE_COMMAND} -E touch "${LOOM_VERIFY_CLASSES_DIR}/.built"
  DEPENDS ${LOOM_VERIFY_JAVA_SOURCES}
  COMMENT "loom_jni_host_verify: javac"
  VERBATIM)
add_custom_target(loom_jni_host_verify_classes ALL DEPENDS "${LOOM_VERIFY_CLASSES_DIR}/.built")

enable_testing()
add_test(NAME jni_host_smoke_test
  COMMAND ${LOOM_JAVA_EXE} -Dfile.encoding=UTF-8
    -Djava.library.path=$<TARGET_FILE_DIR:loom_jni_host>
    -cp "${LOOM_VERIFY_CLASSES_DIR}"
    loomverify.HostSmokeTest $<TARGET_FILE:loom_jni_host>)
set_tests_properties(jni_host_smoke_test PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${LOOM_LIBRARY_DIR}"
  TIMEOUT 60)
add_dependencies(loom_jni_host_verify_classes loom_jni_host)
