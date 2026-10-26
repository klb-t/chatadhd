"""IW-6: start_gate (tools/start_gate.py + tools/routines/roles.json). python3 -m unittest discover -s tools/tests"""
import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import start_gate as sg  # noqa: E402

HARNESS = HERE / "single_package_prompts.mjs"


def gate(*argv):
    buf = io.StringIO()
    with redirect_stdout(buf):
        rc = sg.main(["--json", "--disk-free-gb", "50", *argv])
    return rc, json.loads(buf.getvalue())


class GateTest(unittest.TestCase):
    NOW = "2026-10-26T18:00:00Z"

    def test_role_preset_resolves(self):
        rc, c = gate("--role", "package_executor", "--used", "71", "--now", self.NOW)
        self.assertEqual((rc, c["model"]["model"], c["model"]["why"]), (0, "sonnet", "preset roli"))
        self.assertTrue(c["verdict"].startswith("start"))
        self.assertEqual(c["place"]["id"], "vm:devbox-20260923")

    def test_fable_observation_substitutes_until_reset_only(self):
        rc, c = gate("--role", "designer", "--used", "71", "--now", self.NOW)
        self.assertEqual(c["model"]["model"], "opus")
        self.assertEqual(c["model"]["substituted"][0]["from"], "fable")
        self.assertTrue(any("obs_fable_weekly" in a for a in c["advisories"]))
        rc, c = gate("--role", "designer", "--used", "10", "--now", "2026-10-27T10:30:00Z")
        self.assertEqual(c["model"]["model"], "fable")
        rc, c = gate("--role", "designer", "--used", "71", "--now", self.NOW, "--ignore-observations")
        self.assertEqual(c["model"]["model"], "fable")

    def test_missing_model_is_advisory_not_stop(self):
        rc, c = gate("--role", "nowa_rola", "--used", "71", "--now", self.NOW)
        self.assertTrue(c["model"]["inherited"])
        self.assertEqual(c["verdict"], "start_with_advisories")
        self.assertTrue(any("dziedziczony" in a for a in c["advisories"]))

    def test_cap_r91_all_models_90(self):
        rc, c = gate("--role", "package_executor", "--used", "89.8", "--now", self.NOW)
        self.assertFalse(c["window"]["cap_ok"])
        self.assertEqual((c["verdict"], rc), ("wait", 2))
        rc, c = gate("--role", "package_executor", "--used", "89.8", "--now", self.NOW, "--log-only")
        self.assertEqual((c["verdict"], rc, c["log_only"]), ("wait", 0, True))

    def test_fable_over_100_is_owner_money(self):
        rc, c = gate("--role", "designer", "--model", "fable", "--used", "50", "--used-model", "fable=99.5",
                     "--now", self.NOW, "--ignore-observations")
        self.assertEqual(c["window"]["cap_key"], "fable")
        self.assertEqual((c["verdict"], rc), ("owner", 3))

    def test_x10_goes_to_owner(self):
        rc, c = gate("--role", "package_executor", "--used", "10", "--plan-calls", "500", "--now", "2026-10-20T09:00:00Z")
        self.assertIn("owner", [x["class"] for x in c["exceeds"]])

    def test_disk_before_native_build(self):
        buf = io.StringIO()
        with redirect_stdout(buf):
            sg.main(["--json", "--role", "native_package_executor", "--used", "71", "--now", self.NOW, "--disk-free-gb", "3"])
        c = json.loads(buf.getvalue())
        self.assertFalse(c["place"]["disk_ok"])
        self.assertEqual(c["verdict"], "wait")

    def test_override_differs_from_preset_is_advisory(self):
        rc, c = gate("--role", "checker", "--model", "opus", "--used", "71", "--now", self.NOW)
        self.assertEqual(c["model"]["model"], "opus")
        self.assertTrue(any("inny niż preset" in a for a in c["advisories"]))

    def test_property_every_role_and_model_gives_a_card(self):
        roles = json.loads(sg.ROLES.read_text(encoding="utf-8"))
        for r in list(roles["roles"]) + ["?"]:
            for m in [None, *[k for k in roles["models"] if not k.startswith("_")], "nieznany"]:
                for used in ("0", "50", "89.9", "100"):
                    rc, c = gate("--role", r, *(["--model", m] if m else []), "--used", used, "--now", self.NOW)
                    self.assertIn(c["verdict"], sg.EXIT)
                    self.assertIn(rc, (0, 2, 3))

    def test_run_record_gets_resolved_and_receipt(self):
        with tempfile.TemporaryDirectory() as d:
            rr = [sys.executable, str(HERE.parent / "run_record.py"), "--root", d]
            rid = subprocess.run(rr + ["start", "--routine", "t"], capture_output=True, text=True, check=True).stdout.strip()
            gate("--role", "designer", "--used", "71", "--now", self.NOW, "--run-id", rid, "--runs-root", d)
            rec = json.loads(next(Path(d).rglob(f"{rid}.json")).read_text(encoding="utf-8"))
            self.assertEqual(rec["resolved"]["model"], "opus")
            self.assertEqual(rec["resolved"]["via"], "start_gate")
            self.assertEqual(rec["receipt"][-1]["reliability"], "estimate")

    @unittest.skipUnless(shutil.which("node"), "node niedostępny")
    def test_single_package_uses_card_model(self):
        rc, c = gate("--role", "designer", "--used", "71", "--now", self.NOW)
        with tempfile.TemporaryDirectory() as d:
            p1, p2 = Path(d) / "a.json", Path(d) / "b.json"
            base = {"wt": "/w", "branch": "b", "id": "X", "scope": "s", "accept": "a"}
            p1.write_text(json.dumps({**base, "start_gate": c}), encoding="utf-8")
            p2.write_text(json.dumps(base), encoding="utf-8")
            out = json.loads(subprocess.run(["node", str(HARNESS), str(p1), str(p2)], capture_output=True, text=True,
                                            timeout=60, check=True).stdout)
        self.assertEqual(out[0]["prompts"][0]["model"], "opus")
        self.assertIn("START GATE (IW-6)", out[0]["prompts"][0]["prompt"])
        self.assertIsNone(out[1]["prompts"][0]["model"])


if __name__ == "__main__":
    unittest.main()
