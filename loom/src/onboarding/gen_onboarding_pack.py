"""Compile canonical data references and embed onboarding packs deterministically."""
from copy import deepcopy
from pathlib import Path
import argparse
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "data"
FILES = {
    "kOnboardingPack": DATA / "profiles/user.pack",
    "kOnboardingScenario": DATA / "onboarding/scenario.pack",
    "kOnboardingPresentation": DATA / "onboarding/ui.pack",
}
OUT = Path(__file__).with_name("builtin.inc")
CHUNK_BYTES = 60000
WEB_OUT = ROOT / "web/src/onboarding/generated/ui.json"
WEB_CONVERSATION_VIEW_OUT = ROOT / "web/src/onboarding/generated/conversation-view.json"
WEB_ANIMATION_OUT = ROOT / "web/src/shell/generated/ui-animation.json"


def read_document(path, trail=()):
    path = path.resolve()
    if not path.is_relative_to(DATA.resolve()):
        raise ValueError("data reference leaves the canonical data directory")
    if path in trail:
        raise ValueError("cyclic data reference")
    return expand(json.loads(path.read_bytes().decode("utf-8")), trail + (path,))


def expand(value, trail=()):
    if isinstance(value, dict):
        if "$include" in value:
            if set(value) != {"$include"} or not isinstance(value["$include"], str):
                raise ValueError("data include needs exactly one string reference")
            return read_document(DATA / value["$include"], trail)
        return {key: expand(item, trail) for key, item in value.items()}
    if isinstance(value, list):
        return [expand(item, trail) for item in value]
    return value


def assign_pointer(target, pointer, value):
    if not isinstance(pointer, str) or not pointer.startswith("/"):
        raise ValueError("runtime binding needs a non-root RFC6901 pointer")
    tokens = pointer[1:].split("/")
    for token in tokens:
        # An escape must be one of the two RFC6901 escape forms.
        cursor = 0
        while cursor < len(token):
            if token[cursor] == "~":
                if cursor + 1 == len(token) or token[cursor + 1] not in "01":
                    raise ValueError("malformed RFC6901 escape")
                cursor += 2
            else:
                cursor += 1
    tokens = [token.replace("~1", "/").replace("~0", "~") for token in tokens]
    cursor = target
    for token in tokens[:-1]:
        if token not in cursor:
            cursor[token] = {}
        if not isinstance(cursor[token], dict):
            raise ValueError("overlapping runtime binding pointers")
        cursor = cursor[token]
    if tokens[-1] in cursor:
        raise ValueError("overlapping runtime binding pointers")
    cursor[tokens[-1]] = deepcopy(value)


def compile_layer_bindings(pack):
    definition = pack.get("runtime_definition")
    if not isinstance(definition, dict):
        return pack
    directive = definition.get("defaults")
    if not isinstance(directive, dict) or "$layer_bindings" not in directive:
        return pack
    if set(directive) != {"$layer_bindings"}:
        raise ValueError("runtime defaults directive has additional members")
    binding_name = directive["$layer_bindings"]
    if not isinstance(binding_name, str) or not isinstance(pack.get(binding_name), dict):
        raise ValueError("runtime defaults directive names no binding map")
    entries = {}
    for entry in pack.get("entries", []):
        key = entry["key"]
        if key in entries:
            raise ValueError("duplicate default layer key")
        entries[key] = entry["value"]
    defaults = {}
    pointers = list(pack[binding_name])
    for first in pointers:
        for second in pointers:
            if first != second and second.startswith(first + "/"):
                raise ValueError("overlapping runtime binding pointers")
    for pointer, key in pack[binding_name].items():
        if key not in entries:
            raise ValueError("runtime binding names no default entry")
        assign_pointer(defaults, pointer, entries[key])
    definition["defaults"] = defaults
    return pack


def compile_entry_packs(pack):
    """Compose canonical entry definitions; native DefaultLayers owns resolution."""
    sources = pack.get("entry_packs", [])
    if not isinstance(sources, list):
        raise ValueError("entry_packs must be an array")
    entries = pack.get("entries")
    if not isinstance(entries, list):
        raise ValueError("default layer entries must be an array")
    for source in sources:
        if not isinstance(source, dict) or source.get("schema") != "loom.default_layers_pack/1" or not isinstance(source.get("entries"), list):
            raise ValueError("entry pack needs a default layers pack")
        if any(not isinstance(entry, dict) or not isinstance(entry.get("id"), str) for entry in source["entries"]):
            raise ValueError("entry pack entry needs stable id")
        entries.extend(deepcopy(source["entries"]))
    keys, ids = set(), set()
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("key"), str) or ("id" in entry and not isinstance(entry["id"], str)):
            raise ValueError("default layer entry needs key and id")
        if entry["key"] in keys or ("id" in entry and entry["id"] in ids):
            raise ValueError("duplicate default layer key or id")
        keys.add(entry["key"])
        if "id" in entry:
            ids.add(entry["id"])
    return pack


LOCALES = ("en", "pl")
PLACEHOLDERS = {"{topics}", "{asks}", "{stores}", "{model_sees}"}
SKIP_REASON_CODES = ("known", "declined", "never", "detail_above_level", "sensitivity_above_level", "privacy_denied", "setting", "level_not_chosen")


def canonical(value):
    """Same bytes as loom::json::canonical (sorted keys, compact, UTF-8 as is)."""
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def recipe_hash(scenario):
    return hashlib.sha256(canonical(scenario["graph_method"]).encode("utf-8")).hexdigest()


def need(condition, message):
    if not condition:
        raise ValueError("scenario: " + message)


def localized(value, label):
    """A text for the person exists in every language, and the Polish one is not a copy of the English one."""
    need(isinstance(value, dict) and set(value) == set(LOCALES), label + " needs exactly {en, pl}")
    for locale in LOCALES:
        need(isinstance(value[locale], str) and value[locale].strip(), label + "." + locale + " must be a non-empty string")
    need(value["pl"] != value["en"], label + ".pl must be translated, not copied from en")
    return value


def validate_scenario(scenario):
    """Schema checks of the interview scenario (texts in {en, pl}, levels, remark rules); the C++ side re-validates on load."""
    need(scenario.get("schema") == "loom.onboarding.scenario/1", "unknown schema")
    sections = scenario["sections"]
    section_ids = [section["id"] for section in sections]
    need(len(set(section_ids)) == len(section_ids), "duplicate section id")
    questions = {}
    for section in sections:
        localized(section["title"], "section " + section["id"] + ".title")
        for question in section["questions"]:
            localized(question["text"], "question " + question["field"])
            need(question["field"] not in questions, "two questions for " + question["field"])
            questions[question["field"]] = section["id"]
    categories = {}
    for category in scenario["categories"]:
        need(category["id"] not in categories, "duplicate category " + category["id"])
        localized(category["label"], "category " + category["id"] + ".label")
        categories[category["id"]] = bool(category.get("meta"))
    field_ids, used = set(), set()
    for field in scenario["fields"]:
        fid = field["id"]
        need(fid not in field_ids, "duplicate field " + fid)
        field_ids.add(fid)
        need(field["section"] in section_ids, "field " + fid + " names an unknown section")
        need(questions.get(fid) == field["section"], "field " + fid + " needs its question in its own section")
        need(field["category"] in categories, "field " + fid + " names an unlisted category")
        used.add(field["category"])
        for key in ("detail", "sensitivity"):
            need(isinstance(field[key], (int, float)) and field[key] >= 0, "field " + fid + "." + key + " must be a nonnegative number")
        localized(field["topic"], "field " + fid + ".topic")
        need(("setting_key" in field) == categories[field["category"]], "field " + fid + ": exactly the fields of a meta category carry setting_key")
    need(used == set(categories), "a category without any field")
    levels = scenario["detail_levels"]
    need(len(levels) >= 1 and len({level["id"] for level in levels}) == len(levels), "detail_levels need unique ids")
    previous = (0, 0)
    for level in levels:
        localized(level["label"], "level " + level["id"] + ".label")
        caps = []
        for key in ("max_detail", "max_sensitivity"):
            value = level[key]
            need(value is None or (isinstance(value, (int, float)) and value >= 0), "level " + level["id"] + "." + key + " must be null or nonnegative")
            caps.append(float("inf") if value is None else value)
        need(caps[0] >= previous[0] and caps[1] >= previous[1], "level caps must not shrink from one level to the next")
        previous = (caps[0], caps[1])
        for part, placeholder in (("asks", "{asks}"), ("stores", "{stores}"), ("model_sees", "{model_sees}")):
            text = localized(level["preview"][part], "level " + level["id"] + ".preview." + part)
            for locale in LOCALES:
                need(placeholder in text[locale], "level " + level["id"] + ".preview." + part + "." + locale + " must name " + placeholder)
                need(not any(ch.isdigit() for ch in text[locale]), "preview texts carry no digits of their own: the numbers are computed")
                for token in re.findall(r"\{[^}]*\}", text[locale]):
                    need(token in PLACEHOLDERS, "unknown placeholder " + token)
    need(set(scenario["skip_reasons"]) == set(SKIP_REASON_CODES), "skip_reasons must cover exactly " + ", ".join(SKIP_REASON_CODES))
    for code, text in scenario["skip_reasons"].items():
        localized(text, "skip_reasons." + code)
    for locale in LOCALES:
        need(isinstance(scenario["remark_negators"][locale], list), "remark_negators." + locale + " must be a list")
    rule_ids, never_categories = set(), {locale: set() for locale in LOCALES}
    for rule in scenario["remark_rules"]:
        need(rule["id"] not in rule_ids, "duplicate remark rule " + rule["id"])
        rule_ids.add(rule["id"])
        localized(rule["understood"], "rule " + rule["id"] + ".understood")
        for locale in LOCALES:
            patterns = rule["pattern"][locale]
            need(isinstance(patterns, list) and patterns and all(isinstance(p, str) and p.strip() for p in patterns),
                 "rule " + rule["id"] + " needs patterns in " + locale)
        effect = rule["effect"]
        if "detail_level_delta" in effect:
            need(set(effect) == {"detail_level_delta"} and effect["detail_level_delta"] in (-1, 1), "rule " + rule["id"] + ": detail_level_delta is -1 or 1")
            continue
        need(effect.get("status") in ("never", "unknown") and set(effect) <= {"status", "category", "fields"} and ("category" in effect or "fields" in effect),
             "rule " + rule["id"] + ": effect needs status never|unknown and a category and/or fields")
        need(effect.get("category", next(iter(categories))) in categories, "rule " + rule["id"] + " names an unknown category")
        for fid in effect.get("fields", []):
            need(fid in field_ids, "rule " + rule["id"] + " names an unknown field")
        if effect["status"] == "never" and "category" in effect:
            for locale in LOCALES:
                never_categories[locale].add(effect["category"])
    for category, meta in categories.items():
        if not meta:
            for locale in LOCALES:
                need(category in never_categories[locale], "category " + category + " has no never rule in " + locale)
    need(scenario["graph_method"]["recipe"]["sections"] == sections, "graph_method.recipe.sections must equal sections")
    return scenario


def documents():
    result = {name: read_document(path) for name, path in FILES.items()}
    validate_scenario(result["kOnboardingScenario"])
    need(result["kOnboardingScenario"]["method_ref"]["recipe_hash"] == recipe_hash(result["kOnboardingScenario"]),
         "method_ref.recipe_hash is stale; run gen_onboarding_pack.py without --check")
    compile_entry_packs(result["kOnboardingPack"])
    compile_layer_bindings(result["kOnboardingPack"])
    return result


def serialized(value):
    return json.dumps(value, indent=2, ensure_ascii=False) + "\n"


def generated():
    docs = documents()
    lines = ["// Generated by gen_onboarding_pack.py; edit canonical .pack data instead.\n"]
    for name, value in docs.items():
        raw = serialized(value)
        if ')loom_onboard"' in raw:
            raise ValueError("raw string delimiter collision")
        # Chunks on line boundaries keep every literal under the 65536-byte length
        # C++ compilers must support (clang -Woverlength-strings).
        chunks, current = [], ""
        for line in raw.splitlines(keepends=True):
            if current and len((current + line).encode("utf-8")) > CHUNK_BYTES:
                chunks.append(current)
                current = ""
            current += line
        chunks.append(current)
        # Each chunk is given with its byte length: a bare literal makes the compiler measure it with a constexpr strlen, and clang
        # (the Android and the vendored builds) stops that at its constexpr step limit once the whole array passes ~400 KB
        # (CI 262: kOnboardingPack too large). With an explicit length nothing is counted, so the pack can grow.
        body = ",\n".join(f'std::string_view(R"loom_onboard({chunk})loom_onboard", {len(chunk.encode("utf-8"))})' for chunk in chunks)
        lines.append(f'static constexpr std::string_view {name}[] = {{\n{body}}};\n')
    ui = serialized(docs["kOnboardingPresentation"])
    return {OUT: "".join(lines), WEB_OUT: ui,
            WEB_CONVERSATION_VIEW_OUT: serialized(read_document(DATA / "profiles/conversation_view.pack")),
            WEB_ANIMATION_OUT: serialized({k: v for k, v in read_document(DATA / "runtime/ui_animation.pack")["defaults"].items()
                                           if k not in ("schema", "section", "subsection", "units")})}


def refresh_recipe_hash():
    """method_ref.recipe_hash is derived from graph_method: the generator writes the derived value back into the canonical pack."""
    path = FILES["kOnboardingScenario"]
    scenario = json.loads(path.read_bytes().decode("utf-8"))
    if scenario["method_ref"]["recipe_hash"] != recipe_hash(scenario):
        scenario["method_ref"]["recipe_hash"] = recipe_hash(scenario)
        path.write_bytes(serialized(scenario).encode("utf-8"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        refresh_recipe_hash()
    outputs = generated()
    if args.check:
        for path, content in outputs.items():
            if not path.exists() or path.read_bytes() != content.encode("utf-8"):
                raise SystemExit("onboarding generated data is stale: " + str(path.relative_to(ROOT)))
    else:
        for path, content in outputs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content.encode("utf-8"))


if __name__ == "__main__":
    main()
