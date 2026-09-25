// C API: encryption vault, ASR/OCR, GitHub sync.
// OWNER (wave 2): crypto/media/github. Wrappers complete; not_implemented
// until CryptoVault / MediaProviders / GitHubSync land.
#include "context.h"
#include "loom/config.h"
#include "loom/crypto.h"
#include "loom/github_sync.h"
#include "loom/media_providers.h"
#include "loom/net/http.h"

using namespace loom;
using namespace loom::capi;

namespace {

struct MediaArgs {
  std::optional<std::string> provider;
  std::optional<std::string> language;
};

Result<MediaArgs> media_args(const char* options_json) {
  LOOM_TRY_ASSIGN(Json j, parse_arg(options_json));
  if (!j.is_object()) return Error(Errc::InvalidArgument, "options must be a JSON object");
  return MediaArgs{json::get_opt_string(j, "provider"), json::get_opt_string(j, "language")};
}

Json files_json(const std::vector<GitHubFile>& files) {
  Json arr = Json::array();
  for (const auto& f : files) arr.push_back(f.to_json());
  return arr;
}

}  // namespace

extern "C" {

// ── Crypto ─────────────────────────────────────────────────────────
LOOM_API const char* loom_crypto_status(LoomContext* ctx) {
  return guard_json("loom_crypto_status", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out(ctx->rt->crypto().status());
  });
}

LOOM_API int loom_crypto_setup(LoomContext* ctx, const char* password) {
  return guard_int("loom_crypto_setup", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!password || !*password) return LOOM_E_INVALID_ARGUMENT;
    return code(ctx->rt->crypto().setup(password));
  });
}

LOOM_API int loom_crypto_unlock(LoomContext* ctx, const char* password) {
  return guard_int("loom_crypto_unlock", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!password) return LOOM_E_INVALID_ARGUMENT;
    return code(ctx->rt->crypto().unlock(password));
  });
}

LOOM_API int loom_crypto_lock(LoomContext* ctx) {
  return guard_int("loom_crypto_lock", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    ctx->rt->crypto().lock();
    return LOOM_OK;
  });
}

LOOM_API const char* loom_crypto_encrypt(LoomContext* ctx, const char* plaintext) {
  return guard_json("loom_crypto_encrypt", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!plaintext) return out_error(missing("plaintext"));
    auto r = ctx->rt->crypto().encrypt_text(plaintext);
    if (!r) return out_error(r.error());
    auto blob = json::parse(*r);
    if (!blob) return out_error(blob.error());
    return out(*blob);
  });
}

LOOM_API const char* loom_crypto_decrypt(LoomContext* ctx, const char* blob_json) {
  return guard_json("loom_crypto_decrypt", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!blob_json || !*blob_json) return out_error(missing("blob_json"));
    auto r = ctx->rt->crypto().decrypt_text(blob_json);
    if (!r) return out_error(r.error());
    return out(Json{{"text", *r}});
  });
}

// ── Media ──────────────────────────────────────────────────────────
LOOM_API const char* loom_transcribe(LoomContext* ctx, const char* audio_path, const char* options_json) {
  return guard_json("loom_transcribe", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!audio_path || !*audio_path) return out_error(missing("audio_path"));
    auto a = media_args(options_json);
    if (!a) return out_error(a.error());
    auto r = ctx->rt->media().transcribe(audio_path, a->provider, a->language);
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

LOOM_API const char* loom_ocr(LoomContext* ctx, const char* image_path, const char* options_json) {
  return guard_json("loom_ocr", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!image_path || !*image_path) return out_error(missing("image_path"));
    auto a = media_args(options_json);
    if (!a) return out_error(a.error());
    auto r = ctx->rt->media().ocr(image_path, a->provider, a->language);
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

LOOM_API const char* loom_media_status(LoomContext* ctx) {
  return guard_json("loom_media_status", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out(ctx->rt->media().status());
  });
}

// ── GitHub ─────────────────────────────────────────────────────────
LOOM_API const char* loom_github_sync(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_github_sync", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto rj = parse_arg(request_json, Json(nullptr));
    if (!rj) return out_error(rj.error());
    if (!rj->is_object()) return out_error(Errc::InvalidArgument, "request must be a JSON object");
    std::string action = json::get_string(*rj, "action");
    SyncConfig cfg;
    cfg.repo = json::get_string(*rj, "repo");
    if (cfg.repo.empty()) {  // the Python GitHub panel stores the last repo in config.github_repo
      Json saved = ctx->rt->config().get("github_repo");
      if (saved.is_string()) cfg.repo = saved.get<std::string>();
    }
    if (cfg.repo.empty()) return out_error(missing("repo"));
    cfg.branch = json::get_string(*rj, "branch", "main");
    cfg.local_path = json::get_string(*rj, "local_path", ctx->rt->paths().root.string());
    cfg.sync_direction = json::get_string(*rj, "direction", "bidirectional");
    cfg.token = ctx->rt->secrets().get_string("github_token");
    GitHubSync sync(cfg, ctx->rt->http());

    std::optional<std::vector<GitHubFile>> files;
    if (const Json* f = json::find(*rj, "files"); f && f->is_array()) {
      auto status = sync.get_sync_status();
      if (!status) return out_error(status.error());
      std::vector<GitHubFile> chosen;
      for (const auto& p : *f) {
        if (!p.is_string()) continue;
        for (const auto& s : *status) {
          if (s.path == p.get<std::string>()) chosen.push_back(s);
        }
      }
      files = std::move(chosen);
    }

    if (action == "test") return out(Json{{"connected", sync.test_connection()}});
    if (action == "status") {
      auto r = sync.get_sync_status();
      if (!r) return out_error(r.error());
      return out(Json{{"files", files_json(*r)}});
    }
    Result<Json> r = Error(Errc::InvalidArgument, "action must be test, status, pull, push or sync");
    if (action == "pull") r = sync.pull_all(files);
    else if (action == "push") r = sync.push_all(files);
    else if (action == "sync") r = sync.sync(files);
    if (!r) return out_error(r.error());
    return out(*r);
  });
}

}  // extern "C"
