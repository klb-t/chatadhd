#include "import_usage.h"

#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#if __has_include("loom/usage_policy.h")
#include "loom/usage_policy.h"
#define LOOM_IMPORT_USAGE_AVAILABLE 1
#endif

namespace loom {

struct ImportUsageSession::Impl {
  Json receipt = {{"status", "unavailable"}, {"capability", "loom.usage_policy/1"},
                  {"reason", "integrate thread 2 to enable shared usage admission"}};
#ifdef LOOM_IMPORT_USAGE_AVAILABLE
  std::unique_ptr<UsagePolicy> policy;
  std::string operation_id;
  std::uintmax_t source_bytes = 0;
  bool admitted = false;
#endif
};

ImportUsageSession::ImportUsageSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ImportUsageSession::~ImportUsageSession() {
#ifdef LOOM_IMPORT_USAGE_AVAILABLE
  if (impl_->admitted) (void)impl_->policy->cancel(impl_->operation_id, "import failed before measured completion");
#endif
}
Result<std::unique_ptr<ImportUsageSession>> ImportUsageSession::open(Config& config,
                                                                  const std::filesystem::path& data_dir) {
  auto impl = std::make_unique<Impl>();
#ifdef LOOM_IMPORT_USAGE_AVAILABLE
  LOOM_TRY_ASSIGN(auto options, effective_usage_policy_options(config));
  LOOM_TRY_ASSIGN(impl->policy, UsagePolicy::open(data_dir / "usage-policy.sqlite", options));
#endif
  return std::unique_ptr<ImportUsageSession>(new ImportUsageSession(std::move(impl)));
}

Status ImportUsageSession::request(const std::filesystem::path& source, const ImportOptions& options,
                                  const std::string& operation_id, const std::string& baseline_key,
                                  const std::string& confirm_receipt, const std::string& confirmation_ref) {
#ifdef LOOM_IMPORT_USAGE_AVAILABLE
  std::error_code error;
  impl_->source_bytes = std::filesystem::file_size(source, error);
  if (error) return Error(Errc::Io, "cannot measure import source: " + error.message());
  impl_->operation_id = operation_id.empty() ? gen_id("import_usage_") : operation_id;
  // Reading a source hash is a bounded-memory preflight scan. It is not the
  // import, and no archive bytes/rows have been persisted at this point.
  LOOM_TRY_ASSIGN(auto source_hash, sha256_file_hex(source));
  const Json estimate{{"operation_id", impl_->operation_id}, {"baseline_key", baseline_key},
      {"resources", {{"source_bytes", impl_->source_bytes}}},
      {"source_hash", source_hash}, {"parser_version", std::string(kExportParserVersion)},
      {"import_options", {{"export_mode", options.export_mode == ExportMode::On ? "on" :
                          options.export_mode == ExportMode::Off ? "off" : "auto"},
                          {"force", options.force}, {"resume", options.resume}}}};
  LOOM_TRY_ASSIGN(impl_->receipt, impl_->policy->request(estimate));
  if (!confirm_receipt.empty()) {
    if (operation_id.empty() || confirmation_ref.empty())
      return Error(Errc::InvalidArgument, "confirmation requires explicit operation ID and confirmation reference");
    LOOM_TRY_ASSIGN(impl_->receipt, impl_->policy->confirm(impl_->operation_id, confirm_receipt, true, confirmation_ref));
  }
  if (!impl_->receipt.value("authorized", false))
    return Error(Errc::Cancelled, "import usage admission requires confirmation or was denied");
  impl_->admitted = true;
#else
  if (!operation_id.empty() || !confirm_receipt.empty() || !confirmation_ref.empty())
    return Error(Errc::Unsupported, "shared usage policy capability requires thread 2");
#endif
  return {};
}
Status ImportUsageSession::complete(const ImportResult& result) {
#ifdef LOOM_IMPORT_USAGE_AVAILABLE
  if (!impl_->admitted) return {};
  const bool source_matches = !result.blob_hash.empty() &&
      result.blob_hash == impl_->receipt["estimate"]["source_hash"].get<std::string>();
  if (!source_matches) {
    LOOM_TRY_ASSIGN(impl_->receipt, impl_->policy->cancel(impl_->operation_id,
        "import snapshot differs from admitted source; no measured baseline recorded"));
    impl_->admitted = false;
    return Error(Errc::Conflict, "import source changed between usage preflight and snapshot");
  }
  LOOM_TRY_ASSIGN(impl_->receipt, impl_->policy->complete(impl_->operation_id,
      Json{{"resources", {{"source_bytes", impl_->source_bytes}}}, {"provenance", "instrument_measured"},
           {"measurement", "source bytes verified against admitted blob size/hash; excludes storage expansion/CPU/model tokens"},
           {"cancelled", result.cancelled}}));
  impl_->admitted = false;
#endif
  return {};
}
const Json& ImportUsageSession::receipt() const { return impl_->receipt; }
bool ImportUsageSession::requires_confirmation() const {
  return impl_->receipt.value("status", "") == "requires_confirmation";
}
}  // namespace loom
