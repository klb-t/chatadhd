"""engine.config / engine.paths <-> Loom Config, Secrets and data-dir resolution."""
import json
import os
import shutil
import stat
import unittest

from compat_common import CompatTestCase, tool
from engine.config import DEFAULTS, Config, Secrets
from engine.paths import resolve_data_dir


class ConfigCompatTest(CompatTestCase):

    def test_loom_written_config_is_read_by_python_without_upgrade(self):
        d = self.tmp.path
        tool("config-set", d, self.tmp.file_json("patch.json", {
            "config": {"theme": "amoled", "temperature": 0.3, "default_model": "x/ż", "custom": {"a": [1, None]}},
            "secrets": {"api_key": "sk-zażółć", "github_token": "ghp_x"},
        }))
        text_before = (d / "config.json").read_text(encoding="utf-8")
        cfg = Config(d / "config.json")
        self.assertEqual((d / "config.json").read_text(encoding="utf-8"), text_before, "Python rewrote the file")
        self.assertEqual(cfg.get("theme"), "amoled")
        self.assertEqual(cfg.get("temperature"), 0.3)
        self.assertEqual(cfg.get("custom"), {"a": [1, None]})
        for k, v in DEFAULTS.items():
            if k not in ("theme", "temperature", "default_model"):
                self.assertEqual(cfg.get(k), v, k)
        # Byte-identical to what Python's own save() would write.
        self.assertEqual(text_before, json.dumps(cfg._data, indent=2, ensure_ascii=False))
        sec = Secrets(d / "secrets.json")
        self.assertEqual(sec.get("api_key"), "sk-zażółć")
        self.assertEqual(stat.S_IMODE(os.stat(d / "secrets.json").st_mode), 0o600)
        self.assertFalse((d / "config.tmp").exists())

    def test_python_written_config_is_read_by_loom(self):
        d = self.tmp.path
        cfg = Config(d / "config.json")
        cfg.set("theme", "amoled")
        cfg.set("graph_memory_depth", 3)
        cfg.set("nested", {"k": "ł"})
        cfg.save()
        sec = Secrets(d / "secrets.json")
        sec.set("api_key", "sk-1")
        sec.save()
        out = tool("config-load", d)
        self.assertEqual(out["config"], json.loads((d / "config.json").read_text(encoding="utf-8")))
        self.assertEqual(list(out["config"].keys()), list(cfg._data.keys()))
        self.assertFalse(out["upgraded_on_load"])
        self.assertEqual(out["secret_keys"], ["api_key"])
        self.assertTrue(out["has_api_key"])

    def test_auto_upgrade_is_identical(self):
        legacy = {"semantic_model": "anthropic/claude-haiku-4-5", "theme": "amoled", "old_key": 1}
        for name, content in (("bad_model", legacy), ("already_v3", dict(legacy, _config_version=3)),
                              ("clean", {"theme": "dark"})):
            with self.subTest(case=name):
                a = self.tmp.path / name / "py"
                b = self.tmp.path / name / "cpp"
                for p in (a, b):
                    p.mkdir(parents=True)
                    (p / "config.json").write_text(json.dumps(content), encoding="utf-8")
                py_cfg = Config(a / "config.json")
                out = tool("config-load", b)
                self.assertEqual(out["config"], py_cfg._data)
                self.assertEqual((a / "config.json").read_bytes(), (b / "config.json").read_bytes())

    def test_corrupt_config_falls_back_to_defaults_like_python(self):
        for content in ("{not json", "[1, 2]", ""):
            d = self.tmp.path / f"c{abs(hash(content))}"
            d.mkdir()
            (d / "config.json").write_text(content, encoding="utf-8")
            py_cfg = Config(d / "config.json")
            out = tool("config-load", d)
            self.assertEqual(out["config"], py_cfg._data)

    def test_data_dir_resolution_matches_python(self):
        home = self.tmp.path / "home"
        home.mkdir()
        env = dict(os.environ, HOME=str(home))
        env.pop("CHATADHD_DATA", None)
        env.pop("XDG_DATA_HOME", None)
        loom_path = tool("paths-resolve", env=env)["path"]
        self.assertEqual(loom_path, str(home / ".chatadhd"))
        for sub in (".chatadhd_data", "attachments", "exports", "logs"):
            self.assertTrue((home / ".chatadhd" / sub).exists(), sub)
        sentinel = (home / ".chatadhd" / ".chatadhd_data").read_text(encoding="utf-8")
        shutil.rmtree(home / ".chatadhd")
        old_home = os.environ.get("HOME")
        try:
            os.environ["HOME"] = str(home)
            os.environ.pop("CHATADHD_DATA", None)
            os.environ.pop("XDG_DATA_HOME", None)
            py_path = resolve_data_dir()
            self.assertEqual(str(py_path), loom_path)
            self.assertEqual((py_path / ".chatadhd_data").read_text(encoding="utf-8"), sentinel)
            # Explicit CHATADHD_DATA with ~ and .. is resolved the same way.
            env2 = dict(env, CHATADHD_DATA="~/x/../data dir")
            os.environ["CHATADHD_DATA"] = "~/x/../data dir"
            self.assertEqual(tool("paths-resolve", env=env2)["path"], str(resolve_data_dir()))
            # Explicit override wins over the environment.
            override = str(self.tmp.path / "over")
            self.assertEqual(tool("paths-resolve", override, env=env2)["path"], str(resolve_data_dir(override)))
        finally:
            os.environ.pop("CHATADHD_DATA", None)
            if old_home is not None:
                os.environ["HOME"] = old_home


if __name__ == "__main__":
    unittest.main(verbosity=2)
