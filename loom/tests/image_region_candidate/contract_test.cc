#include "context/image_region_candidate/contract.h"
#include "loom/util/sha256.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace loom;
using namespace loom::context::image_region;
#include "fixture_generated.h"
namespace {
int cases = 0;
void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
void rejected(Selection s, Document d, Source rep, Source original, std::string bytes, const char* reason) {
  const auto result = validate(s, rep, original, d, bytes, 0);
  check(!result, "expected refusal");
  check(result.error().message == reason, reason);
  ++cases;
}
}
int main() try {
  const auto f = fixture();
  const auto& s = f.selection;
  const auto& d = f.document;
  const auto& rep = f.representation;
  const auto& original = f.original;
  const auto& bytes = f.bytes;
  const auto ok = validate(s, rep, original, d, bytes, 0);
  check(bool(ok), "valid stored region must pass");
  check(ok->text == "Zażółć 🙂", "only selected region text");
  check(ok->text.find("UNSELECTED") == std::string::npos, "other region leaked");
  check(ok->codepoints == 8 && ok->frame.extent[0] == 100, "frame/codepoints"); ++cases;
  const auto excerpt = validate(s, rep, original, d, bytes, 4);
  check(bool(excerpt) && excerpt->text == "Zażó" && excerpt->text.size() == 6, "UTF-8 exact prefix"); ++cases;
  const auto long_detail = validate(s, rep, original, d, bytes, 1000);
  check(bool(long_detail) && long_detail->text == ok->text, "detail beyond region stays local"); ++cases;
  auto second = s; second.ordinal = 1; second.pointer = "/regions/1";
  second.observation_pointer = "/regions/1/text";
  second.node_text = second.observation_text = d.regions[1].text;
  check(validate(second, rep, original, d, bytes, 0)->text == d.regions[1].text, "selected ordinal 1"); ++cases;
  const auto bad_s = [&](auto mutate, const char* reason) { auto x=s; mutate(x); rejected(x,d,rep,original,bytes,reason); };
  const auto bad_d = [&](auto mutate, const char* reason) { auto x=d; mutate(x); rejected(s,x,rep,original,bytes,reason); };
  const auto bad_rep = [&](auto mutate) { auto x=rep; mutate(x); rejected(s,d,x,original,bytes,"ocr_region_representation_integrity"); };
  bad_rep([](auto& x){x.hash[0] = x.hash[0] == '0' ? '1' : '0';});
  bad_rep([](auto& x){x.hash="../../blob";});
  bad_rep([](auto& x){++x.size;});
  bad_rep([](auto& x){x.size=-1;});
  rejected(s,d,rep,original,bytes+" ","ocr_region_representation_integrity");
  bad_s([](auto& x){x.unit_source="wrong";}, "ocr_region_source_mismatch");
  bad_s([](auto& x){x.locator_source="wrong";}, "ocr_region_source_mismatch");
  bad_s([](auto& x){x.observation_source="wrong";}, "ocr_region_source_mismatch");
  bad_s([](auto& x){x.original_source="wrong";}, "ocr_region_source_mismatch");
  bad_s([](auto& x){x.original_hash=std::string(64,'0');}, "ocr_region_source_mismatch");
  bad_d([](auto& x){x.original_hash=std::string(64,'0');}, "ocr_region_source_mismatch");
  bad_s([](auto& x){x.evidence="observed";}, "ocr_region_evidence_mismatch");
  bad_s([](auto& x){x.confidence_is_null=false;}, "ocr_region_evidence_mismatch");
  bad_d([](auto& x){x.evidence="observed";}, "ocr_region_evidence_mismatch");
  bad_d([](auto& x){x.confidence_is_null=false;}, "ocr_region_evidence_mismatch");
  bad_d([](auto& x){x.status="no_text";}, "ocr_region_evidence_mismatch");
  bad_d([](auto& x){x.schema="other";}, "ocr_region_evidence_mismatch");
  for (const auto* p : {"/text","/regions/1","/regions/00","/regions/-1","/regions/0/text"})
    bad_s([&](auto& x){x.pointer=p;}, "ocr_region_pointer");
  bad_s([](auto& x){x.observation_pointer="/regions/1/text";}, "ocr_region_pointer");
  bad_s([](auto& x){x.ordinal=-1;}, "ocr_region_pointer");
  bad_s([](auto& x){x.ordinal=2;}, "ocr_region_pointer");
  bad_s([](auto& x){x.node_text="normalized or forged";}, "ocr_region_text_mismatch");
  bad_s([](auto& x){x.observation_text="forged";}, "ocr_region_text_mismatch");
  bad_d([](auto& x){x.regions[0].text="";}, "ocr_region_text_mismatch");
  bad_d([](auto& x){x.regions[0].text="\xc0\xaf";}, "ocr_region_text_mismatch");
  bad_d([](auto& x){x.regions[1].id=x.regions[0].id;}, "ocr_region_duplicate_id");
  bad_d([](auto& x){x.regions[0].frame="missing";}, "ocr_region_missing_frame");
  bad_d([](auto& x){x.frames.push_back(x.frames[0]);}, "ocr_region_ambiguous_frame");
  bad_d([](auto& x){x.frames[0].units[0]="mm";}, "ocr_region_frame_coordinates");
  bad_d([](auto& x){x.frames[0].axes={"y","x"};}, "ocr_region_frame_coordinates");
  bad_d([](auto& x){x.frames[0].origin[0]=1;}, "ocr_region_frame_coordinates");
  bad_d([](auto& x){x.frames[0].orientation="bottom_left";}, "ocr_region_frame_coordinates");
  bad_d([](auto& x){x.regions[0].origin[0]=-1;}, "ocr_region_bounds");
  bad_d([](auto& x){x.regions[0].size[1]=0;}, "ocr_region_bounds");
  bad_d([](auto& x){x.frames[0].extent[0]=0;}, "ocr_region_bounds");
  bad_d([](auto& x){x.regions[0].origin[0]=99;}, "ocr_region_bounds");
  bad_d([](auto& x){x.regions[0].size[0]=101;}, "ocr_region_bounds");
  bad_d([](auto& x){x.regions[0].origin[0]=std::numeric_limits<std::int64_t>::max();}, "ocr_region_bounds");
  auto edge=d; edge.regions[0].origin={88,45};
  check(bool(validate(s,rep,original,edge,bytes,0)), "inclusive frame edge"); ++cases;
  std::cout << cases << " contract cases passed; no backend/provider invoked\n";
  return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
