// G1a: what an import says about what it found and what it saved, per kind of element, with the reason from data when not all of it was
// saved, and one summary sentence in Polish and English. The words (kinds, reasons, notes, sentences) live in runtime/chat_ops.pack
// "import_truth"; this file holds the arithmetic and the matching of reasons to facts, not one visible word.
#pragma once

#include <filesystem>

#include "loom/importer.h"
#include "loom/util/json.h"

namespace loom::import_truth {

// The truth of one import result. `words` is the "import_truth" section of chat_ops (null: facts only, no texts).
// Shape: {schema, path: "export"|"legacy", provider, archive, already_imported, cancelled, partial, warnings, errors,
//         elements: [{kind, label: {pl,en}, found: N|null, imported: N, status: all|some|none|not_counted|empty,
//                     reason: {id, text: {pl,en}, observed?}|null, notes: [{id, text: {pl,en}}]}],
//         summary: {pl, en} (absent without words)}.
// found is null when the reader does not read that kind at all (never a made-up zero); imported is what is stored.
Json build(const ImportResult& result, const Json& words);

// The same with the words read from the data directory (chat_ops). Never fails: unreadable words give facts only and words_unavailable.
Json for_runtime(const ImportResult& result, const std::filesystem::path& data_root);

}  // namespace loom::import_truth
