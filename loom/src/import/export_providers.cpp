// Provider table of the lossless export path (B-MOD-05). Each provider lives in
// its own unit (export_openai.cpp, export_anthropic.cpp) and contributes one
// ProviderHandlers entry; the driver never names a provider.
#include "export_internal.h"

namespace loom::xport {

ProviderRegistry& ProviderRegistry::instance() {
  static ProviderRegistry registry = [] {
    ProviderRegistry r;
    r.add(openai_provider());     // detection order: first claiming provider wins
    r.add(anthropic_provider());
    return r;
  }();
  return registry;
}

void ProviderRegistry::add(ProviderHandlers handlers) {
  for (auto& h : handlers_) {
    if (h.name == handlers.name) {
      h = std::move(handlers);
      return;
    }
  }
  handlers_.push_back(std::move(handlers));
}

const ProviderHandlers* ProviderRegistry::find(std::string_view name) const {
  for (const auto& h : handlers_)
    if (h.name == name) return &h;
  return nullptr;
}

const ProviderHandlers* ProviderRegistry::claim_element(const Json& element) const {
  if (!element.is_object()) return nullptr;
  for (const auto& h : handlers_)
    for (const auto& key : h.element_keys)
      if (json::find(element, key)) return &h;
  return nullptr;
}

const ProviderHandlers* ProviderRegistry::claim_shard(std::string_view base) const {
  for (const auto& h : handlers_)
    if (!h.shard_prefix.empty() && base.rfind(h.shard_prefix, 0) == 0) return &h;
  return nullptr;
}

std::string pointer_token(std::string_view key) {
  std::string escaped;
  for (char character : key) {
    if (character == '~') escaped += "~0";
    else if (character == '/') escaped += "~1";
    else escaped += character;
  }
  return escaped;
}

}  // namespace loom::xport
