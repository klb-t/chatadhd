#pragma once

#include <optional>
#include <vector>

#include "loom/db.h"

namespace loom {

struct ImportAuditPresetValues {
  bool active_only;
  double chars_per_token_low;
  double chars_per_token_high;
  double output_ratio;
};
const ImportAuditPresetValues& default_import_audit_preset();

// Offline planning only; rates are explicitly supplied USD per million tokens.
// This measures normalized message text, never raw JSON, attachment bytes or OCR.
struct ImportAuditOptions {
  bool active_only = default_import_audit_preset().active_only;
  double chars_per_token_low = default_import_audit_preset().chars_per_token_low;
  double chars_per_token_high = default_import_audit_preset().chars_per_token_high;
  double output_ratio = default_import_audit_preset().output_ratio;
  std::optional<double> input_price;
  std::optional<double> output_price;
};

// Complete effective values; no merge with defaults after layer suppression.
// Other fields in the shared native/Python descriptor belong to its consumers.
Result<ImportAuditPresetValues> import_audit_preset_from_values(const Json& values);
Status apply_import_audit_preset_values(ImportAuditOptions& options, const Json& values);
Result<Json> inspect_import_audit_preset();

Status validate_import_audit_options(const ImportAuditOptions& options);
Result<Json> audit_import(Database& db, const std::vector<Conversation>& conversations,
                          const ImportAuditOptions& options = {});

}  // namespace loom
