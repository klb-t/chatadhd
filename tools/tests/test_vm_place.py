"""IW-8: tools/vm_place.py z fałszywym gcloud (żadne prawdziwe polecenie gcloud nie zmienia instancji)."""
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
sys.path.insert(0, str(TOOLS))
import run_record as rr  # noqa: E402
import vm_place as vp  # noqa: E402

FAKE_GCLOUD = r'''#!/usr/bin/env python3
import json, os, sys
st_path = os.environ["FAKE_GCLOUD_STATE"]
st = json.load(open(st_path))
with open(os.environ["FAKE_GCLOUD_LOG"], "a") as f:
    f.write(json.dumps(sys.argv[1:]) + "\n")
a = sys.argv[1:]
if a[:2] != ["compute", "instances"]:
    sys.exit(2)
verb = a[2]
if verb == "describe":
    print(json.dumps({"status": st["status"], "scheduling": {"provisioningModel": st.get("model", "SPOT")},
                      "metadata": {"items": [{"key": k, "value": v} for k, v in st["meta"].items()]}}))
elif verb == "add-metadata":
    kv = a[a.index("--metadata") + 1]
    for part in kv.split(","):
        k, _, v = part.partition("=")
        st["meta"][k] = v
elif verb == "start":
    st["status"] = "RUNNING"
else:
    sys.exit(3)
json.dump(st, open(st_path, "w"))
'''


class VmPlaceBase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.root = t / "repo"
        (self.root / "docs" / "reports" / "runs").mkdir(parents=True)
        bindir = t / "bin"
        bindir.mkdir()
        g = bindir / "gcloud"
        g.write_text(FAKE_GCLOUD)
        g.chmod(g.stat().st_mode | stat.S_IEXEC)
        self.state = t / "gcloud_state.json"
        self.log = t / "gcloud_log.jsonl"
        self.set_state(status="RUNNING", meta={"spot-guard": "off", "work-pending": "false"})
        self.env = mock.patch.dict(os.environ, {"PATH": f"{bindir}:{os.environ['PATH']}", "FAKE_GCLOUD_STATE": str(self.state),
                                                "FAKE_GCLOUD_LOG": str(self.log), "VM_PLACE_NO_METADATA": "1",
                                                "RUN_RECORD_HOOKS": "off"})
        self.env.start()
        self.cfg = vp.load_config()
        self.cfg["scan_roots"] = ["{root}"]
        self.rcfg = rr.load_config()
        self.place = self.cfg["place"]

    def tearDown(self):
        self.env.stop()
        self.tmp.cleanup()

    def set_state(self, **kw):
        st = json.loads(self.state.read_text()) if self.state.exists() else {}
        st.update(kw)
        self.state.write_text(json.dumps(st))

    def meta(self):
        return json.loads(self.state.read_text())["meta"]

    def calls(self):
        if not self.log.exists():
            return []
        return [json.loads(x) for x in self.log.read_text().splitlines()]

    def start_run(self, where=None):
        return rr.start(self.root, self.rcfg, "package.run", where=where or self.place)["run_id"]

    def rec(self, rid):
        return rr.load(self.root, self.rcfg, rid)[0]


class TestSync(VmPlaceBase):
    def test_no_runs_no_change(self):
        s = vp.sync(self.root, self.cfg, self.place)
        self.assertEqual(s["action"], "none")
        self.assertFalse(any(c[2] == "add-metadata" for c in self.calls()))

    def test_running_run_raises_pending_and_finish_releases(self):
        rid = self.start_run()
        s = vp.sync(self.root, self.cfg, self.place)
        self.assertEqual(s["action"], "pending")
        self.assertEqual(self.meta()["work-pending"], "true")
        self.assertTrue(self.meta()["work-note"].startswith("runs: "))
        self.assertIn(rid, self.meta()["work-note"])
        self.assertTrue(any(e["kind"] == "vm_sync" for e in self.rec(rid)["events"]))
        self.assertEqual(vp.sync(self.root, self.cfg, self.place)["action"], "none")  # idempotent
        rr.finish(self.root, self.rcfg, rid)
        s = vp.sync(self.root, self.cfg, self.place)
        self.assertEqual(s["action"], "done")
        self.assertEqual(self.meta()["work-pending"], "false")

    def test_alias_vm_counts_other_place_not(self):
        self.start_run(where="cloud")
        self.assertEqual(vp.sync(self.root, self.cfg, self.place)["action"], "none")
        self.start_run(where="vm")
        self.assertEqual(vp.sync(self.root, self.cfg, self.place)["action"], "pending")

    def test_foreign_pending_is_kept(self):
        self.set_state(meta={"work-pending": "true", "work-note": "auto: ninja", "spot-guard": "on"})
        self.assertEqual(vp.sync(self.root, self.cfg, self.place)["action"], "keep_foreign")
        self.assertEqual(self.meta()["work-pending"], "true")
        self.cfg["release_foreign_pending"] = True
        self.assertEqual(vp.sync(self.root, self.cfg, self.place)["action"], "done")

    def test_preempt_only_raises(self):
        self.set_state(meta={"work-pending": "true", "work-note": "runs: r_x"})
        self.assertEqual(vp.sync(self.root, self.cfg, self.place, only_raise=True)["action"], "none")
        self.assertEqual(self.meta()["work-pending"], "true")
        self.set_state(meta={"work-pending": "false"})
        self.start_run()
        self.assertEqual(vp.sync(self.root, self.cfg, self.place, only_raise=True)["action"], "pending")

    def test_dry_run_changes_nothing(self):
        self.start_run()
        self.assertEqual(vp.sync(self.root, self.cfg, self.place, dry=True)["action"], "pending")
        self.assertEqual(self.meta()["work-pending"], "false")

    def test_no_access_without_gcloud(self):
        with mock.patch.dict(os.environ, {"PATH": "/nonexistent"}):
            self.start_run()
            self.assertEqual(vp.sync(self.root, self.cfg, self.place)["action"], "no_access")

    def test_worktree_roots_are_scanned(self):
        wt = self.root / ".claude" / "worktrees" / "a1"
        (wt / "docs" / "reports" / "runs").mkdir(parents=True)
        rid = rr.start(wt, self.rcfg, "package.run", where=self.place)["run_id"]
        self.cfg["scan_roots"] = ["{root}", "{root}/.claude/worktrees/*"]
        self.assertEqual([r["run_id"] for r in vp.running_here(self.root, self.cfg, self.place)], [rid])


class TestHeartbeat(VmPlaceBase):
    OBS = {"host": "h", "boot": "2026-10-26T10:00:00Z", "disk_free_gb": 10.0}

    def test_heartbeat_line_and_reboot_event(self):
        rid = self.start_run()
        a = vp.heartbeat(self.root, self.cfg, self.place, at="2026-10-26T11:00:00Z", obs=dict(self.OBS))
        self.assertEqual(a["running"], [rid])
        self.assertEqual(a["spot_guard"]["action"], "pending")
        self.assertNotIn("rebooted", a)
        b = vp.heartbeat(self.root, self.cfg, self.place, at="2026-10-26T12:30:00Z",
                         obs={**self.OBS, "boot": "2026-10-26T12:25:00Z"})
        self.assertEqual(b["rebooted"]["last_beat_before"], "2026-10-26T11:00:00Z")
        ev = [e for e in self.rec(rid)["events"] if e["kind"] == "place_rebooted"]
        self.assertEqual(len(ev), 1)
        self.assertIn(f"resume {rid}", ev[0]["text"])
        lines = vp.heartbeat_file(self.root, self.cfg, self.place).read_text().splitlines()
        self.assertEqual(len(lines), 2)
        self.assertEqual(json.loads(lines[-1])["schema"], "loom.place_heartbeat/1")

    def test_stop_request_pauses_runs_once(self):
        rid = self.start_run()
        other = self.start_run(where="cloud")
        g = self.root / "docs" / "spec" / "gestures_state.json"
        g.parent.mkdir(parents=True)
        g.write_text(json.dumps({"stops": [{"at": "2026-10-26T11:00:00Z", "by": "owner", "scope": "wszystko w toku",
                                            "running": [], "state": "requested"}]}))
        hb = vp.heartbeat(self.root, self.cfg, self.place, at="2026-10-26T11:05:00Z", obs=dict(self.OBS))
        self.assertEqual(hb["stops"][0]["runs"], [rid])
        self.assertEqual(self.rec(rid)["state"], "paused")
        self.assertEqual(self.rec(other)["state"], "running")  # inne miejsce: nie ruszone
        self.assertEqual(json.loads(g.read_text())["stops"][0]["handled"][0]["place"], self.place)
        hb2 = vp.heartbeat(self.root, self.cfg, self.place, at="2026-10-26T11:15:00Z", obs=dict(self.OBS))
        self.assertNotIn("stops", hb2)

    def test_stop_for_other_scope_ignored(self):
        rid = self.start_run()
        g = self.root / "docs" / "spec" / "gestures_state.json"
        g.parent.mkdir(parents=True)
        g.write_text(json.dumps({"stops": [{"at": "x", "scope": "agent S2", "running": [], "state": "requested"}]}))
        vp.heartbeat(self.root, self.cfg, self.place, at="2026-10-26T11:05:00Z", obs=dict(self.OBS), do_sync=False)
        self.assertEqual(self.rec(rid)["state"], "running")


class TestWake(VmPlaceBase):
    def beat(self, at):
        vp.heartbeat(self.root, self.cfg, self.place, at=at, obs={"boot": "b"}, do_sync=False)

    def started(self):
        return [c for c in self.calls() if c[2] == "start"]

    def test_idle_does_nothing(self):
        self.set_state(status="TERMINATED")
        self.assertEqual(vp.wake(self.root, self.cfg, self.place, at="2026-10-26T12:00:00Z")["action"], "none")
        self.assertEqual(self.started(), [])

    def test_stale_heartbeat_and_stopped_vm_starts(self):
        rid = self.start_run()
        self.beat("2026-10-26T10:00:00Z")
        self.set_state(status="TERMINATED")
        r = vp.wake(self.root, self.cfg, self.place, dry=True, at="2026-10-26T12:00:00Z")
        self.assertEqual(r["action"], "start")
        self.assertEqual(self.started(), [])
        r = vp.wake(self.root, self.cfg, self.place, at="2026-10-26T12:00:00Z")
        self.assertEqual(r["action"], "start")
        self.assertEqual(len(self.started()), 1)
        self.assertTrue(any(e["kind"] == "vm_wake" for e in self.rec(rid)["events"]))

    def test_fresh_heartbeat_or_running_vm_or_spot_guard(self):
        self.start_run()
        self.beat("2026-10-26T11:55:00Z")
        self.set_state(status="TERMINATED")
        self.assertEqual(vp.wake(self.root, self.cfg, self.place, at="2026-10-26T12:00:00Z")["why"], "tętno świeże")
        self.set_state(status="RUNNING")
        self.assertEqual(vp.wake(self.root, self.cfg, self.place, at="2026-10-26T13:00:00Z")["action"], "none")
        self.set_state(status="TERMINATED", meta={"spot-guard": "on", "work-pending": "true"})
        self.assertEqual(vp.wake(self.root, self.cfg, self.place, at="2026-10-26T13:00:00Z")["action"], "defer")
        self.cfg["defer_to_spot_guard"] = False
        self.assertEqual(vp.wake(self.root, self.cfg, self.place, at="2026-10-26T13:00:00Z")["action"], "start")


class TestScriptsAndData(unittest.TestCase):
    def test_shell_syntax(self):
        for f in ("preempt_hook.sh", "work.sh", "install_heartbeat.sh", "install_spot_guard.sh"):
            r = subprocess.run(["bash", "-n", str(TOOLS / "gcp" / f)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, f + r.stderr)

    def test_preempt_hook_consults_run_records_before_procs(self):
        t = (TOOLS / "gcp" / "preempt_hook.sh").read_text()
        self.assertLess(t.index("vm_place.py"), t.index("spot-guard-busy-procs)"))

    def test_places_vm_node_has_dated_fields(self):
        d = json.loads((TOOLS.parent / "docs" / "ops" / "places.json").read_text(encoding="utf-8"))
        vm = next(p for p in d["places"] if p["id"] == vp.load_config()["place"])
        for f in ("tools", "facts", "heartbeat", "spot"):
            self.assertIn(f, vm)
        for item in vm["tools"] + vm["facts"]:
            self.assertIn("observed_at", item)
            self.assertIn("source", item)


if __name__ == "__main__":
    unittest.main()
