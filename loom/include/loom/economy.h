// loom/economy.h — read-only resource-economy report (R50, GRAPH_ENTRY_PARTIES_ECONOMY section 9).
//
// The report measures waste classes from existing tables and proposes actions
// (pin/link, compression, cache, nothing) with a benefit/cost balance. It never
// writes: the database is opened read-only with query_only, and nothing is
// applied (mode "auto" is reserved, not implemented). Thresholds and cost
// coefficients are preset data in data/runtime/economy.pack (domain "economy");
// the owner overrides them through profiles/economy.pack or per-call overrides.
// A class that cannot be measured is reported {known:false}, never zero.
#pragma once

#include <filesystem>

#include "loom/result.h"
#include "loom/runtime_profile.h"
#include "loom/util/json.h"

namespace loom::economy {

// Effective economy profile: builtin preset + <data_dir>/profiles/economy.pack + overrides.
Result<RuntimeProfile> load_profile(const std::filesystem::path& data_dir,
                                    const Json& overrides = Json::object());

// loom.economy_report/1. mode "off" returns status "disabled" without opening the database.
Result<Json> report(const std::filesystem::path& db_path, const RuntimeProfile& profile);

// Command used by the C ABI, server and CLI: {"overrides": {...}} -> report.
Result<Json> report_for_data_dir(const std::filesystem::path& data_dir,
                                 const std::filesystem::path& db_path, const Json& command);

}  // namespace loom::economy
