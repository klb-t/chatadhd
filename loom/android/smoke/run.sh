#!/usr/bin/env bash
# Android smoke, the part that runs INSIDE the emulator session (reactivecircus/android-emulator-runner runs every
# line of its `script:` as a separate command, so it calls just this file).
# Usage: run.sh APK OUTDIR      (OUTDIR holds smoke.env written by server.sh start)
set -uo pipefail
APK="${1:?apk}"; OUT="${2:?outdir}"
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
[ -z "${ANDROID_HOME:-}" ] || export PATH="$ANDROID_HOME/platform-tools:$PATH"
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
# Read the whole listing first: with pipefail, `unzip | grep -q` fails when grep exits early and unzip gets SIGPIPE.
apk_listing="$(unzip -l "$APK")"
if ! grep -q " lib/$abi/libloom_jni\.so$" <<<"$apk_listing"; then
  echo "ERROR: $APK has no lib/$abi/libloom_jni.so (the emulator is $abi). Build the APK with -Ploom.abis including $abi." >&2
  unzip -l "$APK" | grep "libloom_jni" >&2 || true
  exit 3
fi
log "install"
adb install -r -g "$APK" || { echo "ERROR: adb install failed" >&2; exit 3; }
adb shell pm grant "$PKG" android.permission.POST_NOTIFICATIONS 2>/dev/null || true

# the phone must reach the host server before the app is blamed (the network of a fresh emulator can come up late):
# poll a TCP connect to 10.0.2.2 until it works, up to 90 s; only logged, the driver decides what is fatal
reach=1
for _ in $(seq 1 45); do
  if adb shell "nc -w 2 10.0.2.2 $SMOKE_PORT </dev/null >/dev/null 2>&1; echo \$?" | tr -d '\r' | grep -qx 0; then reach=0; break; fi
  sleep 2
done
log "phone -> 10.0.2.2:$SMOKE_PORT tcp connect: $([ "$reach" = 0 ] && echo ok || echo FAILED)"

log "drive the app"
node "$HERE/drive.mjs"
rc=$?
log "drive.mjs exit $rc"
if [ "$rc" != 0 ]; then
  # Artifacts are not always reachable to whoever reads the run, so the essentials of a failure go to the job log too.
  log "--- screen at failure (texts of fail-ui.xml or a fresh dump)"
  ui="$OUT/fail-ui.xml"
  [ -s "$ui" ] || { adb shell uiautomator dump /sdcard/ui.xml >/dev/null 2>&1; adb exec-out cat /sdcard/ui.xml > "$ui" 2>/dev/null || true; }
  grep -o 'text="[^"]*"\|class="[^"]*"\|resource-id="[^"]*"' "$ui" 2>/dev/null | grep -v '=""' | head -n 80 || true
  log "--- focused activity"
  adb shell dumpsys activity activities 2>/dev/null | grep -E "mResumedActivity|topResumedActivity" | head -n 5 || true
  log "--- logcat (app, errors, crashes; last 80 lines)"
  adb logcat -d -v brief 2>/dev/null | grep -E "chatadhd|AndroidRuntime|loom| E/" | tail -n 80 || true
  log "--- server.log (last 40 lines)"
  tail -n 40 "$OUT/server.log" 2>/dev/null || true
fi
exit $rc
