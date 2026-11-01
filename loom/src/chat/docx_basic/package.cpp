#include "package.h"
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <iterator>
#if defined(__linux__) && !defined(__ANDROID__)
#include <dlfcn.h>
#include <link.h>
#endif
namespace loom::chat {
Result<std::filesystem::path> packaged_reader_directory(const std::string& name) {
  if (name != "docx_basic" && name != "structure_readers" && name != "pdf_structure")
    return Error(Errc::Unavailable, "reader_package_unavailable");
#if defined(__linux__) && !defined(__ANDROID__)
  static const char module_anchor = 0;
  Dl_info module{};
  if (!::dladdr(&module_anchor, &module) || !module.dli_fname)
    return Error(Errc::Unavailable, "docx_reader_unavailable");
  std::filesystem::path location(module.dli_fname);
  std::error_code ec;
  if (!location.is_absolute()) {
    // argv[0] may be relative or found via PATH. Only the main executable's
    // actual mapping may use /proc/self/exe; never substitute it for a library.
    struct MainAnchor { std::uintptr_t address; bool found; } anchor{
        reinterpret_cast<std::uintptr_t>(&module_anchor), false};
    ::dl_iterate_phdr([](dl_phdr_info* info, std::size_t, void* data) {
      auto& probe = *static_cast<MainAnchor*>(data);
      if (info->dlpi_name && *info->dlpi_name) return 0;
      for (std::size_t i = 0; i < info->dlpi_phnum; ++i) {
        const auto& segment = info->dlpi_phdr[i];
        if (segment.p_type != PT_LOAD) continue;
        const auto begin = info->dlpi_addr + segment.p_vaddr;
        if (probe.address >= begin && probe.address - begin < segment.p_memsz) probe.found = true;
      }
      return 1;
    }, &anchor);
    if (!anchor.found) return Error(Errc::Unavailable, "docx_reader_unavailable");
    location = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec || !location.is_absolute()) return Error(Errc::Unavailable, "docx_reader_unavailable");
  }
  const auto binary = std::filesystem::canonical(location, ec);
  if (ec) return Error(Errc::Unavailable, "docx_reader_unavailable");
  // Installed bin/ or lib/, then CMake's library build layout. Both are anchored
  // to this module, never to a caller's document path or current directory.
  for (const auto& parent : {binary.parent_path().parent_path(), binary.parent_path()}) {
    const auto directory = parent / "share" / "loom" / name;
    if (std::filesystem::is_regular_file(directory / "runtime.json", ec)) return directory;
  }
#endif
  return Error(Errc::Unavailable, "docx_reader_unavailable");
}
Result<DocxCommand> docx_command(const Json& reader) {
  DocxCommand command{json::get_string(reader, "python"), json::get_string(reader, "script")};
  const bool packaged_python = command.python == "package:docx_basic";
  const bool packaged_script = command.script == "package:docx_basic";
  if (!packaged_python && !packaged_script) return command;
  LOOM_TRY_ASSIGN(auto directory, packaged_reader_directory("docx_basic"));
  std::ifstream input(directory / "runtime.json", std::ios::binary);
  const std::string raw{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  const auto manifest = json::parse(raw);
  if (!manifest || !manifest->is_object() || json::get_string(*manifest, "schema") != "loom.docx_runtime/1")
    return Error(Errc::Unavailable, "docx_reader_unavailable");
  if (packaged_python) command.python = json::get_string(*manifest, "python");
  if (packaged_script) command.script = (directory / "reader.py").string();
  return command;
}

}
