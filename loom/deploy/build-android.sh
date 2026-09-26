#!/usr/bin/env bash
# loom/deploy/build-android.sh — builds the Loom Android shell (loom/android).
#
# Requires an Android SDK + NDK, which this sandbox does not have (see
# loom/android/README.md's "Verification without an Android SDK" for what
# was checked instead). This script exists so a machine that *does* have
# them can build with one command and a clear error otherwise.
#
# Required environment variables:
#   ANDROID_HOME or ANDROID_SDK_ROOT   Android SDK location (platforms,
#                                      build-tools, and normally the NDK
#                                      under $ANDROID_HOME/ndk/<version>).
#   JAVA_HOME                          A JDK 17+ (Gradle's requirement here;
#                                      see loom/android/app/build.gradle.kts).
#
# Optional:
#   ANDROID_NDK_HOME    Only needed if the NDK isn't the one under
#                        $ANDROID_HOME/ndk/<ndkVersion from build.gradle.kts>
#                        (currently 27.0.12077973) — e.g. a standalone NDK
#                        install. When set, it's passed through to Gradle as
#                        -Pndk.dir (build.gradle.kts is otherwise
#                        SDK-managed and doesn't need it).
#   LOOM_ANDROID_VARIANT Gradle task suffix to build (default: Debug, i.e.
#                        `assembleDebug`). Try `Release` for a signed-ready
#                        (but unsigned — see loom/android/README.md's gaps)
#                        build.
#
# Usage:
#   ANDROID_HOME=~/Android/Sdk JAVA_HOME=/usr/lib/jvm/java-17-openjdk \
#     loom/deploy/build-android.sh
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
android_dir="$(cd -- "${script_dir}/../android" >/dev/null 2>&1 && pwd)"
web_dist="${script_dir}/../web/dist"

sdk_root="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [[ -z "${sdk_root}" ]]; then
  echo "error: set ANDROID_HOME (or ANDROID_SDK_ROOT) to your Android SDK path" >&2
  exit 1
fi
if [[ ! -d "${sdk_root}" ]]; then
  echo "error: ANDROID_HOME/ANDROID_SDK_ROOT does not exist: ${sdk_root}" >&2
  exit 1
fi
if [[ -z "${JAVA_HOME:-}" ]]; then
  echo "error: set JAVA_HOME to a JDK 17+ install" >&2
  exit 1
fi

if [[ ! -d "${web_dist}" || -z "$(ls -A "${web_dist}" 2>/dev/null)" ]]; then
  echo "warning: ${web_dist} is missing or empty - build loom/web first" \
       "(cd loom/web && npm run build). Gradle's copyWebAssets task will" \
       "warn and skip rather than fail, so this only affects what ends up" \
       "in the APK, not whether the build succeeds." >&2
fi

variant="${LOOM_ANDROID_VARIANT:-Debug}"
gradle_args=("assemble${variant}")
if [[ -n "${ANDROID_NDK_HOME:-}" ]]; then
  gradle_args+=("-Pndk.dir=${ANDROID_NDK_HOME}")
fi

echo "local.properties -> sdk.dir=${sdk_root}"
printf 'sdk.dir=%s\n' "${sdk_root}" > "${android_dir}/local.properties"

echo "==> ${android_dir}/gradlew ${gradle_args[*]}" >&2
(cd "${android_dir}" && ./gradlew "${gradle_args[@]}")

echo "==> done: see loom/android/app/build/outputs/apk/" >&2
