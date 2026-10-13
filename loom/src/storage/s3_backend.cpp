#include <algorithm>
#include <chrono>
#include <ctime>
#include <map>

#include "loom/config.h"
#include "loom/log.h"
#include "loom/util/sha256.h"
#include "storage_internal.h"

namespace loom::storage::detail {

namespace {
std::string raw(const Sha256::Digest& d) { return std::string(reinterpret_cast<const char*>(d.data()), d.size()); }
}  // namespace

std::string sigv4_signature(const SigV4In& in, std::string* signed_headers_out) {
  std::string canon_headers, signed_headers;
  for (const auto& [k, v] : in.headers) {  // std::map: sorted by lowercase name
    canon_headers += k + ":" + v + "\n";
    signed_headers += (signed_headers.empty() ? "" : ";") + k;
  }
  if (signed_headers_out) *signed_headers_out = signed_headers;
  const std::string canonical = in.method + "\n" + in.canonical_path + "\n" + in.canonical_query + "\n" + canon_headers +
                                "\n" + signed_headers + "\n" + in.payload_hash;
  const std::string datestamp = in.amzdate.substr(0, 8);
  const std::string scope = datestamp + "/" + in.region + "/" + in.service + "/aws4_request";
  const std::string to_sign = "AWS4-HMAC-SHA256\n" + in.amzdate + "\n" + scope + "\n" + Sha256::hex(canonical);
  std::string k = raw(hmac_sha256("AWS4" + in.secret_key, datestamp));
  k = raw(hmac_sha256(k, in.region));
  k = raw(hmac_sha256(k, in.service));
  k = raw(hmac_sha256(k, "aws4_request"));
  return to_hex(hmac_sha256(k, to_sign));
}

namespace {

std::string amz_now() {
  std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%SZ", &tm);
  return buf;
}

class S3Backend final : public StorageBackend {
 public:
  S3Backend(std::string id, const Json& cfg, const BackendEnv& env) : id_(std::move(id)), env_(env) {
    endpoint_ = json::get_string(cfg, "endpoint");
    while (!endpoint_.empty() && endpoint_.back() == '/') endpoint_.pop_back();
    bucket_ = json::get_string(cfg, "bucket");
    region_ = json::get_string(cfg, "region", "us-east-1");
    prefix_ = json::get_string(cfg, "prefix");
    secret_ref_ = json::get_string(cfg, "secret_ref", "storage_" + id_);
    // Multipart upload: presets, not rules. part size 8 MiB; objects larger than the threshold (default =
    // part size) are uploaded in parts; multipart_threshold < 0 turns multipart off. The only limits are the
    // S3 protocol's own (10000 parts: the part size grows to fit; the server enforces its minimum part size).
    part_size_ = std::max<std::int64_t>(1, json::get_int(cfg, "multipart_part_size", 8LL * 1024 * 1024));
    threshold_ = json::get_int(cfg, "multipart_threshold", part_size_);
    if (auto u = net::parse_url(endpoint_)) {
      host_ = u->host;
      bool def = (u->scheme == "https" && u->port == 443) || (u->scheme == "http" && u->port == 80);
      if (!def) host_ += ":" + std::to_string(u->port);
    }
  }
  const std::string& id() const noexcept override { return id_; }
  std::string kind() const override { return "s3"; }

  Status put(std::string_view key, std::string_view bytes, std::string_view mime) override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    std::map<std::string, std::string> extra{{"x-amz-meta-sha256", Sha256::hex(bytes)}};
    if (!mime.empty()) extra["content-type"] = std::string(mime);
    if (threshold_ >= 0 && static_cast<std::int64_t>(bytes.size()) > threshold_) return put_multipart(key, bytes, extra);
    LOOM_TRY(call("PUT", object_path(key), "", std::string(bytes), extra, {}));
    return {};
  }
  Result<std::string> get(std::string_view key) const override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    LOOM_TRY_ASSIGN(auto r, call("GET", object_path(key), "", "", {}, {}));
    return std::move(r.body);
  }
  Result<ObjectStat> stat(std::string_view key) const override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    LOOM_TRY_ASSIGN(auto r, call("HEAD", object_path(key), "", "", {}, {}));
    ObjectStat s;
    s.key = std::string(key);
    try { s.size = std::stoll(r.header("content-length")); } catch (...) { s.size = 0; }
    s.sha256 = r.header("x-amz-meta-sha256");
    s.mime = r.header("content-type");
    return s;
  }
  Result<std::vector<ObjectStat>> list(std::string_view prefix) const override {
    std::vector<ObjectStat> out;
    const std::string full_prefix = join_key(prefix_, prefix);
    std::string token;
    for (int guard = 0; guard < 100000; ++guard) {
      std::string q = "list-type=2&prefix=" + uri_encode(full_prefix, false);
      if (!token.empty()) q = "continuation-token=" + uri_encode(token, false) + "&" + q;
      LOOM_TRY_ASSIGN(auto r, call("GET", "/" + bucket_, q, "", {}, {}));
      for (std::string_view c : xml_elements(r.body, "Contents")) {
        ObjectStat s;
        std::string k = xml_text(c, "Key");
        if (!prefix_.empty()) {
          std::string p = join_key(prefix_, "");
          if (!p.empty() && p.back() != '/') p += "/";
          if (k.rfind(p, 0) != 0) continue;
          k = k.substr(p.size());
        }
        s.key = k;
        try { s.size = std::stoll(xml_text(c, "Size")); } catch (...) { s.size = 0; }
        out.push_back(std::move(s));
      }
      if (xml_text(r.body, "IsTruncated") != "true") break;
      token = xml_text(r.body, "NextContinuationToken");
      if (token.empty()) break;
    }
    std::sort(out.begin(), out.end(), [](const ObjectStat& a, const ObjectStat& b) { return a.key < b.key; });
    return out;
  }
  Status remove(std::string_view key) override {
    if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid storage key");
    LOOM_TRY(call("DELETE", object_path(key), "", "", {}, {404}));
    return {};
  }
  Status probe() const override {
    LOOM_TRY(call("GET", "/" + bucket_, "list-type=2&max-keys=1", "", {}, {}));
    return {};
  }

 private:
  // CreateMultipartUpload -> UploadPart* -> CompleteMultipartUpload; AbortMultipartUpload on any failure.
  Status put_multipart(std::string_view key, std::string_view bytes, const std::map<std::string, std::string>& extra) {
    constexpr std::int64_t kMaxParts = 10000;  // S3 protocol limit
    std::int64_t part = part_size_;
    const auto total = static_cast<std::int64_t>(bytes.size());
    if ((total + part - 1) / part > kMaxParts) part = (total + kMaxParts - 1) / kMaxParts;
    const std::string path = object_path(key);
    LOOM_TRY_ASSIGN(auto created, call("POST", path, "uploads=", "", extra, {}));
    const std::string upload_id = xml_text(created.body, "UploadId");
    if (upload_id.empty()) return Error(Errc::Http, "s3 multipart: no UploadId in the reply");
    const std::string uq = "uploadId=" + uri_encode(upload_id, false);
    auto abort_upload = [&] {
      auto a = call("DELETE", path, uq, "", {}, {404});
      if (!a) log::warn("storage.s3", "abort multipart upload on {} failed: {}", id_, a.error().message);
    };
    std::string complete = "<CompleteMultipartUpload>";
    int n = 0;
    for (std::int64_t off = 0; off < total; off += part) {
      ++n;
      auto r = call("PUT", path, "partNumber=" + std::to_string(n) + "&" + uq,
                    std::string(bytes.substr(static_cast<std::size_t>(off), static_cast<std::size_t>(part))), {}, {});
      if (!r) { abort_upload(); return r.error(); }
      const std::string etag = r->header("etag");
      if (etag.empty()) { abort_upload(); return Error(Errc::Http, "s3 multipart: part " + std::to_string(n) + " returned no ETag"); }
      complete += "<Part><PartNumber>" + std::to_string(n) + "</PartNumber><ETag>" + xml_escape(etag) + "</ETag></Part>";
    }
    complete += "</CompleteMultipartUpload>";
    auto done = call("POST", path, uq, complete, {{"content-type", "application/xml"}}, {});
    // S3 may answer 200 with an <Error> document.
    if (done && !xml_elements(done->body, "Error").empty())
      done = Error(Errc::Http, "s3 multipart: complete failed: " + xml_text(done->body, "Code"));
    if (!done) { abort_upload(); return done.error(); }
    return {};
  }

  static std::string xml_escape(std::string_view s) {
    std::string o;
    for (char c : s) {
      switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default: o += c;
      }
    }
    return o;
  }

  std::string object_path(std::string_view key) const { return "/" + bucket_ + "/" + join_key(prefix_, key); }

  Result<net::HttpResponse> call(const std::string& method, const std::string& path, const std::string& query,
                                 const std::string& body, std::map<std::string, std::string> extra,
                                 std::initializer_list<int> accept) const {
    if (endpoint_.empty() || bucket_.empty()) return Error(Errc::InvalidArgument, "s3 backend " + id_ + " needs endpoint and bucket");
    if (!env_.secrets) return Error(Errc::Auth, "no secrets store for s3 backend " + id_);
    const std::string ak = env_.secrets->get_string(secret_ref_ + "_access_key");
    const std::string sk = env_.secrets->get_string(secret_ref_ + "_secret_key");
    if (ak.empty() || sk.empty()) return Error(Errc::Auth, "s3 credentials missing (secrets " + secret_ref_ + "_access_key/_secret_key)");
    SigV4In in;
    in.method = method;
    in.canonical_path = uri_encode(path, true);
    in.canonical_query = query;  // callers pass an already sorted, encoded query
    in.payload_hash = Sha256::hex(body);
    in.amzdate = amz_now();
    in.region = region_;
    in.secret_key = sk;
    in.headers = std::move(extra);
    in.headers["host"] = host_;
    in.headers["x-amz-content-sha256"] = in.payload_hash;
    in.headers["x-amz-date"] = in.amzdate;
    std::string signed_headers;
    const std::string sig = sigv4_signature(in, &signed_headers);
    net::HttpRequest req;
    req.method = method;
    req.url = endpoint_ + in.canonical_path + (query.empty() ? "" : "?" + query);
    for (const auto& [k, v] : in.headers) if (k != "host") req.headers.emplace_back(k, v);
    req.headers.emplace_back("Authorization", "AWS4-HMAC-SHA256 Credential=" + ak + "/" + in.amzdate.substr(0, 8) + "/" +
                                                  region_ + "/s3/aws4_request, SignedHeaders=" + signed_headers +
                                                  ", Signature=" + sig);
    req.body = body;
    return http_call(env_, req, accept);
  }

  std::string id_;
  BackendEnv env_;
  std::string endpoint_, bucket_, region_, prefix_, secret_ref_, host_;
  std::int64_t part_size_ = 8LL * 1024 * 1024, threshold_ = 8LL * 1024 * 1024;
};
}  // namespace

Result<std::unique_ptr<StorageBackend>> make_s3(const std::string& id, const Json& cfg, const BackendEnv& env) {
  return std::unique_ptr<StorageBackend>(new S3Backend(id, cfg, env));
}

}  // namespace loom::storage::detail
