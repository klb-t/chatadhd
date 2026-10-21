#include "loom/turn_carrier.h"

namespace loom {

void TurnCarrier::add(const std::string& conv_id, const std::string& key, const Json& value, bool append) {
  std::lock_guard lk(mu_);
  Json& mine = by_conv_[conv_id];
  if (!mine.is_object()) mine = Json::object();
  if (!append) {
    mine[key] = value;
    return;
  }
  Json list = Json::array();
  if (mine.contains(key)) {
    if (mine[key].is_array()) list = mine[key];
    else if (mine[key].is_string()) list.push_back(mine[key]);
  }
  if (value.is_array()) {
    for (const auto& v : value) list.push_back(v);
  } else {
    list.push_back(value);
  }
  mine[key] = std::move(list);
}

Json TurnCarrier::peek(const std::string& conv_id) const {
  std::lock_guard lk(mu_);
  const auto it = by_conv_.find(conv_id);
  return it == by_conv_.end() ? Json::object() : it->second;
}

Json TurnCarrier::take(const std::string& conv_id) {
  std::lock_guard lk(mu_);
  const auto it = by_conv_.find(conv_id);
  if (it == by_conv_.end()) return Json::object();
  Json out = std::move(it->second);
  by_conv_.erase(it);
  return out;
}

void TurnCarrier::give_back(const std::string& conv_id, const Json& values) {
  if (!values.is_object() || values.empty()) return;
  std::lock_guard lk(mu_);
  Json& mine = by_conv_[conv_id];
  if (!mine.is_object()) mine = Json::object();
  for (auto it = values.begin(); it != values.end(); ++it)
    if (!mine.contains(it.key())) mine[it.key()] = it.value();
}

bool TurnCarrier::clear(const std::string& conv_id) {
  std::lock_guard lk(mu_);
  return by_conv_.erase(conv_id) > 0;
}

std::size_t TurnCarrier::waiting() const {
  std::lock_guard lk(mu_);
  return by_conv_.size();
}

}  // namespace loom
