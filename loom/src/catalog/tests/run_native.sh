#!/usr/bin/env bash
# Compile the owned catalog regressions against an already completed Linux
# static-core build. This runner never generates, rewrites or cleans the build.
set -euo pipefail

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'USAGE'
Usage: run_native.sh [completed-build-directory] [output-directory] [doctest options...]

Defaults: loom/build/dev; a fresh directory under /tmp.
Set CXX to choose a compiler; otherwise the build's compiler is used when known.
Compilation and test logs remain in the output directory. No cleanup runs.
USAGE
  exit 0
fi

catalog_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
catalog_loom_dir="$(cd -- "${catalog_script_dir}/../../.." && pwd -P)"
catalog_build_input="${1:-${catalog_loom_dir}/build/dev}"
if [[ ! -d "${catalog_build_input}" ]]; then
  printf 'Completed build directory does not exist: %s\n' "${catalog_build_input}" >&2
  exit 1
fi
catalog_build_dir="$(cd -- "${catalog_build_input}" && pwd -P)"
catalog_core="${catalog_build_dir}/libloom_core.a"
catalog_miniz="${catalog_build_dir}/libloom_miniz.a"
for catalog_archive in "${catalog_core}" "${catalog_miniz}"; do
  if [[ ! -f "${catalog_archive}" ]]; then
    printf 'Required completed-build archive is missing: %s\n' "${catalog_archive}" >&2
    exit 1
  fi
done

catalog_cached_compiler=""
catalog_system_sqlite=""
catalog_openssl_mode=""
catalog_ssl_library=""
catalog_crypto_library=""
catalog_cache="${catalog_build_dir}/CMakeCache.txt"
if [[ -f "${catalog_cache}" ]]; then
  while IFS= read -r catalog_line; do
    case "${catalog_line}" in
      CMAKE_CXX_COMPILER:FILEPATH=*) catalog_cached_compiler="${catalog_line#*=}" ;;
      LOOM_USE_SYSTEM_SQLITE:BOOL=*) catalog_system_sqlite="${catalog_line#*=}" ;;
      LOOM_WITH_OPENSSL:STRING=*) catalog_openssl_mode="${catalog_line#*=}" ;;
      OPENSSL_SSL_LIBRARY:FILEPATH=*) catalog_ssl_library="${catalog_line#*=}" ;;
      OPENSSL_CRYPTO_LIBRARY:FILEPATH=*) catalog_crypto_library="${catalog_line#*=}" ;;
    esac
  done < "${catalog_cache}"
fi
catalog_compiler="${CXX:-${catalog_cached_compiler:-c++}}"
catalog_include_flags=(-I"${catalog_loom_dir}/include"
  -isystem "${catalog_loom_dir}/third_party/nlohmann"
  -isystem "${catalog_loom_dir}/third_party/doctest")
catalog_sqlite_archive="${catalog_build_dir}/libloom_sqlite3_amalgamation.a"
if [[ "${catalog_system_sqlite}" != "ON" && -f "${catalog_sqlite_archive}" ]]; then
  catalog_sqlite_flags=("${catalog_sqlite_archive}")
  catalog_include_flags+=(-DLOOM_VENDORED_SQLITE=1 -isystem "${catalog_loom_dir}/third_party/sqlite")
else
  catalog_sqlite_flags=(-lsqlite3)
fi
catalog_tls_flags=()
if [[ "${catalog_openssl_mode}" != "OFF" ]]; then
  if [[ -f "${catalog_ssl_library}" && -f "${catalog_crypto_library}" ]]; then
    catalog_tls_flags=("${catalog_ssl_library}" "${catalog_crypto_library}")
  elif [[ ! -f "${catalog_cache}" ]]; then
    # A copied completed build may lack its cache. Standard development
    # builds use OpenSSL; its pkg-config flags avoid hardcoded library paths.
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists openssl; then
      read -r -a catalog_tls_flags <<< "$(pkg-config --libs openssl)"
    else
      catalog_tls_flags=(-lssl -lcrypto)
    fi
  fi
fi

if [[ -n "${2:-}" ]]; then
  catalog_output_dir="$(realpath -m -- "${2}")"
  case "${catalog_output_dir}/" in
    "${catalog_build_dir}/"*)
      printf 'Output directory must be outside the completed build: %s\n' "${catalog_output_dir}" >&2
      exit 1
      ;;
  esac
  mkdir -p -- "${catalog_output_dir}"
else
  catalog_output_dir="$(mktemp -d /tmp/loom-catalog-native.XXXXXXXX)"
fi
catalog_compile_log="${catalog_output_dir}/compile.log"
catalog_test_log="${catalog_output_dir}/tests.log"
catalog_test_object="${catalog_output_dir}/test_semantic_candidates.o"
catalog_executable="${catalog_output_dir}/test_semantic_candidates"
catalog_test_options=()
if (( $# > 2 )); then catalog_test_options=("${@:3}"); fi

printf 'Compiling owned catalog regressions into %s\n' "${catalog_output_dir}"
if ! "${catalog_compiler}" -std=c++20 -pipe -Wall -Wextra -Wpedantic -Wshadow \
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual \
    -Wnull-dereference -Wimplicit-fallthrough -Wno-unused-parameter -Werror \
    "${catalog_include_flags[@]}" "${catalog_script_dir}/test_semantic_candidates.cc" \
    -c -o "${catalog_test_object}" > "${catalog_compile_log}" 2>&1; then
  cat -- "${catalog_compile_log}" >&2
  exit 1
fi
if ! "${catalog_compiler}" "${catalog_test_object}" \
    -Wl,--start-group "${catalog_core}" "${catalog_miniz}" "${catalog_sqlite_flags[@]}" -Wl,--end-group \
    "${catalog_tls_flags[@]}" -ldl -pthread -lm -o "${catalog_executable}" \
    >> "${catalog_compile_log}" 2>&1; then
  cat -- "${catalog_compile_log}" >&2
  exit 1
fi

"${catalog_executable}" --no-intro=true "${catalog_test_options[@]}" 2>&1 | tee "${catalog_test_log}"
printf 'Retained compilation and test logs: %s\n' "${catalog_output_dir}"
