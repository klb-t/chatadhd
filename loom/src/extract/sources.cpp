// Reading files into units for the extractor (read_units / text_unit).
// Sources are content-addressed ("sha256:<hex of the raw file>", I1); a unit
// is located by member + JSON pointer. Deterministic order.
#include <algorithm>

#include "archive/archive_internal.h"
#include "loom/extract.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include "miniz.h"

namespace loom::extract {

namespace fs = std::filesystem;

namespace {

std::string lower_ext(const std::string& name) {
  std::string e = fs::path(name).extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e;
}

UnitContent make_unit(const std::string& source, const std::string& member, const std::string& pointer,
                      const std::string& kind, const std::string& title, const std::string& date, std::int64_t bytes) {
  UnitContent u;
  u.unit.source = source;
  u.unit.kind = kind;
  u.unit.locator.source = source;
  u.unit.locator.member = member;
  u.unit.locator.json_pointer = pointer;
  u.unit.title = title;
  u.unit.date = date;
  u.unit.bytes = bytes;
  u.unit.id = model::Unit::make_id(source, u.unit.locator);
  return u;
}

void from_json_doc(const std::string& source, const std::string& member, const Json& doc, std::vector<UnitContent>& out) {
  auto one = [&](const Json& e, const std::string& pointer) {
    std::string shape = archive::sniff_export_element(e);
    if (shape == "chatgpt" || shape == "claude") {
      archive::ChatWalk w = shape == "chatgpt" ? archive::walk_chatgpt(e) : archive::walk_claude(e);
      UnitContent u = make_unit(source, member, pointer, "conversation", w.title, w.date,
                                static_cast<std::int64_t>(json::dump(e).size()));
      u.structured = e;
      u.unit.attrs = Json{{"platform", shape},
                          {"ext_id", json::get_string(e, shape == "chatgpt" ? "id" : "uuid")}};
      out.push_back(std::move(u));
    } else if (shape == "claude_projects") {
      UnitContent u = make_unit(source, member, pointer, "project", json::get_string(e, "name"),
                                archive::normalize_date(json::get_string(e, "created_at")),
                                static_cast<std::int64_t>(json::dump(e).size()));
      u.structured = e;
      u.unit.attrs = Json{{"platform", shape}, {"ext_id", json::get_string(e, "uuid")}};
      out.push_back(std::move(u));
    } else if (shape == "claude_memories") {
      UnitContent u = make_unit(source, member, pointer, "memory", "memories", "", static_cast<std::int64_t>(json::dump(e).size()));
      u.structured = e;
      u.unit.attrs = Json{{"platform", shape}};
      out.push_back(std::move(u));
    }
  };
  if (doc.is_array()) {
    for (std::size_t i = 0; i < doc.size(); ++i) one(doc[i], "/" + std::to_string(i));
  } else if (doc.is_object()) {
    one(doc, "");
  }
}

void from_bytes(const std::string& source, const std::string& member, const std::string& bytes, std::vector<UnitContent>& out) {
  std::string ext = lower_ext(member);
  if (ext == ".json") {
    auto doc = json::parse(bytes);
    if (doc) from_json_doc(source, member, *doc, out);
    return;
  }
  if (ext == ".jsonl") {
    std::size_t pos = 0;
    int line = 0;
    while (pos < bytes.size()) {
      std::size_t eol = bytes.find('\n', pos);
      if (eol == std::string::npos) eol = bytes.size();
      auto doc = json::parse(std::string_view(bytes).substr(pos, eol - pos));
      if (doc && doc->is_object()) {
        std::vector<UnitContent> tmp;
        from_json_doc(source, member, *doc, tmp);
        for (auto& u : tmp) {
          u.unit.locator.byte_start = static_cast<std::int64_t>(pos);
          u.unit.locator.byte_len = static_cast<std::int64_t>(eol - pos);
          u.unit.locator.line = line + 1;
          u.unit.id = model::Unit::make_id(source, u.unit.locator);
          out.push_back(std::move(u));
        }
      }
      ++line;
      pos = eol + 1;
    }
    return;
  }
  std::string lang = archive::code_language(member);
  std::string kind;
  std::string mime;
  if (ext == ".md" || ext == ".markdown" || ext == ".txt" || ext == ".rst") kind = "file";
  else if (ext == ".eml") {
    kind = "email";
    mime = "message/rfc822";
  } else if (ext == ".vtt" || ext == ".srt") kind = "segment";
  else if (ext == ".fountain") kind = "file";
  else if (!lang.empty()) kind = "file";
  if (kind.empty() || !utf8::is_valid(bytes)) return;
  UnitContent u = make_unit(source, member, "", kind, member, archive::first_date(bytes.substr(0, 4096)),
                            static_cast<std::int64_t>(bytes.size()));
  u.text = bytes;
  u.unit.attrs = Json::object();
  if (!lang.empty()) u.unit.attrs["language"] = lang;
  if (!mime.empty()) u.unit.attrs["mime"] = mime;
  out.push_back(std::move(u));
}

}  // namespace

UnitContent text_unit(std::string_view member, std::string_view text, std::string_view date, std::string_view source) {
  std::string src = source.empty() ? "sha256:" + Sha256::hex(text) : std::string(source);
  UnitContent u = make_unit(src, std::string(member), "", "file", std::string(member), std::string(date),
                            static_cast<std::int64_t>(text.size()));
  u.text = std::string(text);
  std::string lang = archive::code_language(std::string(member));
  if (!lang.empty()) u.unit.attrs["language"] = lang;
  return u;
}

Result<std::vector<UnitContent>> read_units(const fs::path& path) {
  std::error_code ec;
  std::vector<UnitContent> out;
  if (fs::is_directory(path, ec)) {
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
      if (ec) break;
      if (it->is_directory(ec)) {
        std::string n = it->path().filename().string();
        if (n == ".git" || n == "node_modules" || n == "__pycache__" || n == "build") it.disable_recursion_pending();
        continue;
      }
      if (it->is_regular_file(ec)) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
      auto r = read_units(f);
      if (!r) continue;
      for (auto& u : *r) {
        // members relative to the directory keep path-based detection working
        if (u.unit.locator.member == f.filename().string()) {
          u.unit.locator.member = f.lexically_relative(path).generic_string();
          if (u.unit.title == f.filename().string()) u.unit.title = u.unit.locator.member;
          u.unit.id = model::Unit::make_id(u.unit.source, u.unit.locator);
        }
        out.push_back(std::move(u));
      }
    }
    return out;
  }
  if (!fs::is_regular_file(path, ec)) return Error(Errc::NotFound, "no such file: " + path.string());
  LOOM_TRY_ASSIGN(std::string bytes, fsutil::read_file(path));
  std::string source = "sha256:" + Sha256::hex(bytes);
  if (lower_ext(path.filename().string()) == ".zip") {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) return Error(Errc::Parse, "not a readable zip: " + path.string());
    std::vector<std::pair<std::string, mz_uint>> members;
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
      if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
      char name[1024];
      mz_zip_reader_get_filename(&zip, i, name, sizeof name);
      members.emplace_back(name, i);
    }
    std::sort(members.begin(), members.end());
    for (const auto& [name, idx] : members) {
      std::size_t sz = 0;
      void* p = mz_zip_reader_extract_to_heap(&zip, idx, &sz, 0);
      if (!p) continue;
      std::string data(static_cast<const char*>(p), sz);
      mz_free(p);
      from_bytes(source, name, data, out);
    }
    mz_zip_reader_end(&zip);
    return out;
  }
  from_bytes(source, path.filename().string(), bytes, out);
  return out;
}

}  // namespace loom::extract
