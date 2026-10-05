#!/usr/bin/env python3
"""Compile owned catalog checks against a completed Linux core build.

This never configures, rewrites or cleans the supplied build. The probe must be
compiled separately for old/new private headers; use --probe-only on a baseline
that predates the checked profile API.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--probe-only", action="store_true")
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    if build == output or build in output.parents:
        parser.error("output must be outside the completed build")
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    loom = Path(cache["CMAKE_HOME_DIRECTORY"])
    sources = Path(__file__).resolve().parent
    compiler = cache["CMAKE_CXX_COMPILER"]
    archives = [build / "libloom_core.a", build / "libloom_miniz.a"]
    if cache.get("LOOM_USE_SYSTEM_SQLITE") == "ON":
        sqlite = [cache.get("SQLite3_LIBRARY", "-lsqlite3")]
    else:
        archives.append(build / "libloom_sqlite3_amalgamation.a")
        sqlite = []
    for archive in archives:
        if not archive.is_file() or archive.stat().st_size == 0:
            parser.error(f"missing completed-build archive: {archive}")
    compilation = json.loads((build / "compile_commands.json").read_text())
    have_tls = any("LOOM_HAVE_OPENSSL=1" in (c.get("command", "") or " ".join(c.get("arguments", [])))
                   for c in compilation)
    tls = []
    if have_tls:
        for key in ("OPENSSL_SSL_LIBRARY", "OPENSSL_CRYPTO_LIBRARY"):
            value = cache.get(key)
            if value and Path(value).is_file():
                tls.append(value)
        if not tls:
            tls = shlex.split(subprocess.check_output(
                ["pkg-config", "--libs", "openssl"], text=True))
    flags = ["-std=c++20", "-pipe", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow",
             "-Wnon-virtual-dtor", "-Wold-style-cast", "-Wcast-align",
             "-Woverloaded-virtual", "-Wnull-dereference", "-Wimplicit-fallthrough",
             "-Wno-unused-parameter", "-Werror", "-g0", "-I" + str(loom / "include"),
             "-I" + str(loom / "src"), "-I" + str(loom / "tests")]
    if have_tls:
        flags += ["-DLOOM_HAVE_OPENSSL=1", "-DCPPHTTPLIB_OPENSSL_SUPPORT=1"]
    for library in ("nlohmann", "doctest", "sqlite"):
        flags += ["-isystem", str(loom / "third_party" / library)]
    names = ["profile_probe"]
    if not args.probe_only:
        names.append("test_profile_policy")
    output.mkdir(parents=True, exist_ok=True)
    commands = []
    with (output / "compile.log").open("w") as log:
        for name in names:
            obj, executable = output / (name + ".o"), output / name
            compile_command = [compiler, *flags, str(sources / (name + ".cc")),
                               "-c", "-o", str(obj)]
            link_command = [compiler, str(obj), "-Wl,--no-keep-memory",
                            "-Wl,--start-group", *map(str, archives), *sqlite,
                            "-Wl,--end-group", *tls, "-ldl", "-pthread", "-lm",
                            "-o", str(executable)]
            for command in (compile_command, link_command):
                commands.append(command)
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    receipt = {"commands": commands, "build": str(build), "header_root": str(loom),
               "sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in [*archives, loom / "src/catalog/catalog_internal.h",
                                    *(output / n for n in names),
                                    *(sources / (n + ".cc") for n in names)]}}
    (output / "compile_receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    if not args.probe_only:
        with (output / "native-tests.log").open("w") as log:
            subprocess.run([str(output / "test_profile_policy"), "--no-intro=true"],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    print("Completed owned catalog checks in", output)


if __name__ == "__main__":
    main()
