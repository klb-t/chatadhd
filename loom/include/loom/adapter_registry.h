// loom/adapter_registry.h — AdapterRegistry<Port>: the one registry shape for
// "a name selects an adapter that implements a port" (B-MOD-02).
//
//   Port    the abstract interface a consumer talks to (StorageBackend, AsrProvider,
//           OcrProvider, llm::Adapter, BatchBackend, ...).
//   Env     what the consumer lends the factory (transport, secrets, a profile);
//           NoAdapterEnv when the factory needs nothing.
//   entry   name + status ("implemented" | "planned") + factory + free-form `meta`
//           (data about the adapter that a profile or a UI may show).  A planned
//           entry has no factory and reports Errc::NotImplemented; an unknown name
//           reports Errc::Unsupported and lists the names that are known.
//   alias   a second spelling of an entry ("openai" -> "openai_embeddings"); lookups
//           and create() resolve it, names() lists real entries only.
//
// Consumers never spell an adapter name: the name comes from data (a descriptor's
// "adapter" key, a manifest, a stored batch record) and a new adapter is one add().
// The registry has no lock: consumers that register after start-up (llm::Adapter)
// guard their instance themselves; the built-in instances are filled once.
#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

struct NoAdapterEnv {};

template <class Port, class Env = NoAdapterEnv>
struct AdapterDescriptor {
  using Factory = std::function<Result<std::unique_ptr<Port>>(const std::string& id, const Json& cfg, const Env& env)>;
  std::string name;
  std::string status = "implemented";  // "implemented" | "planned"
  Factory factory;                     // empty for planned entries
  Json meta = Json::object();          // data about the adapter (shown, never interpreted here)
};

template <class Port, class Env = NoAdapterEnv>
class AdapterRegistry {
 public:
  using Descriptor = AdapterDescriptor<Port, Env>;
  using Factory = typename Descriptor::Factory;

  // `selector_key`: the key of the config object create() reads the name from;
  // `noun`: how errors call an entry ("storage backend kind", "ASR adapter").
  explicit AdapterRegistry(std::string selector_key = "adapter", std::string noun = "adapter")
      : selector_key_(std::move(selector_key)), noun_(std::move(noun)) {}

  // Replaces an entry of the same name (keeping its place), else appends.
  void add(Descriptor d) {
    for (auto& e : entries_)
      if (e.name == d.name) { e = std::move(d); return; }
    entries_.push_back(std::move(d));
  }
  // Pointers from find() stay valid until remove().
  bool remove(std::string_view name) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Descriptor& e) { return e.name == name; });
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
  }
  // `alias` resolves to the entry `target` (which may be added later).
  void alias(std::string alias, std::string target) { aliases_[std::move(alias)] = std::move(target); }

  // The entry's own name for a spelling (itself when it is not an alias).
  std::string canonical(std::string_view name) const {
    auto it = aliases_.find(name);
    return it == aliases_.end() ? std::string(name) : it->second;
  }
  const Descriptor* find(std::string_view name) const {
    const std::string key = canonical(name);
    for (const auto& e : entries_)
      if (e.name == key) return &e;
    return nullptr;
  }
  bool contains(std::string_view name) const { return find(name) != nullptr; }
  // Real entries in registration order (the order is data: detection and fallback chains use it).
  std::vector<std::string> names() const {
    std::vector<std::string> out;
    for (const auto& e : entries_) out.push_back(e.name);
    return out;
  }
  const std::deque<Descriptor>& entries() const noexcept { return entries_; }
  const std::string& selector_key() const noexcept { return selector_key_; }
  const std::string& noun() const noexcept { return noun_; }

  // Instantiates the entry named in cfg[selector_key].
  Result<std::unique_ptr<Port>> create(const std::string& id, const Json& cfg, const Env& env) const {
    return create_named(json::get_string(cfg, selector_key_), id, cfg, env);
  }
  Result<std::unique_ptr<Port>> create_named(std::string_view name, const std::string& id, const Json& cfg,
                                             const Env& env) const {
    const Descriptor* d = find(name);
    if (!d) return Error(Errc::Unsupported, unknown_message(name, id));
    if (!d->factory)
      return Error(Errc::NotImplemented, noun_ + " '" + std::string(name) + "' is " + d->status + " (no adapter yet)");
    return d->factory(id, cfg, env);
  }
  std::string unknown_message(std::string_view name, const std::string& id) const {
    std::string known;
    for (const auto& e : entries_) known += (known.empty() ? "" : ", ") + e.name;
    return "unknown " + noun_ + " '" + std::string(name) + "'" + (id.empty() ? "" : " for " + id) + " (known: " + known + ")";
  }

 private:
  std::string selector_key_, noun_;
  std::deque<Descriptor> entries_;
  std::map<std::string, std::string, std::less<>> aliases_;
};

}  // namespace loom
