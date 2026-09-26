// catalog_internal.h: OffsetArrayScanner (streaming JSON array parse with
// byte offsets) and zip member streaming (miniz pull iterator, never
// extract-to-temp). Both are pure/bounded-memory building blocks for scan().
#include "catalog_internal.h"

#include <cctype>

#include "miniz.h"

namespace loom::catalog::internal {

// ── OffsetArrayScanner ──────────────────────────────────────────────────
// Invariant this relies on: once an element starts (have_start_ becomes
// true), every subsequent byte up to and including the char that closes it
// (the matching '}'/']' that brings depth_ back to 0, or the delimiting ','/
// top-level ']' for a bare literal) is pushed into buf_ with NO drops --
// interior whitespace at depth>0 is preserved verbatim, and once an element
// has started at depth 0 (a bare number/string/literal) nothing before its
// end is skipped either. So elem_start_ + buf_.size() is always the true
// absolute end offset; no separate end-position bookkeeping is needed.
void OffsetArrayScanner::feed(std::string_view chunk, const ElementFn& on_element) {
  if (done_) return;
  std::size_t i = 0;
  if (!started_) {
    while (i < chunk.size()) {
      unsigned char c = static_cast<unsigned char>(chunk[i]);
      if (std::isspace(c)) {
        ++i;
        continue;
      }
      if (chunk[i] == '[') {
        started_ = true;
        ++i;
        break;
      }
      not_array_ = true;
      done_ = true;
      return;
    }
    if (!started_) {
      abs_pos_ += static_cast<std::int64_t>(chunk.size());
      return;
    }
  }

  auto flush = [&] {
    if (!buf_.empty() && !stopped_) {
      std::int64_t begin = elem_start_;
      std::int64_t end = elem_start_ + static_cast<std::int64_t>(buf_.size());
      if (!on_element(buf_, begin, end)) stopped_ = true;
    }
    buf_.clear();
    have_start_ = false;
  };

  for (; i < chunk.size() && !stopped_; ++i) {
    char ch = chunk[i];
    std::int64_t pos = abs_pos_ + static_cast<std::int64_t>(i);
    if (escape_) {
      buf_.push_back(ch);
      escape_ = false;
      continue;
    }
    if (ch == '\\' && in_string_) {
      buf_.push_back(ch);
      escape_ = true;
      continue;
    }
    if (ch == '"') {
      if (!have_start_) {
        elem_start_ = pos;
        have_start_ = true;
      }
      in_string_ = !in_string_;
      buf_.push_back(ch);
      continue;
    }
    if (in_string_) {
      buf_.push_back(ch);
      continue;
    }
    if (ch == '{' || ch == '[') {
      if (!have_start_) {
        elem_start_ = pos;
        have_start_ = true;
      }
      ++depth_;
      buf_.push_back(ch);
    } else if (ch == '}' || ch == ']') {
      if (depth_ == 0) {
        done_ = true;
        abs_pos_ += static_cast<std::int64_t>(chunk.size());
        return;  // end of the top-level array; ignore trailing bytes
      }
      --depth_;
      buf_.push_back(ch);
      if (depth_ == 0) flush();
    } else if (ch == ',' && depth_ == 0) {
      flush();
    } else {
      if (depth_ > 0 || !std::isspace(static_cast<unsigned char>(ch))) {
        if (!have_start_) {
          elem_start_ = pos;
          have_start_ = true;
        }
        buf_.push_back(ch);
      }
    }
  }
  abs_pos_ += static_cast<std::int64_t>(i);  // i == chunk.size() unless stopped_ cut it short
}

// ── Zip streaming ─────────────────────────────────────────────────────
Result<std::vector<ZipEntry>> list_zip_entries(const std::filesystem::path& zip_path) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, zip_path.string().c_str(), 0)) {
    return Error(Errc::Io, "not a readable zip: " + zip_path.string());
  }
  std::vector<ZipEntry> out;
  mz_uint n = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < n; ++i) {
    mz_zip_archive_file_stat st{};
    if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
    ZipEntry e;
    e.name = st.m_filename;
    e.index = i;
    e.uncompressed_size = static_cast<std::int64_t>(st.m_uncomp_size);
    e.is_dir = mz_zip_reader_is_file_a_directory(&zip, i) != 0;
    out.push_back(std::move(e));
  }
  mz_zip_reader_end(&zip);
  return out;
}

Status stream_zip_member(const std::filesystem::path& zip_path, unsigned index,
                         const std::function<void(std::string_view)>& on_chunk) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, zip_path.string().c_str(), 0)) {
    return Error(Errc::Io, "not a readable zip: " + zip_path.string());
  }
  auto* it = mz_zip_reader_extract_iter_new(&zip, static_cast<mz_uint>(index), 0);
  if (!it) {
    mz_zip_reader_end(&zip);
    return Error(Errc::Io, "cannot open zip member index " + std::to_string(index));
  }
  std::string buf(kReadChunk, '\0');
  bool ok = true;
  while (true) {
    std::size_t n = mz_zip_reader_extract_iter_read(it, buf.data(), buf.size());
    if (n == 0) break;
    on_chunk(std::string_view(buf.data(), n));
  }
  if (!mz_zip_reader_extract_iter_free(it)) ok = false;
  mz_zip_reader_end(&zip);
  if (!ok) return Error(Errc::Io, "zip member " + std::to_string(index) + ": CRC/decompression error");
  return {};
}

}  // namespace loom::catalog::internal
