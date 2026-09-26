// Internal: non-throwing JSON reader/writer helpers shared by src/model/*.cpp
// (and by the pack validators, which parse pack files through the model
// types). The reader records the FIRST error and returns defaults afterwards,
// so from_json bodies read top to bottom and check ok() once.
#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/model.h"

namespace loom::model::detail {

class Rd {
 public:
  Rd(const Json& j, std::string path) : j_(j), path_(std::move(path)) {
    if (!j.is_object()) fail("", "expected a JSON object");
  }

  bool ok() const noexcept { return !err_.has_value(); }
  const Error& error() const { return *err_; }
  const std::string& path() const noexcept { return path_; }
  std::string sub(std::string_view key) const { return path_ + "/" + std::string(key); }

  void fail(std::string_view key, const std::string& msg) {
    if (err_) return;
    err_ = Error(Errc::InvalidArgument, (key.empty() ? path_ : sub(key)) + ": " + msg);
  }
  void fail_with(const Error& e) {
    if (!err_) err_ = e;
  }

  // Present and not null.
  const Json* get(std::string_view key, bool required = false) {
    if (!j_.is_object()) return nullptr;
    const Json* p = json::find(j_, key);
    if (!p || p->is_null()) {
      if (required) fail(key, "missing required key");
      return nullptr;
    }
    return p;
  }
  bool has(std::string_view key) { return get(key) != nullptr; }

  std::string str(std::string_view key, bool required = false, std::string def = {}) {
    const Json* p = get(key, required);
    if (!p) return def;
    if (!p->is_string()) {
      fail(key, "expected a string");
      return def;
    }
    std::string s = p->get<std::string>();
    if (required && s.empty()) fail(key, "must not be empty");
    return s;
  }
  double num(std::string_view key, double def, bool required = false) {
    const Json* p = get(key, required);
    if (!p) return def;
    if (!p->is_number()) {
      fail(key, "expected a number");
      return def;
    }
    double v = p->get<double>();
    if (!std::isfinite(v)) fail(key, "must be finite");
    return v;
  }
  // A number in [0, 1].
  double unit(std::string_view key, double def, bool required = false) {
    double v = num(key, def, required);
    if (ok() && (v < 0.0 || v > 1.0)) fail(key, "must be in [0, 1]");
    return v;
  }
  std::int64_t i64(std::string_view key, std::int64_t def, bool required = false) {
    const Json* p = get(key, required);
    if (!p) return def;
    if (!p->is_number_integer()) {
      fail(key, "expected an integer");
      return def;
    }
    return p->get<std::int64_t>();
  }
  int integer(std::string_view key, int def, bool required = false) {
    return static_cast<int>(i64(key, def, required));
  }
  std::optional<std::int64_t> opt_i64(std::string_view key) {
    if (!get(key)) return std::nullopt;
    return i64(key, 0);
  }
  std::optional<double> opt_num(std::string_view key) {
    if (!get(key)) return std::nullopt;
    return num(key, 0.0);
  }
  bool boolean(std::string_view key, bool def) {
    const Json* p = get(key);
    if (!p) return def;
    if (!p->is_boolean()) {
      fail(key, "expected a boolean");
      return def;
    }
    return p->get<bool>();
  }
  std::vector<std::string> strs(std::string_view key, bool required = false) {
    std::vector<std::string> out;
    const Json* p = get(key, required);
    if (!p) return out;
    if (!p->is_array()) {
      fail(key, "expected an array of strings");
      return out;
    }
    for (std::size_t i = 0; i < p->size(); ++i) {
      if (!(*p)[i].is_string()) {
        fail(std::string(key) + "/" + std::to_string(i), "expected a string");
        return out;
      }
      out.push_back((*p)[i].get<std::string>());
    }
    return out;
  }
  // Raw JSON (null when missing).
  Json raw(std::string_view key, Json def = nullptr) {
    const Json* p = get(key);
    return p ? *p : def;
  }
  Json object(std::string_view key) {
    const Json* p = get(key);
    if (!p) return Json::object();
    if (!p->is_object()) {
      fail(key, "expected an object");
      return Json::object();
    }
    return *p;
  }
  Json array(std::string_view key) {
    const Json* p = get(key);
    if (!p) return Json::array();
    if (!p->is_array()) {
      fail(key, "expected an array");
      return Json::array();
    }
    return *p;
  }
  // Null / missing -> nullopt; otherwise must be an object (expression).
  Json expr(std::string_view key) {
    const Json* p = get(key);
    if (!p) return nullptr;
    if (!p->is_object()) fail(key, "expected an expression object {\"op\",\"args\"}");
    return *p;
  }

  template <ClosedSetEnum E>
  E en(std::string_view key, E def, bool required = false) {
    const Json* p = get(key, required);
    if (!p) return def;
    if (!p->is_string()) {
      fail(key, "expected a string");
      return def;
    }
    auto r = parse<E>(p->get<std::string>(), sub(key));
    if (!r) {
      fail_with(r.error());
      return def;
    }
    return *r;
  }
  template <ClosedSetEnum E>
  std::optional<E> opt_en(std::string_view key) {
    if (!get(key)) return std::nullopt;
    return en<E>(key, E{});
  }
  template <ClosedSetEnum E>
  std::vector<E> ens(std::string_view key) {
    std::vector<E> out;
    for (const auto& s : strs(key)) {
      auto r = parse<E>(s, sub(key));
      if (!r) {
        fail_with(r.error());
        return out;
      }
      out.push_back(*r);
    }
    return out;
  }
  Text text(std::string_view key, bool required = false) {
    const Json* p = get(key, required);
    if (!p) return {};
    auto t = text_from_json(*p, sub(key));
    if (!t) {
      fail_with(t.error());
      return {};
    }
    if (required && t->empty()) fail(key, "must not be empty");
    return *t;
  }
  template <class T>
  std::optional<T> opt_obj(std::string_view key) {
    const Json* p = get(key);
    if (!p) return std::nullopt;
    auto r = T::from_json(*p);
    if (!r) {
      fail_with(Error(r.error().code, sub(key) + "/" + r.error().message));
      return std::nullopt;
    }
    return std::move(*r);
  }
  template <class T>
  T obj(std::string_view key, bool required = false) {
    if (required && !get(key, true)) return T{};
    auto v = opt_obj<T>(key);
    return v ? std::move(*v) : T{};
  }
  template <class T>
  std::vector<T> list(std::string_view key, bool required = false) {
    std::vector<T> out;
    const Json* p = get(key, required);
    if (!p) return out;
    if (!p->is_array()) {
      fail(key, "expected an array");
      return out;
    }
    for (std::size_t i = 0; i < p->size(); ++i) {
      auto r = T::from_json((*p)[i]);
      if (!r) {
        fail_with(Error(r.error().code, sub(key) + "/" + std::to_string(i) + "/" + r.error().message));
        return out;
      }
      out.push_back(std::move(*r));
    }
    return out;
  }
  std::optional<kb::ExpectedProperty> expected(std::string_view key) {
    const Json* p = get(key);
    if (!p) return std::nullopt;
    auto r = kb::ExpectedProperty::from_json(*p);
    if (!r) {
      fail_with(Error(r.error().code, sub(key) + ": " + r.error().message));
      return std::nullopt;
    }
    return std::move(*r);
  }

 private:
  const Json& j_;
  std::string path_;
  std::optional<Error> err_;
};

inline Json strs(const std::vector<std::string>& v) {
  Json a = Json::array();
  for (const auto& s : v) a.push_back(s);
  return a;
}
template <ClosedSetEnum E>
Json en(E e) {
  return std::string(to_string(e));
}
template <ClosedSetEnum E>
Json opt_en(const std::optional<E>& e) {
  return e ? en(*e) : Json(nullptr);
}
template <ClosedSetEnum E>
Json ens(const std::vector<E>& v) {
  Json a = Json::array();
  for (auto e : v) a.push_back(std::string(to_string(e)));
  return a;
}
template <class T>
Json list(const std::vector<T>& v) {
  Json a = Json::array();
  for (const auto& x : v) a.push_back(x.to_json());
  return a;
}
template <class T>
Json opt(const std::optional<T>& v) {
  return v ? v->to_json() : Json(nullptr);
}

// '\x1f'-joined key parts.
std::string join_key(std::initializer_list<std::string_view> parts);

}  // namespace loom::model::detail
