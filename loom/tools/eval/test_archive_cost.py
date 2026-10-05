import contextlib
import hashlib
import importlib.util
import io
import json
from copy import deepcopy
import sqlite3
import tempfile
import unittest
from pathlib import Path
from types import MappingProxyType
from unittest import mock

import archive_cost


def make_db(path):
    con = sqlite3.connect(path)
    con.execute("CREATE TABLE messages(id TEXT, conv_id TEXT, role TEXT, text TEXT, status TEXT)")
    con.executemany("INSERT INTO messages VALUES (?,?,?,?,?)", [
        ("m1", "c1", "user", "a" * 1200, "active"),
        ("m2", "c1", "assistant", "b" * 2400, "active"),
        ("m3", "c1", "user", "c" * 600, "version"),
        ("m4", "c2", "user", "d" * 1200, "active"),
        ("m5", "c2", "assistant", "e" * 999, "excluded"),
        ("m6", "c2", "assistant", "f" * 5000, "deleted"),
    ])
    con.commit()
    con.close()


class ArchiveCostTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.db = Path(self.tmp.name) / "chatadhd.db"
        make_db(self.db)
        self.pricing = json.loads(archive_cost.PRICING.read_text(encoding="utf-8"))

    def tearDown(self):
        self.tmp.cleanup()

    def test_stats_split_by_status(self):
        s = archive_cost.archive_stats(self.db)
        self.assertEqual(s["conversations"], 2)
        self.assertEqual(s["messages"], 5)  # deleted never counted
        self.assertEqual(s["chars_active"], 4800)
        self.assertEqual(s["chars_versions"], 600)
        self.assertEqual(s["chars_excluded"], 999)
        self.assertEqual(s["chars_by_role"], {"assistant": 2400, "user": 2400})

    def test_estimate_arithmetic(self):
        s = archive_cost.archive_stats(self.db)
        e = archive_cost.estimate(s, self.pricing, output_ratio=0.5, prefix_tokens=0)
        self.assertEqual(e["tokens"], {"low": 1350, "high": 1800})  # 5400 chars / 4 and / 3
        p = self.pricing["models"]["claude-opus-5-5"]
        want = round((1800 * p["input"] + 900 * p["output"]) / 1e6, 2)
        self.assertEqual(e["models"]["claude-opus-5-5"]["high"], want)
        self.assertEqual(e["models"]["claude-opus-5-5"]["high_batch"], round(want * 0.5, 2))

    def test_fraction_and_versions(self):
        s = archive_cost.archive_stats(self.db)
        e = archive_cost.estimate(s, self.pricing, fraction=0.5, include_versions=False)
        self.assertEqual(e["tokens"]["high"], 800)  # 4800 * 0.5 / 3
        with self.assertRaises(ValueError):
            archive_cost.estimate(s, self.pricing, fraction=0)

    def test_db_opened_read_only(self):
        before = self.db.read_bytes()
        archive_cost.main(["--db", str(self.db), "--json"])
        self.assertEqual(self.db.read_bytes(), before)

    def test_reserved_uri_characters_open_exact_file_without_creating_sibling(self):
        for name in ("literal?archive.db", "literal#archive.db", "percent%20 space.db"):
            with self.subTest(name=name):
                path = Path(self.tmp.name) / name
                make_db(path)
                before = path.read_bytes()
                names = {p.name for p in path.parent.iterdir()}
                self.assertEqual(archive_cost.archive_stats(path)["chars_active"], 4800)
                self.assertEqual(path.read_bytes(), before)
                self.assertEqual({p.name for p in path.parent.iterdir()}, names)

    def test_missing_database_is_not_created(self):
        path = Path(self.tmp.name) / "absent?new.db"
        names = {p.name for p in path.parent.iterdir()}
        with self.assertRaises(sqlite3.OperationalError):
            archive_cost.archive_stats(path)
        self.assertEqual({p.name for p in path.parent.iterdir()}, names)

    def test_even_conversation_median_uses_both_middle_sizes(self):
        stats=archive_cost.archive_stats(self.db)
        # c1=4200, c2=1200. The prior upper-middle value4200 overstated this.
        self.assertEqual(stats["conversation_chars"], {"median":2700,"max":4200})
        with sqlite3.connect(self.db) as con:
            con.execute("DELETE FROM messages")
        self.assertEqual(archive_cost.archive_stats(self.db)["conversation_chars"],
                         {"median":0,"max":0})

    def test_unknown_status_counted_explicitly_and_configurable(self):
        with sqlite3.connect(self.db) as con:
            con.executemany("INSERT INTO messages VALUES (?,?,?,?,?)", [
                ("null", "c3", "user", "a"*60, None),
                ("future", "c3", "assistant", "b"*60, "future_status"),
            ])
        stats = archive_cost.archive_stats(self.db)
        self.assertEqual(stats["messages"], 7)
        self.assertEqual(stats["conversations"], 3)
        self.assertEqual(stats["chars_active"], 4800)
        self.assertEqual(stats["unknown_status_messages"], 2)
        self.assertEqual(stats["chars_unknown_status"], 120)
        self.assertEqual(archive_cost.estimate(stats,self.pricing)["tokens"]["high"],1840)
        self.assertEqual(archive_cost.estimate(stats,self.pricing,include_unknown_status=False)["tokens"]["high"],1800)

    def test_invalid_parameters_rejected_before_cost(self):
        stats=archive_cost.archive_stats(self.db)
        for field in ("output_ratio", "prefix_tokens", "fraction"):
            for value in (-1, float("nan"), float("inf"), float("-inf"), True, "1"):
                with self.subTest(field=field,value=value), self.assertRaises(ValueError):
                    archive_cost.estimate(stats,self.pricing,**{field:value})
        for field in ("include_versions","include_unknown_status"):
            with self.subTest(field=field),self.assertRaises(ValueError):
                archive_cost.estimate(stats,self.pricing,**{field:1})

    def test_invalid_counts_and_prices_rejected(self):
        stats=archive_cost.archive_stats(self.db)
        for field in ("chars_active","chars_versions","conversations","chars_unknown_status"):
            for value in (-1, True, 1.5, float("nan")):
                bad={**stats,field:value}
                with self.subTest(field=field,value=value),self.assertRaises(ValueError):
                    archive_cost.estimate(bad,self.pricing)
        for field in ("input","output","cache_read"):
            for value in (-1,True,float("nan"),float("inf")):
                bad=deepcopy(self.pricing)
                next(iter(bad["models"].values()))[field]=value
                with self.subTest(field=field,value=value),self.assertRaises(ValueError):
                    archive_cost.estimate(stats,bad)
        for value in (-1,True,float("nan"),float("inf")):
            with self.subTest(discount=value),self.assertRaises(ValueError):
                archive_cost.estimate(stats,{**self.pricing,"batch_discount":value})

    def test_large_finite_budget_supported_overflow_rejected(self):
        stats={"chars_active":10**12,"chars_versions":0,"conversations":10**9}
        pricing={"models":{"expensive":{"input":10**6,"output":10**6,"cache_read":10**6}}}
        out=archive_cost.estimate(stats,pricing,output_ratio=10,prefix_tokens=10**9)
        self.assertGreater(out["models"]["expensive"]["high"],10**9)
        huge={"chars_active":10**300,"chars_versions":0,"conversations":1}
        with self.assertRaises(ValueError):
            archive_cost.estimate(huge,pricing,output_ratio=1e300)
        for key in ("prefix_cache","batch"):
            self.assertIn("unverified",out["assumptions"][key])


class ArchiveCostRegressionTest(unittest.TestCase):
    """Eight synthetic audit regressions retained from the W5 evidence suite."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.db = Path(self.tmp.name) / "audit.db"
        with sqlite3.connect(self.db) as con:
            con.execute("CREATE TABLE messages(conv_id TEXT, role TEXT, text TEXT, status TEXT)")
            con.executemany("INSERT INTO messages VALUES (?,?,?,?)", [
                ("a", "user", "ą\x00🙂", "active"),
                ("a", "tool", "tools", "excluded"),
                ("a", "assistant", "saved", "version"),
                ("b", "user", "gone", "deleted"),
                ("c", "function", "call", "active"),
                ("d", None, "future", "new"),
                ("e", None, None, None),
            ])

    def tearDown(self):
        self.tmp.cleanup()

    def test_complete(self):
        stats = archive_cost.archive_stats(self.db)
        self.assertEqual(stats["raw"], {"conversations": 5, "messages": 7, "chars": 27})
        self.assertEqual(stats["tool_messages"], 2)
        self.assertEqual(stats["chars_tools"], 9)
        self.assertEqual(stats["chars_active"], 7)

    def test_nul_unicode(self):
        self.assertEqual(archive_cost.archive_stats(self.db)["chars_active"],
                         len("ą\x00🙂") + len("call"))

    def test_exact_projection_conversations(self):
        stats = archive_cost.archive_stats(self.db)
        projected = archive_cost.project_stats(stats, include_versions=False,
                                              include_unknown_status=False, include_tools=False)
        self.assertEqual((projected["conversations"], projected["messages"], projected["chars"]),
                         (1, 1, 3))
        projected = archive_cost.project_stats(stats, include_active=False, include_versions=False,
                                              include_unknown_status=False, include_deleted=True)
        self.assertEqual((projected["conversations"], projected["messages"], projected["chars"]),
                         (1, 1, 4))

    def test_no_rates(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            archive_cost.main(["--db", str(self.db), "--json"])
        result = json.loads(output.getvalue())
        self.assertIsNone(result["estimate"]["models"])
        self.assertEqual(result["stats"]["projected"]["chars"], 27)
        self.assertEqual(result["estimate"]["model_calls"], 0)
        self.assertEqual(result["estimate"]["local_import_model_cost_usd"], 0)
        self.assertIsNone(result["estimate"]["local_compute_cost_usd"])
        self.assertNotIn("local_import_cost_usd", result["estimate"])

    def test_rates_and_estimator(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            archive_cost.main(["--db", str(self.db), "--input-price", "2", "--output-price", "10",
                               "--chars-per-token-low", "5", "--chars-per-token-high", "2",
                               "--prefix-tokens", "4", "--scope", "active", "--no-tools", "--json"])
        estimate = json.loads(output.getvalue())["estimate"]
        self.assertEqual(estimate["prefix_tokens"], 4)
        self.assertAlmostEqual(estimate["models_unrounded"]["configured"]["high"],
                               (3 / 2 * 2 + 3 / 2 * .4 * 10 + 4 * 2) / 1e6)

    def test_bad_estimator_cache(self):
        for value in (0, -1, True):
            with self.assertRaises(ValueError):
                archive_cost.archive_stats(self.db, sqlite_cache_kib=value)
        stats = archive_cost.archive_stats(self.db)
        for low, high in ((0, 0), (3, 4), (4, float("nan"))):
            with self.assertRaises(ValueError):
                archive_cost.estimate(stats, chars_per_token_low=low, chars_per_token_high=high)

    def test_file_identical(self):
        before = self.db.read_bytes()
        names = list(Path(self.tmp.name).iterdir())
        archive_cost.archive_stats(self.db)
        self.assertEqual(self.db.read_bytes(), before)
        self.assertEqual(list(Path(self.tmp.name).iterdir()), names)

    def test_empty(self):
        with sqlite3.connect(self.db) as con:
            con.execute("DELETE FROM messages")
        stats = archive_cost.archive_stats(self.db)
        self.assertEqual(stats["raw"], {"conversations": 0, "messages": 0, "chars": 0})
        self.assertEqual(stats["conversation_chars"], {"median": 0, "max": 0})


class ArchiveAuditPresetTest(unittest.TestCase):
    """Authoritative preset data, complete effective values and call overrides."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.db = Path(self.tmp.name) / "audit.db"
        make_db(self.db)
        self.pack = json.loads(archive_cost.BUILTIN_AUDIT_PRESET.read_bytes())
        self.path = Path(self.tmp.name) / "import_audit.pack"

    def tearDown(self):
        self.tmp.cleanup()

    def write_pack(self, pack=None):
        self.path.write_text(json.dumps(self.pack if pack is None else pack), encoding="utf-8")
        return self.path

    def cli(self, *flags):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(archive_cost.main(["--db", str(self.db), *flags, "--json"]), 0)
        return json.loads(output.getvalue())

    def test_builtin_hash_and_distinct_api_cli_defaults(self):
        raw = archive_cost.BUILTIN_AUDIT_PRESET.read_bytes()
        info = archive_cost.inspect_audit_preset()
        self.assertEqual(info["sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(info["hash_scope"], "exact pack source bytes")
        self.assertEqual(info["values"], self.pack["values"])
        stats = archive_cost.archive_stats(self.db)
        api = archive_cost.estimate(stats)
        cli = self.cli()["estimate"]
        self.assertEqual(api["assumptions"]["prefix_tokens"], 8000)
        self.assertEqual(cli["assumptions"]["prefix_tokens"], 0)
        self.assertEqual(api["projected"]["chars"], 5400)
        self.assertEqual(cli["projected"]["chars"], 11399)
        self.assertNotIn("audit_preset", api)
        self.assertNotIn("sha256", api)
        self.assertEqual(archive_cost.PRICING, archive_cost._REPO_ROOT / info["values"]["historical_pricing"])

    def test_explicit_none_is_rejected_but_no_pricing_is_valid(self):
        stats = archive_cost.archive_stats(self.db)
        with self.assertRaises(ValueError):
            archive_cost.archive_stats(self.db, sqlite_cache_kib=None)
        numeric = ("fraction", "output_ratio", "prefix_tokens", "chars_per_token_low", "chars_per_token_high")
        boolean = ("include_active", "include_versions", "include_unknown_status",
                   "include_excluded", "include_deleted", "include_tools")
        for name in (*numeric, *boolean):
            with self.subTest(name=name), self.assertRaises(ValueError):
                archive_cost.estimate(stats, **{name: None})
        for name in boolean:
            with self.subTest(projection=name), self.assertRaises(ValueError):
                archive_cost.project_stats(stats, **{name: None})
        for call in (archive_cost.load_audit_preset, archive_cost.inspect_audit_preset):
            with self.assertRaises(ValueError):
                call(None)
        self.assertIsNone(archive_cost.estimate(stats, None)["models"])

    def test_custom_file_api_and_cli_presets(self):
        values = self.pack["values"]
        values.update(chars_per_token_low=10.0, chars_per_token_high=5.0, output_ratio=2.0,
                      fraction=.5, sqlite_cache_kib=256, api_prefix_tokens=7,
                      cli_prefix_tokens=2, api_include_versions=False, api_include_excluded=True,
                      cli_scope="active", cli_include_tools=False)
        path = self.write_pack()
        stats = archive_cost.archive_stats(self.db, audit_preset=path)
        api = archive_cost.estimate(stats, audit_preset=path)
        self.assertEqual(stats["execution"]["sqlite_cache_kib"], 256)
        self.assertEqual(api["projected"]["chars"], 5799)
        self.assertEqual(api["tokens"], {"low": 290, "high": 580})
        self.assertEqual(api["prefix_tokens"], 7)
        cli = self.cli("--audit-preset", str(path))
        self.assertEqual(cli["estimate"]["projected"]["chars"], 4800)
        self.assertEqual(cli["estimate"]["tokens"], {"low": 240, "high": 480})
        self.assertEqual(cli["estimate"]["prefix_tokens"], 2)

    def test_explicit_flags_and_api_arguments_override_preset(self):
        self.pack["values"].update(chars_per_token_low=10.0, chars_per_token_high=5.0,
                                  output_ratio=2.0, fraction=.5, sqlite_cache_kib=256,
                                  api_prefix_tokens=7, cli_prefix_tokens=2, cli_scope="active")
        path = self.write_pack()
        stats = archive_cost.archive_stats(self.db, audit_preset=path, sqlite_cache_kib=512)
        api = archive_cost.estimate(stats, audit_preset=path, output_ratio=0,
                                    prefix_tokens=0, fraction=1.0, include_versions=False)
        self.assertEqual(stats["execution"]["sqlite_cache_kib"], 512)
        self.assertEqual(api["tokens"], {"low": 480, "high": 960})
        self.assertEqual(api["output_tokens"], {"low": 0.0, "high": 0.0})
        self.assertEqual(api["prefix_tokens"], 0)
        cli = self.cli("--audit-preset", str(path), "--scope", "all", "--fraction", "1",
                       "--output-ratio", "0", "--prefix-tokens", "0", "--no-versions",
                       "--chars-per-token-low", "8", "--chars-per-token-high", "4",
                       "--sqlite-cache-kib", "512", "--no-include-excluded", "--no-include-deleted")
        self.assertEqual(cli["stats"]["execution"]["sqlite_cache_kib"], 512)
        self.assertEqual(cli["estimate"]["tokens"], {"low": 600, "high": 1200})
        self.assertEqual(cli["estimate"]["output_tokens"], {"low": 0.0, "high": 0.0})
        self.assertEqual(cli["estimate"]["prefix_tokens"], 0)

    def test_complete_effective_mapping_needs_no_builtin_file(self):
        values = MappingProxyType(dict(self.pack["values"]))
        with mock.patch.object(archive_cost, "BUILTIN_AUDIT_PRESET", Path(self.tmp.name) / "missing.pack"):
            stats = archive_cost.archive_stats(self.db, audit_preset=values)
            self.assertEqual(archive_cost.estimate(stats, audit_preset=values)["tokens"],
                             {"low": 1350, "high": 1800})
            info = archive_cost.inspect_audit_preset(values)
            canonical = json.dumps(dict(values), sort_keys=True, separators=(",", ":")).encode()
            self.assertEqual(info["sha256"], hashlib.sha256(canonical).hexdigest())
            self.assertEqual(info["hash_scope"], "canonical effective-values JSON")

    def test_cold_import_with_missing_or_bad_builtin_can_use_explicit_data(self):
        root = Path(self.tmp.name) / "sandbox"
        source = root / "loom/tools/eval/archive_cost.py"
        source.parent.mkdir(parents=True)
        source.write_bytes(Path(archive_cost.__file__).read_bytes())
        builtin = root / "loom/data/presets/import_audit.pack"
        for missing in (True, False):
            if not missing:
                builtin.parent.mkdir(parents=True)
                builtin.write_text('{"values":null}')
            spec = importlib.util.spec_from_file_location("sandbox_archive_cost", source)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            with self.subTest(missing=missing):
                stats = module.archive_stats(self.db, audit_preset=self.pack["values"])
                self.assertEqual(module.estimate(stats, audit_preset=self.pack["values"])["tokens"],
                                 {"low": 1350, "high": 1800})
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    self.assertEqual(module.main(["--db", str(self.db), "--audit-preset",
                                                  str(self.write_pack()), "--json"]), 0)
                self.assertIsNone(json.loads(output.getvalue())["estimate"]["models"])
                with self.assertRaises((FileNotFoundError, ValueError)):
                    module.load_audit_preset()

    def test_cli_can_reenable_preset_disabled_channels(self):
        with sqlite3.connect(self.db) as con:
            con.executemany("INSERT INTO messages VALUES(?,?,?,?,?)", [
                ("tool", "ctool", "tool", "call", "active"),
                ("unknown", "cunknown", "user", "future", "future_status"),
            ])
        self.pack["values"].update(cli_scope="active", cli_include_versions=False,
                                  cli_include_unknown_status=False, cli_include_tools=False)
        path = self.write_pack()
        self.assertEqual(self.cli("--audit-preset", str(path))["estimate"]["projected"]["chars"], 4800)
        flags = ("--audit-preset", str(path), "--include-versions", "--include-unknown-status")
        enabled = self.cli(*flags, "--include-tools")["estimate"]["projected"]
        self.assertEqual(enabled["chars"], 5410)
        self.assertEqual(enabled["scope"]["status_classes"], ["active", "version", "unknown"])
        self.assertTrue(enabled["scope"]["include_tools"])
        self.assertEqual(self.cli(*flags, "--include-tools", "--no-tools")["estimate"]["projected"]["chars"], 5406)
        self.assertEqual(self.cli(*flags, "--no-tools", "--include-tools")["estimate"]["projected"]["chars"], 5410)

    def test_missing_or_incomplete_data_does_not_fall_back(self):
        stats = archive_cost.archive_stats(self.db)
        missing = Path(self.tmp.name) / "missing.pack"
        with mock.patch.object(archive_cost, "BUILTIN_AUDIT_PRESET", missing):
            with self.assertRaises(FileNotFoundError):
                archive_cost.archive_stats(self.db, sqlite_cache_kib=512)
        incomplete = dict(self.pack["values"])
        del incomplete["output_ratio"]
        with self.assertRaises(ValueError):
            archive_cost.estimate(stats, audit_preset=incomplete, output_ratio=0)
        for value in ({}, {"enabled": False}, {"excluded": True}, {"values": self.pack["values"]}):
            with self.subTest(value=value), self.assertRaises(ValueError):
                archive_cost.load_audit_preset(value)

    def test_bad_numeric_data_and_disabled_envelopes_fail_before_db(self):
        bad_packs = []
        for name in ("fraction", "output_ratio", "sqlite_cache_kib", "api_prefix_tokens",
                     "cli_prefix_tokens", "chars_per_token_low", "chars_per_token_high", "pricing_batch_discount"):
            for value in (None, True, -1, float("nan"), float("inf")):
                pack = deepcopy(self.pack)
                pack["values"][name] = value
                bad_packs.append(pack)
        for change in ({"values": {}}, {"values": None}, {"enabled": False}, {"excluded": True}):
            bad_packs.append({**self.pack, **change})
        for index, pack in enumerate(bad_packs):
            path = self.write_pack(pack)
            with self.subTest(index=index), mock.patch.object(archive_cost.sqlite3, "connect") as connect:
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                    archive_cost.main(["--db", str(self.db), "--audit-preset", str(path)])
                self.assertEqual(error.exception.code, 2)
                connect.assert_not_called()

    def test_invalid_schema_version_types_and_flags(self):
        for change in ({"schema": "old"}, {"schema": "loom.import_audit_preset/2"},
                       {"version": 0}, {"version": -1}, {"version": None},
                       {"version": 1.0}, {"version": True},
                       {"id": None}, {"id": ""}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                archive_cost.load_audit_preset(self.write_pack({**self.pack, **change}))
        for name, value in (("cli_scope", None), ("cli_scope", "unsupported"),
                            ("historical_pricing", None), ("historical_pricing", ""),
                            ("api_include_active", None), ("cli_include_tools", 1),
                            ("native_active_only", None), ("sqlite_cache_kib", 0),
                            ("api_prefix_tokens", 1.5)):
            values = {**self.pack["values"], name: value}
            with self.subTest(name=name, value=value), self.assertRaises(ValueError):
                archive_cost.load_audit_preset(values)

    def test_positive_data_revision_two_changes_estimate_and_inspection(self):
        self.pack["version"] = 2
        self.pack["values"].update(output_ratio=3.0, chars_per_token_low=6.0, chars_per_token_high=2.0)
        path = self.write_pack()
        info = archive_cost.inspect_audit_preset(path)
        self.assertEqual(info["version"], 2)
        self.assertEqual(info["schema"], "loom.import_audit_preset/1")
        self.assertEqual(info["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual(archive_cost.load_audit_preset(path), self.pack["values"])
        stats = archive_cost.archive_stats(self.db, audit_preset=path)
        estimate = archive_cost.estimate(stats, audit_preset=path)
        self.assertEqual(estimate["tokens"], {"low": 900, "high": 2700})
        self.assertEqual(estimate["output_tokens"], {"low": 2700.0, "high": 8100.0})

    def test_source_edits_change_defaults_and_source_hash_without_cache(self):
        path = self.write_pack()
        with mock.patch.object(archive_cost, "BUILTIN_AUDIT_PRESET", path):
            stats = archive_cost.archive_stats(self.db)
            first = archive_cost.inspect_audit_preset()
            self.pack["values"].update(output_ratio=3.0, chars_per_token_low=6.0, chars_per_token_high=2.0)
            self.write_pack()
            second = archive_cost.inspect_audit_preset()
            estimate = archive_cost.estimate(stats)
            self.assertNotEqual(first["sha256"], second["sha256"])
            self.assertEqual(estimate["tokens"], {"low": 900, "high": 2700})
            self.assertEqual(estimate["output_tokens"], {"low": 2700.0, "high": 8100.0})
            self.assertEqual(second["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())

    def test_pricing_reference_opt_in_and_discount_from_data(self):
        values = {**self.pack["values"], "historical_pricing": "no-such-historical-table.json",
                  "pricing_batch_discount": 2.0}
        stats = archive_cost.archive_stats(self.db, audit_preset=values)
        self.assertIsNone(archive_cost.estimate(stats, audit_preset=values)["models"])
        pricing = {"models": {"mock": {"input": 1000, "output": 500, "cache_read": 0}}}
        estimate = archive_cost.estimate(stats, pricing, audit_preset=values)
        row = estimate["models_unrounded"]["mock"]
        self.assertEqual(row["high_batch"], row["high"] * 2)
        explicit = archive_cost.estimate(stats, {**pricing, "batch_discount": 0}, audit_preset=values)
        self.assertEqual(explicit["models_unrounded"]["mock"]["high_batch"], 0)


if __name__ == "__main__":
    unittest.main()
