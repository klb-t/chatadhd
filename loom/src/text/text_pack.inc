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
  "alignment": {"similarity": "words", "replace_threshold": 0.5, "move_threshold": 1.0, "inner_level": "word"}
}
)LOOMTXT", 2376);
