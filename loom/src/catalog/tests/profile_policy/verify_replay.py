#!/usr/bin/env python3
"""Check exact default replay and actual native profile-policy overrides."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def canonical(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    root = args.evidence
    checks = []

    def check(condition: bool, name: str) -> None:
        checks.append({"name": name, "passed": bool(condition)})

    def read(name: str) -> dict:
        return json.loads((root / name).read_text())

    before, after = read("probe-before-default.json"), read("probe-after-default.json")
    check(canonical(before) == canonical(after), "entire default native probe identical")
    check(after["profile"]["ok"] and after["default_profile"]["ok"] and after["scan"]["ok"],
          "profiles and scan actually executed")
    check(len(after["units"]) == 1 and bool(after["units"][0]["mentions"]),
          "native scan has a real unit and alias mentions")
    profile = after["profile"]["value"]
    check(any(t["class"] == "path" for t in profile["terms"]), "profile has authored repo terms")
    check(any(t["class"] == "principle" for t in profile["terms"]), "profile has principle terms")
    for sample in after["standalone_alias_weights"]:
        check(sample["mentions"] == after["standalone_alias_weights"][0]["mentions"],
              "alias matching ignores unused weight: " + str(sample["input_weight"]))
    observations = after["standalone_alias_weights"][0]["mentions"]
    hits = [hit for row in observations for hit in row["hits"]]
    versions = [hit for row in observations for hit in row["versions"]]
    check(any(h["key"] == "folio" and not h["trap"] for h in hits), "exact Folio alias actually matched")
    check(any(h["key"] == "4.7" for h in versions), "alias-bound version actually matched")
    check(any(h["key"] == "warden" for h in hits) and any(h["key"] == "deposits" for h in hits),
          "both authored Polish inflection cases actually matched")
    check(any(h["kind"] == "principle" for h in hits), "standalone principle actually matched")
    check(any(h["key"] == "boreal" and h["trap"] for h in hits), "standalone negative context actually matched")

    old_overlay, new_overlay = read("probe-before-weights.json"), read("probe-after-weights.json")
    expected = {"alias": -4.25, "principle": 0.0, "path": 6.75}
    terms = new_overlay["profile"]["value"]["terms"]
    check(all(t["weight"] == expected[t["class"]] for t in terms), "overlay reaches every emitted term")
    check(old_overlay["profile"]["value"]["terms"] != terms, "baseline exposes the old hidden weights")

    def content_only(value: dict) -> dict:
        result = json.loads(json.dumps(value))
        result.pop("id"); result.pop("input_hash")
        for term in result["terms"]:
            term.pop("weight")
        return result

    check(content_only(profile) == content_only(new_overlay["profile"]["value"]),
          "override preserves all other profile fields and ordering")
    check(after["profile_mentions"] == new_overlay["profile_mentions"],
          "override preserves profile alias matching")
    for mode in ("missing-alias", "missing-principle", "empty-map"):
        bad = read("probe-after-" + mode + ".json")
        check(bad["pack"]["ok"] and not bad["profile"]["ok"] and not bad["scan"]["ok"],
              mode + " fails explicitly in native catalog")
        check(bad["tables_before"] == bad["tables_after_profile"] == bad["tables_after_scan"],
              mode + " leaves catalog tables untouched")
    path = read("probe-after-missing-path.json")
    check(not path["profile"]["ok"] and path["default_profile"]["ok"] and path["scan"]["ok"],
          "path weight required only when repo terms are generated")
    check(path["tables_before"] == path["tables_after_profile"], "missing path rejects before writes")
    for mode in ("missing-map", "invalid-map", "invalid-alias"):
        check(not read("probe-after-" + mode + ".json")["pack"]["ok"],
              mode + " rejected by existing pack validator")

    dev_before, dev_after = read("dev-before.json"), read("dev-after.json")
    for key in ("native_profiles", "conversations", "auxiliary_documents", "native_score_summary", "summary"):
        check(canonical(dev_before[key]) == canonical(dev_after[key]), "entire DEV " + key + " identical")
    def stage_content(value: dict) -> dict:
        # Task identities belong to fresh runtime execution; keep every
        # stage input/output hash, stats, cache/resume flag and root summary.
        return {**{k: v for k, v in value.items() if k not in {"task_id", "stages"}},
                "stages": [{k: v for k, v in stage.items() if k != "task_id"}
                           for stage in value["stages"]]}
    check(canonical(stage_content(dev_before["native_preparation"])) ==
          canonical(stage_content(dev_after["native_preparation"])),
          "DEV preparation identical except fresh task identities")
    rows = dev_after["conversations"] + dev_after["auxiliary_documents"]
    check(len(rows) == 68 and len({r["unit_id"] for r in rows}) == 68, "all 68 DEV records compared")
    check(dev_before["inputs"]["fixture_sha256"] == dev_after["inputs"]["fixture_sha256"],
          "DEV fixture bytes identical")
    check(dev_before["inputs"]["native_pack"] == dev_after["inputs"]["native_pack"],
          "effective pack unchanged")
    check(dev_before["inputs"]["library"]["sha256"] != dev_after["inputs"]["library"]["sha256"],
          "before and after are distinct built binaries")
    inputs = sorted(root.glob("probe-*.json")) + [root / "dev-before.json", root / "dev-after.json"]
    receipt = {"valid": all(c["passed"] for c in checks), "checks": checks,
               "dev_summary": dev_after["summary"],
               "compared_dev_records": len(rows),
               "default_profile_terms": len(after["default_profile"]["value"]["terms"]),
               "repo_profile_terms": len(profile["terms"]),
               "preparation_identity_exclusions": ["/task_id", "/stages/*/task_id"],
               "sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
    (root / "replay-verification.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n")
    failed = [c["name"] for c in checks if not c["passed"]]
    if failed:
        raise SystemExit("Failed replay checks: " + "; ".join(failed))
    print(f"Exact replay verified: {len(checks)} checks, all {len(rows)} DEV records.")


if __name__ == "__main__":
    main()
