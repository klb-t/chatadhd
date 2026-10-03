# loom/android/app/src/main/cpp/CMakeLists.txt
#
# Builds libloom_jni.so: the Loom kernel (loom_core, statically linked, with
# the vendored SQLite amalgamation and OpenSSL off — HTTP is injected from
# Kotlin instead) plus the JNI glue in loom_jni.cpp. Consumed by Gradle via
# app/build.gradle.kts's externalNativeBuild.cmake block.
cmake_minimum_required(VERSION 3.22)
project(loom_jni LANGUAGES C CXX)

# The top-level Loom CMakeLists.txt (loom/CMakeLists.txt) five levels up.
get_filename_component(LOOM_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

if(NOT EXISTS "${LOOM_ROOT_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "loom_jni: expected the Loom core at ${LOOM_ROOT_DIR} (layout changed?)")
endif()

# Android-appropriate options for the core, set before add_subdirectory so
# they take effect as this subproject's cache values (see loom/CMakeLists.txt
# for what each one controls). HTTP goes through LoomHttp.kt/HttpURLConnection
# instead of cpp-httplib+OpenSSL, so OpenSSL stays off on this platform.
set(LOOM_USE_SYSTEM_SQLITE OFF CACHE BOOL "Android: no system SQLite; vendor it" FORCE)
set(LOOM_WITH_OPENSSL OFF CACHE STRING "Android: HTTP/crypto injected from Kotlin, not linked in" FORCE)
set(LOOM_BUILD_TESTS OFF CACHE BOOL "Android: no test targets in the on-device build" FORCE)
set(LOOM_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LOOM_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LOOM_SHARED OFF CACHE BOOL "Android: loom_jni.so is the only shared object; loom_core links into it" FORCE)
set(LOOM_WERROR OFF CACHE BOOL "Do not fail an NDK build over a warning the desktop build already gates" FORCE)

add_subdirectory(${LOOM_ROOT_DIR} loom_core_build)

add_library(loom_jni SHARED loom_jni.cpp)
target_link_libraries(loom_jni PRIVATE loom_core)
if(ANDROID)
  target_link_libraries(loom_jni PRIVATE log)
endif()
set_target_properties(loom_jni PROPERTIES CXX_VISIBILITY_PRESET default)
