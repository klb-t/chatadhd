// loom/cleanup.h — housekeeping of abandoned scratch files and unreferenced blobs, run when a runtime starts its workers.
//
// Config key `cleanup` (object, absent = presets; every value is a preset the user replaces, none is a limit on use):
//   upload_part_max_age_hours  24    `<data>/tmp/uploads/up_<12 hex>.part` (chunked uploads that were never committed) older than this are
//                                    removed. 0 = keep forever.
//   orphan_blob_max_age_days   0     blobs of the blob store older than this that NO graph node, link, run, source, artifact or other row of
//                                    the database (any text column) and no top-level JSON file of the data directory names by hash are
//                                    candidates. 0 = off (preset): nothing is looked at.
//   orphan_blob_dry_run        true  candidates are only logged and reported; set false to delete them. A blob that is named anywhere is
//                                    never a candidate, whatever these settings say.
#pragma once

#include <filesystem>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

struct CleanupOptions {
  double upload_part_max_age_hours = 24;
  double orphan_blob_max_age_days = 0;
  bool orphan_blob_dry_run = true;
};

// Reads the `cleanup` object over the presets; Errc::InvalidArgument for a wrong type or a negative number.
Result<CleanupOptions> cleanup_options(const Config& config);

// Runs both passes. `now_iso` (UTC ISO, default: now) is the clock of the blob age. Returns
// {schema:"loom.cleanup/1", upload_parts:{removed,bytes,files[]}, orphan_blobs:{status:"off"|"dry_run"|"deleted",candidates,bytes,hashes[],removed}}.
Result<Json> run_cleanup(Database& db, BlobStore& blobs, const std::filesystem::path& data_root, const CleanupOptions& options,
                         const std::string& now_iso = "");

}  // namespace loom
