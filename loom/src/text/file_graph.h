#pragma once
#include "host_reader.h"
namespace loom { class Runtime; }
namespace loom::text::sources::host {
bool file_format(std::string_view format);
Result<Prepared> prepare_file_graph(const std::filesystem::path&, const std::string&, const std::string&, const std::string&, const std::string&);
Status store_file_sources(Runtime&, const Prepared&, const Json& recipe);
Result<Json> store_file_structure(Runtime&, const Prepared&, const Json& result, const Json& recipe);
Result<Json> materialize_archive(Runtime&, const Json& request);
}
