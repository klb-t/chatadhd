// The incremental index behind sequence and group detection. See include/loom/action_index.h.
#include "loom/action_index.h"

#include <algorithm>
#include <chrono>

#include "loom/util/time.h"

namespace loom::actions {

namespace {

constexpr std::string_view kSchema = "loom.sequence_index/1";
constexpr std::size_t kProcessedKept = 256;

std::optional<std::int64_t> seconds_of(const std::string& iso) {
  auto tp = timeutil::parse_iso_utc(iso);
  if (!tp) return std::nullopt;
  return std::chrono::duration_cast<std::chrono::seconds>(tp->time_since_epoch()).count();
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? sep : std::string()) + parts[i];
  return out;
}

}  // namespace

Json SequenceItem::to_json() const {
  return Json{{"run", run_id}, {"action", action}, {"class", cls}, {"element", element_id}, {"conv", conv}, {"message", message_id},
              {"created", created}, {"updated", updated}, {"produced", Json(std::vector<std::string>(produced.begin(), produced.end()))}};
}

SequenceItem SequenceItem::from_json(const Json& j) {
  SequenceItem i;
  i.run_id = json::get_string(j, "run");
  i.action = json::get_string(j, "action");
  i.cls = json::get_string(j, "class");
  i.element_id = json::get_string(j, "element");
  i.conv = json::get_string(j, "conv");
  i.message_id = json::get_string(j, "message");
  i.created = json::get_string(j, "created");
  i.updated = json::get_string(j, "updated");
  for (const auto& p : j.value("produced", Json::array()))
    if (p.is_string()) i.produced.insert(p.get<std::string>());
  return i;
}

bool is_pipeline(const std::vector<SequenceItem>& list, std::size_t from, std::size_t len, std::int64_t gap_seconds) {
  if (from + len > list.size() || list[from].cls.empty()) return false;
  for (std::size_t k = from; k + 1 < from + len; ++k) {
    const SequenceItem& a = list[k];
    const SequenceItem& b = list[k + 1];
    if (!a.produced.count(b.element_id)) return false;  // the next one works on what the one before made
    if (a.action == b.action) return false;
    auto after = seconds_of(a.updated), before = seconds_of(b.created);
    if (after && before && *before - *after > gap_seconds) return false;
  }
  return true;
}

std::string chain_signature(const std::vector<SequenceItem>& list, std::size_t from, std::size_t len) {
  std::vector<std::string> names;
  for (std::size_t k = from; k < from + len; ++k) names.push_back(list[k].action);
  return list[from].cls + "|" + join(names, "+");
}

IndexSettings IndexSettings::from_routing(const Json& sequences) {
  IndexSettings s;
  s.max_len = static_cast<std::size_t>(std::max<std::int64_t>(2, json::get_int(sequences, "max_length", 3)));
  s.gap_seconds = json::get_int(sequences, "gap_seconds", 900);
  s.chains_per_class = static_cast<std::size_t>(std::max<std::int64_t>(1, json::get_int(sequences, "window_runs", 200)));
  const Json index = sequences.value("index", Json::object());
  s.convs = static_cast<std::size_t>(std::max<std::int64_t>(1, json::get_int(index, "convs", 64)));
  s.snapshot_every = static_cast<std::size_t>(std::max<std::int64_t>(1, json::get_int(index, "snapshot_every", 100)));
  s.sources = static_cast<std::size_t>(std::max<std::int64_t>(1, json::get_int(index, "sources", 16)));
  return s;
}

void SequenceIndex::fold(const SequenceItem& item, std::int64_t seq, const IndexSettings& s) {
  ++stats_.runs_folded;
  ++since_snapshot_;
  Conv& conv = convs_[item.conv];
  conv.touched = seq;
  conv.tail.push_back(item);
  while (conv.tail.size() > s.max_len) conv.tail.erase(conv.tail.begin());
  // The pipelines that end at this run: one occurrence per length.
  for (std::size_t len = 2; len <= std::min(s.max_len, conv.tail.size()); ++len) {
    const std::size_t from = conv.tail.size() - len;
    if (!is_pipeline(conv.tail, from, len, s.gap_seconds)) continue;
    const std::string signature = chain_signature(conv.tail, from, len);
    auto& ring = classes_[conv.tail[from].cls];
    ring.push_back(Occurrence{signature});
    ++counts_[signature];
    while (ring.size() > s.chains_per_class) {
      auto it = counts_.find(ring.front().signature);
      if (it != counts_.end() && --it->second == 0) counts_.erase(it);
      ring.erase(ring.begin());
    }
  }
  Use& use = actions_[item.action];
  if (!item.cls.empty() && std::find(use.classes.begin(), use.classes.end(), item.cls) == use.classes.end()) use.classes.push_back(item.cls);
  if (!item.message_id.empty()) {
    use.messages.push_back(item.message_id);
    while (use.messages.size() > s.sources) use.messages.erase(use.messages.begin());
  }
  // Only the conversations touched most recently keep a tail; the chains they made are counted already.
  while (convs_.size() > s.convs) {
    auto oldest = convs_.begin();
    for (auto it = convs_.begin(); it != convs_.end(); ++it)
      if (it->second.touched < oldest->second.touched) oldest = it;
    convs_.erase(oldest);
  }
}

Json SequenceIndex::state_unlocked() const {
  Json convs = Json::array();
  for (const auto& [key, conv] : convs_) {
    Json tail = Json::array();
    for (const auto& item : conv.tail) tail.push_back(item.to_json());
    convs.push_back(Json{{"conv", key}, {"touched", conv.touched}, {"tail", tail}});
  }
  Json classes = Json::object();
  for (const auto& [cls, ring] : classes_) {
    Json sigs = Json::array();
    for (const auto& o : ring) sigs.push_back(o.signature);
    classes[cls] = sigs;
  }
  Json actions = Json::object();
  for (const auto& [action, use] : actions_) actions[action] = Json{{"classes", Json(use.classes)}, {"messages", Json(use.messages)}};
  return Json{{"schema", std::string(kSchema)}, {"last_seq", last_seq_}, {"processed", Json(processed_)}, {"convs", convs}, {"classes", classes}, {"actions", actions}};
}

void SequenceIndex::load_unlocked(const Json& snapshot) {
  last_seq_ = json::get_int(snapshot, "last_seq", 0);
  for (const auto& p : snapshot.value("processed", Json::array()))
    if (p.is_string()) processed_.push_back(p.get<std::string>());
  for (const auto& row : snapshot.value("convs", Json::array())) {
    Conv conv;
    conv.touched = json::get_int(row, "touched", 0);
    for (const auto& item : row.value("tail", Json::array())) conv.tail.push_back(SequenceItem::from_json(item));
    convs_[json::get_string(row, "conv")] = std::move(conv);
  }
  const Json classes = snapshot.value("classes", Json::object());
  for (auto it = classes.begin(); it != classes.end(); ++it)
    for (const auto& sig : it.value())
      if (sig.is_string()) {
        classes_[it.key()].push_back(Occurrence{sig.get<std::string>()});
        ++counts_[sig.get<std::string>()];
      }
  const Json actions = snapshot.value("actions", Json::object());
  for (auto it = actions.begin(); it != actions.end(); ++it) {
    for (const auto& cls : it.value().value("classes", Json::array()))
      if (cls.is_string()) actions_[it.key()].classes.push_back(cls.get<std::string>());
    for (const auto& id : it.value().value("messages", Json::array()))
      if (id.is_string()) actions_[it.key()].messages.push_back(id.get<std::string>());
  }
}

Status SequenceIndex::refresh(const std::function<bool(const std::string&)>& is_content, const IndexSettings& s) {
  std::lock_guard lk(mu_);
  ++stats_.refreshes;
  if (!loaded_) {
    loaded_ = true;
    LOOM_TRY_ASSIGN(auto snapshot, store_.load_index());
    if (snapshot && json::get_string(*snapshot, "schema") == kSchema) {
      load_unlocked(*snapshot);
      ++stats_.loaded_from_snapshot;
    }
  }
  constexpr std::size_t kPage = 2000;
  for (;;) {
    LOOM_TRY_ASSIGN(auto page, store_.checkpoints_since(last_seq_, kPage));
    for (const auto& cp : page) {
      ++stats_.checkpoints_read;
      last_seq_ = cp.seq;
      const Json& run = cp.run;
      if (json::get_string(run, "status") != "completed") continue;
      const std::string id = json::get_string(run, "id");
      if (std::find(processed_.begin(), processed_.end(), id) != processed_.end()) continue;  // a later checkpoint of a run folded already
      processed_.push_back(id);
      if (processed_.size() > kProcessedKept) processed_.erase(processed_.begin());
      const Json request = run.value("request", Json::object());
      const std::string action = json::get_string(request, "action");
      if (action.empty() || !is_content(action)) continue;
      const Json element = run.value("element", Json::object());
      const Json context = request.value("context", Json::object());
      SequenceItem item;
      item.run_id = id;
      item.action = action;
      item.cls = json::get_string(element, "class");
      item.element_id = json::get_string(element, "id");
      item.conv = json::get_string(context, "conv_id", json::get_string(context, "origin"));
      item.message_id = json::get_string(context, "message_id");
      item.created = json::get_string(run, "created");
      item.updated = json::get_string(run, "updated", item.created);
      if (const Json* result = json::find(run, "result"); result && result->is_object())
        if (const Json* produced = json::find(*result, "produced"); produced && produced->is_object())
          for (const auto& row : produced->value("items", Json::array()))
            for (const char* k : {"form", "message_id"})
              if (const std::string v = json::get_string(row, k); !v.empty()) item.produced.insert(v);
      fold(item, cp.seq, s);
    }
    if (page.size() < kPage) break;
  }
  if (since_snapshot_ >= s.snapshot_every) {
    LOOM_TRY(store_.save_index(state_unlocked()));
    since_snapshot_ = 0;
    ++stats_.snapshots;
  }
  return {};
}

std::vector<SequenceItem> SequenceIndex::tail(const std::string& conv) const {
  std::lock_guard lk(mu_);
  auto it = convs_.find(conv);
  return it == convs_.end() ? std::vector<SequenceItem>() : it->second.tail;
}

std::size_t SequenceIndex::count(const std::string& signature) const {
  std::lock_guard lk(mu_);
  auto it = counts_.find(signature);
  return it == counts_.end() ? 0 : it->second;
}

std::vector<std::string> SequenceIndex::classes_of(const std::string& action) const {
  std::lock_guard lk(mu_);
  auto it = actions_.find(action);
  return it == actions_.end() ? std::vector<std::string>() : it->second.classes;
}

std::vector<std::string> SequenceIndex::messages_of(const std::string& action) const {
  std::lock_guard lk(mu_);
  auto it = actions_.find(action);
  return it == actions_.end() ? std::vector<std::string>() : it->second.messages;
}

Json SequenceIndex::state() const {
  std::lock_guard lk(mu_);
  return state_unlocked();
}

SequenceIndex::Stats SequenceIndex::stats() const {
  std::lock_guard lk(mu_);
  return stats_;
}

}  // namespace loom::actions
