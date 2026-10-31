#include "document_text.h"

#include <memory>

#include "loom/util/fs.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <dlfcn.h>

// Opaque types of the stable public Poppler-GLib C ABI. No private C++ Poppler ABI.
// https://poppler.freedesktop.org/api/glib/PopplerDocument.html
struct _GBytes;
struct _GError;
struct _PopplerDocument;
struct _PopplerPage;
#endif

namespace loom::chat {
namespace {
#if defined(__linux__) && !defined(__ANDROID__)
bool has_text(const std::string& text) {
  return text.find_first_not_of(" \t\r\n\f\v") != std::string::npos;
}

struct Poppler {
  // GLib registers process-wide GTypes containing function pointers. NODELETE
  // keeps those valid after closing our handle, including later reader calls.
  void* library = nullptr;
  _GBytes* (*bytes_new)(const void*, std::size_t) = nullptr;
  void (*bytes_unref)(_GBytes*) = nullptr;
  _PopplerDocument* (*document_new)(_GBytes*, const char*, _GError**) = nullptr;
  int (*n_pages)(_PopplerDocument*) = nullptr;
  _PopplerPage* (*get_page)(_PopplerDocument*, int) = nullptr;
  char* (*get_text)(_PopplerPage*) = nullptr;
  void (*unref)(void*) = nullptr;
  void (*free_text)(void*) = nullptr;
  void (*free_error)(_GError*) = nullptr;

  ~Poppler() { if (library) dlclose(library); }
  template<class T> bool bind(T& fn, const char* name) {
    fn = reinterpret_cast<T>(dlsym(library, name));
    return fn != nullptr;
  }
  bool open(const std::string& name) {
    if (name.empty()) return false;
    library = dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    return library && bind(bytes_new, "g_bytes_new") && bind(bytes_unref, "g_bytes_unref") &&
        bind(document_new, "poppler_document_new_from_bytes") && bind(n_pages, "poppler_document_get_n_pages") &&
        bind(get_page, "poppler_document_get_page") && bind(get_text, "poppler_page_get_text") &&
        bind(unref, "g_object_unref") && bind(free_text, "g_free") && bind(free_error, "g_error_free");
  }
};
#endif
}  // namespace

Result<DocumentText> read_document_text(const std::filesystem::path& path, const Json& reader) {
  if (json::get_string(reader, "adapter") != "poppler_glib")
    return Error(Errc::Unavailable, "document_reader_unavailable");
#if defined(__linux__) && !defined(__ANDROID__)
  Poppler api;
  if (!api.open(json::get_string(reader, "library")))
    return Error(Errc::Unavailable, "document_reader_unavailable");
  LOOM_TRY_ASSIGN(auto source, fsutil::read_file(path));
  std::unique_ptr<_GBytes, decltype(api.bytes_unref)> bytes(api.bytes_new(source.data(), source.size()), api.bytes_unref);
  _GError* error = nullptr;
  std::unique_ptr<_PopplerDocument, decltype(api.unref)> doc(api.document_new(bytes.get(), nullptr, &error), api.unref);
  if (error) api.free_error(error);
  if (!doc) return Error(Errc::Parse, "document_invalid");

  DocumentText result;
  result.pages = api.n_pages(doc.get());
  for (int i = 0; i < result.pages; ++i) {
    std::unique_ptr<_PopplerPage, decltype(api.unref)> page(api.get_page(doc.get(), i), api.unref);
    if (!page) return Error(Errc::Parse, "document_invalid");
    std::unique_ptr<char, decltype(api.free_text)> text(api.get_text(page.get()), api.free_text);
    if (!text) return Error(Errc::Parse, "document_invalid");
    if (!has_text(text.get())) result.pages_without_text.push_back(i + 1);
    if (i) result.text += '\f';  // preserve page boundaries in derived text
    result.text += text.get();
  }
  if (!has_text(result.text)) return Error(Errc::Unavailable, "document_no_text");
  return result;
#else
  (void)path;
  return Error(Errc::Unavailable, "document_reader_unavailable");
#endif
}

}  // namespace loom::chat
