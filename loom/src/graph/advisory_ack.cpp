// Remembered answers to advisories (`advisory_ack`, G3). See include/loom/advisory.h.
#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "loom/advisory.h"
#include "loom/log.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::advisory {
namespace {

Json canonical(const Json& v) {
  if (v.is_object()) {
    std::vector<std::string> keys;
    for (auto it = v.begin(); it != v.end(); ++it) keys.push_back(it.key());
    std::sort(keys.begin(), keys.end());
    Json out = Json::object();
    for (const auto& k : keys) out[k] = canonical(v.at(k));
    return out;
  }
  if (v.is_array()) {
    Json out = Json::array();
    for (const auto& x : v) out.push_back(canonical(x));
    return out;
  }
  return v;
}

std::string ack_key(const std::string& code, const std::string& fp) { return code + "|" + fp; }

void merge_nullable(Json& into, const Json& patch) {
  if (!patch.is_object()) return;
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (it.value().is_null()) into.erase(it.key());
    else into[it.key()] = it.value();
  }
}

Json empty_doc() { return Json{{"schema", "loom.advisory_ack/1"}, {"acks", Json::object()}, {"policy", Json::object()}}; }

}  // namespace

std::string fingerprint(const Json& configuration) { return Sha256::hex(json::dump(canonical(configuration))).substr(0, 16); }

Json effective_policy(const Json& preset, const Json& store_policy, const std::string& code) {
  Json out{{"ask", true}, {"remember_days", nullptr}};
  auto layer = [&](const Json& l) {
    if (!l.is_object()) return;
    for (auto it = l.begin(); it != l.end(); ++it) {
      if (it.key() == "codes") continue;
      if (it.value().is_null() && it.key() != "remember_days") continue;
      out[it.key()] = it.value();
    }
  };
  layer(preset);
  if (preset.is_object() && preset.contains("codes") && preset["codes"].is_object() && preset["codes"].contains(code)) layer(preset["codes"][code]);
  // the store's layer: a null value there clears back to the preset (so it is skipped, never copied)
  auto store_layer = [&](const Json& l) {
    if (!l.is_object()) return;
    for (auto it = l.begin(); it != l.end(); ++it)
      if (it.key() != "codes" && !it.value().is_null()) out[it.key()] = it.value();
  };
  store_layer(store_policy);
  if (store_policy.is_object() && store_policy.contains("codes") && store_policy["codes"].is_object() && store_policy["codes"].contains(code))
    store_layer(store_policy["codes"][code]);
  return out;
}

AckStore AckStore::open(const std::filesystem::path& data_dir) {
  AckStore s;
  s.doc_ = empty_doc();
  if (data_dir.empty()) return s;
  s.path_ = data_dir / kAckStoreFile;
  std::error_code ec;
  if (!std::filesystem::exists(s.path_, ec)) return s;
  auto text = fsutil::read_file(s.path_);
  if (!text) {
    s.issues_.push_back(Json{{"code", "advisory_ack_unreadable"}, {"path", s.path_.string()}, {"error", text.error().message}});
    return s;
  }
  Json d = Json::parse(*text, nullptr, false);
  if (!d.is_object() || json::get_string(d, "schema") != "loom.advisory_ack/1") {
    s.issues_.push_back(Json{{"code", "advisory_ack_unreadable"}, {"path", s.path_.string()}, {"error", "not a loom.advisory_ack/1 document"}});
    return s;
  }
  if (!d.contains("acks") || !d["acks"].is_object()) d["acks"] = Json::object();
  if (!d.contains("policy") || !d["policy"].is_object()) d["policy"] = Json::object();
  s.doc_ = std::move(d);
  return s;
}

Status AckStore::save() {
  if (path_.empty()) return {};
  return fsutil::atomic_write(path_, json::dump(doc_, 2));
}

Json AckStore::find(const std::string& code, const std::string& fp) const {
  const Json& acks = doc_.at("acks");
  const std::string k = ack_key(code, fp);
  return acks.contains(k) ? acks.at(k) : Json(nullptr);
}

Status AckStore::acknowledge(const std::string& code, const std::string& fp, const std::string& choice, const Json& context, std::string now) {
  if (code.empty() || fp.empty()) return Error(Errc::InvalidArgument, "an advisory answer needs a code and a fingerprint");
  if (now.empty()) now = timeutil::utc_now_iso();
  Json entry{{"code", code}, {"fingerprint", fp}, {"choice", choice}, {"ack_at", now}};
  if (context.is_object() && !context.empty()) entry["context"] = context;
  doc_["acks"][ack_key(code, fp)] = std::move(entry);
  return save();
}

Result<int> AckStore::forget(const std::string& code, const std::string& fp) {
  const int n = static_cast<int>(doc_["acks"].erase(ack_key(code, fp)));
  if (n) LOOM_TRY(save());
  return n;
}

Result<int> AckStore::forget_all() {
  const int n = static_cast<int>(doc_["acks"].size());
  doc_["acks"] = Json::object();
  if (n) LOOM_TRY(save());
  return n;
}

Status AckStore::set_policy(const Json& patch) {
  if (!patch.is_object()) return Error(Errc::InvalidArgument, "an advisory policy patch is an object");
  Json& pol = doc_["policy"];
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (it.key() == "codes" && it.value().is_object()) {
      if (!pol.contains("codes") || !pol["codes"].is_object()) pol["codes"] = Json::object();
      for (auto c = it.value().begin(); c != it.value().end(); ++c) {
        if (c.value().is_null()) { pol["codes"].erase(c.key()); continue; }
        Json cur = pol["codes"].contains(c.key()) ? pol["codes"][c.key()] : Json::object();
        merge_nullable(cur, c.value());
        if (cur.empty()) pol["codes"].erase(c.key());
        else pol["codes"][c.key()] = cur;
      }
      continue;
    }
    if (it.value().is_null()) pol.erase(it.key());
    else pol[it.key()] = it.value();
  }
  return save();
}

Json AckStore::annotate(Json record, const std::string& fp, const Json& preset_policy, const std::string& now) const {
  const std::string code = json::get_string(record, "code");
  const Json pol = effective_policy(preset_policy, doc_.at("policy"), code);
  Json ask = record.contains("ask") && record["ask"].is_object() ? record["ask"] : Json::object();
  if (!ask.contains("key")) ask["key"] = "advisory." + code + ".ask_every";
  ask["fingerprint"] = fp;
  if (!ask.contains("choices") && pol.contains("choices")) ask["choices"] = pol["choices"];
  ask["policy"] = Json{{"ask", pol.value("ask", true)}, {"remember_days", pol.value("remember_days", Json(nullptr))}};
  bool asking = json::get_bool(pol, "ask", true);
  std::string state = asking ? "ask" : "muted";
  if (asking) {
    const Json ack = find(code, fp);
    if (ack.is_object()) {
      bool valid = true;
      const Json days = pol.value("remember_days", Json(nullptr));
      if (days.is_number()) {
        const auto at = timeutil::parse_iso_utc(json::get_string(ack, "ack_at"));
        const auto t = timeutil::parse_iso_utc(now.empty() ? timeutil::utc_now_iso() : now);
        valid = at && t && std::chrono::duration<double>(*t - *at).count() < days.get<double>() * 86400.0;
      }
      ask["ack"] = ack;
      if (valid) {
        asking = false;
        state = "acknowledged";
      } else {
        ask["ack_expired"] = true;
      }
    }
  }
  ask["asking"] = asking;
  ask["state"] = state;
  record["ask"] = std::move(ask);
  return record;
}

}  // namespace loom::advisory
