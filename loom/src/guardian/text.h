// Internal: word-level text handling of the guardian (folding, phrases, noise spans). Offsets are Unicode codepoint indices,
// the unit of message annotations (db.h).
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "loom/util/json.h"

namespace loom::guardian::text {

struct Tok {
  std::string folded;
  std::size_t start = 0, end = 0;  // [start, end) codepoints of the original
};

// A phrase of the data: words folded the same way as the text; a word ending in '*' is a prefix.
struct Phrase {
  std::string source;
  std::vector<std::string> words;
  std::vector<bool> prefix;
  bool raw = false;  // no word characters at all (e.g. "!!"): matched as a substring of the lowercase text
};

struct Hit {
  std::size_t first = 0, last = 0;  // token indices, inclusive
  std::string phrase;
};

std::string fold(std::string_view text, const Json& fold_map);
std::vector<Tok> tokenize(const std::u32string& text, const Json& fold_map);
std::vector<Phrase> compile(const std::vector<std::string>& phrases, const Json& fold_map);
// Every non-overlapping hit of every phrase, in text order (the longest phrase wins at one position).
std::vector<Hit> find(const std::vector<Tok>& toks, const std::vector<Phrase>& phrases);
bool contains_raw(std::string_view lowered_text, const std::vector<Phrase>& phrases);
std::vector<std::string> strings_of(const Json* arr);

struct Span {
  std::string category;
  std::size_t start = 0, end = 0;
  std::string matched;
};

// The cheap noise categories of the shared annotation layer, from the data (profile guardian, levels.noise): filler, repetition, restart.
std::vector<Span> noise_spans(const std::u32string& text, const Json& levels_noise, const Json& fold_map);

// The text without the given spans (union), whitespace and stray punctuation tidied. Spans in codepoints.
std::string without(const std::u32string& text, std::vector<std::pair<std::size_t, std::size_t>> spans);

}  // namespace loom::guardian::text
