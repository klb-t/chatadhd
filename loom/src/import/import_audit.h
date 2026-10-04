#pragma once

#include <optional>
#include <vector>

#include "loom/db.h"

namespace loom {

// Offline planning only; rates are explicitly supplied USD per million tokens.
// This measures normalized message text, never raw JSON, attachment bytes or OCR.
struct ImportAuditOptions {
  bool active_only = false;
  double chars_per_token_low = 4.0;
  double chars_per_token_high = 3.0;
  double output_ratio = 0.4;
  std::optional<double> input_price;
  std::optional<double> output_price;
};

Status validate_import_audit_options(const ImportAuditOptions& options);
Result<Json> audit_import(Database& db, const std::vector<Conversation>& conversations,
                          const ImportAuditOptions& options = {});

}  // namespace loom
