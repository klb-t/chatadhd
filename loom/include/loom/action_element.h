// loom/action_element.h — an id is enough to ask about an element (GRAF_HUB_KOD_SILNIK §5, §8, R54).
//
// `actions.for` and every other action query used to need the caller to say what the element is (its
// class, its extra traits). The resolver reads that from the stores: a bare message / conversation /
// node / link / version-group id, a blob hash, or the key of a settings field becomes an element with
// its class, its own traits (what the stored thing carries: a MIME type, text) and the attributes the
// rules may match (role, kind, field scope ...). Which prefix means which source, and which class a
// node kind or MIME type gives, is data (data/runtime/element_resolution.pack); the code only knows
// the sources it can read. The same resolver supplies the typed parts (text of a message or a whole
// conversation, a stored image / recording) that an action takes as input.
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/use_model.h"
#include "loom/util/json.h"

namespace loom {
class Database;
class BlobStore;
}  // namespace loom

namespace loom::actions {

// What the resolver may read. Anything may be null; an id whose source is missing is reported, never guessed.
struct ElementSources {
  Database* db = nullptr;
  BlobStore* blobs = nullptr;
  std::filesystem::path data_dir;  // user overlays of the profiles (element_resolution, settings_fields)
};

class ElementResolver {
 public:
  static Result<ElementResolver> load(const std::filesystem::path& data_dir = {});
  static Result<ElementResolver> from_values(const Json& element_resolution);

  // True when `text` matches an id form of the data (not whether the thing exists).
  bool is_id(std::string_view text) const;

  // `element` as given to the matching engine: a string (class id, id, settings key) or an object with
  // an "id" and perhaps no "class". Returns the engine's element object
  //   {"id","class","traits":[own traits],"attrs":{...}, "source": <id form source>, "label"?}
  // unchanged when it needs no resolution (a class id, or an object that already names its class).
  // `known_class` tells a class id from an id (a string that is a class id is never resolved).
  // Errc::NotFound when the id has a known form but nothing stored under it.
  Result<Json> resolve(const ElementSources& sources, const Json& element,
                       const std::function<bool(std::string_view)>& known_class) const;

  // The element's content as typed parts: text of a message / node / text blob, the transcript of a
  // conversation, the bytes reference of a media blob. Empty for elements that carry no content.
  Result<std::vector<llm::Part>> content_parts(const ElementSources& sources, const Json& resolved_element) const;

 private:
  struct Form {
    std::string id, prefix, source, cls;
    std::size_t hex_length = 0;
  };
  const Form* form_of(std::string_view text) const;
  Result<Json> resolve_form(const ElementSources& sources, const Form& form, std::string_view id) const;
  Result<Json> resolve_blob(const ElementSources& sources, std::string_view hash) const;
  Result<Json> resolve_setting(const ElementSources& sources, std::string_view key) const;
  Json mime_element(std::string_view mime) const;

  Json values_ = Json::object();
  std::vector<Form> forms_;
};

}  // namespace loom::actions
