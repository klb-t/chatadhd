#include <doctest/doctest.h>
#include <iostream>
#include "loom/extract.h"
#include "loom/kb.h"
#include "test_helpers.h"
using namespace loom;
TEST_CASE("zz debug text") {
  auto p = loom::test::unwrap(kb::Pack::load_builtin());
  extract::Extractor ex(p);
  auto u = extract::text_unit("call.vtt", "WEBVTT\n\n00:00:01.000 --> 00:00:04.000\nAnna: We ship on Friday.\n");
  auto r = loom::test::unwrap(ex.process(u));
  for (const auto& o : r.observations) std::cerr << o.to_json().dump() << "\n";
  Json conv{{"uuid", "c"}, {"name", "c"}, {"created_at", "2025-03-01T10:00:00Z"}, {"chat_messages", Json::array({
    Json{{"uuid","m0"},{"sender","human"},{"text","cos jest nie tak z 0.9.0, checklisty sie zgubily przy porcie."}},
    Json{{"uuid","m1"},{"sender","assistant"},{"text","1.0.0: sync v2. A checklisty?"}},
    Json{{"uuid","m2"},{"sender","human"},{"text","przywrocone w tej samej wersji."}}})}};
  extract::UnitContent c; c.structured = conv; c.unit.id = "un_x"; c.unit.source="s";
  auto r2 = loom::test::unwrap(ex.process(c));
  for (const auto& s : r2.statuses) std::cerr << s.to_json().dump() << "\n";
  for (const auto& e : r2.entities) std::cerr << e.kind << ":" << e.label << "\n";
}
