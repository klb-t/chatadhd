# loom-server — REST/SSE HTTP server exposing the loom.h C ABI to the web UI.
#
# Built only when LOOM_BUILD_SERVER is ON (the top-level CMakeLists.txt adds
# this subdirectory conditionally). Links loom_core directly (not the shared
# libloom.so). Existing routes use include/loom/loom.h; server-owned UI
# adapters also borrow its authoritative static Runtime declaration from
# src/capi/context.h. They create no second runtime and add no C ABI exports.

add_executable(loom-server
  src/main.cpp
  src/app.cpp
)

target_include_directories(loom-server PRIVATE ${LOOM_ROOT}/include ${LOOM_ROOT}/server/src)
target_include_directories(loom-server SYSTEM PRIVATE ${LOOM_TP}/cpp-httplib ${LOOM_TP}/nlohmann)

target_link_libraries(loom-server PRIVATE loom_core loom_warnings Threads::Threads)

if(LOOM_HAVE_OPENSSL)
  target_link_libraries(loom-server PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  target_compile_definitions(loom-server PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT=1)
endif()

if(WIN32)
  target_link_libraries(loom-server PRIVATE ws2_32 crypt32)
endif()

set_target_properties(loom-server PROPERTIES OUTPUT_NAME "loom-server")
