#include "media/local_ocr.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include <cerrno>
#include <algorithm>
#if defined(__unix__) && !defined(__ANDROID__)
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace loom {
namespace {
#include "local_ocr_worker.inc"
}
std::string LocalOcrProvider::name() const { return json::get_string(settings_, "name"); }

Result<OcrResult> MediaProviders::ocr_local_bytes(std::string_view bytes, std::string_view format,
                                                   const std::string& provider,
                                                   const std::optional<std::string>& language) {
  std::vector<std::shared_ptr<OcrProvider>> candidates;
  {
    std::lock_guard lock(mu_);
    candidates = ocr_;
    candidates.insert(candidates.end(), extra_ocr_.begin(), extra_ocr_.end());
  }
  for (const auto& candidate : candidates)
    if (candidate->runs_locally() && candidate->name() == provider)
      return candidate->recognize_bytes(bytes, format, language);
  return Error(Errc::Unavailable, "ocr_local_provider_unavailable");
}

Result<OcrResult> decode_local_ocr(const Json& doc, std::string_view source_hash) {
  if (!doc.is_object() || json::get_string(doc, "schema") != "loom.ocr_regions/1" ||
      json::get_string(doc, "source_sha256") != source_hash || !doc.contains("regions") ||
      !doc.at("regions").is_array() || !doc.contains("frames") || !doc.at("frames").is_array())
    return Error(Errc::Parse, "ocr_result_contract");
  if (!doc.contains("method") || !doc.at("method").is_object() || !doc.contains("reason") ||
      !doc.at("reason").is_string() || !doc.contains("text") || !doc.at("text").is_string())
    return Error(Errc::Parse, "ocr_result_metadata");
  for (const auto& frame : doc.at("frames")) {
    if (!frame.is_object() || !frame.contains("id") || !frame.at("id").is_string() ||
        !frame.contains("extent") || !frame.at("extent").is_array() || frame.at("extent").size() != 2)
      return Error(Errc::Parse, "ocr_result_frame");
    for (const auto& size : frame.at("extent"))
      if (!size.is_number_integer() || size.get<std::int64_t>() <= 0) return Error(Errc::Parse, "ocr_result_frame_size");
  }
  for (const auto& region : doc.at("regions")) {
    if (!region.is_object() || !region.contains("text") || !region.at("text").is_string() ||
        region.at("text").get<std::string>().empty() || !region.contains("origin") || !region.contains("size") ||
        !region.at("origin").is_array() || !region.at("size").is_array() ||
        region.at("origin").size() != 2 || region.at("size").size() != 2)
      return Error(Errc::Parse, "ocr_result_region");
    const auto frame = std::find_if(doc.at("frames").begin(), doc.at("frames").end(), [&](const Json& item) {
      return json::get_string(item, "id") == json::get_string(region, "frame");
    });
    if (frame == doc.at("frames").end()) return Error(Errc::Parse, "ocr_result_region_frame");
    for (std::size_t axis = 0; axis < 2; ++axis) {
      if (!region.at("origin")[axis].is_number_integer() || !region.at("size")[axis].is_number_integer())
        return Error(Errc::Parse, "ocr_result_region_coordinate");
      const auto start = region.at("origin")[axis].get<std::int64_t>();
      const auto size = region.at("size")[axis].get<std::int64_t>();
      const auto extent = frame->at("extent")[axis].get<std::int64_t>();
      if (start < 0 || size <= 0 || size > extent || start > extent - size)
        return Error(Errc::Parse, "ocr_result_region_bounds");
    }
  }
  const auto status = json::get_string(doc, "status");
  if (status != "recognized" && status != "no_text" && status != "unavailable" && status != "failed")
    return Error(Errc::Parse, "ocr_result_status");
  OcrResult out;
  out.text = json::get_string(doc, "text");
  if ((status == "recognized") != (!out.text.empty() && !doc.at("regions").empty()))
    return Error(Errc::Parse, "ocr_result_text_status");
  if (status != "recognized" && (!out.text.empty() || !doc.at("regions").empty()))
    return Error(Errc::Parse, "ocr_result_fabricated_text");
  if (!doc.contains("lines") || !doc.at("lines").is_array()) return Error(Errc::Parse, "ocr_result_lines");
  for (const auto& line : doc.at("lines")) {
    if (!line.is_string()) return Error(Errc::Parse, "ocr_result_line");
    out.lines.push_back(line.get<std::string>());
  }
  if (doc.contains("method")) out.language = json::get_string(doc.at("method"), "language");
  out.representation = doc; // engine scores remain separate from calibrated confidence
  return out;
}

Result<OcrResult> LocalOcrProvider::recognize_bytes(std::string_view bytes, std::string_view,
                                                  const std::optional<std::string>& language) {
#if defined(__unix__) && !defined(__ANDROID__)
  fsutil::TempDir temp;
  if (!temp.valid()) return Error(Errc::Io, "ocr_temp_directory");
  if (::chmod(temp.path().c_str(), 0700) != 0) return Error(Errc::Io, "ocr_temp_permissions");
  const auto image = temp.path() / "image";
  const auto script = temp.path() / "worker.py";
  const auto input = temp.path() / "request.json";
  const auto output = temp.path() / "response.json";
  LOOM_TRY(fsutil::write_file(image, bytes));
  LOOM_TRY(fsutil::write_file(script, kLocalOcrWorker));
  Json request{{"image_path", image.string()}, {"settings", settings_}};
  if (language) request["language"] = *language;
  LOOM_TRY(fsutil::write_file(input, request.dump()));
  std::string executable = json::get_string(settings_, "python");
  if (executable.empty()) return Error(Errc::InvalidArgument, "ocr_python_missing");
  std::string script_arg = script.string();
  char* argv[] = {executable.data(), script_arg.data(), nullptr};
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) return Error(Errc::Io, "ocr_spawn_actions");
  const int a = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, input.c_str(), O_RDONLY, 0);
  const int b = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  const int c = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  pid_t pid = -1;
  const int spawned = (a || b || c) ? EIO : posix_spawnp(&pid, executable.c_str(), &actions, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (spawned != 0) return Error(Errc::Unavailable, "ocr_worker_unavailable");
  int status = 0;
  pid_t waited;
  do { waited = ::waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
  if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    return Error(Errc::Io, "ocr_worker_failed");
  LOOM_TRY_ASSIGN(auto raw, fsutil::read_file(output));
  LOOM_TRY_ASSIGN(auto document, json::parse(raw));
  return decode_local_ocr(document, Sha256::hex(bytes));
#else
  (void)bytes;
  (void)language;
  return Error(Errc::Unavailable, "ocr_local_process_unsupported");
#endif
}
Result<OcrResult> LocalOcrProvider::recognize(const std::filesystem::path& path,
                                            const std::optional<std::string>& language) {
  LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(path));
  return recognize_bytes(bytes, "", language);
}
} // namespace loom
