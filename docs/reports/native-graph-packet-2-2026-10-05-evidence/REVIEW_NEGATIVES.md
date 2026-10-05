# W4 review negatives — archive only

These are complete intermediate files retained after read-only review, not measured failing test runs. Positive implementation is on gpt/native-graph-packet-2-2026-10-05.

- review-negative.unselected-tests.verify.cc.gz: included registrations used the include filename, while CTest filters */test_kb_pack.cpp. Fix: logical source attribution before registrations; required native receipt must show 27 cases.
- review-negative.normalize.cpp.gz: Unicode opt-in undoubling measured re-encoded replacement-character width against original malformed bytes. Witness: direct stem(\"a\xFF\xFFing\", En), undouble_allow_non_ascii=true. Fix: utf8::byte_offset at the original character boundary; final test retains a plus one FF byte.
- review-negative.kb.h.gz together with normalize source: only the unchecked constructor existed. A validated smaller pack without lexicons/stemming.json lacked an explicit capability error. Fix: additive Normalizer::create returning Unavailable; legacy constructor precondition documented.

Decompress each file to reproduce the reviewed intermediate source. All three files were captured before the fixes. The malformed byte witness applies to the opt-in setting; builtin flags do not take that path. Do not infer a before/after builtin regression from this static review. Baseline build OOM and intentionally interrupted retry logs in this archive parent are infrastructure evidence, not full baseline gates.
