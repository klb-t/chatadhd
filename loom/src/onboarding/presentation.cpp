#include "loom/onboarding_presentation.h"

namespace loom::onboarding {
namespace {
Error invalid(std::string message) {
  return Error(Errc::InvalidArgument, "onboarding presentation: " + std::move(message));
}
}  // namespace

Status validate_presentation(const Json& catalog) {
  if (!catalog.is_object() || catalog.value("schema", Json{}) != "loom.onboarding.presentation/1" ||
      !catalog.contains("default_locale") || !catalog["default_locale"].is_string() ||
      catalog["default_locale"].get_ref<const std::string&>().empty() ||
      !catalog.contains("locales") || !catalog["locales"].is_object() ||
      !catalog["locales"].contains(catalog["default_locale"].get<std::string>()) ||
      !catalog.contains("defaults") || !catalog["defaults"].is_object())
    return invalid("catalog requires schema, declared default locale, locale maps and defaults");
  for (const auto& [locale, messages] : catalog["locales"].items()) {
    if (locale.empty() || !messages.is_object()) return invalid("locale needs an object message catalog");
    for (const auto& [id, value] : messages.items())
      if (id.empty() || !value.is_string()) return invalid("message needs a nonempty identity and string template");
  }
  return ok_status();
}

Result<std::string> presentation_text(const Json& catalog, std::string_view id,
                                     const Json& parameters, std::string_view requested_locale) {
  try {
    LOOM_TRY(validate_presentation(catalog));
    if (!parameters.is_object()) return invalid("template parameters must be an object");
    const std::string locale = requested_locale.empty()
        ? catalog["default_locale"].get<std::string>() : std::string(requested_locale);
    if (!catalog["locales"].contains(locale)) return invalid("requested locale is not declared: " + locale);
    const auto& messages = catalog["locales"][locale];
    if (!messages.contains(std::string(id))) return invalid("message is not declared: " + std::string(id));
    const auto& body = messages[std::string(id)].get_ref<const std::string&>();
    std::string output;
    std::size_t begin = 0;
    while (begin < body.size()) {
      const auto opening = body.find("{{", begin);
      if (opening == std::string::npos) { output.append(body, begin, std::string::npos); break; }
      output.append(body, begin, opening - begin);
      const auto closing = body.find("}}", opening + 2);
      if (closing == std::string::npos) return invalid("unterminated template parameter: " + std::string(id));
      const auto name = body.substr(opening + 2, closing - opening - 2);
      if (name.empty() || name.find("{{") != std::string::npos)
        return invalid("malformed template parameter: " + std::string(id));
      if (!parameters.contains(name)) return invalid("template parameter is missing: " + name);
      const auto& value = parameters[name];
      output += value.is_string() ? value.get<std::string>() : json::canonical(value);
      begin = closing + 2;
    }
    return output;
  } catch (const Json::exception& error) { return invalid(error.what()); }
}
}  // namespace loom::onboarding
