// Internal helpers shared by the storage backends. Not installed.
#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "loom/net/http.h"
#include "loom/storage_backend.h"

namespace loom::storage::detail {

// {dir} -> data directory.
std::string expand_dir(std::string_view path, const std::filesystem::path& data_dir);
// Percent-encodes everything except unreserved characters (and '/' when keep_slash).
std::string uri_encode(std::string_view s, bool keep_slash);
std::string join_key(std::string_view prefix, std::string_view key);

// Sends a request; maps transport failures and non-2xx (except `accept`) to Errors. Error
// messages carry method, status and a trimmed body, never headers or credentials.
Result<net::HttpResponse> http_call(const BackendEnv& env, const net::HttpRequest& req,
                                    std::initializer_list<int> accept = {});

// Minimal tolerant XML scanning by local element name (namespace prefixes ignored).
// Returns the inner text of every <local>…</local> in `xml` (non-nested use).
std::vector<std::string_view> xml_elements(std::string_view xml, std::string_view local);
std::string xml_text(std::string_view xml, std::string_view local);  // first element's text, unescaped
std::string xml_unescape(std::string_view s);

// AWS Signature V4 (exposed for the published test vector). `headers` is the exact set of signed
// headers, lowercase names; canonical_path/query are already URI-encoded.
struct SigV4In {
  std::string method, canonical_path, canonical_query, payload_hash, amzdate, region, service = "s3", secret_key;
  std::map<std::string, std::string> headers;
};
std::string sigv4_signature(const SigV4In& in, std::string* signed_headers_out = nullptr);

Result<std::unique_ptr<StorageBackend>> make_local(const std::string& id, const Json& cfg, const BackendEnv& env);
Result<std::unique_ptr<StorageBackend>> make_s3(const std::string& id, const Json& cfg, const BackendEnv& env);
Result<std::unique_ptr<StorageBackend>> make_gdrive(const std::string& id, const Json& cfg, const BackendEnv& env);
Result<std::unique_ptr<StorageBackend>> make_webdav(const std::string& id, const Json& cfg, const BackendEnv& env);

}  // namespace loom::storage::detail
