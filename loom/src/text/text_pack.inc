// Generated from loom/data/text/segmentation.pack; do not edit.
// Regenerate: python3 loom/src/text/gen_text_pack.py
constexpr std::string_view kTextPackSource = "loom/data/text/segmentation.pack";
constexpr std::string_view kTextPack = std::string_view(R"LOOMTXT({
  "schema": "loom.text_segmentation/1",
  "id": "loom.preset.text_segmentation",
  "version": 1,
  "note": "Presets, not rules. Every value here is an example the engine reads as data: levels, methods, languages, abbreviations, normalization, fingerprint and alignment thresholds. A user overlay replaces any value; a language missing here uses the \"*\" rules and the result says so.",
  "levels": {
    "paragraph": {"method": "separator", "parent": "document", "min_newlines": 2},
    "line": {"method": "separator", "parent": "document", "min_newlines": 1},
    "sentence": {"method": "sentence", "parent": "paragraph"},
    "word": {"method": "word", "parent": "sentence"}
  },
  "default_language": "*",
  "languages": {
    "*": {
      "sentence": {
        "terminators": [".", "!", "?", "…"],
        "closers": ["\"", "'", "”", "’", "»", ")", "]"],
        "openers": ["\"", "'", "„", "“", "«", "(", "["],
        "require_space_after": true,
        "next_start": ["upper", "digit", "opening", "uncased"],
        "abbreviations": [],
        "abbreviations_case_sensitive": false,
        "initials_are_abbreviations": true,
        "break_on_newline": false
      },
      "word": {"joiners": ["'", "’", "-"], "split_punctuation": true}
    },
    "pl": {
      "sentence": {
        "abbreviations": ["np", "m.in", "tzn", "tj", "dr", "prof", "mgr", "inż", "hab", "ul", "al", "pl", "godz", "ok", "zob", "por", "wg", "im", "nr", "str", "s", "ds", "przyp", "red", "tel", "ks", "św", "gen", "płk", "mjr"]
      }
    },
    "en": {
      "sentence": {
        "abbreviations": ["e.g", "i.e", "mr", "mrs", "ms", "dr", "prof", "st", "vs", "no", "jr", "sr", "inc", "ltd", "approx", "cf", "fig", "dept", "est", "mt"]
      }
    },
    "ja": {"sentence": {"terminators": ["。", "！", "？", ".", "!", "?"], "closers": ["」", "』", "）", ")"], "require_space_after": false, "next_start": []}},
    "zh": {"sentence": {"terminators": ["。", "！", "？", ".", "!", "?"], "closers": ["”", "’", "」", "』", "）", ")"], "require_space_after": false, "next_start": []}}
  },
  "normalization": {"case": "lower", "whitespace": "collapse", "punctuation": "keep", "strip": true},
  "fingerprint": {"hash": "sha256", "hex_chars": 0},
  "alignment": {"similarity": "words", "replace_threshold": 0.5, "move_threshold": 1.0, "inner_level": "word"},
  "genesis": {
    "note": "R102 G2: the chain of versions in the graph. Every value is a preset; kinds are strings and the lists are open (a new op kind becomes an event of its own name, a new edge kind is used by naming it).",
    "level": "sentence",
    "record_initial_additions": false,
    "nodes": {"fragment": "text_fragment", "version": "text_version", "event": "text_event"},
    "edges": {
      "occurrence": "part_of", "version_chain": "derived_from",
      "about": "about", "in": "occurred_in", "from": "from_version", "successor": "successor",
      "removal": "deleted_in", "addition": "added_in", "replacement": "replaced_by", "move": "moved_to",
      "replacement_back": "derived_from"
    },
    "events": {"delete": "removal", "insert": "addition", "replace": "replacement", "move": "move"},
    "silent_ops": ["equal"],
    "questions": {
      "origin": {"pl": "Skąd pochodzi to zdanie", "en": "Where this sentence comes from"},
      "replaced": {"pl": "Co zastąpiło to zdanie", "en": "What replaced this sentence"},
      "missing": {"pl": "Czego brakuje w tej wersji względem poprzedniej", "en": "What is missing in this version against the previous one"},
      "events": {"pl": "Co się stało z tym zdaniem", "en": "What happened to this sentence"}
    },
    "match": {"contains": true, "max_candidates": 5}
  }
}
)LOOMTXT", 3725);
