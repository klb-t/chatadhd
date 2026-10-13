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
  const PlatformSpec* platform = sniff_platform(element);
  return platform && !platform->provider.empty() ? find(platform->provider) : nullptr;
}

const ProviderHandlers* ProviderRegistry::claim_shard(std::string_view base) const {
  const PlatformSpec* platform = platform_for_shard(base);
  return platform && !platform->provider.empty() ? find(platform->provider) : nullptr;
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
