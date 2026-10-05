// Included by tests/test_kb_pack.cpp. These public-pack overlays exercise the
// normalization recipe independently; no provider or archive data is used.
#include <cstdint>
#include <cmath>
#include <limits>

namespace {

Json normalization_recipe() {
  auto pack = unwrap(kb::Pack::load_builtin());
  return pack->lexicon("stemming")["normalization"];
}

std::shared_ptr<const kb::Pack> normalization_overlay(
    const Json& recipe, const std::function<void(Json&)>& stopwords_edit = {}) {
  auto builtin = unwrap(kb::Pack::load_builtin());
  Json stemming = builtin->lexicon("stemming");
  stemming["normalization"] = recipe;
  fsutil::TempDir td;
  LOOM_REQUIRE_OK(fsutil::ensure_dir(td.path() / "lexicons"));
  LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "lexicons/stemming.json", json::dump(stemming)));
  if (stopwords_edit) {
    Json stopwords = builtin->lexicon("stopwords");
    stopwords_edit(stopwords);
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "lexicons/stopwords.json", json::dump(stopwords)));
  }
  auto overlay = unwrap(kb::Pack::load_with_overlay(td.path()));
  CHECK(overlay->lexicon("stemming")["normalization"] == recipe);
  if (recipe != builtin->lexicon("stemming")["normalization"]) CHECK(overlay->hash() != builtin->hash());
  return overlay;
}

std::string normalization_error(const std::function<void(Json&)>& edit) {
  auto docs = dir_docs();
  edit(docs["lexicons/stemming.json"]);
  return error_of(std::move(docs));
}

}  // namespace

// CTest selects this translation unit by doctest's source-file filter.
// Keep the scoped include's registrations attributed to that logical TU.
#line 1 "loom/tests/test_kb_pack.cpp"
TEST_SUITE("kb_pack") {
  TEST_CASE("checked construction reports absent capability without restoring builtin defaults") {
    auto builtin = unwrap(kb::Pack::load_builtin());
    auto available = kb::Normalizer::create(*builtin);
    REQUIRE(available);
    CHECK(available->match_key("stored", kb::Lang::En) == "store");

    auto docs = dir_docs();
    docs.erase("lexicons/stemming.json");
    Json& files = docs["pack.json"]["files"];
    for (auto it = files.begin(); it != files.end();) {
      if (json::get_string(*it, "path") == "lexicons/stemming.json") it = files.erase(it);
      else ++it;
    }
    auto smaller = unwrap(kb::Pack::from_documents(std::move(docs)));
    CHECK(smaller->lexicon("stemming").is_null());
    auto absent = kb::Normalizer::create(*smaller);
    REQUIRE_FALSE(absent);
    CHECK(absent.error().code == Errc::Unavailable);
    CHECK(absent.error().message == "kb_normalization_recipe_unavailable");
  }

  TEST_CASE("character cues are editable Unicode data and empty cues disable that signal") {
    auto base = unwrap(kb::Pack::load_builtin());
    kb::Normalizer original(*base);
    CHECK(original.token_lang("żu") == kb::Lang::Pl);
    CHECK(original.guess_lang("żu") == kb::Lang::Pl);
    CHECK(original.token_lang("q") == kb::Lang::En);

    Json recipe = normalization_recipe();
    recipe["pl_character_cues"] = "";
    auto disabled = normalization_overlay(recipe);
    kb::Normalizer without_cues(*disabled);
    CHECK(without_cues.token_lang("żu") == kb::Lang::En);
    CHECK(without_cues.guess_lang("żu") == kb::Lang::Unknown);

    recipe["pl_character_cues"] = "q";
    auto changed = normalization_overlay(recipe);
    kb::Normalizer custom_cues(*changed);
    CHECK(custom_cues.token_lang("q") == kb::Lang::Pl);
    CHECK(custom_cues.guess_lang("q") == kb::Lang::Pl);
    CHECK(custom_cues.token_lang("żu") == kb::Lang::En);
  }

  TEST_CASE("verbal vowel eligibility comes from the character table and the non-ASCII setting") {
    auto base = unwrap(kb::Pack::load_builtin());
    kb::Normalizer original(*base);
    CHECK(original.stem("stored", kb::Lang::En) == "store");
    CHECK(original.stem("bźzing", kb::Lang::En) == "bźz");

    Json recipe = normalization_recipe();
    recipe["vowels"] = "a";
    auto a_only = normalization_overlay(recipe);
    kb::Normalizer a_vowels(*a_only);
    CHECK(a_vowels.stem("stored", kb::Lang::En) == "stored");

    recipe["vowels"] = "";
    auto empty = normalization_overlay(recipe);
    kb::Normalizer no_ascii_vowels(*empty);
    CHECK(no_ascii_vowels.stem("stored", kb::Lang::En) == "stored");

    recipe = normalization_recipe();
    recipe["non_ascii_vowel"] = false;
    auto ascii_only = normalization_overlay(recipe);
    kb::Normalizer without_non_ascii(*ascii_only);
    CHECK(without_non_ascii.stem("bźzing", kb::Lang::En) == "bźzing");
    CHECK(without_non_ascii.stem("stored", kb::Lang::En) == "store");
  }

  TEST_CASE("Unicode vowels and repair permissions are independent recipe data") {
    Json recipe = normalization_recipe();
    recipe["vowels"] = "é";
    recipe["non_ascii_vowel"] = false;
    auto explicit_vowel = normalization_overlay(recipe);
    kb::Normalizer unicode_vowel(*explicit_vowel);
    CHECK(unicode_vowel.stem("béting", kb::Lang::En) == "bét");

    recipe["cvc_allow_non_ascii"] = true;
    auto cvc = normalization_overlay(recipe);
    kb::Normalizer unicode_cvc(*cvc);
    CHECK(unicode_cvc.stem("béting", kb::Lang::En) == "béte");

    recipe["cvc_terminal_exceptions"] = "tλ";
    auto blocked_cvc = normalization_overlay(recipe);
    kb::Normalizer unicode_cvc_exceptions(*blocked_cvc);
    CHECK(unicode_cvc_exceptions.stem("béting", kb::Lang::En) == "bét");

    recipe = normalization_recipe();
    auto defaults = normalization_overlay(recipe);
    kb::Normalizer classic_undoubling(*defaults);
    CHECK(classic_undoubling.stem("baλλing", kb::Lang::En) == "baλλ");
    const std::string malformed = std::string("a") + std::string(2, '\xff') + "ing";
    CHECK(classic_undoubling.stem(malformed, kb::Lang::En) == std::string("a") + std::string(2, '\xff'));
    recipe["undouble_allow_non_ascii"] = true;
    auto undouble = normalization_overlay(recipe);
    kb::Normalizer unicode_undoubling(*undouble);
    CHECK(unicode_undoubling.stem("baλλing", kb::Lang::En) == "baλ");
    CHECK(unicode_undoubling.stem(malformed, kb::Lang::En) == std::string("a") + std::string(1, '\xff'));

    recipe["undouble_exceptions"] = "λ";
    auto blocked_undouble = normalization_overlay(recipe);
    kb::Normalizer unicode_undouble_exceptions(*blocked_undouble);
    CHECK(unicode_undouble_exceptions.stem("baλλing", kb::Lang::En) == "baλλ");
  }

  TEST_CASE("required CVC vowel-group count is an editable integer") {
    Json recipe = normalization_recipe();
    recipe["cvc_vowel_groups"] = 2;
    auto changed = normalization_overlay(recipe);
    kb::Normalizer two_groups(*changed);
    CHECK(two_groups.stem("stored", kb::Lang::En) == "stor");
    CHECK(two_groups.stem("created", kb::Lang::En) == "create");
  }

  TEST_CASE("CVC and undoubling exception sets change repair independently") {
    auto base = unwrap(kb::Pack::load_builtin());
    kb::Normalizer original(*base);
    CHECK(original.stem("snowing", kb::Lang::En) == "snow");
    CHECK(original.stem("falling", kb::Lang::En) == "fall");
    CHECK(original.stem("running", kb::Lang::En) == "run");

    Json recipe = normalization_recipe();
    recipe["cvc_terminal_exceptions"] = "";
    auto cvc = normalization_overlay(recipe);
    kb::Normalizer cvc_without_exceptions(*cvc);
    CHECK(cvc_without_exceptions.stem("snowing", kb::Lang::En) == "snowe");
    CHECK(cvc_without_exceptions.stem("falling", kb::Lang::En) == "fall");

    recipe = normalization_recipe();
    recipe["undouble_exceptions"] = "";
    auto undouble = normalization_overlay(recipe);
    kb::Normalizer undouble_without_exceptions(*undouble);
    CHECK(undouble_without_exceptions.stem("falling", kb::Lang::En) == "fal");
    CHECK(undouble_without_exceptions.stem("snowing", kb::Lang::En) == "snow");

    recipe["undouble_exceptions"] = "lszn";
    auto keep_n = normalization_overlay(recipe);
    kb::Normalizer keep_double_n(*keep_n);
    CHECK(keep_double_n.stem("running", kb::Lang::En) == "runn");
  }

  TEST_CASE("restoration text is data for both ending-driven and CVC repair") {
    Json recipe = normalization_recipe();
    recipe["restore_suffix"] = "";
    auto disabled = normalization_overlay(recipe);
    kb::Normalizer no_restore(*disabled);
    CHECK(no_restore.stem("created", kb::Lang::En) == "creat");
    CHECK(no_restore.stem("stored", kb::Lang::En) == "stor");

    recipe["restore_suffix"] = "es";
    auto changed = normalization_overlay(recipe);
    kb::Normalizer custom_restore(*changed);
    CHECK(custom_restore.stem("created", kb::Lang::En) == "creates");
    CHECK(custom_restore.stem("stored", kb::Lang::En) == "stores");
    CHECK(custom_restore.stem("running", kb::Lang::En) == "run");
  }

  TEST_CASE("language-guess thresholds and short-text cutoff are independent overlays") {
    auto base = unwrap(kb::Pack::load_builtin());
    kb::Normalizer original(*base);
    CHECK(original.guess_lang("the") == kb::Lang::Unknown);
    CHECK(original.guess_lang("the i zzqa") == kb::Lang::Mixed);
    CHECK(original.guess_lang("i zzqa zzqb") == kb::Lang::Pl);
    CHECK(original.guess_lang("the zzqa zzqb") == kb::Lang::En);

    Json recipe = normalization_recipe();
    recipe["guess_min_tokens"] = 1;
    auto short_text = normalization_overlay(recipe);
    kb::Normalizer classify_short(*short_text);
    CHECK(classify_short.guess_lang("the") == kb::Lang::En);

    recipe = normalization_recipe();
    recipe["mixed_min_fraction"] = 0.4;
    auto mixed = normalization_overlay(recipe);
    kb::Normalizer higher_mixed(*mixed);
    CHECK(higher_mixed.guess_lang("the i zzqa") == kb::Lang::En);

    recipe = normalization_recipe();
    recipe["pl_min_fraction"] = 1.0;
    auto pl = normalization_overlay(recipe);
    kb::Normalizer higher_pl(*pl);
    CHECK(higher_pl.guess_lang("i zzqa zzqb") == kb::Lang::Unknown);
    CHECK(higher_pl.guess_lang("the zzqa zzqb") == kb::Lang::En);

    recipe = normalization_recipe();
    recipe["en_min_fraction"] = 1.0;
    auto en = normalization_overlay(recipe);
    kb::Normalizer higher_en(*en);
    CHECK(higher_en.guess_lang("the zzqa zzqb") == kb::Lang::Unknown);
    CHECK(higher_en.guess_lang("i zzqa zzqb") == kb::Lang::Pl);
  }

  TEST_CASE("stopword sources and language-signal fields are editable data") {
    auto base = unwrap(kb::Pack::load_builtin());
    kb::Normalizer original(*base);
    CHECK(original.is_stopword("the"));
    CHECK(original.is_stopword("się"));
    CHECK(!original.is_stopword("qx"));

    Json recipe = normalization_recipe();
    recipe["stopword_sources"] = Json::array();
    auto disabled = normalization_overlay(recipe);
    kb::Normalizer no_stopwords(*disabled);
    CHECK(!no_stopwords.is_stopword("the"));
    CHECK(!no_stopwords.is_stopword("się"));
    CHECK(no_stopwords.guess_lang("the zzqa zzqb") == kb::Lang::Unknown);

    recipe["guess_min_tokens"] = 1;
    recipe["stopword_sources"] = Json::array(
        {Json{{"lexicon", "stopwords"}, {"fields", Json::array({"custom"})},
              {"pl_signal_fields", Json::array()}}});
    auto custom = normalization_overlay(recipe, [](Json& s) { s["custom"] = Json::array({"qx"}); });
    kb::Normalizer custom_words(*custom);
    CHECK(custom_words.is_stopword("qx"));
    CHECK(!custom_words.is_stopword("the"));
    CHECK(custom_words.token_lang("qx") == kb::Lang::En);
    CHECK(custom_words.guess_lang("qx") == kb::Lang::En);

    recipe["stopword_sources"][0]["pl_signal_fields"] = Json::array({"custom"});
    auto custom_pl = normalization_overlay(recipe, [](Json& s) { s["custom"] = Json::array({"qx"}); });
    kb::Normalizer custom_polish_signal(*custom_pl);
    CHECK(custom_polish_signal.is_stopword("qx"));
    CHECK(custom_polish_signal.token_lang("qx") == kb::Lang::Pl);
    CHECK(custom_polish_signal.guess_lang("qx") == kb::Lang::Pl);
  }

  TEST_CASE("normalization recipe is required and rejects invalid representations") {
    auto rejects = [&](const std::function<void(Json&)>& edit, const std::string& pointer) {
      const std::string message = normalization_error(edit);
      INFO(message);
      CHECK(message.find("lexicons/stemming.json") != std::string::npos);
      CHECK(message.find(pointer) != std::string::npos);
    };
    rejects([](Json& s) { s.erase("normalization"); }, "/normalization");
    rejects([](Json& s) { s["normalization"] = Json::array(); }, "/normalization");
    for (const char* key : {"pl_character_cues", "vowels", "non_ascii_vowel", "cvc_terminal_exceptions",
                            "undouble_exceptions", "restore_suffix", "guess_min_tokens", "mixed_min_fraction",
                            "pl_min_fraction", "en_min_fraction", "cvc_allow_non_ascii", "undouble_allow_non_ascii",
                            "cvc_vowel_groups", "stopword_sources"}) {
      rejects([&](Json& s) { s["normalization"].erase(key); }, std::string("/normalization/") + key);
    }
    for (const char* key : {"pl_character_cues", "vowels", "cvc_terminal_exceptions", "undouble_exceptions",
                            "restore_suffix"}) {
      rejects([&](Json& s) { s["normalization"][key] = false; }, std::string("/normalization/") + key);
    }
    for (const char* key : {"non_ascii_vowel", "cvc_allow_non_ascii", "undouble_allow_non_ascii"}) {
      rejects([&](Json& s) { s["normalization"][key] = 1; }, std::string("/normalization/") + key);
    }
    const double unrepresentable_float = std::ldexp(1.0, std::numeric_limits<std::size_t>::digits);
    for (const char* key : {"guess_min_tokens", "cvc_vowel_groups"}) {
      for (Json value : {Json(true), Json(1.5), Json(-1), Json(unrepresentable_float)}) {
        rejects([&](Json& s) { s["normalization"][key] = value; }, std::string("/normalization/") + key);
      }
    }
    for (const char* key : {"mixed_min_fraction", "pl_min_fraction", "en_min_fraction"}) {
      for (Json value : {Json(true), Json("0.25"), Json(std::numeric_limits<double>::quiet_NaN()),
                         Json(std::numeric_limits<double>::infinity()), Json(-std::numeric_limits<double>::infinity())}) {
        rejects([&](Json& s) { s["normalization"][key] = value; }, std::string("/normalization/") + key);
      }
    }
  }

  TEST_CASE("stopword sources reject missing references and malformed field lists") {
    auto rejects = [&](const std::function<void(Json&)>& edit, const std::string& pointer) {
      const std::string message = normalization_error(edit);
      INFO(message);
      CHECK(message.find("lexicons/stemming.json") != std::string::npos);
      CHECK(message.find(pointer) != std::string::npos);
    };
    rejects([](Json& s) { s["normalization"]["stopword_sources"] = false; }, "/normalization/stopword_sources");
    rejects([](Json& s) { s["normalization"]["stopword_sources"][0] = "stopwords"; },
            "/normalization/stopword_sources/0");
    for (const char* key : {"lexicon", "fields", "pl_signal_fields"}) {
      rejects([&](Json& s) { s["normalization"]["stopword_sources"][0].erase(key); },
              std::string("/normalization/stopword_sources/0/") + key);
    }
    rejects([](Json& s) { s["normalization"]["stopword_sources"][0]["lexicon"] = "no_such_lexicon"; },
            "/normalization/stopword_sources/0/lexicon");
    rejects([](Json& s) { s["normalization"]["stopword_sources"][0]["lexicon"] = false; },
            "/normalization/stopword_sources/0/lexicon");
    for (const char* key : {"fields", "pl_signal_fields"}) {
      rejects([&](Json& s) { s["normalization"]["stopword_sources"][0][key] = "en"; },
              std::string("/normalization/stopword_sources/0/") + key);
      rejects([&](Json& s) { s["normalization"]["stopword_sources"][0][key] = Json::array({false}); },
              std::string("/normalization/stopword_sources/0/") + key);
      rejects([&](Json& s) { s["normalization"]["stopword_sources"][0][key] = Json::array({"missing_field"}); },
              std::string("/normalization/stopword_sources/0/") + key);
    }

    for (Json value : {Json(false), Json::array({false})}) {
      auto docs = dir_docs();
      docs["lexicons/stopwords_base.json"]["en"] = value;
      const std::string message = error_of(std::move(docs));
      INFO(message);
      CHECK(message.find("lexicons/stopwords_base.json") != std::string::npos);
      CHECK(message.find("/en") != std::string::npos);
    }
  }

  TEST_CASE("stem length parameters accept their native range instead of the old 32-character ceiling") {
    for (const char* key : {"min_token", "min_stem"}) {
      auto docs = dir_docs();
      docs["lexicons/stemming.json"]["en"][key] = std::numeric_limits<std::size_t>::max();
      auto extended = unwrap(kb::Pack::from_documents(std::move(docs)));
      kb::Normalizer large_lengths(*extended);
      CHECK(large_lengths.stem("graphs", kb::Lang::En) == "graphs");

      for (Json invalid : {Json(true), Json(-1), Json(1.5)}) {
        auto bad = dir_docs();
        bad["lexicons/stemming.json"]["en"][key] = invalid;
        const std::string message = error_of(std::move(bad));
        INFO(message);
        CHECK(message.find(std::string("/en/") + key) != std::string::npos);
      }
    }
    auto docs = dir_docs();
    docs["lexicons/stemming.json"]["pl"]["min_token"] = 0;
    docs["lexicons/stemming.json"]["pl"]["min_stem"] = 0;
    auto zero = unwrap(kb::Pack::from_documents(std::move(docs)));
    kb::Normalizer zero_lengths(*zero);
    CHECK(zero_lengths.stem("a", kb::Lang::Pl).empty());
  }

  TEST_CASE("representable counts and finite comparison cutoffs do not carry preset-sized ceilings") {
    Json recipe = normalization_recipe();
    recipe["guess_min_tokens"] = std::numeric_limits<std::size_t>::max();
    auto large = normalization_overlay(recipe);
    kb::Normalizer high_cutoff(*large);
    CHECK(high_cutoff.guess_lang("the i zzqa") == kb::Lang::Unknown);

    recipe["guess_min_tokens"] = static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1;
    CHECK(normalization_overlay(recipe)->lexicon("stemming")["normalization"] == recipe);
    recipe["cvc_vowel_groups"] = std::numeric_limits<std::size_t>::max();
    CHECK(normalization_overlay(recipe)->lexicon("stemming")["normalization"] == recipe);
    recipe["cvc_vowel_groups"] = 1;

    recipe["guess_min_tokens"] = 0;
    for (const char* key : {"mixed_min_fraction", "pl_min_fraction", "en_min_fraction"}) recipe[key] = 0.0;
    auto zero = normalization_overlay(recipe);
    kb::Normalizer zero_thresholds(*zero);
    CHECK(zero_thresholds.guess_lang("") == kb::Lang::Unknown);
    CHECK(zero_thresholds.guess_lang("the") == kb::Lang::Mixed);

    for (const char* key : {"mixed_min_fraction", "pl_min_fraction", "en_min_fraction"}) recipe[key] = 1.0;
    CHECK(normalization_overlay(recipe)->lexicon("stemming")["normalization"] == recipe);

    for (const char* key : {"mixed_min_fraction", "pl_min_fraction", "en_min_fraction"}) {
      recipe = normalization_recipe();
      recipe[key] = 1.01;
      auto above_one = normalization_overlay(recipe);
      kb::Normalizer disabled_rule(*above_one);
      const std::string field(key);
      if (field == "mixed_min_fraction") {
        CHECK(disabled_rule.guess_lang("the i zzqa") == kb::Lang::En);
        CHECK(disabled_rule.guess_lang("i zzqa zzqb") == kb::Lang::Pl);
      } else if (field == "pl_min_fraction") {
        CHECK(disabled_rule.guess_lang("i zzqa zzqb") == kb::Lang::Unknown);
        CHECK(disabled_rule.guess_lang("the zzqa zzqb") == kb::Lang::En);
      } else {
        CHECK(disabled_rule.guess_lang("the zzqa zzqb") == kb::Lang::Unknown);
        CHECK(disabled_rule.guess_lang("i zzqa zzqb") == kb::Lang::Pl);
      }

      recipe[key] = -0.01;
      auto below_zero = normalization_overlay(recipe);
      kb::Normalizer negative_cutoff(*below_zero);
      if (field == "mixed_min_fraction") {
        CHECK(negative_cutoff.guess_lang("zzqa zzqb zzqc") == kb::Lang::Mixed);
      } else if (field == "en_min_fraction") {
        CHECK(negative_cutoff.guess_lang("zzqa zzqb zzqc") == kb::Lang::En);
      } else {
        CHECK(negative_cutoff.guess_lang("i zzqa zzqb") == kb::Lang::Pl);
      }
      recipe[key] = std::numeric_limits<double>::max();
      CHECK(normalization_overlay(recipe)->lexicon("stemming")["normalization"] == recipe);
      recipe[key] = std::numeric_limits<double>::lowest();
      CHECK(normalization_overlay(recipe)->lexicon("stemming")["normalization"] == recipe);
    }
  }
}
