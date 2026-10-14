#!/usr/bin/env bash
# Android smoke, the part that runs INSIDE the emulator session (reactivecircus/android-emulator-runner runs every
# line of its `script:` as a separate command, so it calls just this file).
# Usage: run.sh APK OUTDIR      (OUTDIR holds smoke.env written by server.sh start)
set -uo pipefail
APK="${1:?apk}"; OUT="${2:?outdir}"
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PKG=com.chatadhd.android
# shellcheck disable=SC1091
. "$OUT/smoke.env"
export SMOKE_OUT SMOKE_PORT SMOKE_URL SMOKE_LOGIN SMOKE_PASS SMOKE_KEY
mkdir -p "$OUT/shots"
log() { printf '==> %s\n' "$*" >&2; }

adb wait-for-device
log "device: $(adb shell getprop ro.product.model | tr -d '\r') API $(adb shell getprop ro.build.version.sdk | tr -d '\r') abi $(adb shell getprop ro.product.cpu.abilist | tr -d '\r')"
# no animations (steady UI Automator dumps), no soft keyboard in front of the form
adb shell settings put global window_animation_scale 0
adb shell settings put global transition_animation_scale 0
adb shell settings put global animator_duration_scale 0
adb shell settings put secure show_ime_with_hard_keyboard 0 || true

adb logcat -c
adb logcat -v threadtime > "$OUT/logcat.txt" 2>&1 &
LOGCAT_PID=$!
finish() {
  adb exec-out screencap -p > "$OUT/shots/99-final.png" 2>/dev/null || true
  adb shell dumpsys activity activities 2>/dev/null | head -n 80 > "$OUT/activities.txt" || true
  kill "$LOGCAT_PID" 2>/dev/null || true
  adb logcat -d -b crash > "$OUT/logcat-crash.txt" 2>&1 || true
}
trap finish EXIT

# the APK must carry the emulator's ABI (package.yml builds arm64-v8a only unless asked for x86_64)
abi="$(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
if ! unzip -l "$APK" | grep -q " lib/$abi/libloom_jni\.so$"; then
  echo "ERROR: $APK has no lib/$abi/libloom_jni.so (the emulator is $abi). Build the APK with -Ploom.abis including $abi." >&2
  unzip -l "$APK" | grep "libloom_jni" >&2 || true
  exit 3
fi
log "install"
adb install -r -g "$APK" || { echo "ERROR: adb install failed" >&2; exit 3; }
adb shell pm grant "$PKG" android.permission.POST_NOTIFICATIONS 2>/dev/null || true

# the phone must see the host server before the app is blamed
adb shell "printf 'GET /api/healthz HTTP/1.0\\r\\n\\r\\n' | nc -w 5 10.0.2.2 $SMOKE_PORT" > "$OUT/phone-healthz.txt" 2>&1 || true
log "phone -> server healthz: $(head -c 100 "$OUT/phone-healthz.txt")"

log "drive the app"
node "$HERE/drive.mjs"
rc=$?
log "drive.mjs exit $rc"
exit $rc
