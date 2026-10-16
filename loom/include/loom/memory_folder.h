// loom/memory_folder.h — a folder into the memory tree, with the full content of its files (R39, owner 2026-02-06:
// "Katalog do pamięci – pełna treść plików").
//
//   attach(memory, root, options): one node "dir" for the folder and each subfolder (content = its name), one node "file"
//   for each file under its folder: a text file (valid UTF-8, no NUL) carries its whole content, a binary file is listed
//   with no content (metadata.binary). Metadata of every node: source "folder", path (relative to the root), and for a
//   file size, sha256, truncated. The memory goes into the prompt like any other node; switching a node off keeps it.
//
//   Sizes are presets, not limits (settings, 0 = no cut): memory_folder_file_chars (characters kept per file, preset 0),
//   memory_folder_max_files (files read, preset 0), memory_folder_include_hidden (dot files and folders, preset false).
//   A request may override each of them (file_chars, max_files, include_hidden).
//
//   invoke(rt, "attach_folder", {path, parent_id?, title?, file_chars?, max_files?, include_hidden?}) — the port the chat
//   action memory.attach_folder and loom_invoke("memory_folder", ...) use; the folder a device picked
//   (device.pick_folder) is materialised to a path by the caller first.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "loom/util/json.h"
#include "loom/result.h"

namespace loom {
class Config;
class MemoryEngine;
class Runtime;
}  // namespace loom

namespace loom::memory_folder {

struct Options {
  std::size_t file_chars = 0;   // characters kept per text file; 0 = the whole file
  std::size_t max_files = 0;    // files read; 0 = all
  bool include_hidden = false;  // names starting with '.'
  std::optional<std::string> parent_id;  // memory node the folder goes under; none = top level
  std::string title;            // name of the root node; "" = the folder's name
};

// The settings, then the request's own values on top.
Options options_from(const Config& cfg, const Json& request);

// Reads `root` (a directory) into `memory`. Returns {root_id, dirs, files, text_files, binary_files, truncated, chars, skipped}.
Result<Json> attach(MemoryEngine& memory, const std::filesystem::path& root, const Options& options);

// Port / C ABI entry: action "attach_folder".
Result<Json> invoke(Runtime& rt, std::string_view action, const Json& request);

}  // namespace loom::memory_folder
