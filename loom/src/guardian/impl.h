// Internal: the guardian's state and the functions its translation units share.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

#include "internal.h"
#include "loom/runtime.h"

namespace loom::guardian {

struct Signal {
  std::string kind;
  double score = 1.0;
  std::string by;              // heuristic | model | annotation | structural | user
  bool tone = false, repeat = false;
  std::string scope;           // explicit scope marker ("" = none)
  std::vector<std::string> exceptions;
  std::string subject;         // what the claim says ("" = the user's own words)
  std::string lang = "en";
  // The segment the signal stands on (codepoints; -1 = the whole message), its computed confidence, and the kind of statement it is.
  std::int64_t start = -1, end = -1;
  double confidence = 1.0;
  std::string qualifier;       // quoted | hypothetical | "" (a qualified segment is annotated but does not count as evidence)
  bool counts = true;
  int cluster = 0;             // the n-th segment of this kind in the message (0 = the first)
  std::string segment;         // the text of the segment
  std::vector<std::pair<std::int64_t, std::int64_t>> exception_spans;  // where the exceptions stand (annotated as `exception`)
  Json cost = Json::object();  // what making the signal cost (a model reading)
  std::string method_version;  // of the reading that made it
};

struct Method {
  std::string name;
  bool heuristic = false, model = false;
  std::string ask_model_when;
};

struct Guardian::Impl {
  explicit Impl(Runtime& r) : rt(r) {}
  Runtime& rt;
  ScopedSubscription sub;
  std::mutex mu;
  std::condition_variable cv, idle_cv;
  std::deque<std::string> queue;  // message ids waiting for a model reading
  int busy = 0;
  bool stopping = false;
  std::thread worker;

  Result<detail::Data> data();
  Method method(const detail::Data& d, const std::string& override_name = std::string());

  // observation
  void on_event(const Json& event);
  Result<Json> observe(const std::string& message_id, const Json& opts);
  std::vector<Signal> detect_heuristic(const detail::Data& d, const Message& m, bool has_goal, const std::set<std::string>& skip_kinds);
  Result<std::optional<Signal>> detect_model(const detail::Data& d, const Message& m, Json& issues);
  Result<std::string> write_signal(const detail::Data& d, const Message& m, const Signal& s);
  Result<Json> record(const detail::Data& d, const Message& m, const Signal& s);
  void run_worker();

  // hypotheses
  Json weigh(const detail::Data& d, Json& hyp_meta, const std::vector<Node>& others);
  Status reevaluate(const detail::Data& d, const std::string& hyp_id);

  // proposals and promotion
  Result<Json> post_pending(const detail::Data& d, const std::string& conv_id);
  Result<Json> promote(const detail::Data& d, const Json& request);
  Result<Json> undo(const detail::Data& d, const Json& request);
  Result<Json> status(const detail::Data& d, const Json& request);
  Result<Json> compile(const detail::Data& d, const Json& request);
  Result<Json> levels(const detail::Data& d, const Json& request);
  Result<Json> annotate(const detail::Data& d, const Json& request);
  Result<Json> promote_node(const detail::Data& d, const std::string& hyp_id, const Json& request);
};

}  // namespace loom::guardian
