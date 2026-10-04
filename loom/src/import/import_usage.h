#pragma once

#include <memory>
#include "loom/config.h"
#include "loom/importer.h"

namespace loom {

// Optional shared-policy adapter: thread 2 is integrated before thread 5.
// A build without that capability reports its absence explicitly.
class ImportUsageSession {
 public:
  static Result<std::unique_ptr<ImportUsageSession>> open(Config& config, const std::filesystem::path& data_dir);
  ~ImportUsageSession();
  Status request(const std::filesystem::path& source, const ImportOptions& options,
                 const std::string& operation_id, const std::string& baseline_key,
                 const std::string& confirm_receipt = "", const std::string& confirmation_ref = "");
  Status complete(const ImportResult& result);
  const Json& receipt() const;
  bool requires_confirmation() const;
 private:
  struct Impl;
  explicit ImportUsageSession(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace loom
