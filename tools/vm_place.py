#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""VM jako miejsce w danych + trwałość biegu (pakiet IW-8, 2026-10-26).

0 tokenów, bez modelu. Projekt: docs/architecture/INTERFEJS_WSPOLPRACY_v2_2026-10-23.md §2.7, §4 IW-8, §5 (trwałość
biegu na VM). Fakty o maszynie: docs/ops/places.json, docs/ops/VM_RECON_2026-10-23.md, docs/ops/VM_DEPLOY_2026-10-23.md.

Związek z spot-guard (tools/gcp/spot_guard.yaml): bieg `running` z `where` = to miejsce (albo alias „vm”) znaczy
„praca w toku”, czyli metadane work-pending=true (przez tools/gcp/work.sh). Po wywłaszczeniu spot-guard przywraca
maszynę jako Standard tylko wtedy; bez biegów w toku zostaje zatrzymana (§5: na bezczynności nic).

  python3 tools/vm_place.py status [--json]        biegi na VM, flaga spot-guard, ostatnie tętno
  python3 tools/vm_place.py sync [--dry-run]       work-pending := czy jest bieg `running where: vm` (work.sh pending/done)
  python3 tools/vm_place.py preempt                z haka wyłączenia (120 s): jest bieg w toku → work.sh pending (nigdy done)
  python3 tools/vm_place.py heartbeat [--commit] [--push] [--no-sync]
        obserwacje z datą → docs/reports/places/heartbeat/<miejsce>.jsonl; ponowny start maszyny → zdarzenie w biegach
        w toku z poleceniem wznowienia; prośby „stop” z docs/spec/gestures_state.json adresowane do tego miejsca
  python3 tools/vm_place.py wake [--dry-run] [--pull]
        z chmury: bieg `running where: vm` + tętno starsze niż stale_minutes + instancja zatrzymana → instances start
  python3 tools/vm_place.py resume-list            biegi do wznowienia na tym miejscu (po starcie maszyny)

Presety: tools/routines/vm_place.json (--config / VM_PLACE_CONFIG). gcloud z PATH (testy: fałszywy gcloud).
VM_PLACE_NO_METADATA=1 wyłącza serwer metadanych (wtedy gcloud describe).
"""
from __future__ import annotations

import argparse
import datetime as _dt
import glob
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
import run_record as rr  # noqa: E402

DEFAULT_CONFIG = HERE / "routines" / "vm_place.json"
GESTURES = "docs/spec/gestures_state.json"


def load_config(path=None) -> dict:
    p = Path(path or os.environ.get("VM_PLACE_CONFIG") or DEFAULT_CONFIG)
    return json.loads(p.read_text(encoding="utf-8"))


def now() -> _dt.datetime:
    return _dt.datetime.now(_dt.timezone.utc)


def iso(t: _dt.datetime) -> str:
    return t.strftime("%Y-%m-%dT%H:%M:%SZ")


def safe(place: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", place)


# ---------------------------------------------------------------- biegi na miejscu

def on_place(where, cfg: dict, place: str) -> bool:
    if not where:
        return False
    return where == place or where in cfg.get("where_aliases", [])


def roots(root: Path, cfg: dict) -> list[Path]:
    out, seen = [], set()
    for pat in cfg.get("scan_roots", ["{root}"]):
        for p in sorted(glob.glob(pat.replace("{root}", str(root)))) if any(c in pat for c in "*?[") \
                else [pat.replace("{root}", str(root))]:
            rp = Path(p).resolve()
            if rp not in seen and (rp / "docs" / "reports" / "runs").exists():
                seen.add(rp)
                out.append(rp)
    return out or [Path(root).resolve()]


def running_here(root: Path, cfg: dict, place: str, rcfg: dict | None = None) -> list[dict]:
    rcfg = rcfg or rr.load_config()
    out = []
    for r in roots(root, cfg):
        for rid, row in rr.read_index(r, rcfg).items():
            if row.get("state") == "running" and on_place(row.get("where"), cfg, place):
                out.append({**row, "_root": str(r)})
    return out


def note_runs(root_rows: list[dict], kind: str, text: str, at: str, rcfg: dict) -> None:
    for row in root_rows:
        try:
            rr.add_event(Path(row["_root"]), rcfg, row["run_id"], kind, text=text, at=at, use_default_delta=False)
        except rr.RunError as e:
            print(f"uwaga: {row['run_id']}: {e}", file=sys.stderr)


# ---------------------------------------------------------------- flaga spot-guard

def _gcloud_describe(cfg: dict) -> dict | None:
    if not shutil.which("gcloud"):
        return None
    argv = ["gcloud", "compute", "instances", "describe", cfg["instance"], "--zone", cfg["zone"], "--format=json"]
    if cfg.get("project"):
        argv += ["--project", cfg["project"]]
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if p.returncode != 0:
        print(f"uwaga: gcloud describe: {p.stderr.strip()[-300:]}", file=sys.stderr)
        return None
    try:
        return json.loads(p.stdout)
    except ValueError:
        return None


def read_flags(cfg: dict, prefer_describe: bool = False) -> dict:
    """{work-pending, work-note, spot-guard, …, _status, _model, _source}; puste przy braku dostępu."""
    url = cfg.get("metadata_url")
    if url and not prefer_describe and not os.environ.get("VM_PLACE_NO_METADATA"):
        try:
            req = urllib.request.Request(url + "/attributes/?recursive=true", headers={"Metadata-Flavor": "Google"})
            with urllib.request.urlopen(req, timeout=float(cfg.get("metadata_timeout_s", 2))) as f:
                attrs = json.loads(f.read().decode())
            return {**{k: str(v) for k, v in attrs.items()}, "_status": "RUNNING", "_source": "metadata"}
        except (OSError, ValueError):
            pass
    d = _gcloud_describe(cfg)
    if d is None:
        return {"_source": "none"}
    items = {i["key"]: i.get("value", "") for i in d.get("metadata", {}).get("items", [])}
    return {**items, "_status": d.get("status"), "_model": d.get("scheduling", {}).get("provisioningModel"),
            "_source": "gcloud describe"}


def work_sh(root: Path, cfg: dict, *args: str) -> tuple[int, str]:
    script = Path(cfg.get("work_sh", "tools/gcp/work.sh"))
    if not script.is_absolute():
        script = REPO / script
    env = {**os.environ, "INSTANCE": cfg["instance"], "ZONE": cfg["zone"]}
    try:
        p = subprocess.run(["bash", str(script), *args], capture_output=True, text=True, env=env, timeout=90)
    except (OSError, subprocess.TimeoutExpired) as e:
        return 1, str(e)
    return p.returncode, (p.stdout + p.stderr).strip()


def sync(root: Path, cfg: dict, place: str, *, dry: bool = False, only_raise: bool = False, at: str | None = None,
         rcfg: dict | None = None) -> dict:
    rcfg = rcfg or rr.load_config()
    at = at or iso(now())
    runs = running_here(root, cfg, place, rcfg)
    flags = read_flags(cfg)
    current = flags.get("work-pending") == "true"
    want = bool(runs)
    note = (cfg.get("own_note_prefix", "runs:") + " " + " ".join(r["run_id"] for r in runs))[:200]
    action, out = "none", ""
    if flags.get("_source") == "none":
        action = "no_access"
    elif want and not current:
        action = "pending"
    elif not want and current and not only_raise:
        ours = str(flags.get("work-note", "")).startswith(cfg.get("own_note_prefix", "runs:"))
        action = "done" if ours or cfg.get("release_foreign_pending") else "keep_foreign"
    if action in ("pending", "done") and not dry:
        code, out = work_sh(root, cfg, *(["pending", note] if action == "pending" else ["done"]))
        if code != 0:
            action = f"failed_{action}"
        else:
            note_runs(runs, "vm_sync", f"spot-guard: work-pending={'true' if action == 'pending' else 'false'}", at, rcfg)
    return {"place": place, "running": [r["run_id"] for r in runs], "work_pending": current, "want": want,
            "action": action, "dry_run": dry, "source": flags.get("_source"), "out": out[-300:]}


# ---------------------------------------------------------------- tętno

def _cmd(argv: list[str], timeout=10) -> str | None:
    if not shutil.which(argv[0]):
        return None
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return p.stdout.strip()


def boot_time() -> str | None:
    try:
        for line in Path("/proc/stat").read_text().splitlines():
            if line.startswith("btime "):
                return iso(_dt.datetime.fromtimestamp(int(line.split()[1]), _dt.timezone.utc))
    except OSError:
        return None
    return None


def observe(root: Path, cfg: dict) -> dict:
    du = shutil.disk_usage("/")
    obs = {"host": socket.gethostname(), "boot": boot_time(),
           "disk_free_gb": round(du.free / 2**30, 1), "disk_used_pct": round(100 * du.used / du.total, 1)}
    try:
        obs["load"] = [round(x, 2) for x in os.getloadavg()]
    except OSError:
        pass
    units = {}
    for u in cfg.get("probe_user_units", []):
        a = _cmd(["systemctl", "--user", "is-active", u])
        e = _cmd(["systemctl", "--user", "is-enabled", u])
        units[f"user:{u}"] = f"{a or '?'}/{e or '?'}"
    for u in cfg.get("probe_system_units", []):
        units[u] = _cmd(["systemctl", "is-active", u]) or "?"
    obs["units"] = units
    if cfg.get("probe_tmux_session"):
        w = _cmd(["tmux", "list-windows", "-t", cfg["probe_tmux_session"], "-F", "#W"])
        obs["tmux"] = w.split("\n") if w else []
    return obs


def heartbeat_file(root: Path, cfg: dict, place: str) -> Path:
    return Path(root) / cfg.get("heartbeat_dir", "docs/reports/places/heartbeat") / f"{safe(place)}.jsonl"


def last_heartbeat(root: Path, cfg: dict, place: str) -> dict | None:
    p = heartbeat_file(root, cfg, place)
    if not p.exists():
        return None
    for line in reversed(p.read_text(encoding="utf-8").splitlines()):
        try:
            return json.loads(line)
        except ValueError:
            continue
    return None


def handle_stops(root: Path, cfg: dict, place: str, runs: list[dict], at: str, rcfg: dict, dry: bool) -> list[dict]:
    p = Path(root) / GESTURES
    try:
        st = json.loads(p.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return []
    pats = [re.compile(x, re.I) for x in cfg.get("stop_scope_match", [])]
    done, changed = [], False
    for stop in st.get("stops", []):
        if stop.get("state") != "requested" or any(h.get("place") == place for h in stop.get("handled", [])):
            continue
        listed = {r.get("run_id") for r in stop.get("running", []) if on_place(r.get("where"), cfg, place)}
        scoped = any(pt.search(str(stop.get("scope", ""))) for pt in pats)
        if not listed and not scoped:
            continue
        targets = [r for r in runs if scoped or r["run_id"] in listed]
        if not dry:
            for r in targets:
                rr.set_field(Path(r["_root"]), rcfg, r["run_id"], "state", cfg.get("stop_state_after", "paused"), at)
                rr.add_event(Path(r["_root"]), rcfg, r["run_id"], "stop", at=at, use_default_delta=False,
                             text=f"prośba „stop” z {stop.get('at')} ({stop.get('by') or '?'}): bieg wstrzymany na {place}; "
                                  f"odwrót: python3 tools/routine.py resume {r['run_id']}")
            if cfg.get("on_stop"):
                subprocess.run([a.replace("{place}", place) for a in cfg["on_stop"]], cwd=root, timeout=120)
            stop.setdefault("handled", []).append({"place": place, "at": at, "runs": [r["run_id"] for r in targets]})
            changed = True
        done.append({"stop_at": stop.get("at"), "runs": [r["run_id"] for r in targets]})
    if changed:
        tmp = p.with_suffix(".tmp")
        tmp.write_text(json.dumps(st, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        tmp.replace(p)
    return done


def heartbeat(root: Path, cfg: dict, place: str, *, do_sync=True, commit=False, push=False, dry=False,
              at: str | None = None, obs: dict | None = None) -> dict:
    rcfg = rr.load_config()
    at = at or iso(now())
    prev = last_heartbeat(root, cfg, place)
    obs = obs if obs is not None else observe(root, cfg)
    runs = running_here(root, cfg, place, rcfg)
    line = {"schema": "loom.place_heartbeat/1", "at": at, "place": place, **obs, "running": [r["run_id"] for r in runs]}
    if prev and prev.get("boot") and obs.get("boot") and prev["boot"] != obs["boot"]:
        line["rebooted"] = {"previous_boot": prev["boot"], "last_beat_before": prev.get("at")}
        if not dry:
            for r in runs:
                rr.add_event(Path(r["_root"]), rcfg, r["run_id"], "place_rebooted", at=at, use_default_delta=False,
                             text=f"{place} wstała ponownie (start {obs['boot']}, ostatnie tętno przed {prev.get('at')}); "
                                  f"bieg do wznowienia z rekordu: python3 tools/routine.py resume {r['run_id']}")
    stops = handle_stops(root, cfg, place, runs, at, rcfg, dry)
    if stops:
        line["stops"] = stops
    if do_sync:
        s = sync(root, cfg, place, dry=dry, at=at, rcfg=rcfg)
        line["spot_guard"] = {"action": s["action"], "work_pending_before": s["work_pending"], "source": s["source"]}
    if not dry:
        hf = heartbeat_file(root, cfg, place)
        hf.parent.mkdir(parents=True, exist_ok=True)
        with open(hf, "a", encoding="utf-8") as fh:
            fh.write(json.dumps(line, ensure_ascii=False, sort_keys=True) + "\n")
        if commit or push:
            rel = str(hf.relative_to(root))
            subprocess.run(["git", "-C", str(root), "add", "--", rel], timeout=60)
            subprocess.run(["git", "-C", str(root), "commit", "-q", "-m", f"tętno {place} {at}", "--", rel], timeout=60)
            if push:
                br = cfg.get("push_branch")
                subprocess.run(["git", "-C", str(root), "push", "-q", "origin"] + ([f"HEAD:{br}"] if br else []), timeout=120)
    return line


# ---------------------------------------------------------------- vm.wake (z chmury)

def wake(root: Path, cfg: dict, place: str, *, dry=False, at: str | None = None) -> dict:
    rcfg = rr.load_config()
    t = now() if at is None else _dt.datetime.fromisoformat(at.replace("Z", "+00:00"))
    at = iso(t)
    runs = running_here(root, cfg, place, rcfg)
    res = {"place": place, "running": [r["run_id"] for r in runs], "action": "none", "dry_run": dry}
    if not runs:
        res["why"] = "brak biegu w toku na tym miejscu — na bezczynności nic (§5)"
        return res
    hb = last_heartbeat(root, cfg, place)
    age = None
    if hb:
        age = (t - _dt.datetime.fromisoformat(hb["at"].replace("Z", "+00:00"))).total_seconds() / 60
    res["heartbeat_age_min"] = None if age is None else round(age, 1)
    if age is not None and age < float(cfg.get("stale_minutes", 20)):
        res["why"] = "tętno świeże"
        return res
    flags = read_flags(cfg, prefer_describe=True)
    res["status"] = flags.get("_status")
    if flags.get("_source") == "none":
        res["action"], res["why"] = "no_access", "gcloud describe niedostępny (uprawnienia z chmury: luka L3)"
        return res
    if flags.get("_status") == "RUNNING":
        res["why"] = "instancja działa, tętno ustało — sprawdź timer tętna (nic nie uruchamiam)"
        return res
    if cfg.get("defer_to_spot_guard") and flags.get("spot-guard") == "on" and flags.get("work-pending") == "true":
        res["action"], res["why"] = "defer", "spot-guard włączony i work-pending=true — on przywróci maszynę"
        return res
    res["action"] = "start"
    if not dry:
        argv = ["gcloud", "compute", "instances", "start", cfg["instance"], "--zone", cfg["zone"]]
        if cfg.get("project"):
            argv += ["--project", cfg["project"]]
        p = subprocess.run(argv, capture_output=True, text=True, timeout=300)
        res["exit"] = p.returncode
        if p.returncode != 0:
            res["action"] = "failed_start"
            res["err"] = p.stderr.strip()[-300:]
        else:
            note_runs(runs, "vm_wake", f"vm.wake: {cfg['instance']} uruchomiona (status był {flags.get('_status')}, "
                                       f"tętno sprzed {res['heartbeat_age_min']} min)", at, rcfg)
    return res


# ---------------------------------------------------------------- CLI

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=os.environ.get("RUN_RECORD_ROOT"))
    ap.add_argument("--config")
    ap.add_argument("--at")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("status", "sync", "preempt", "heartbeat", "wake", "resume-list"):
        s = sub.add_parser(name)
        s.add_argument("--place")
        s.add_argument("--json", action="store_true")
        if name in ("sync", "heartbeat", "wake"):
            s.add_argument("--dry-run", action="store_true")
        if name == "heartbeat":
            s.add_argument("--commit", action="store_true")
            s.add_argument("--push", action="store_true")
            s.add_argument("--no-sync", action="store_true")
        if name == "wake":
            s.add_argument("--pull", action="store_true", help="git pull --ff-only przed odczytem tętna")
    a = ap.parse_args(argv)
    cfg = load_config(a.config)
    root = Path(a.root) if a.root else REPO
    place = a.place or cfg["place"]
    if a.cmd == "status":
        hb = last_heartbeat(root, cfg, place)
        res = {"place": place, "running": [r["run_id"] for r in running_here(root, cfg, place)],
               "flags": {k: v for k, v in read_flags(cfg).items() if k.startswith(("work-", "spot-guard", "_"))},
               "last_heartbeat": hb and hb.get("at")}
    elif a.cmd == "sync":
        res = sync(root, cfg, place, dry=a.dry_run, at=a.at)
    elif a.cmd == "preempt":
        res = sync(root, cfg, place, only_raise=True, at=a.at)
    elif a.cmd == "heartbeat":
        res = heartbeat(root, cfg, place, do_sync=not a.no_sync, commit=a.commit or cfg.get("commit"),
                        push=a.push or cfg.get("push"), dry=a.dry_run, at=a.at)
    elif a.cmd == "wake":
        if a.pull:
            subprocess.run(["git", "-C", str(root), "pull", "-q", "--ff-only"], timeout=120)
        res = wake(root, cfg, place, dry=a.dry_run, at=a.at)
    else:
        res = [{"run_id": r["run_id"], "title": r.get("title"), "root": r["_root"],
                "resume": f"python3 tools/routine.py --root {r['_root']} resume {r['run_id']}"}
               for r in running_here(root, cfg, place)]
    print(json.dumps(res, ensure_ascii=False, indent=None if not getattr(a, "json", False) else 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
