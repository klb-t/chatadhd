#!/usr/bin/env bash
# X-Port (loom/android, module :xport) on the emulator: installs, opens, adds synthetic sources from
# loom/tests/fixtures/extract (never real exports), visits every tab. Runs inside android-emulator-runner.
# Usage: xport.sh APK OUTDIR
set -uo pipefail
APK="${1:?apk}"; OUT="${2:?outdir}"
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$HERE/../../.." && pwd)"
[ -z "${ANDROID_HOME:-}" ] || export PATH="$ANDROID_HOME/platform-tools:$PATH"
export XPORT_PKG=com.chatadhd.xport XPORT_OUT="$OUT"
mkdir -p "$OUT/shots"
log() { printf '==> %s\n' "$*" >&2; }

adb wait-for-device
for _ in $(seq 1 60); do [ "$(adb shell getprop sys.boot_completed | tr -d '\r')" = 1 ] && break; sleep 2; done
log "device: $(adb shell getprop ro.product.model | tr -d '\r') API $(adb shell getprop ro.build.version.sdk | tr -d '\r') abi $(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
adb shell settings put global window_animation_scale 0
adb shell settings put global transition_animation_scale 0
adb shell settings put global animator_duration_scale 0
adb shell input keyevent KEYCODE_HOME || true
sleep 15
adb logcat -c
adb logcat -v threadtime > "$OUT/logcat.txt" 2>&1 &
LOGCAT_PID=$!
trap 'kill "$LOGCAT_PID" 2>/dev/null; adb logcat -d -b crash > "$OUT/logcat-crash.txt" 2>&1 || true' EXIT

log "APK: $(basename "$APK") $(du -h "$APK" | cut -f1); native: $(unzip -l "$APK" | grep -c '\.so$') .so files"
t0=$(date +%s)
adb install -r -g "$APK" || { echo "ERROR: adb install failed" >&2; exit 3; }
log "installed in $(( $(date +%s) - t0 )) s"

# synthetic sources only (the fixtures of the extract tool), under distinctive names so the picker finds each one
adb shell rm -rf /sdcard/Download/xport 2>/dev/null || true
adb shell mkdir -p /sdcard/Download/xport
FX="$ROOT/loom/tests/fixtures/extract"
adb push "$FX/ours/conversation.json" /sdcard/Download/xport-ours-conversation.json >/dev/null
adb push "$FX/grok/grok_com.json" /sdcard/Download/xport-grok.json >/dev/null
adb push "$FX/gemini/takeout.zip" /sdcard/Download/xport-gemini-takeout.zip >/dev/null
for f in xport-ours-conversation.json xport-grok.json xport-gemini-takeout.zip; do
  adb shell "am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d file:///sdcard/Download/$f" >/dev/null 2>&1 || true
done

node "$HERE/xport.mjs"
rc=$?
log "xport.mjs exit $rc"
exit $rc
