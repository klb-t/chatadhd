#!/usr/bin/env bash
# Full gate for one checkout: GCC Debug dev build (+server), full CTest with the
# executed-case guard (verify_ctest.py), JNI smoke, web build + web test scripts.
# Usage: gate.sh <repo-root> [label]   -> writes $GATE_OUT (default $TMPDIR/gate-<label>)/gate.log, exit 0 iff all green.
# Duration: 40-90 min on this machine (longer when another worktree builds in parallel) - start it with a 2 h background timeout,
# a 1 h limit killed gate k2b4 on 2026-10-23 in the middle of the web e2e (the kill shows up as 'Target page ... has been closed').
# (copied from the session scratchpad into tools/ on 2026-10-21, into branch w2b2 from ~/chatadhd on 2026-10-29; the lint step is a heuristic report, it does not fail the gate)
set -uo pipefail
export CCACHE_BASEDIR="${CCACHE_BASEDIR:-$HOME}" CCACHE_NOHASHDIR=true CCACHE_DIR="${CCACHE_DIR:-$HOME/.ccache}" CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-12G}"  # OPS2: was hard-coded /home/user
TOOLS="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$1" && pwd)"; LABEL="${2:-$(basename "$ROOT")}"
OUT="${GATE_OUT:-${TMPDIR:-/tmp}/gate-$LABEL}"; mkdir -p "$OUT"   # logs outside the working tree (override with GATE_OUT)
LOG="$OUT/gate.log"; : > "$LOG"
# S1-(1) 2026-10-29: /tmp is tmpfs (RAM) on the VM, so everything the build and tests write goes to a per-gate dir on disk,
# emptied before and after the run (override the base with LOOM_GATE_TMP; logs stay in $OUT above).
GT="${LOOM_GATE_TMP:-$HOME/.cache/loom-tmp}/$LABEL"; rm -rf "$GT"; mkdir -p "$GT"; export TMPDIR="$GT" TMP="$GT" TEMP="$GT"
trap 'n=$(find "$GT" -mindepth 1 -maxdepth 1 2>/dev/null | wc -l); echo "tmp leftovers in $GT: $n" >>"$LOG"; rm -rf "$GT"' EXIT
step() { echo "== $1" | tee -a "$LOG"; shift; ( "$@" ) >>"$LOG" 2>&1; rc=$?; echo "   rc=$rc" | tee -a "$LOG"; [ $rc -eq 0 ] || FAIL="$FAIL $STEP"; }
FAIL=""
cd "$ROOT/loom"
STEP=lint;      step lint bash -c "cd \"$ROOT\" && python3 \"$TOOLS/lint_asan_range_for.py\" origin/main || { echo '(lint: przejrzyj wypisane linie; to heurystyka, nie błąd kompilacji)'; true; }"
STEP=uistrings; step uistrings bash -c "cd \"$ROOT\" && python3 \"$TOOLS/lint_ui_strings.py\" && python3 \"$TOOLS/lint_ui_strings.py\" --missing"   # M1-JEZYKI: no new user-visible text in code; missing keys per language (report)
STEP=configure; step configure cmake --preset dev -DLOOM_BUILD_SERVER=ON -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DPython3_EXECUTABLE="$(which python3)"
STEP=build;     step build cmake --build --preset dev --parallel 4
V=build/dev/verification; mkdir -p "$V"
STEP=clangsyntax; step clangsyntax python3 "$TOOLS/clang_inc_check.py" "$ROOT/loom/build/dev"
# K3-1 (2026-10-23): the embedded K3 packs (dimensions, address syntax) and their sha256 pins must match the data files
[ -f "$ROOT/loom/src/graph/gen_k3_packs.py" ] && { STEP=k3packs; step k3packs bash -c "cd \"$ROOT\" && python3 loom/src/graph/gen_k3_packs.py --check"; }
# R102 G1 (2026-10-30): the embedded text segmentation pack must match loom/data/text/segmentation.pack
[ -f "$ROOT/loom/src/text/gen_text_pack.py" ] && { STEP=textpack; step textpack bash -c "cd \"$ROOT\" && python3 loom/src/text/gen_text_pack.py --check"; }
[ -f "$ROOT/loom/src/onboarding/gen_onboarding_pack.py" ] && { STEP=onbpack; step onbpack bash -c "cd \"$ROOT\" && python3 loom/src/onboarding/gen_onboarding_pack.py --check"; }   # generated words (builtin.inc, web copy) match the packs (RI-2b gate 2026-10-28)
STEP=ctest;     step ctest bash -c "ctest --preset dev --show-only=json-v1 > $V/ctest-manifest.json && ctest --preset dev --no-tests=error -j3 --test-output-size-passed 10485760 --test-output-size-failed 10485760 --output-junit $PWD/$V/ctest.xml"
cd "$ROOT"
STEP=guard;     step guard python3 .github/scripts/verify_ctest.py --policy .github/ctest-evidence-policy.json --manifest loom/$V/ctest-manifest.json --junit loom/$V/ctest.xml --preset dev --output loom/$V/executed-cases.json
STEP=scripts;   step scripts python3 -m unittest discover -s .github/scripts -p 'test_*.py'
cd "$ROOT/loom/web"; export ONBOARDING_HOST_EVIDENCE_DIR="$OUT/onboarding-host-evidence"; rm -rf "$ONBOARDING_HOST_EVIDENCE_DIR"  # per-gate dir: parallel worktree gates no longer collide
STEP=web;       step web bash -c "npm ci --no-audit --no-fund && npm run build"
STEP=webtests;  step webtests bash -c 'f=""; for s in $(node -e "const p=require(\"./package.json\");console.log(Object.keys(p.scripts).filter(k=>k.startsWith(\"test:\")||k===\"e2e\").join(\" \"))"); do echo "--- $s"; if npm run -s $s; then echo "PASSED $s"; else echo "FAILED $s"; f="$f $s"; fi; done; [ -z "$f" ] || { echo "WEB FAILURES:$f"; exit 1; }'
grep -E "tests passed|tests failed|Verified|^   rc|PASSED|FAILED|WEB FAILURES" "$LOG" | tail -60
[ -z "$FAIL" ] && echo "GATE GREEN ($LABEL)" || { echo "GATE RED ($LABEL):$FAIL"; exit 1; }
