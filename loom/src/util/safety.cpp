#include "util/safety.h"

#include <fstream>

#include "loom/config.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"

namespace loom::safety {
namespace {
Json config_json(const std::filesystem::path& data_dir) {
  if (data_dir.empty()) return Json::object();
  auto text = fsutil::read_file(data_dir / "config.json");
  if (!text) return Json::object();
  auto parsed = json::parse(*text);
  return parsed && parsed->is_object() ? *parsed : Json::object();
}
Json setting(const Json& cfg, const char* key) {
  if (cfg.contains(key)) return cfg[key];
  return loom_config_defaults().at(key);
}
}  // namespace

ZipLimits zip_limits(const std::filesystem::path& data_dir) {
  const Json cfg = config_json(data_dir);
  ZipLimits l;
  const Json total = setting(cfg, "loom_zip_max_total_bytes"), ratio = setting(cfg, "loom_zip_max_ratio");
  if (total.is_number() && total.get<double>() > 0) l.max_total_bytes = static_cast<std::uint64_t>(total.get<double>());
  if (ratio.is_number() && ratio.get<double>() > 0) l.max_ratio = ratio.get<double>();
  return l;
}

bool git_allow_repo_config(const std::filesystem::path& data_dir) {
  const Json v = setting(config_json(data_dir), "loom_git_allow_repo_config");
  return v.is_boolean() && v.get<bool>();
}

std::vector<std::string> git_hardening_args() {
  return {"-c", "core.hooksPath=/dev/null", "-c", "core.fsmonitor=false", "-c", "core.pager=cat",
          "-c", "core.sshCommand=false",    "-c", "core.askPass=",        "-c", "credential.helper=",
          "-c", "protocol.allow=never",     "-c", "protocol.file.allow=never",
          "-c", "diff.external=",           "-c", "diff.textconv=",       "-c", "core.attributesFile=/dev/null",
          "-c", "gpg.program=false",        "-c", "uploadpack.packObjectsHook="};
}

Status ZipBudget::refuse(std::string why) {
  refused_ = true;
  reason_ = std::move(why);
  return Error(Errc::Parse, reason_);
}

Status ZipBudget::admit(const mz_zip_archive_file_stat& st) {
  const std::uint64_t size = st.m_uncomp_size, comp = st.m_comp_size ? st.m_comp_size : 1;
  if (limits_.max_ratio > 0 && static_cast<double>(size) > limits_.max_ratio * static_cast<double>(comp))
    return refuse("zip archive refused: entry '" + std::string(st.m_filename) + "' expands " +
                  std::to_string(size / comp) + "x (limit loom_zip_max_ratio; set it to 0 to lift)");
  if (limits_.max_total_bytes > 0 && declared_ + size > limits_.max_total_bytes)
    return refuse("zip archive refused: it expands beyond " + std::to_string(limits_.max_total_bytes) +
                  " bytes (limit loom_zip_max_total_bytes; set it to 0 to lift)");
  declared_ += size;
  return {};
}

namespace {
struct Sink {
  ZipBudget* budget;
  std::uint64_t* written;
  std::uint64_t total_limit, entry_limit, entry_written = 0;
  std::ofstream* file;
  std::string* mem;
  bool over = false;
};
std::size_t sink_write(void* opaque, mz_uint64, const void* buf, std::size_t n) {
  auto* s = static_cast<Sink*>(opaque);
  s->entry_written += n;
  *s->written += n;
  if ((s->total_limit && *s->written > s->total_limit) || (s->entry_limit && s->entry_written > s->entry_limit)) {
    s->over = true;
    return 0;  // short write: miniz aborts the extraction
  }
  if (s->file) {
    s->file->write(static_cast<const char*>(buf), static_cast<std::streamsize>(n));
    if (!*s->file) return 0;
  } else {
    s->mem->append(static_cast<const char*>(buf), n);
  }
  return n;
}
}  // namespace

static Status stream_entry(ZipBudget& b, ZipLimits lim, std::uint64_t& written, mz_zip_archive& zip, mz_uint index,
                           std::ofstream* file, std::string* mem, bool& refused_flag, std::string& reason) {
  mz_zip_archive_file_stat st;
  if (!mz_zip_reader_file_stat(&zip, index, &st)) return Error(Errc::Parse, "cannot read zip entry metadata");
  LOOM_TRY(b.admit(st));
  const std::uint64_t comp = st.m_comp_size ? st.m_comp_size : 1;
  Sink s{&b, &written, lim.max_total_bytes, 0, 0, file, mem};
  if (lim.max_ratio > 0) s.entry_limit = static_cast<std::uint64_t>(lim.max_ratio * static_cast<double>(comp));
  const bool ok = mz_zip_reader_extract_to_callback(&zip, index, sink_write, &s, 0);
  if (s.over) {
    refused_flag = true;
    reason = "zip archive refused: entry '" + std::string(st.m_filename) +
             "' expands past the limit while extracting (loom_zip_max_total_bytes / loom_zip_max_ratio; 0 lifts)";
    return Error(Errc::Parse, reason);
  }
  if (!ok) return Error(Errc::Parse, "zip entry could not be extracted (corrupt?)");
  return {};
}

Status ZipBudget::extract_to_file(mz_zip_archive& zip, mz_uint index, const std::filesystem::path& dest) {
  std::ofstream out(dest, std::ios::binary);
  if (!out) return Error(Errc::Io, "cannot write " + dest.string());
  auto st = stream_entry(*this, limits_, written_, zip, index, &out, nullptr, refused_, reason_);
  out.close();
  if (!st) {
    std::error_code ec;
    std::filesystem::remove(dest, ec);
  }
  return st;
}

Result<std::string> ZipBudget::extract_to_string(mz_zip_archive& zip, mz_uint index) {
  std::string out;
  LOOM_TRY(stream_entry(*this, limits_, written_, zip, index, nullptr, &out, refused_, reason_));
  return out;
}

}  // namespace loom::safety
