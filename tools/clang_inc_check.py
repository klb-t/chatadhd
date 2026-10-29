#!/usr/bin/env python3
"""clang -fsyntax-only for every source that #includes a generated *.inc (big raw-string data): gcc accepts what clang's constexpr step limit
rejects (CI 262: kOnboardingPack ~420 KB). Uses the dev build's compile_commands.json with gcc-only flags dropped.

  python3 tools/clang_inc_check.py [<build-dir, default loom/build/dev>]      exit 1 if any file fails; skips (exit 0) when clang++ is not installed
"""
import json, re, shlex, shutil, subprocess, sys
from pathlib import Path

build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "loom/build/dev"
root = build.parents[2]   # <checkout>/loom/build/<preset> -> <checkout>
clang = shutil.which("clang++") or next((shutil.which(f"clang++-{v}") for v in range(25, 13, -1) if shutil.which(f"clang++-{v}")), None)
if not clang:
    print("clang++ not installed: skipped")
    sys.exit(0)
cc = json.loads((build / "compile_commands.json").read_text())
inc = re.compile(r'#\s*include\s+"[^"]+\.inc"')
bad = 0
for e in cc:
    f = Path(e["file"])
    if f.suffix != ".cpp" or "/third_party/" in str(f) or "/tests/" in str(f):
        continue
    try:
        if not inc.search(f.read_text(encoding="utf-8", errors="replace")):
            continue
    except OSError:
        continue
    args = shlex.split(e["command"])
    out, skip = [clang], False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
        elif a in ("-MD", "-MMD", "-Wmaybe-uninitialized") or a.startswith("-Werror=maybe"):
            continue
        else:
            out.append(a)
    out += ["-fsyntax-only", "-Wno-unknown-warning-option", "-Wno-unused-command-line-argument"]
    r = subprocess.run(out, cwd=e["directory"], capture_output=True, text=True)
    print(("ok   " if r.returncode == 0 else "FAIL ") + (str(f.relative_to(root)) if f.is_relative_to(root) else str(f)))
    if r.returncode:
        bad += 1
        print((r.stderr or "")[:600])
sys.exit(1 if bad else 0)
