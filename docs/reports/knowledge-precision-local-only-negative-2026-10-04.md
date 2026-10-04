# Rejected W1 local-only precision candidate — 2026-10-04

DO NOT INTEGRATE. Exact source tree is commit `caa2619af2594bd18872354517bf663027b3afe4` on this archive branch (tree `0bd8448dff77053ffd61eb88b9e773a6842463f4`, identical to the local experiment). Baseline is `9d15d2dd0733274356f768816e207e26e13845e4`.

Music min_weight=3/window=1; research min_weight=3.5/window=1; dominant_context=false; weighted evidence uses the best single unit. Full unchanged CTest:104/106. Missing valid music with evidence distributed across units caused unit.test_generalize and unit.test_generalize_eval to fail. Existing thresholds were not relaxed and no tests were removed.

Initial synthetic had unchanged metrics despite these native failures. Frozen public source:1164 files/1413 selected units; initial candidate extracted8014 claims,249 names;498 instances;3222 absence claims;12793 stored claims. Counts do not establish accuracy. Full benchmark timers/counters/stage hashes and W1 stage stats follow; unchanged catalog scoring rows and per-file source metadata can be reconstructed from the baseline commit.

The legacy micro-probe below has a superseded oracle: a cross-unit case was incorrectly labelled negative even though both units had source-supported claims for the same subject. Do not use its score as a final precision result. The final feature report carries corrected case labels plus a truly unrelated-unit negative. Strict research weight5 and whole-unit dominance were also rejected; all modes and observations below remain reproducible.

## Replay

Build this exact source commit using the existing dev preset and vendored SQLite (`cmake --preset dev -DLOOM_USE_SYSTEM_SQLITE=OFF`, build, `ctest --preset dev --output-on-failure`). To reproduce the matched benchmark, stage public input with `knowledge_eval.py bench stage --repo <baseline-worktree> --dest <frozen>`, then `bench run --loom <this-build>/cli/loom --input <frozen> --work <fresh-directory> --label after --out <card.json>`. Input absolute paths affect IDs and hashes, not the listed counts. `knowledge_eval.py selfhost --loom <this-build>/cli/loom --repo <baseline-worktree> --work <fresh-directory> --out <products>` is the independent normal selfhost run. No paid calls.

## Full CTest output

<details><summary>Recorded evidence</summary>

```text
Test project /workspace/scratch/98e6ad903811/chatadhd/loom/build/dev
        Start   1: unit.test_archive
        Start   2: unit.test_batch_api
        Start   3: unit.test_capi
        Start   4: unit.test_capi_knowledge
  1/106 Test   #2: unit.test_batch_api ................................   Passed    0.09 sec
        Start   5: unit.test_capi_knowledge_latest_run
  2/106 Test   #4: unit.test_capi_knowledge ...........................   Passed    0.56 sec
        Start   6: unit.test_catalog
  3/106 Test   #5: unit.test_capi_knowledge_latest_run ................   Passed    0.96 sec
        Start   7: unit.test_catalog_context
  4/106 Test   #3: unit.test_capi .....................................   Passed    2.80 sec
        Start   8: unit.test_catalog_eval
  5/106 Test   #6: unit.test_catalog ..................................   Passed    3.09 sec
        Start   9: unit.test_catalog_links
  6/106 Test   #8: unit.test_catalog_eval .............................   Passed    1.05 sec
        Start  10: unit.test_catalog_retention
  7/106 Test   #7: unit.test_catalog_context ..........................   Passed    2.97 sec
        Start  11: unit.test_catalog_scale
  8/106 Test  #11: unit.test_catalog_scale ............................   Passed    0.01 sec
        Start  12: unit.test_chat_active_task
  9/106 Test   #1: unit.test_archive ..................................   Passed    4.43 sec
        Start  13: unit.test_chat_active_task_acceptance_audit
 10/106 Test  #10: unit.test_catalog_retention ........................   Passed    1.38 sec
        Start  14: unit.test_chat_active_task_acceptance_scale
 11/106 Test  #12: unit.test_chat_active_task .........................   Passed    1.35 sec
        Start  15: unit.test_chat_active_task_acceptance_validation
 12/106 Test  #13: unit.test_chat_active_task_acceptance_audit ........   Passed    1.61 sec
        Start  16: unit.test_chat_active_task_concurrency
 13/106 Test  #15: unit.test_chat_active_task_acceptance_validation ...   Passed    1.07 sec
        Start  17: unit.test_chat_active_task_durability
 14/106 Test  #16: unit.test_chat_active_task_concurrency .............   Passed    0.80 sec
        Start  18: unit.test_chat_active_task_durable_audit
 15/106 Test  #17: unit.test_chat_active_task_durability ..............   Passed    0.60 sec
        Start  19: unit.test_chat_active_task_exceptional
 16/106 Test  #19: unit.test_chat_active_task_exceptional .............   Passed    0.29 sec
        Start  20: unit.test_chat_active_task_history_aba
 17/106 Test  #20: unit.test_chat_active_task_history_aba .............   Passed    0.10 sec
        Start  21: unit.test_chat_active_task_reference
 18/106 Test  #21: unit.test_chat_active_task_reference ...............   Passed    0.09 sec
        Start  22: unit.test_chat_active_task_retention
 19/106 Test  #22: unit.test_chat_active_task_retention ...............   Passed    1.90 sec
        Start  23: unit.test_chat_active_task_revisions
 20/106 Test  #14: unit.test_chat_active_task_acceptance_scale ........   Passed    6.68 sec
        Start  24: unit.test_chat_active_task_scope_identity
 21/106 Test   #9: unit.test_catalog_links ............................   Passed    8.40 sec
        Start  25: unit.test_chat_engine
 22/106 Test  #25: unit.test_chat_engine ..............................   Passed    0.09 sec
        Start  26: unit.test_chat_knowledge_context
 23/106 Test  #24: unit.test_chat_active_task_scope_identity ..........   Passed    0.34 sec
        Start  27: unit.test_chat_retrieval_plan
 24/106 Test  #23: unit.test_chat_active_task_revisions ...............   Passed    4.05 sec
        Start  28: unit.test_config
 25/106 Test  #28: unit.test_config ...................................   Passed    0.01 sec
        Start  29: unit.test_context_channel_integration
 26/106 Test  #29: unit.test_context_channel_integration ..............   Passed    1.66 sec
        Start  30: unit.test_context_controls
 27/106 Test  #26: unit.test_chat_knowledge_context ...................   Passed    4.31 sec
        Start  31: unit.test_context_counter
 28/106 Test  #27: unit.test_chat_retrieval_plan ......................   Passed    7.13 sec
        Start  32: unit.test_context_diagnostics
 29/106 Test  #31: unit.test_context_counter ..........................   Passed    4.63 sec
        Start  33: unit.test_context_engine
 30/106 Test  #33: unit.test_context_engine ...........................   Passed    0.01 sec
        Start  34: unit.test_context_eval
 31/106 Test  #34: unit.test_context_eval .............................   Passed    0.91 sec
        Start  35: unit.test_context_native_consumer
 32/106 Test  #30: unit.test_context_controls .........................   Passed    7.35 sec
        Start  36: unit.test_context_plan
 33/106 Test  #36: unit.test_context_plan .............................   Passed    4.21 sec
        Start  37: unit.test_context_retrieval
 34/106 Test  #18: unit.test_chat_active_task_durable_audit ...........   Passed   20.07 sec
        Start  38: unit.test_context_retrieval_dev
 35/106 Test  #38: unit.test_context_retrieval_dev ....................   Passed    1.23 sec
        Start  39: unit.test_context_tfidf_reuse
 36/106 Test  #35: unit.test_context_native_consumer ..................   Passed    6.68 sec
        Start  40: unit.test_crypto
 37/106 Test  #37: unit.test_context_retrieval ........................   Passed    2.66 sec
        Start  41: unit.test_db
 38/106 Test  #41: unit.test_db .......................................   Passed    0.56 sec
        Start  42: unit.test_event_bus
 39/106 Test  #42: unit.test_event_bus ................................   Passed    0.09 sec
        Start  43: unit.test_extract
 40/106 Test  #40: unit.test_crypto ...................................   Passed    4.66 sec
        Start  44: unit.test_extract_eval
 41/106 Test  #44: unit.test_extract_eval .............................   Passed    2.68 sec
        Start  45: unit.test_fts
 42/106 Test  #45: unit.test_fts ......................................   Passed    0.07 sec
        Start  46: unit.test_generalize
 43/106 Test  #43: unit.test_extract ..................................   Passed    8.23 sec
        Start  47: unit.test_generalize_eval
 44/106 Test  #46: unit.test_generalize ...............................***Failed    2.78 sec
===============================================================================
/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize.cpp:387:
TEST SUITE: generalize
TEST CASE:  stage: knowledge.generalize through the engine writes the store deterministically (synthetic_dev)

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize.cpp:434: ERROR: CHECK( inst.size() == 1 ) is NOT correct!
  values: CHECK( 0 == 1 )
  logged: 

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize.cpp:434: ERROR: CHECK( inst.size() == 1 ) is NOT correct!
  values: CHECK( 0 == 1 )
  logged: 

===============================================================================
[doctest] test cases:   7 |   6 passed | 1 failed | 635 skipped
[doctest] assertions: 191 | 189 passed | 2 failed |
[doctest] Status: FAILURE!

        Start  48: unit.test_github
 45/106 Test  #48: unit.test_github ...................................   Passed    0.03 sec
        Start  49: unit.test_graph
 46/106 Test  #49: unit.test_graph ....................................   Passed    0.08 sec
        Start  50: unit.test_import
 47/106 Test  #50: unit.test_import ...................................   Passed    0.37 sec
        Start  51: unit.test_import_export_fidelity
 48/106 Test  #39: unit.test_context_tfidf_reuse ......................   Passed   11.39 sec
        Start  52: unit.test_import_exports
 49/106 Test  #51: unit.test_import_export_fidelity ...................   Passed    0.19 sec
        Start  53: unit.test_import_source_materialization
 50/106 Test  #47: unit.test_generalize_eval ..........................***Failed    1.33 sec
===============================================================================
/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:68:
TEST SUITE: generalize_eval
TEST CASE:  synthetic_dev: principles, operators, predictions, false certainty, EP soundness

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:106: MESSAGE: principle pr.optionality_over_speed -> p_4b48be69aa2f49d2 level value ok form invariant ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:106: MESSAGE: principle pr.honesty_over_comfort -> p_e3a2d7988454a4aa level value ok form invariant ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:106: MESSAGE: principle pr.keep_both_branches -> p_0c2b3b04100e4bd2 level epistemic ok form heuristic WRONG

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:106: MESSAGE: principle pr.data_over_branches -> p.presets_are_data level strategy ok form invariant ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:106: MESSAGE: principle pr.write_down_the_blocker -> p_db3c63210eee0c93 level epistemic ok form heuristic ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:106: MESSAGE: principle pr.counterexample_hunting -> p_8654c1ed489e8d32 level epistemic ok form meta WRONG

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:98: MESSAGE: principle not recalled: pr.provider_not_special_case

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:98: MESSAGE: principle not recalled: pr.momentum_over_perfection

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:106: MESSAGE: principle pr.decide_fast_correct_later -> p_57a4844d39c09722 level strategy ok form default ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:106: MESSAGE: principle pr.small_reversible_steps -> p_b7552f556fbfe41a level strategy ok form default ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:106: MESSAGE: principle pr.owner_confirmation_for_money -> p_962c0010c407c989 level strategy ok form conflict_resolution ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:106: MESSAGE: principle pr.two_masters_conflict -> p_231595376cdf4c41 level strategy ok form conflict_resolution ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:106: MESSAGE: principle pr.meta_two_examples_rule -> p_599da6dc0ad9146d level epistemic WRONG form meta ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:114: MESSAGE: principles: 30 (29 discovered), recall 0.846154, level acc 0.909091, form acc 0.818182

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:144: MESSAGE: operator op.new_source_new_adapter: pre-T candidate yes, pre/post merged yes

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:144: MESSAGE: operator op.new_artifact_shared_structure: pre-T candidate yes, pre/post merged no

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:144: MESSAGE: operator op.observe_before_automate: pre-T candidate yes, pre/post merged no

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:144: MESSAGE: operator op.uncertainty_keep_alternatives: pre-T candidate yes, pre/post merged yes

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:144: MESSAGE: operator op.impl_detail_not_propagated: pre-T candidate yes, pre/post merged no

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:144: MESSAGE: operator op.cost_gate_on_irreversibility: pre-T candidate yes, pre/post merged yes

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:162: MESSAGE: operators: 14 mined <= T, pre-T recovery 6/6; full-corpus recurrence 3/6, merged operators 3 (impure 0)

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:180: MESSAGE: prediction pred.op.new_source_new_adapter: holds

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:180: MESSAGE: prediction pred.op.new_artifact_shared_structure: MISSED

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:180: MESSAGE: prediction pred.op.observe_before_automate: MISSED

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:180: MESSAGE: prediction pred.op.uncertainty_keep_alternatives: holds

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:180: MESSAGE: prediction pred.op.impl_detail_not_propagated: MISSED

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:180: MESSAGE: prediction pred.op.cost_gate_on_irreversibility: holds

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:195: MESSAGE: predictions: 15 made at T, 3 evaluated (3 hold); holdout pair accuracy 0.5; negative control attributed 0x, specific-choice predictions 0, confident-but-violated 0

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:215: MESSAGE: kind proj.analogghosts (music):  WRONG

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:215: MESSAGE: kind proj.lokatorka (legal_case): legal_case ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:215: MESSAGE: kind proj.noteflow (multiplatform_app): software_app+multiplatform ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:215: MESSAGE: kind proj.reeltime (pipeline): film+staged_transformation WRONG

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:215: MESSAGE: kind proj.watchdog (agent_system): software_app ok

/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:257: MESSAGE: matching: kind accuracy 3/5; produced 136 claims (11 inferred, 4 transferred, 3 extrapolated, 2 analogies); false-certainty rate 0; inferred with EP 14/14; EP re-check after T: holds 6, violated 0, pending 6 (soundness 1)

GENERALIZE_EVAL principle_recall=0.846 level_acc=0.909 form_acc=0.818 op_pre=6/6 op_recur=3/6 impure=0 pred_acc=0.500 neg_attr=0 specific=0 fc=0.000 ep_sound=1.000 kinds=3/5
/workspace/scratch/98e6ad903811/chatadhd/loom/tests/test_generalize_eval.cpp:278: ERROR: CHECK( kind_ok >= 4 ) is NOT correct!
  values: CHECK( 3 >= 4 )

===============================================================================
[doctest] test cases:   1 |   0 passed | 1 failed | 641 skipped
[doctest] assertions: 152 | 151 passed | 1 failed |
[doctest] Status: FAILURE!

        Start  54: unit.test_import_stream
 51/106 Test  #53: unit.test_import_source_materialization ............   Passed    0.31 sec
        Start  55: unit.test_kb_pack
 52/106 Test  #52: unit.test_import_exports ...........................   Passed    4.16 sec
        Start  56: unit.test_knowledge
 53/106 Test  #55: unit.test_kb_pack ..................................   Passed    7.03 sec
        Start  57: unit.test_knowledge_candidate_graph
 54/106 Test  #32: unit.test_context_diagnostics ......................   Passed   28.94 sec
        Start  58: unit.test_knowledge_candidates
 55/106 Test  #58: unit.test_knowledge_candidates .....................   Passed    0.08 sec
        Start  59: unit.test_knowledge_flow
 56/106 Test  #59: unit.test_knowledge_flow ...........................   Passed    1.89 sec
        Start  60: unit.test_knowledge_semantic
 57/106 Test  #57: unit.test_knowledge_candidate_graph ................   Passed    3.75 sec
        Start  61: unit.test_knowledge_semantic_graph
 58/106 Test  #56: unit.test_knowledge ................................   Passed    9.07 sec
        Start  62: unit.test_knowledge_store
 59/106 Test  #62: unit.test_knowledge_store ..........................   Passed    0.22 sec
        Start  63: unit.test_materialize
 60/106 Test  #63: unit.test_materialize ..............................   Passed    1.35 sec
        Start  64: unit.test_media
 61/106 Test  #64: unit.test_media ....................................   Passed    0.12 sec
        Start  65: unit.test_memory
 62/106 Test  #65: unit.test_memory ...................................   Passed    0.08 sec
        Start  66: unit.test_model
 63/106 Test  #66: unit.test_model ....................................   Passed    0.04 sec
        Start  67: unit.test_net_sse
 64/106 Test  #67: unit.test_net_sse ..................................   Passed    0.01 sec
        Start  68: unit.test_pack_model
 65/106 Test  #60: unit.test_knowledge_semantic .......................   Passed    4.49 sec
        Start  69: unit.test_provenance
 66/106 Test  #69: unit.test_provenance ...............................   Passed    0.05 sec
        Start  70: unit.test_providers
 67/106 Test  #70: unit.test_providers ................................   Passed    0.02 sec
        Start  71: unit.test_regex
 68/106 Test  #71: unit.test_regex ....................................   Passed    0.02 sec
        Start  72: unit.test_relations
 69/106 Test  #72: unit.test_relations ................................   Passed    0.02 sec
        Start  73: unit.test_resolve
 70/106 Test  #73: unit.test_resolve ..................................   Passed    0.80 sec
        Start  74: unit.test_resolve_attribution
 71/106 Test  #68: unit.test_pack_model ...............................   Passed    2.51 sec
        Start  75: unit.test_resolve_lineage
 72/106 Test  #61: unit.test_knowledge_semantic_graph .................   Passed    9.78 sec
        Start  76: unit.test_runtime
 73/106 Test  #76: unit.test_runtime ..................................   Passed    0.05 sec
        Start  77: unit.test_selector
 74/106 Test  #77: unit.test_selector .................................   Passed    0.01 sec
        Start  78: unit.test_semantic_llm
 75/106 Test  #78: unit.test_semantic_llm .............................   Passed    0.05 sec
        Start  79: unit.test_semantic_worker
 76/106 Test  #74: unit.test_resolve_attribution ......................   Passed    4.99 sec
        Start  80: unit.test_tasks
 77/106 Test  #80: unit.test_tasks ....................................   Passed    0.10 sec
        Start  81: unit.test_util
 78/106 Test  #79: unit.test_semantic_worker ..........................   Passed    0.26 sec
        Start  82: compat.test_abi_compat
 79/106 Test  #81: unit.test_util .....................................   Passed    0.04 sec
        Start  83: compat.test_active_task_compiler_compat
 80/106 Test  #82: compat.test_abi_compat .............................   Passed    0.34 sec
        Start  84: compat.test_candidate_graph_native
 81/106 Test  #75: unit.test_resolve_lineage ..........................   Passed    4.77 sec
        Start  85: compat.test_chat_active_task_http
 82/106 Test  #83: compat.test_active_task_compiler_compat ............   Passed    1.57 sec
        Start  86: compat.test_chat_compat
 83/106 Test  #84: compat.test_candidate_graph_native .................   Passed    1.77 sec
        Start  87: compat.test_config_compat
 84/106 Test  #87: compat.test_config_compat ..........................   Passed    0.26 sec
        Start  88: compat.test_crypto_compat
 85/106 Test  #85: compat.test_chat_active_task_http ..................   Passed    3.46 sec
        Start  89: compat.test_db_compat
 86/106 Test  #89: compat.test_db_compat ..............................   Passed    1.44 sec
        Start  90: compat.test_github_sync_defaults
 87/106 Test  #90: compat.test_github_sync_defaults ...................   Passed    0.17 sec
        Start  91: compat.test_graph_compat
 88/106 Test  #86: compat.test_chat_compat ............................   Passed    5.65 sec
        Start  92: compat.test_graph_packet_store
 89/106 Test  #92: compat.test_graph_packet_store .....................   Passed    0.81 sec
        Start  93: compat.test_import_compat
 90/106 Test  #93: compat.test_import_compat ..........................   Passed    0.80 sec
        Start  94: compat.test_memory_compat
 91/106 Test  #91: compat.test_graph_compat ...........................   Passed    4.24 sec
        Start  95: compat.test_selector_compat
 92/106 Test  #95: compat.test_selector_compat ........................   Passed    0.12 sec
        Start  96: compat.test_semantic_compat
 93/106 Test  #94: compat.test_memory_compat ..........................   Passed    4.13 sec
        Start  97: compat.test_semantic_recovery
 94/106 Test  #88: compat.test_crypto_compat ..........................   Passed   14.10 sec
        Start  98: compat.test_util_compat
 95/106 Test  #97: compat.test_semantic_recovery ......................   Passed    4.24 sec
        Start  99: research.structure
 96/106 Test  #96: compat.test_semantic_compat ........................   Passed    8.24 sec
        Start 100: research.independent_protocol
 97/106 Test #100: research.independent_protocol ......................   Passed    0.23 sec
        Start 101: research.graph_native_protocol
 98/106 Test #101: research.graph_native_protocol .....................   Passed    0.85 sec
        Start 102: research.candidate_graph_protocol
 99/106 Test #102: research.candidate_graph_protocol ..................   Passed    0.59 sec
        Start 103: research.contracts
100/106 Test  #98: compat.test_util_compat ............................   Passed    4.25 sec
        Start 104: research.seeding
101/106 Test #104: research.seeding ...................................   Passed    9.92 sec
        Start 105: cli.smoke
102/106 Test #105: cli.smoke ..........................................   Passed    3.24 sec
        Start 106: eval.harness
103/106 Test #106: eval.harness .......................................   Passed    1.04 sec
104/106 Test  #54: unit.test_import_stream ............................   Passed   61.35 sec
105/106 Test #103: research.contracts .................................   Passed   26.94 sec
106/106 Test  #99: research.structure .................................   Passed   41.72 sec

98% tests passed, 2 tests failed out of 106

Total Test time (real) = 119.78 sec

The following tests FAILED:
	 46 - unit.test_generalize (Failed)
	 47 - unit.test_generalize_eval (Failed)
Errors while running CTest
```

</details>

## Synthetic card

<details><summary>Recorded evidence</summary>

```json
{
 "corpus": "synthetic_dev",
 "temporal_cut": "2026-05-01",
 "catalog": {
  "recall": 0.6889,
  "precision": 1.0,
  "precision_population": "labeled_conversations",
  "trap_fpr": 0.0,
  "generic_selected": 0,
  "selected": 34,
  "labeled_selected": 31,
  "unlabeled_selected": 3
 },
 "resolve": {
  "project_recall": 1.0,
  "alias_recall_on_canonical": 0.24,
  "fragmented_extra_entities": 1,
  "spurious_projects": 0,
  "per_project": {
   "proj.noteflow": {
    "entities": 2,
    "aliases_on_best": 2,
    "aliases": 5
   },
   "proj.reeltime": {
    "entities": 1,
    "aliases_on_best": 1,
    "aliases": 5
   },
   "proj.watchdog": {
    "entities": 1,
    "aliases_on_best": 1,
    "aliases": 5
   },
   "proj.lokatorka": {
    "entities": 1,
    "aliases_on_best": 1,
    "aliases": 5
   },
   "proj.analogghosts": {
    "entities": 1,
    "aliases_on_best": 1,
    "aliases": 5
   }
  }
 },
 "extract": {
  "version_recall_chat": 0.6818,
  "versions_total_chat": 22,
  "decision_recall": 0.3333,
  "chosen_accuracy": 0.1429,
  "supersession_recall": 0.0,
  "decisions_total": 21,
  "decisions_found": 12,
  "fork_recall": 1.0,
  "forks_found": 4,
  "status_event_recall": 0.5484,
  "status_events_total": 31,
  "oscillating_features_gt": 2,
  "oscillating_entities_found": 0,
  "status_records": 35,
  "open_question_recall": 0.2,
  "area_recall": 1.0,
  "areas_found": 6,
  "inferred_members": 0
 },
 "assess": {
  "contradiction_recall": 0.0,
  "contested_claims": 3
 },
 "generalize": {
  "principle_recall": 0.7692,
  "level_accuracy": 0.9,
  "form_accuracy": 0.8,
  "principles_with_evidence": 56,
  "principle_precision": 0.5,
  "principles_total": 56,
  "operator_recall": 0.0,
  "operators_found": 10
 },
 "retrospective_consistency": {
  "solution_class_match_rate": 0.0,
  "protocol": "retrospective_full_corpus_with_cut_priors",
  "benchmark_valid_for_prediction": false,
  "predictive_accuracy": null,
  "limitation": "Post-cutoff examples are visible during operator construction; matches measure retrospective consistency.",
  "predictions_made": 16,
  "outcomes": {
   "pending": 16
  },
  "negative_control_false_positives": 0,
  "negative_controls": 1,
  "detail": [
   {
    "id": "pred.op.new_source_new_adapter",
    "predicted_from_before_T_decision": true,
    "held_on_actual_decision": false
   },
   {
    "id": "pred.op.new_artifact_shared_structure",
    "predicted_from_before_T_decision": true,
    "held_on_actual_decision": false
   },
   {
    "id": "pred.op.observe_before_automate",
    "predicted_from_before_T_decision": true,
    "held_on_actual_decision": false
   },
   {
    "id": "pred.op.uncertainty_keep_alternatives",
    "predicted_from_before_T_decision": false,
    "held_on_actual_decision": false
   },
   {
    "id": "pred.op.impl_detail_not_propagated",
    "predicted_from_before_T_decision": true,
    "held_on_actual_decision": false
   },
   {
    "id": "pred.op.cost_gate_on_irreversibility",
    "predicted_from_before_T_decision": false,
    "held_on_actual_decision": false
   }
  ]
 },
 "work_dir": "/tmp/chatadhd-precision-synthetic-after/synthetic-60uq1gtw",
 "epistemic": {
  "false_certainty_rate": 0.0,
  "claims_with_structural_violations": 0,
  "claims_evaluated": 255,
  "observed_without_support": 0,
  "observed_quote_not_in_observation": 0,
  "inferred_without_expected_property": 0,
  "extrapolated_or_absent_premises": 0,
  "claims_by_evidence_class": {
   "absent": 34,
   "derived": 2,
   "observed": 219
  },
  "expected_property_states": {}
 },
 "calibration_ece": null,
 "determinism": {
  "stage_hashes_identical": true,
  "differing_stages": [],
  "products_byte_identical": true,
  "run_ids_identical": true,
  "products": 65
 },
 "stages": {
  "catalog": {
   "scan": {
    "units": 68,
    "new": 68,
    "unchanged": 0,
    "refreshed": 0,
    "input_hash": "a73c04262c493cd38e918d4545fe1fb784226b102a527fd421beb15daa2f5156",
    "versions": 0,
    "bytes": 93279,
    "warnings": []
   },
   "score": {
    "run_id": "run_f41e1ac553e5964e",
    "scoring_evidence_version": 3,
    "legacy_combined_features": [
     "id_hits",
     "class_diversity",
     "code_evidence"
    ],
    "identity_unavailable": 0,
    "relevant": 34,
    "candidate": 7,
    "irrelevant": 27,
    "traps": 4,
    "expanded_terms": [
     {
      "term": "zero",
      "pass": 1,
      "lift": 3.4,
      "df_relevant": 3,
      "df_corpus": 6,
      "evidence": [
       "un_3d70b7a34635f5fe",
       "un_9e8228809931d5d0",
       "un_e7c579ba8fc08dd1"
      ],
      "reason": "lift >= 3.000000 against the verified-relevant set (vocabulary expansion)"
     }
    ],
    "semantic": {
     "available": true,
     "passes": 1,
     "seeds": 40,
     "method": "tfidf_cosine(word_stems)+tfidf_cosine(char_4grams)",
     "floor_word": 0.0283,
     "ref_word": 0.1362,
     "floor_gram": 0.0865,
     "ref_gram": 0.2412,
     "word_vocabulary": 1267,
     "gram_vocabulary": 4074
    },
    "channels": {
     "lexical_relevant": 10,
     "semantic_relevant": 5,
     "both": 3,
     "lexical_only": 7,
     "semantic_only": 2
    },
    "links": 56,
    "link_stats": {
     "mode": "candidates",
     "units": 68,
     "links": 56,
     "project_groups_aggregated": 0,
     "minhash_pairs_verified": 1,
     "lsh_candidates": 1,
     "rare_term_candidates": 653,
     "uncorroborated_sessions": 0
    }
   },
   "selected": 34,
   "import": {
    "imported": 34,
    "skipped": 0,
    "bytes": 53271,
    "conversations": [
     "c_677a542d4c2f",
     "c_bd01fb240c86",
     "c_351ddb4c5d1e",
     "c_2c2680340fb4",
     "c_91d0e720ff2f",
     "c_62ea0b0d4972",
     "c_933d46c37ac3",
     "c_63e48e391f2f",
     "c_9da42a96abd0",
     "c_a11fc5fd7927",
     "c_99b086a857f3",
     "c_0eccdfdf5c36",
     "c_b69e23a35ae1",
     "c_3f0453a9715e",
     "c_d61f66047b4d",
     "c_bffae9d8fd9d",
     "c_c4477b2641b0",
     "c_0ce16555f2aa",
     "c_475a048ec9c3",
     "c_db8c234f5232",
     "c_53d9c29ed025",
     "c_6837909ed908",
     "c_700dabdeed4d",
     "c_b31b399b281b",
     "c_0b22adb24a13",
     "c_9fb7249df661",
     "c_bb2b1e29c037",
     "c_bc76584a1ce6",
     "c_bc71766b37f4",
     "c_998dacfd0422",
     "c_e0990129fa6b",
     "c_e9f4feff31ab",
     "c_fa655a25db48",
     "c_2a6d680601c1"
    ],
    "units": [
     "un_0013895a80a1b2a7",
     "un_0de37bf11510f0b7",
     "un_1ac65f67bfc02c4d",
     "un_267c4f1062957111",
     "un_2904f423528904d7",
     "un_30db4f34c157b0d5",
     "un_3d70b7a34635f5fe",
     "un_3e372ae8f888ff0f",
     "un_4cca8cd17d164c76",
     "un_540da04fb9885a97",
     "un_6d2a023fdeb7a03a",
     "un_7283b4adb0636089",
     "un_73900872279b2762",
     "un_7e259a91fcfa495b",
     "un_80835787f9aee81b",
     "un_87a5708795d0a8e7",
     "un_87f0ef8ff9fed335",
     "un_8b4622b578921644",
     "un_90417490f68e0d18",
     "un_950ab0bd5a638ffd",
     "un_970632604cfc50de",
     "un_9790aefa0f908671",
     "un_9b97ad889525d568",
     "un_9e8228809931d5d0",
     "un_b74c13729d8d4db1",
     "un_bc0de66056ba0a4e",
     "un_bce8e5c0f50d82f2",
     "un_c10e2e05a92b0ab7",
     "un_c15910292b0349f6",
     "un_e2e9097262f75584",
     "un_e3f2bff4d4674204",
     "un_e7c579ba8fc08dd1",
     "un_f401707062be8e81",
     "un_f4160c51b1fc27f5"
    ],
    "retained_sources": []
   }
  },
  "extract": {
   "from": "catalog",
   "units": 34,
   "failed": 0,
   "names": 9,
   "observations": 208,
   "entities": 112,
   "claims": 217,
   "areas": 6,
   "principles": 26,
   "decisions": 12,
   "forks": 4,
   "statuses": 33,
   "semantic": {
    "status": "off",
    "candidate_ids": [],
    "rejections": [],
    "skipped": [],
    "requests": 0,
    "cache_hits": 0,
    "accepted": 0,
    "rejected": 0,
    "failed": 0,
    "input_bytes": 0,
    "chunks": 0,
    "omitted_observations": 0,
    "failed_chunk_observations": 0,
    "selected_chunks": [],
    "identity": {
     "version": "2",
     "requested": false,
     "limits": {
      "max_requests": 4,
      "max_observations": 16,
      "max_chunk_bytes": 16000,
      "max_input_bytes": 64000,
      "max_output_tokens": 1600,
      "max_proposals": 16,
      "max_response_bytes": 128000,
      "timeout_ms": 30000
     },
     "representation": "relation_v1"
    },
    "requests_spent": 0,
    "input_bytes_spent": 0,
    "retry_required": false,
    "representation": "relation_v1",
    "accepted_bundles": 0,
    "entity_drafts": 0,
    "claim_drafts": 0,
    "abstentions": 0,
    "response_outcomes": [],
    "output": "497ffcd6abfb43c9ff830263cd31933c77590d2c6e0deed4d908dd9b7b3dda62"
   }
  },
  "resolve": {
   "entities": 110,
   "mentions": 112,
   "merges": 2,
   "blocked": 50,
   "claims": 221,
   "lineage": []
  },
  "assess": {
   "claims": 221,
   "contested": 3,
   "conflicts": 1,
   "reversals": 0,
   "replayed": 0
  },
  "generalize": {
   "instances": 31,
   "absent": 34,
   "analogies": 0,
   "inferred": 0,
   "extrapolated": 0,
   "transferred": 0,
   "principles": 35,
   "operators": 10,
   "models": 3,
   "predictions": 19,
   "predictions_holds": 0,
   "predictions_violated": 0
  },
  "materialize": {
   "dossiers": 31,
   "extrapolated_specs": 31,
   "products": 64
  }
 }
}
```

</details>

## Legacy probe source

<details><summary>Recorded evidence</summary>

```cpp
#include <iostream>
#include "loom/generalize.h"
#include "loom/kb.h"
using namespace loom;
using namespace loom::generalize;
struct Builder {
  Evidence e; int ordinal=0;
  std::string subject(const std::string& key) {
    model::Entity entity; entity.kind="project"; entity.canonical_key=key; entity.label=key;
    entity.id=model::Entity::make_id(entity.kind,key); e.entities.push_back(entity); return entity.id;
  }
  std::string obs(const std::string& text,const std::string& unit="u") {
    model::Observation o; o.unit=unit; o.text=text; o.ordinal=ordinal++; o.kind=model::ObservationKind::Sentence;
    o.locator.source="sha256:probe"; o.locator.json_pointer="/"+std::to_string(o.ordinal);
    o.id=model::Observation::make_id(unit,o.locator,text); e.observations.push_back(o); return o.id;
  }
  void support(const std::string& subject,const std::string& observation, model::EvidenceClass evidence=model::EvidenceClass::Observed,
               model::ClaimStatus status=model::ClaimStatus::Active) {
    model::Claim c; c.subject=subject; c.predicate="mentioned_in"; c.value=e.observations.back().unit;
    c.qualifiers.scope=observation; c.assessment.evidence=evidence; c.assessment.status=status; c.assessment.confidence=.8;
    c.assessment.support.push_back(model::Support{observation,{},"","test@1",1.0});
    c.id=model::Claim::make_id(c.subject,c.predicate,c.object,c.value,c.qualifiers); e.claims.push_back(c);
  }
};
std::shared_ptr<const kb::Pack> pack(const std::string& mode){
  auto base=kb::Pack::load_builtin(); if(!base){std::cerr<<base.error().message<<"\n"; exit(2);}
  std::map<std::string,Json> docs; for(const auto& path:(*base)->files()) docs[path]=(*base)->file(path);
  auto& anchors=docs["lexicons/cues.json"]["anchor_policy"];
  anchors=Json::object();
  if(mode!="legacy") {
    bool dominant=mode.find("dominant")==0;
    int window=mode.find("window2")!=std::string::npos?2:1;
    anchors["paradigm.music"]=Json{{"min_weight",3},{"window",window},{"dominant_context",dominant}};
    anchors["paradigm.research"]=Json{{"min_weight",mode.find("35")!=std::string::npos?3.5:5.0},{"window",window},{"dominant_context",dominant}};
  }
  auto p=kb::Pack::from_documents(std::move(docs)); if(!p){std::cerr<<p.error().message<<"\n"; exit(2);} return *p;
}
bool matches(const std::shared_ptr<const kb::Pack>& p,const Builder& b,const std::string& id,Json* trace,const std::string& paradigm="music"){
  auto ms=ParadigmMatcher(p).match_projects(b.e); if(!ms){std::cerr<<ms.error().message<<"\n";exit(2);}
  for(auto& m:*ms) if(m.instance.paradigm==paradigm && m.instance.subject==id){if(trace)*trace=m.reasons;return true;} return false;
}
int main(){
  Json out=Json::array();
  for(const std::string mode:{"legacy","local","dominant","local_research35","dominant_research35","local_window2_research35"}) {
    auto p=pack(mode);
    auto run=[&](const std::string& name,Builder b,const std::string& subject,bool expected,const std::string& paradigm="music"){
      Json trace; bool result=matches(p,b,subject,&trace,paradigm);
      out.push_back(Json{{"mode",mode},{"paradigm",paradigm},{"case",name},{"expected",expected},{"actual",result},{"correct",result==expected},{"trace",trace}});
    };
    {Builder b; auto s=b.subject("Software"); for(int i=0;i<30;++i){auto o=b.obs("Software: keep track of the master branch");b.support(s,o);} run("repeated_ambiguous_tokens",b,s,false);}
    {Builder b; auto s=b.subject("Song"); auto o=b.obs("Song: chorus, verse, bpm");b.support(s,o); run("single_observation_distinct_music",b,s,true);}
    {Builder b; auto s=b.subject("Song");auto o=b.obs("Song arrangement");b.support(s,o);b.obs("chorus");b.obs("verse");run("subject_owns_unit_neighbour_cues",b,s,true);}
    {Builder b; auto s=b.subject("Software"); auto m=b.subject("Song");auto o=b.obs("Software module");b.support(s,o);for(int i=0;i<4;++i)b.obs("neutral filler");o=b.obs("Song chorus");b.support(m,o);b.obs("verse bpm");run("mixed_subject_wrong_domain",b,s,false);run("mixed_subject_true_domain",b,m,true);}
    {Builder b;auto s=b.subject("Software");for(int i=0;i<20;++i){auto o=b.obs("chorus");b.support(s,o);}run("repeated_one_strong_cue",b,s,false);}
    {Builder b;auto s=b.subject("Software");auto o=b.obs("chorus","u1");b.support(s,o);o=b.obs("verse","u2");b.support(s,o);run("cross_unit_cue_aggregation",b,s,false);}
    {Builder b;auto s=b.subject("Software");auto o=b.obs("chorus verse bpm");b.support(s,o,model::EvidenceClass::Observed,model::ClaimStatus::Rejected);b.obs("chorus verse bpm");run("rejected_support_not_an_anchor",b,s,false);}
    {Builder b;auto s=b.subject("Software");auto o=b.obs("chorus verse bpm");b.support(s,o,model::EvidenceClass::Inferred);b.obs("chorus verse bpm");run("inferred_support_not_an_anchor",b,s,false);}
    {Builder b;auto s=b.subject("OwnerMusic");b.e.entities.front().attrs=Json{{"kind_hint","music"}};auto o=b.obs("master branch");b.support(s,o);run("explicit_kind_hint_preserved",b,s,true);}
    {Builder b;auto s=b.subject("Software");auto o=b.obs("Software documentation");b.support(s,o);for(int i=0;i<4;++i)b.obs("neutral filler");b.obs("meta-model example table: music | chorus | verse");b.obs("meta-model example table: bpm");run("dominant_mixed_domain_table",b,s,false);}
    {Builder b;auto s=b.subject("Petri");auto o=b.obs("Petri hypothesis is testable");b.support(s,o);o=b.obs("Petri experiment will test the hypothesis");b.support(s,o);run("unhinted_research_two_clear_cues",b,s,true,"research");}
    {Builder b;auto s=b.subject("Petri");auto o=b.obs("Petri hypothesis; an experiment tests it");b.support(s,o);run("unhinted_research_one_observation",b,s,true,"research");}
    {Builder b;auto s=b.subject("Software");auto o=b.obs("Software function takes a dataset parameter");b.support(s,o);o=b.obs("Software has an experiment flag option");b.support(s,o);run("ambiguous_research_implementation_tokens",b,s,false,"research");}
    {Builder b;auto s=b.subject("Petri");auto o=b.obs("Petri hypothesis and experiment, using a dataset to falsify our predictions");b.support(s,o);run("unhinted_research_full_vocabulary",b,s,true,"research");}

  }
  std::cout<<out.dump(1)<<"\n";
}
```

</details>

## Legacy baseline results

<details><summary>Recorded evidence</summary>

```json
[
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.25
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.25
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.25
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.25
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.25
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 }
]
```

</details>

## Rejected weighted and dominance modes

<details><summary>Recorded evidence</summary>

```json
[
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.25
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_dada0222b861946b"
     ]
    }
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_7af666a4ffbc4a3c",
      "ob_fe6e25525031cec5"
     ]
    }
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.16666666666666666,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 1.0,
     "distinct_phrases": 1,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_4628fdbb4ff69837"
     ]
    }
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 5.0,
     "distinct_weight": 6.5,
     "distinct_phrases": 4,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_261b32a87e59c977"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_dada0222b861946b"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_98b4db35ba0c1945",
      "ob_cdf4da3570e6e9ff"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": false,
     "observations": [
      "ob_7af666a4ffbc4a3c",
      "ob_fe6e25525031cec5"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.16666666666666666,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 1.0,
     "distinct_phrases": 1,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_4628fdbb4ff69837"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_32fc66cd087d18e2",
      "ob_ff684df46b045f28"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 5.0,
     "distinct_weight": 6.5,
     "distinct_phrases": 4,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_261b32a87e59c977"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_dada0222b861946b"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_7af666a4ffbc4a3c",
      "ob_fe6e25525031cec5"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.16666666666666666,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 1.0,
     "distinct_phrases": 1,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_4628fdbb4ff69837"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_70baee6d931d9f33",
      "ob_90c0f81059211f4f"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_a6ff7015d448b5c4"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 6.5,
     "distinct_phrases": 4,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_261b32a87e59c977"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_dada0222b861946b"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_98b4db35ba0c1945",
      "ob_cdf4da3570e6e9ff"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": false,
     "observations": [
      "ob_7af666a4ffbc4a3c",
      "ob_fe6e25525031cec5"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.16666666666666666,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 1.0,
     "distinct_phrases": 1,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_4628fdbb4ff69837"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_32fc66cd087d18e2",
      "ob_ff684df46b045f28"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_70baee6d931d9f33",
      "ob_90c0f81059211f4f"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_a6ff7015d448b5c4"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 6.5,
     "distinct_phrases": 4,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_261b32a87e59c977"
     ]
    }
   }
  ]
 }
]
```

</details>

## Intermediate window2 probe with superseded oracle

<details><summary>Recorded evidence</summary>

```json
[
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.25
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0
   }
  ]
 },
 {
  "mode": "legacy",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_dada0222b861946b"
     ]
    }
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_7af666a4ffbc4a3c",
      "ob_fe6e25525031cec5"
     ]
    }
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.16666666666666666,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 1.0,
     "distinct_phrases": 1,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_4628fdbb4ff69837"
     ]
    }
   }
  ]
 },
 {
  "mode": "local",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 5.0,
     "distinct_weight": 6.5,
     "distinct_phrases": 4,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_261b32a87e59c977"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_dada0222b861946b"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_98b4db35ba0c1945",
      "ob_cdf4da3570e6e9ff"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": false,
     "observations": [
      "ob_7af666a4ffbc4a3c",
      "ob_fe6e25525031cec5"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.16666666666666666,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 1.0,
     "distinct_phrases": 1,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_4628fdbb4ff69837"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_32fc66cd087d18e2",
      "ob_ff684df46b045f28"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 5.0,
     "distinct_weight": 6.5,
     "distinct_phrases": 4,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_261b32a87e59c977"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_dada0222b861946b"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": false,
  "correct": false,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_7af666a4ffbc4a3c",
      "ob_fe6e25525031cec5"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.16666666666666666,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 1.0,
     "distinct_phrases": 1,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_4628fdbb4ff69837"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_70baee6d931d9f33",
      "ob_90c0f81059211f4f"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_a6ff7015d448b5c4"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_research35",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 6.5,
     "distinct_phrases": 4,
     "unit": "u",
     "window": 1,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_261b32a87e59c977"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_dada0222b861946b"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_98b4db35ba0c1945",
      "ob_cdf4da3570e6e9ff"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": false,
     "observations": [
      "ob_7af666a4ffbc4a3c",
      "ob_fe6e25525031cec5"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.16666666666666666,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 1.0,
     "distinct_phrases": 1,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_4628fdbb4ff69837"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": true,
  "correct": false,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_32fc66cd087d18e2",
      "ob_ff684df46b045f28"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_70baee6d931d9f33",
      "ob_90c0f81059211f4f"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_a6ff7015d448b5c4"
     ]
    }
   }
  ]
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "dominant_research35",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 6.5,
     "distinct_phrases": 4,
     "unit": "u",
     "window": 1,
     "dominant_context": true,
     "whole_unit": true,
     "observations": [
      "ob_261b32a87e59c977"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "music",
  "case": "repeated_ambiguous_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "music",
  "case": "single_observation_distinct_music",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 2,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_dada0222b861946b"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "music",
  "case": "subject_owns_unit_neighbour_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 2,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_98b4db35ba0c1945",
      "ob_cdf4da3570e6e9ff"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "music",
  "case": "mixed_subject_wrong_domain",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "music",
  "case": "mixed_subject_true_domain",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 5.5,
     "distinct_phrases": 3,
     "unit": "u",
     "window": 2,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_7af666a4ffbc4a3c",
      "ob_fe6e25525031cec5"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "music",
  "case": "repeated_one_strong_cue",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "music",
  "case": "cross_unit_cue_aggregation",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "music",
  "case": "rejected_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "music",
  "case": "inferred_support_not_an_anchor",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "music",
  "case": "explicit_kind_hint_preserved",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "kind_hint",
    "op": {
     "op": "kind_hint",
     "value": "music"
    },
    "score": 1.0
   },
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.music",
     "min": 2
    },
    "score": 0.16666666666666666,
    "evidence": {
     "class": "paradigm.music",
     "min_weight": 3.0,
     "distinct_weight": 1.0,
     "distinct_phrases": 1,
     "unit": "u",
     "window": 2,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_4628fdbb4ff69837"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "music",
  "case": "dominant_mixed_domain_table",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "research",
  "case": "unhinted_research_two_clear_cues",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 2,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_70baee6d931d9f33",
      "ob_90c0f81059211f4f"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "research",
  "case": "unhinted_research_one_observation",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 3.5,
     "distinct_phrases": 2,
     "unit": "u",
     "window": 2,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_a6ff7015d448b5c4"
     ]
    }
   }
  ]
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "research",
  "case": "ambiguous_research_implementation_tokens",
  "expected": false,
  "actual": false,
  "correct": true,
  "trace": null
 },
 {
  "mode": "local_window2_research35",
  "paradigm": "research",
  "case": "unhinted_research_full_vocabulary",
  "expected": true,
  "actual": true,
  "correct": true,
  "trace": [
   {
    "anchor": "cue",
    "op": {
     "op": "cue",
     "class": "paradigm.research",
     "min": 2
    },
    "score": 1.0,
    "evidence": {
     "class": "paradigm.research",
     "min_weight": 3.5,
     "distinct_weight": 6.5,
     "distinct_phrases": 4,
     "unit": "u",
     "window": 2,
     "dominant_context": false,
     "whole_unit": false,
     "observations": [
      "ob_261b32a87e59c977"
     ]
    }
   }
  ]
 }
]
```

</details>

## Frozen benchmark card (W1 stages)

```json
{
 "label": "after",
 "run": "kr_dd8deb4310d4f33b",
 "wall_s": 509.337,
 "timers_ms": {
  "assess.total": 8485.6,
  "extract.extract_units": 178230.6,
  "extract.hash": 8439.2,
  "extract.read_units": 2407.9,
  "extract.semantic_proposals": 161.5,
  "extract.store": 10462.6,
  "extract.total": 199702.4,
  "generalize.analogies": 47.2,
  "generalize.assemble": 144.9,
  "generalize.extrapolate": 722.6,
  "generalize.extrapolate.stratum2": 196.6,
  "generalize.hash_output": 2702.2,
  "generalize.infer": 834.6,
  "generalize.infer.areas": 0.0,
  "generalize.infer.stratum0": 130.9,
  "generalize.infer.stratum1": 313.4,
  "generalize.load_evidence": 27688.6,
  "generalize.match_artifacts": 660.4,
  "generalize.match_projects": 3574.5,
  "generalize.match_projects.anchor_fill": 3259.0,
  "generalize.match_projects.constraints": 14.0,
  "generalize.models": 233.6,
  "generalize.operators": 476.7,
  "generalize.predictions": 455.7,
  "generalize.principles": 15097.9,
  "generalize.principles.cluster": 39.5,
  "generalize.principles.discovered": 12.5,
  "generalize.principles.seeds": 94.3,
  "generalize.principles.statements": 2428.7,
  "generalize.principles.typing": 12002.2,
  "generalize.store": 2115.0,
  "generalize.total": 54801.0,
  "generalize.transfer": 46.0,
  "resolve.hash": 321.4,
  "resolve.lineage": 0.0,
  "resolve.load": 27907.3,
  "resolve.remap": 762.7,
  "resolve.repoint": 373.6,
  "resolve.resolve": 3425.7,
  "resolve.store": 5476.5,
  "resolve.total": 38267.5
 },
 "counters": {
  "generalize.claims": 9364,
  "generalize.entities": 3010,
  "generalize.match_projects.instances": 79,
  "generalize.observations": 36734,
  "generalize.principles.cluster_pairs_compared": 17158,
  "generalize.principles.statements": 1041
 },
 "stage_hashes": {
  "catalog": "c6dfb5189d9fcabeaf41d62b8e37e210b189116533352495f48f6fa43602bc4d",
  "extract": "a486c8e7dd9838f2537865e0de2bf4097087e91315ed20e5721322eb9468c1e0",
  "resolve": "2147cc970a487303cebd4648b2e6700f95600576bd006c091cb7686c2b01e5cb",
  "assess": "f1e7c9952c5f78cb54f8d839122585f1b329caed0f4061642db3aba796547868",
  "generalize": "c45dec4200d06dbd61c975d4d5077c8fd2c8efce50e8a45bd4c6819038fe4939",
  "materialize": "9a54e2122a43e308e46d1cecf0101b219ae286529e7c6f4c37ffef3187169188"
 },
 "stage_stats": {
  "extract": {
   "from": "catalog",
   "units": 1413,
   "failed": 0,
   "names": 249,
   "observations": 37249,
   "entities": 3010,
   "claims": 8014,
   "areas": 1,
   "principles": 824,
   "decisions": 52,
   "forks": 0,
   "statuses": 139,
   "semantic": {
    "status": "off",
    "candidate_ids": [],
    "rejections": [],
    "skipped": [],
    "requests": 0,
    "cache_hits": 0,
    "accepted": 0,
    "rejected": 0,
    "failed": 0,
    "input_bytes": 0,
    "chunks": 0,
    "omitted_observations": 0,
    "failed_chunk_observations": 0,
    "selected_chunks": [],
    "identity": {
     "version": "2",
     "requested": false,
     "limits": {
      "max_requests": 4,
      "max_observations": 16,
      "max_chunk_bytes": 16000,
      "max_input_bytes": 64000,
      "max_output_tokens": 1600,
      "max_proposals": 16,
      "max_response_bytes": 128000,
      "timeout_ms": 30000
     },
     "representation": "relation_v1"
    },
    "requests_spent": 0,
    "input_bytes_spent": 0,
    "retry_required": false,
    "representation": "relation_v1",
    "accepted_bundles": 0,
    "entity_drafts": 0,
    "claim_drafts": 0,
    "abstentions": 0,
    "response_outcomes": [],
    "output": "497ffcd6abfb43c9ff830263cd31933c77590d2c6e0deed4d908dd9b7b3dda62"
   }
  },
  "resolve": {
   "entities": 2513,
   "mentions": 3010,
   "merges": 497,
   "blocked": 3245,
   "claims": 9364,
   "lineage": []
  },
  "assess": {
   "claims": 9364,
   "contested": 49,
   "conflicts": 7,
   "reversals": 0,
   "replayed": 0
  },
  "generalize": {
   "instances": 498,
   "absent": 3222,
   "analogies": 62,
   "inferred": 39,
   "extrapolated": 101,
   "transferred": 5,
   "principles": 828,
   "operators": 5,
   "models": 3,
   "predictions": 11,
   "predictions_holds": 0,
   "predictions_violated": 0
  }
 },
 "db": {
  "tables": {
   "loom_cat_checkpoint": {
    "rows": 1078,
    "sha256": "31a8d361e0eb76ef6afe849b3d1bcbcbb0b99bad8fa6444da23c4da75adf25e8"
   },
   "loom_cat_decisions": {
    "rows": 2698,
    "sha256": "9e929c59840f2caf93afc3e6d6f0477040f278adb236aa9ce6421d05775f5b3a"
   },
   "loom_cat_imports": {
    "rows": 1413,
    "sha256": "4630f1187b7a1455eabba4e9716c44543b81f9f7e3a7d326ab1f0479e5a73777"
   },
   "loom_cat_links": {
    "rows": 64727,
    "sha256": "f9e8377cc806b4003e4887a176af3e646ab613e327f154bc6b4df029c5d44a98"
   },
   "loom_cat_overrides": {
    "rows": 0,
    "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
   },
   "loom_cat_profiles": {
    "rows": 1,
    "sha256": "30289f67de50b1f7921d7851ae0547a251f8045b3296b105b852c2f66cdb288a"
   },
   "loom_cat_scores": {
    "rows": 2698,
    "sha256": "cd3cdb011d5a02f3f56e9f60af540d4905bcd9f11cb4073f96f98693525430a1"
   },
   "loom_cat_sources": {
    "rows": 1078,
    "sha256": "ad844fa37844243e8d553ce65afca935b54663c358fbd14bfea35ec22a504679"
   },
   "loom_cat_units": {
    "rows": 2698,
    "sha256": "4a07dd8408272350235c5863b4e93ff80634cb991e8b1e47f85d67e68af5e66f"
   },
   "loom_kb_aliases": {
    "rows": 2943,
    "sha256": "cbea40c7bfab124e76dc64f80c1f5080b632c21b244151340deb8a9fddf5db97"
   },
   "loom_kb_areas": {
    "rows": 1,
    "sha256": "aab87004b947891ad5026910adf9356f8dca395d7dfa82f8df38857123427b3b"
   },
   "loom_kb_candidates": {
    "rows": 0,
    "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
   },
   "loom_kb_claim_support": {
    "rows": 17748,
    "sha256": "f4e2e57c000628d62222cc55d8308025695d1f07324f01f9145af6bc523cdb31"
   },
   "loom_kb_claims": {
    "rows": 12793,
    "sha256": "790f1e0e7aa6fa194e5b18661552c38cb348b1e5a15bc238c4e375913164a905"
   },
   "loom_kb_decisions": {
    "rows": 52,
    "sha256": "fc1c093ea382535d3aef56ef103642690a02f556cc7fcb870c54c31ebb2944c3"
   },
   "loom_kb_entities": {
    "rows": 3010,
    "sha256": "ae29e158ac458b38c96f16c3ab18cf0a9134ece1bf202c78cf91876cd94820df"
   },
   "loom_kb_forks": {
    "rows": 0,
    "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
   },
   "loom_kb_graph_receipts": {
    "rows": 0,
    "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
   },
   "loom_kb_instances": {
    "rows": 498,
    "sha256": "50f26c1ddc85136f25c4bd59f9bb76e999de4980d6220fbbe7eeb2cecbca545b"
   },
   "loom_kb_judgements": {
    "rows": 0,
    "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
   },
   "loom_kb_llm_cache": {
    "rows": 0,
    "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
   },
   "loom_kb_meta": {
    "rows": 1,
    "sha256": "ccfcca9f7cde28d2cb449acf0583a85c0d9db1c90d6d86856a4eb4d84e4b4587"
   },
   "loom_kb_models": {
    "rows": 3,
    "sha256": "11aad743336f61540ba467acc2c633a33120eebffec93e9228a6570282a4ecbd"
   },
   "loom_kb_morphisms": {
    "rows": 136,
    "sha256": "872e407146f96001a444135afc51a25c39f1978d59a9dfedcf506ffb6601c4ed"
   },
   "loom_kb_observations": {
    "rows": 37249,
    "sha256": "798d75f57ffb15e3215f061d64eb6d06984a8c03390b473551fb5b93639b0f8b"
   },
   "loom_kb_operators": {
    "rows": 5,
    "sha256": "0ea3b7c74f8dfca7b4549dca8ebf6b94898a7fb0fdf75643985de316dc6c1c59"
   },
   "loom_kb_policy_versions": {
    "rows": 0,
    "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
   },
   "loom_kb_predictions": {
    "rows": 11,
    "sha256": "eec788162eb8eb3cb90a724de78f0fd2974461ba2327edc903700791bb68502f"
   },
   "loom_kb_principles": {
    "rows": 1132,
    "sha256": "245cc0892c9dc0d08ecfebf9c6ccbbc6ebe7b02d619492cc886d8404885f94c0"
   },
   "loom_kb_products": {
    "rows": 998,
    "sha256": "9771e3da33fcb099d59e6ed77e24ff7c6e8ee1ebd42bec2e76cd4a64101c8e30"
   },
   "loom_kb_runs": {
    "rows": 1,
    "sha256": "a283bda10b9d42e60884fbe77b31b00fd1b57c64eff8c1ef5844216a1db9b2d4"
   },
   "loom_kb_slot_values": {
    "rows": 5385,
    "sha256": "f25b398e8ee3817a6df2fb869671ab4385c5f0bde8859af86faa8ac2e4ed0bcf"
   },
   "loom_kb_status_records": {
    "rows": 175,
    "sha256": "bfb29aafb76cc510fc44425bfccce2d6984277efc684707cc6fa9fa866c34ef7"
   }
  },
  "sha256": "281752b724dc5fbc41300ad6103bf05cce2db44c156991e773ac07cafdd0c797"
 },
 "products": {
  "files": 999,
  "sha256": "a102fa78addfe5cf6dbf6151d62522c5778ba093cc77d9e33b26dccd58411166",
  "per_file": {
   ".loom-archive": "f363e042b62c03a3144ed7c0d1c64eab360411a7ed9cfb082ddc89bbd8f3016b",
   "BACKLOG.md": "1c1c9d2eeab94cd9fea2ad8a87f183dcdf035dde91634004b86bd18467c8b406",
   "SELF.md": "c940b5b6a58cee71242b987ba864b98117248f4f2804df49fa538a0d62d4b67b",
   "dossier_in_0026162272593cc4.md": "35f6357fed9763b17e875943bfbf395d8d897aa1663e99883ead02e19fc52efd",
   "dossier_in_013c081f98831b12.md": "fea43e77e552be2024cd649a07440c172e045f697992cae0b1cb15bfc465d89a",
   "dossier_in_01f37d3b848c6c61.md": "1a7bb9e7d83e7039c74fd9b52b7088dd6249cc61d1a5d05da8a00d7a2c39413d",
   "dossier_in_023518b65456ecb3.md": "11987bca92a513cc47da460a82f47299b334e0338f50e2cbef591218caeec642",
   "dossier_in_02575581d0ede86a.md": "ec06826959c394fa05e0473db6fcdf2326b37b31dba8f87dbbc459c0dae1f264",
   "dossier_in_02622e540fb9b9b4.md": "855107523caefdc8d16d4dd3224c66acd6efdb4aea23522de088f22b25cde336",
   "dossier_in_0274f98af64e2795.md": "f14d631dfcfd302c654f12db52d4d076a10f3ed38687affe3d7ddd92b87aede6",
   "dossier_in_027ea0e3da321447.md": "7b312471c3a59cf92790a23abe323abc78e4434448dc34f7e113231ca5ebbcab",
   "dossier_in_02eb6b6ac560997e.md": "a8f57f62622d0cd4a9855de0e5a8ba117c2ebcfa0a3643d7088765f7914a1be9",
   "dossier_in_031ec517c6b73b69.md": "42d8abed52be20cc2e085f80de09103be44122f1356cac9ab21bba7f6a771204",
   "dossier_in_04e4ea15fdd7f357.md": "78135b229d2d90aa6cd9c12a67b4081b9e42ff43b0f5bfa08e966129441a2537",
   "dossier_in_058ad3f9740154ed.md": "84c09731485edc00305db44d62e4114750dc6b523189cf4b16a67905545df79c",
   "dossier_in_05eb7f9b57c0a291.md": "75aa109a88fca1ee1b335b78bb10352ea98b2bee6fec122d02cc30f8d20c2756",
   "dossier_in_062da72a2e3586ff.md": "85c9c3fae93374beeb9172768139ae210b5bd982e7b670cc526613b82b7be79e",
   "dossier_in_06694cfc633237ae.md": "553cc5d34e9fed41bf71aa04872ac9d30068c4ab8bf6cc803621267d97e9be70",
   "dossier_in_066bd4ae2b2235d6.md": "e1e8b829efb35e0660143a95eb989b7f5f7e4e9c3e4f763bb48821ed1ed5dfc7",
   "dossier_in_06726548fbb905a5.md": "f4c64878465480e9243addd85ee21a0abb902976fbb1b7351f373dbe63f5c15e",
   "dossier_in_0697bc02afd1da02.md": "ad0f9357648a20f3f94e624c3a084ea9420b3016ad7229a47dd81ed60aa4a14d",
   "dossier_in_06e7c3427bc6329c.md": "21feebdde1474ee536535b20ebb97890e2b3b624222169968334c3f2f406ffcd",
   "dossier_in_06f6520c952f56b9.md": "c10b723b1cea283c72c4f3a96e1bb5da139e5d1df843d29a6f9fcd836bf0f61e",
   "dossier_in_073bfbd860371ff9.md": "26a2a67623fc41dde438089f5ab619f7e5b9be047a47ca434d94ac9985aaacc3",
   "dossier_in_075b04630573cca3.md": "d5748dc7fd4c54c52b4b7cc6ca7833e4292e91d0604ef936e5060b771e7adeab",
   "dossier_in_07ab28b8b67275a9.md": "0d67f77e1b5a98a4aa91136d4199e4c0125d1590f7a123bf4d8f97ba81cf7594",
   "dossier_in_07be289fca280fe5.md": "c02d6bab679450f3713d4971ffc44df176fa4f2a53a463154082ae511bdea84b",
   "dossier_in_07f4506756ed61bd.md": "ba6c28f0f635b199321b1f5aa96646ad0c038e454a27b8d160694c30a1c8a028",
   "dossier_in_07f94b41166b5125.md": "66f14eb2c262e2b54401171366dd5084e8caaf5cbd4ed17459137230e3b57091",
   "dossier_in_08484bf85fe6a4a5.md": "dc533816a2d2413fc981989daecaad2cd612596588ecba78ee7672eb9976b5a9",
   "dossier_in_0941aeb4eb810f0b.md": "f7c929632fd8083f697d9a2cd3c987acadde8c545049c8bfb3c9bd3bfb9911c7",
   "dossier_in_0952a3d8ffd12734.md": "58815bf4ff3aef3eb2e4b540802d7efef8519392d67d8785a56dda37d8d96064",
   "dossier_in_09682da555139b03.md": "13873a1e94ad1d30166016eaddae8f932868784947b40a2f23b4248f65d92903",
   "dossier_in_0accd247ff889bce.md": "2f5d385d322a52fdfe48f22a86804f0f0c694474225c47829f284f811f622940",
   "dossier_in_0bc4b4e36d8fe878.md": "fc206b4cf0b841f818b1cdea69baeb5ac93743a908069349304e48af8e676754",
   "dossier_in_0c17e3dc46e12795.md": "627f1518a3649aba07512029537b0a8ed53167c2d29d930c8a2977423612bad7",
   "dossier_in_0d178b7d32efae15.md": "ec7d629989f0638fac4ff2016a32cd1bfdec6a646cbc7209a9289208968aacd7",
   "dossier_in_0dff362d9672e155.md": "78468e8d551cf2e952e634d1d4e5195c7561c2b3a798b79f68ab8081939c19c6",
   "dossier_in_0ee72e6fed85b72b.md": "3876a1b994a4ad3f9225e35a637e756739e1e65a594dd372209667fd44e2d186",
   "dossier_in_0ffe7c42d12ed0b2.md": "fb8a386a4fbd82010b847acff0ef48a9df9317c1bc3a79e9d1f9b91c59ed047c",
   "dossier_in_100f3811f2627b4c.md": "ab80577c0f09cb2b66fdc9f846a0669ce2ebac9a0efeb5446b7c0d234fcb8767",
   "dossier_in_101da3eb16a3ca55.md": "ce354b95ded0c999515819e06cadc6515532f6c3e34e300d0051c57b20dbbcd2",
   "dossier_in_1051b65ab29e18fd.md": "7328663c917f5002ded8d185f349fdc5fd6eb0a1e369b5a836f9c36934951293",
   "dossier_in_11299b273770e8a8.md": "7c2ff1b8761758c9815eff83f991f29f29864af4b123848d8daf9a2d3e8d93b8",
   "dossier_in_113e279b7bbdffc3.md": "b2675aa379718d4a5400978b54bbe04411c4601b40a0e10c759396d735b92c0a",
   "dossier_in_1166c67282b8478a.md": "f63671d5adfa1c2794e26ed975676e968556f43c386781dc9e3a40a3770f4bf7",
   "dossier_in_11f7e70cac1ca142.md": "aa9f82b40c5ce605af392a6741e1f9114619278ded8c5c077e431f809b02418c",
   "dossier_in_120d7ac1cf88e79d.md": "7a3ad4a7d639e5a486c944888c0afc9a429a914a3ae5053330b6ff5fd446ebd3",
   "dossier_in_123e3dc3424b83da.md": "a8df944d597f2d6306a430ca281bcf1330f5d147b269c80b76c95ce0f1803b84",
   "dossier_in_124ac8c06c14c05b.md": "7e7688be37503a15a447529731ee83ad6bda15f4d1543a155abd2730b7afd562",
   "dossier_in_1331ff07041d8c18.md": "b6930b88ed54d247cc0bf6b4b14cb58bdd887bb13a3401062e79a80d23f97f6e",
   "dossier_in_13ee1293e2c76c86.md": "96c2b8bc25a9e0065c9e0db1d62e11e73ffa009a298f03cbab459617515614e0",
   "dossier_in_148a6be15f50370d.md": "cd26a7e570b2ee8ae8192eaaa2baab139b4612bf75e02a7e8f4ebd221c7d01b5",
   "dossier_in_153f92aa786c5e59.md": "ba143d279c06410e9b91eedc8a3881ff6afaa9e6788c0906c448c9a430202836",
   "dossier_in_15fde14bf12d85f8.md": "07973bf1cdb3ea8f63e6ef7673016a98b5e2a1baf8e23a6e06d849faa97b8e33",
   "dossier_in_1669b84323c01bc1.md": "8f5f37df64eccef6ae374f9da67985be1c3e9d743b73d4eab8d5d73b93326931",
   "dossier_in_1677bb654640fa03.md": "84849d3595b93e94fcef24f537189092f420a7ac85acd2eb21cd33ffffacbfa9",
   "dossier_in_1682848b3c021fae.md": "c65bd18cdb7cd431184f32b898359d191af36b2664461b3333290d2e8c82f0df",
   "dossier_in_170e11e068b0c55e.md": "d98790375bcb9c75c191520b381532afd02955fb25bff570bead6c4f70d6dbe5",
   "dossier_in_17f8885215472d8c.md": "b9c94bf24dfd3894e1e8efec6b2deffd06af5b4c33d2d9ea79f34fc8ba263a3a",
   "dossier_in_1872378afb60aee2.md": "9e19e5cd643d9ced239fcc6fc354308851e4ef5d32cd08e933f7fb66ff91e5bb",
   "dossier_in_191fab9b3de52ed4.md": "366f27cc363a3cdab796ef987f2d23cf4fe67027f4abf5673a7e7970b28b08a8",
   "dossier_in_19558cbb04548669.md": "38688fcb1678756f0274ed6d03f28c7e161c300f076bae799768ef23d1475335",
   "dossier_in_199191dceae1e331.md": "4931dd4794c1d955e18d8e36d537082b30904ec03a105fa1965aa1655ed77780",
   "dossier_in_19c37698889ab195.md": "0074c1f3488898fdd8f432da9fad2bc93db0dacfaa87d871f9d42af9cec6ab90",
   "dossier_in_1a2a73ae686ff915.md": "ffaf842b7d8bfe17e4450cf064f5bc0b0f1639435e5cd1316e593be1d9b6a9fb",
   "dossier_in_1a46d462a1e589f2.md": "b7e48efc7722004eab352ce2a0193c292ae7edd7e907bbb189dade831cc1ad1d",
   "dossier_in_1a6519bcbf50d20f.md": "87c92f8d04a54f98f544ffce930041ad5295a3e938b912fb4f8554572d14ad68",
   "dossier_in_1a78c4d7ca2904c1.md": "b7f1d8dc2fb0010c6ea93f5d509c4366718001f2fe6b13584eb0f1543d6f3c35",
   "dossier_in_1b29d7d9067f8fe6.md": "d851970bcf0e3b59d016ca5946778642d218e7b603cf15ff70949ef64a78af92",
   "dossier_in_1b645f8cbf741237.md": "221d66ffdf44b1819a8468e2d8eb2bb83d5d167470b17d7a14590b56e069bdbd",
   "dossier_in_1c26356f95e26193.md": "872636c008f9d1eaf471f5c1e87723ebc2d43e57f2adcc5279d26ceaff127785",
   "dossier_in_1cca47f3a0d7589c.md": "b9d817dac46fcd83e7f5fd4145cc7a693897ddeb4c5ef55df42b0c684a82feca",
   "dossier_in_1dd3ec7378f4f741.md": "7345ace0fa46484c486ca6fe607cc4b53eaaf3d6cb336b0a847f6aa4aa4c4970",
   "dossier_in_1ddefa02433b6328.md": "f4023867e28413850094758c81c3c21ffb0663a067476d77ec0961454f5815ce",
   "dossier_in_1e3aa11375b0029b.md": "4b21974c1620b697f91ab62155eb1f7b2d31875d1284c7d669df52a40116ea7e",
   "dossier_in_1e85d3d81e10785a.md": "ab9f7a11064b96fc878f2dcab8b1bb7d5696e0b081278765792b2a1b4e817f1e",
   "dossier_in_1efb3f841ed68f2f.md": "9b6e441c2124e564d85ed2678a6f7ff813222d4dbcc860e8462f34fc41078765",
   "dossier_in_2423100566953142.md": "3dc3d9836509a48652d0751fd7f9431285afc43b90bf335109da4f6afa689809",
   "dossier_in_242c3e4d12729091.md": "2316e917bf7715c34e60ca2b45093f1c3d135799ee735cbb33b246ba45933a19",
   "dossier_in_2456d72c1efc6e28.md": "aa73f25389ad60b4bb4f90f74c0366ce2c0d233e00fcdd381759b8f5d20a5e8c",
   "dossier_in_247f5e7806e14a91.md": "0ab6a57ed8c4d1705778b6196490c5150a1eb5f25ab7db954f86c3bf3244e05f",
   "dossier_in_256405e2f067092d.md": "19aa2093c1c9b8c297d5eeba9632c5ef6d89012ccd6e0342ccc1298102e7fa15",
   "dossier_in_258c67251b2fb1fa.md": "298b2b99d6b7ef9af6f426417d1c05663ffbd2a3111eee35b0b57250f972a985",
   "dossier_in_26cd97167a86ab2d.md": "72e0428976e2580bd059e866b3b9d6aa2b2dc7c0987d6d0e31e5946c131b603e",
   "dossier_in_2749711e98c02f1c.md": "253e52af35ef84c7c7709c5f268c5e8819b9035e51f0e5de642292df67e6d003",
   "dossier_in_2802febb83347740.md": "edf41a89237f921003f4e65675fb5f238ab57e4d6e7f1432c06c6ed7aba9a3cd",
   "dossier_in_282eb8628c21cdc6.md": "e97feaf6f3b5de0d4f5a416835ea60b9267ef4622dcf1958c66a60f2c134184e",
   "dossier_in_289a24d239ed7df1.md": "59dcf833531a859a548f0a205eb5d24c679e76d3db5e449921b2a7c82b1e685c",
   "dossier_in_28a9b2cba9e3edb2.md": "ec4e1eec5ead43be2198aeb28ca3a4db3ca65ad6a2a84e3ada70ee3bcec2dadb",
   "dossier_in_28f7ff3b4c5d12a1.md": "9085d3a4a7e1613088e563415b811c3586ab84f767a4c839206a808419ac3f50",
   "dossier_in_299244ef22a3cd70.md": "3cb9589f232e87211d32cd13b8f6b9adfd47357936981dfc9202aac5112ebfe3",
   "dossier_in_29ed9d359c37b513.md": "3ed46699001630aa630b282303f2fa7d493fb11a42c7a68097f256ecc9b5f765",
   "dossier_in_2a89a333a941c116.md": "c54bb28a603808bcad3716c0d8881a2d7798491fe61fad47190245485298b01d",
   "dossier_in_2bd5d0cf7b557cfa.md": "8efda49b5cd9e32bcf2675dc470e44cc8b6897ca385066ac4cd60ad9ae28c67e",
   "dossier_in_2cccb617b592628c.md": "f58c76cc150829debe64cdf95f60a255853d17ebbadf28ad8a9203414f365c93",
   "dossier_in_2ddfbaa645f52562.md": "09f6f28b74599fa693d4f0d170d5be4ddd4e2565586436678aabb03322f1e5e8",
   "dossier_in_2de616af1bdb1d3a.md": "de5f926ce0cb34bc614f84e37ad41a61b162bc8ba4d3e8252c724c8489b49a6d",
   "dossier_in_2dfb10d3b3e093ee.md": "deedb944f615c30cfd55430a211f8146904d2acd124e4d9b90b3db1bbb5a34d8",
   "dossier_in_2f5401f3c0f1dbf8.md": "850403a58adf41c8b1629ff300954b3f320cbe230eebfc0b0e1e8dcd84aa9309",
   "dossier_in_2feaf2f515e4b9aa.md": "2f5b6b2c0cdc3480c92c9dbcbe8c10dafb15957fb8b5e6df172e8b5f315cb6db",
   "dossier_in_3004aab1769f9842.md": "ac5d0bb693797e1d3daefa88c16bfd0dd8efd392323e3a2c61c1d2c6341a744e",
   "dossier_in_3032637ea5e58c7a.md": "02d857a62c7b7cb69aec90c4f10a2535a248a6a940b2a1032c755a4834e03431",
   "dossier_in_30a096b928fd82f6.md": "4761b2853b02c022fbfee40fa818ee8b17016b6ead9b1dd36b61da41d456b689",
   "dossier_in_3139a6326a607d88.md": "bb04a3e858cefcdaa54aa418159b369e613e13bc390cbf629aebfefea43f704d",
   "dossier_in_3171a851dd10fc60.md": "40e5a259e0b1c2ce0d98dcd8f99ee567deea1054bc4ff3290f8dafcb1acf1dfe",
   "dossier_in_32ddd9891bc7d2c4.md": "080346588e1cbc5bbd324cef2a354251e4fb56e503ae32ecb5275ddcde24e9a1",
   "dossier_in_33a37bb3cf24d799.md": "083a34e90c072a2033982c7950dd7e46baa98b79f8c9cdc4b1923e8552190d69",
   "dossier_in_33bf855c7eb30203.md": "13c62ddfd6e873496cd59b2aaaa33ff4e596c6b4e29f30727c1157a27f42d755",
   "dossier_in_351d7fd9dbe35dec.md": "ed88d3eb60a6444c7ef854710eb8f2a21d340bc0a578aacb9466ed1a8b1573fe",
   "dossier_in_352c8765e69793c1.md": "f2a8af706cf11fc64ea2e73487b41a36d1459745c265912f6e6c03d158579af5",
   "dossier_in_35cf050ebeb8c4e2.md": "9e419611a2379f9ba1954634dae1c5f81d2b967dd030223e9debb1baa70c4577",
   "dossier_in_36bda3d72e664b3d.md": "900b41ae452c11363a4e1d08a370c8cbaad78430cb58ad5881b9f5ac34203ded",
   "dossier_in_37419f3c1c7b8ae5.md": "93bb848798beca9990135501102958d57c6564d38fd7fa798465126c82fbcedb",
   "dossier_in_381a2f4af1f7b1b0.md": "d73595cc064f21d1965d4c827eb6f3f7bbadf23aa2d8054cc2f44f5fa919e548",
   "dossier_in_386fb556b34f47a5.md": "a727ea720c9e2a7621cec7f1e8afecf3a2f2c93fb485e131bd2310f103953e03",
   "dossier_in_38f2897e6b1ff340.md": "06adbcb644158ba88b16a3c8f857244cab2bdc3e6fdcbde501ede4d922e28906",
   "dossier_in_394d15bb29383ca5.md": "6ac3143274152f412621e37ad1e60edc353d862eab28297005e88429dcde1215",
   "dossier_in_39e02f3b2700adda.md": "74480dbbf98f31f54c76914064c6c82e1ef2577d69d979bc37ea9d53aa9f9a5b",
   "dossier_in_3a5cd13c78628442.md": "b270b053cddb8638dbc0867a3f74a3126f1ba599b4f4973c74363bea5b04d3df",
   "dossier_in_3d6ea502155c6da7.md": "f51fa9c5488853f6630c7e4d21be5d03d164efe34b49711b93ac13c040d8d658",
   "dossier_in_3d706f289cb8e6c4.md": "e8f44a2bbc4d3afee1d9f444dd5a256a027bb1c82012f1cf02f15add05656101",
   "dossier_in_3dfbe1e6ab56e933.md": "3672312d6b51a76cc619c99d9b347263eb9e7552397f6ff14e7cfd0868001c4e",
   "dossier_in_3e53a18d4cb2aeff.md": "1337a6540ff03a8068710922d3db952130cb5bf9642eb24b4e10b2eaac6ed30c",
   "dossier_in_3e6f8f6040b3c94c.md": "786e13d17b12211b55f247f6834a5c9b036bab1e52d2f9a4475c730ac314bb63",
   "dossier_in_3e8f73756574f677.md": "9773dcb02d1e9f8c8e12c61879175101a4b3b19f7c5eae7188225ff73ffca1ba",
   "dossier_in_3ef258320c6fce0c.md": "30cdd5b280be8b0852b00668aaea20c8bd0590a84081c89e1c3fa47baa6e1f9e",
   "dossier_in_3f56ed70a45fb8c1.md": "d7b749431e2a0a3af414b630d7dfbb7cec109e696c165e5333e12544638d522b",
   "dossier_in_3f90e55c21382f1b.md": "2b18ae5877c05aa0d385ee7ec0dcb2d033610f48bf1c6276c0ec0dd25b743a83",
   "dossier_in_3f96c955361d154b.md": "7445fa1ae6e389861f5a9084ea855c7d2e0861839942e3d12a0159b16ace2821",
   "dossier_in_404bf91d2a9b1239.md": "05c919b08a5b442f7eb868bcc505587372d808281d74ba58e2a995f7e7d68323",
   "dossier_in_409494562383fbf1.md": "72415837936ca1c5a9ffa0b7c3f9489d28356266d6d261e40494b7738a0c1021",
   "dossier_in_40b00fd6aeac3847.md": "4e8f7aae30375331ba69c36249c6e7c9d69da3d82a553e6574cefebea371184f",
   "dossier_in_416fd06afd93a5f5.md": "9eed7ba9d96db674a2b8cdb4c385d1313b701b4e6ad28cd14001438b29395446",
   "dossier_in_41740d4abbd03e37.md": "4108bc6ac60da9fabe6b46e1bd331f9900dfa59cf44c4469bcc5a771a7af3fd7",
   "dossier_in_41b46fb346923603.md": "e1f6c4b91ec2eb2a426f1492cce38688706d941b5eb17bc70dc480d602e318cb",
   "dossier_in_41d7776f4f341efb.md": "2afac97c3f2d51e5a3dc3e07db18cded02b71cebed7bda072753d7b8e6301b69",
   "dossier_in_4220aeb754f711a7.md": "163b3882530670d80d62b4bebad14513e5605a9a1c8e500cd6b8789e66afd37c",
   "dossier_in_4275ea8f42aa6f00.md": "8b94e892c506034ef8b79524f956f28a8a02ea01a72776680f103fbabeee9416",
   "dossier_in_45278f72dc0c7362.md": "07d9c8f56cb76dd52481056d2e4cb83159fa8f54b94f571bb38b8402342b1860",
   "dossier_in_457ca2925db0c133.md": "cab4f9835af10c5ff90d8bbf4f5b4114019b6223f8acc8739a3014aa6d0996f6",
   "dossier_in_45d0c754083c48a3.md": "5a63dcb5396b55865ad3d0e703ec42f4fb7e82448c0afabc2782a5f0d5949a8b",
   "dossier_in_462167360ebd21ae.md": "874ed1e8fa38f90c7854d3097525cbbe0b872ed54e26ec6fddeb5964d09ed721",
   "dossier_in_463b2e98ce0a4b57.md": "45ba032db93580858d6b7a02d81af042c0a2e3671c553fcbbefa82a4e26d30cd",
   "dossier_in_46954b4a93c30adb.md": "5f1cabe072343c4f58b7a72909a5e77d2112498e4812d2b2cd9be3150432c549",
   "dossier_in_469dfaeca94101a0.md": "9f5f152148445bd196dc44f0e1edc3e2e027dcb37fa09d7f9d9d59583769999e",
   "dossier_in_46d0f429abc89e8b.md": "35b864505361cdbe6c28b9dc011375589659d7f9a4424e754ebd9e229102c88f",
   "dossier_in_47327a0d5ff19355.md": "93e141f5439afbfc2622fa7aebd33da24955f04f6404f08709c75d69a3575a58",
   "dossier_in_47ed9578befb0f79.md": "609ed38de085ba0548fa815971c8eac9ad3765c5b3b4da889179060422282be5",
   "dossier_in_48019d7238bc3c8e.md": "e7e6e8fae4593ee43c11920f16af054f1ba2421d78c2009c85e6a3ce505ceb82",
   "dossier_in_489274750d1dda34.md": "08b7907c30529eb1ab58fba6e6f6f602b4f32c70e9fb5c03deab2c6706065df5",
   "dossier_in_493a55c85bcb0c8c.md": "bb86fc15027628708cc0c8d94326cd0414dde4e1f3ad3996bd0ddeb7c279ff7e",
   "dossier_in_497c8bbebb1aa6be.md": "ecb28d03876bffb7a49e55d01ce09b67cc0f2492fcb603ba46318d1445ac3484",
   "dossier_in_49be39fa241181c5.md": "6115fd793d09d6a8ef2c76fa98d5917f32f8b8f6faa96ab8748937c44209ca93",
   "dossier_in_4a03037d07f7d5ec.md": "4934be51aeceb6e7a738970da145a386927fb57ec5344d1e480c3792d5912505",
   "dossier_in_4bfbef367d03e866.md": "b8d365251168fa2015f9e22ca0f96e6a88afd6eae65d562bcfb700cff5941a57",
   "dossier_in_4c0f39039abf65a6.md": "a916c4a0c27715e7fbec2c055303254fae9e43f58a37e7a0c86142a11aabab24",
   "dossier_in_4cb2d2b8df14a684.md": "cb0a255acc0380203e39d47345c5c8ece6662dc8d3c46995bc5228225ac0de47",
   "dossier_in_4d6cc47fc31e4aa5.md": "f6de6085a53c2e10c2461a9d673f1c9e986362bd9594022c6a2ca0011127d9e6",
   "dossier_in_4db2dd1c8fa50846.md": "31f4f6bbc6b519eab88053ded4b2f1f0ece3bac8caa01fe712ba17c24bbff4cf",
   "dossier_in_4f8c5a3683cde4cf.md": "f4737ad4e6018961cb090c279438050a46418c91399f392f477658186b80e421",
   "dossier_in_4fbb1702a6eaad73.md": "6b6784f8ccb3f3b4aa2175a49a5283751e88ad220dcf1b9b9749b1705839a946",
   "dossier_in_501d95f30476d979.md": "57386a537c9dcb60999df6de7a85319675b79479625c964d93fe4410afed88b9",
   "dossier_in_5038e69347cbcb88.md": "87dd62f56fdfa1697cbeeec26a13fb0d6093379356dc7491253e41f7e7a1105d",
   "dossier_in_504ae54ed6b53fd9.md": "02a731ba00ea4fb303f5aeed7d668be3121b923e6610f1798cb175b94e40f4f9",
   "dossier_in_5073b724ead05666.md": "2895e919518c0fda642d839b9cfa5bd25ce821ab254494a13a0db6f2efd42fde",
   "dossier_in_50d26a03de3051eb.md": "49087235d7b46137016a445b4d2c002239b993c005a6f1e6dc4d61662da63db9",
   "dossier_in_510a44a949d52753.md": "8e6ee53b9e594573bafcd31744892a384e796924f366aa5b01ddb48c31aa4e35",
   "dossier_in_517125b1101c89c1.md": "807d324dc1730e65d1949a13104cb729ca8478f7319a89d2d28f79052f8c920c",
   "dossier_in_5187a87da755b16d.md": "51afb68d5e3c57932ed4ca3e97be7a65dec1c684f5ea10da09a7dea9020ed02a",
   "dossier_in_51cc919b886ce146.md": "e8c483f54c6bd285f14e0053133ff322600a13b10402b5ecd175acec2f75ec03",
   "dossier_in_52064d62541d133e.md": "793599a1d690ed88c61d2b8757b372bf51a49a047ce4478a6a5553309d0884fe",
   "dossier_in_5224c56a68940b97.md": "6f42a9c2f9450a764cd66726df93f3d641a9e1d3a7d0720d021724836340f50d",
   "dossier_in_53ff22befba31a63.md": "e106d03fa9dbfa080f90d59bf12d8a52b9f82c35152362491c8caa6be89f611b",
   "dossier_in_5497a9cde9016f73.md": "c8c0a763cca9c7089036d0ca891a5d89635f527ed464b558b935dc43145e8001",
   "dossier_in_54efff9de13db555.md": "22efa289f56232208942571c4b2cee824f1727e8230e4f5c4d2d0e8ac2a69277",
   "dossier_in_5510be7ecaed0af7.md": "8c75b320897ffe0bdc9be1ece4fc97faca2434b32ba6058800abacd3af79009f",
   "dossier_in_551a9dd0acb7efd5.md": "781fe34d3a8a5433f45e199f5314d49d5911fb3e0486e454052cc2cfb6f1f173",
   "dossier_in_560e1eb6b10801a9.md": "0d18b8841d1ef8158cf1c3ead77bbc81b52d73064d3d654b83c1681a2911e72a",
   "dossier_in_562ed79277156ff8.md": "8856476f6b5f7409419d0a3ce898cf4f60cc3b694ad94d2de8b1e9de9ade355d",
   "dossier_in_56aa2d2d80ea71a7.md": "04b0d34dee89c137acbbac3592b1aad0f427d585bbff3acc2e7b762bec4a9de3",
   "dossier_in_573a7c118f985289.md": "80041cb7f1ef4966a0e80b1fb7462eb947f6b0bee0e3740fd312b308b3919f69",
   "dossier_in_58847c03bce522ce.md": "f1cff6a767b3c54ebbfd6f03d4303f574774ae3b05ec7556299d7cf623a25ce8",
   "dossier_in_59e5b5b17a6cf72a.md": "3e0c368876940896e28262db0f679dc927aecb46480fa5a1f01dbda7b05a33f2",
   "dossier_in_5b50e1ee84ab5a26.md": "be814d0b5fd6882bf4807bd23a5efc62958b3487a434e97cbbf4e0884185cbc5",
   "dossier_in_5c24928675d4467f.md": "a8f7ba84ea8ad75df551fde85a441c8c455ff8644c6256a11fc6964d599fc160",
   "dossier_in_5c6d75f40613e741.md": "55ccd9be06e2502200bc4b2c35b1316ed042c1c933c7aa38b871ca19d650f17b",
   "dossier_in_5c8db826c5f68b5c.md": "a21f3b8c2d35da9f8117de693ec724c433513eadfdbe3e6c5f5c1310357ce563",
   "dossier_in_5c9feda898e59c3c.md": "fc2c3b0eeedd404a598a6e3bf8596d8888b722e8f7a5825256f8f2bf4d5d2f3c",
   "dossier_in_5d83a74245acc6e3.md": "12f4c7df8d039c83c674dd6a7e4aadb5514b87e0f13cea3df7761b2cf2836daf",
   "dossier_in_5da4795d7e632fba.md": "392adf4352ac85060c54088da7b5f16b4d0ba978bfa484741fe51fc3eb47a641",
   "dossier_in_5fe0fb7931e8fb7c.md": "13c20f484884bf79e069a85d4ac1b118a9d2ee4ab50f3f91f07c953355ac6d1e",
   "dossier_in_5ff36a389cfce72f.md": "d6d782719b32ba463dbe8344d8ad7c0ac31d6a75b6b373e0aff5c86c16351411",
   "dossier_in_5ff3ed0fdc240405.md": "5aa5592456326806b1ea297b95cb03ae5c099e690a16bcc8159b78d69155433c",
   "dossier_in_61233b7beb48815b.md": "5854a973a937eb3fc164d857c935de83f715d77b804d78854b728600794b1bd7",
   "dossier_in_6149cab91d409313.md": "2467261d0aba600d378f3f55564e95929be4906b82e6fdb64d31331de9b1bd5a",
   "dossier_in_619232fa0fc3e362.md": "07094a4e838ed39784dc7322fd24255c3ebb8790ed47e3a520c20c3ac0ef87d1",
   "dossier_in_63b70c6edd29b0be.md": "088d95e97589f09a47c507866a263694c75361c89fd3835e32bb9efb3a532d55",
   "dossier_in_640b9750133f5884.md": "6803cbe949284773929d4927e386f9d41fb89eeb469d988bf1c99e378ae34d5e",
   "dossier_in_65ebbdf744a6817c.md": "a1255ec3a0ac7497048b0c0ba711b5000ddbb1c3114419a5bf1f2d17d149bd35",
   "dossier_in_6675888e3c53ebd7.md": "4f61bc79498063726637699b6632a3a5ea8c93ced3637e56476b1a95883b4ddb",
   "dossier_in_6697833edfe831ae.md": "d329c66c67c539b9c73acd277782640aa41c88fa54eab5e103281cdbea4ae567",
   "dossier_in_68183951b6e3fbd0.md": "713ead74bb94e89cac2493c1297f70d49aae2bfa7a2953564b5591eb726aee52",
   "dossier_in_68538b70c7bed9de.md": "749a4404e173f4dfc579229ed55ea732d79e052097b94b49aa2b4b9f3fc00a9d",
   "dossier_in_68542789dc746d39.md": "16791728bcd4b9f1c0af47b433a7062c12425a2859178611009b655b65892dca",
   "dossier_in_695101ce7fd71dd8.md": "6eac2f483fa222004554fe7655ac993741798abbaf8a6c52a5ef5c8af0d5e000",
   "dossier_in_69aa0204fbb6079f.md": "4ab6762eff5e944cf32f9be9c71e75d56ec60a8e6c78374f2fb80428e7b313e5",
   "dossier_in_6a2f0bef8f639b71.md": "a6863233104a8d80ddb256b3eb6cb12b14cb92fa3d17e2aabbb370320a1fdc51",
   "dossier_in_6ac102447e1148d7.md": "b2eee579a22acab330f35e5d83032ebe894bb472b486630fa766456ac38419a1",
   "dossier_in_6adec1773b1ae43c.md": "e473d36aa462d19479c3e46f974ed1f5b41715df38e1a09544628b01805d80d9",
   "dossier_in_6b88387753838b1c.md": "8352fb390a7af4b967a0427da28bdb12a5f544d748ec79c4545d54438fd5a024",
   "dossier_in_6b9a6110d47121bb.md": "a336581c9691904f185da8f06ca08d6fe9f81ff50b732b18b538ab0f097730dc",
   "dossier_in_6bca218f1e251559.md": "57b80a50301b6c89896e9fbb1dbd23b3a4c23f7b89f639630bfeacd9c8d4cfd9",
   "dossier_in_6cc03da768b979c3.md": "adcacb9a7720c31388b687ddbf2e574de593bec5b3a41c488fb415501ed1bb90",
   "dossier_in_6e5c3d4bcda5c493.md": "726b7bd627fd4ba21c61f5ba44c643c0f06703af62acf851199e7ea75d391171",
   "dossier_in_6f1dd50c4bc51465.md": "70cffe2f92a23c4b002ee5dcf656c5037aa3c23bc94adc7ff788aebb0e587762",
   "dossier_in_6f6328f5c2eaf6fc.md": "d2f8c379904831cae57d22a5c4e3e2896ffc4317cb2523900523824a4640e0c7",
   "dossier_in_6f78628d953cac3d.md": "27ec36f14001939c914f133b3bfa19f1023147ee791d1019984f5f91c53725e0",
   "dossier_in_6feea031c7aa47cc.md": "0dc0ba56ea401a6699399a1bd59a925b9e0527293160393b39fff1a1ba9d507a",
   "dossier_in_712f0d21ffa22a4a.md": "911a2d539883e8102dace2d32c003bab7c3244b5494c9afb5555531aab2018ab",
   "dossier_in_713b6f62d8b57ae0.md": "6fbf494c0d0a6af167f6ca639ebdbd5dce914ecdf9b4cdf2455b9c4cbd81c3df",
   "dossier_in_71b8d196c6eb34c1.md": "5d754ef9fe03295d7ee26a0ff5e3aabffc23451be442325d11f5dd3f8abd53f2",
   "dossier_in_723ea883248ec983.md": "e9f70a8c409f347c17fb247e7f00dc1e5f421feea67f2b58803a10db754aa581",
   "dossier_in_73e607c5071aaa1d.md": "5715ed9c9da9e6eba74d975ee0bc72e4de207359eac983b939f4cfdd40a36a18",
   "dossier_in_73f065d03aec20d9.md": "1aef9e2bf08ed94e290e669c82c375e53bd1e76641c7b8fd8b6892e1f4d30b41",
   "dossier_in_74273c7c5df1bb31.md": "9c266955630a8996ee0444e5583521b39925700707b5563da3baf24f0558b27d",
   "dossier_in_74331233ec6fcb59.md": "2a62a806e5dcd283bff16e0fd0d13f3d3e6526b6d5018da57616833c2f0a5d52",
   "dossier_in_747495db2a127593.md": "c9c91a906efa1d3f145c8ad285921243b39a546cc6ce2329f9f19a959023b1b7",
   "dossier_in_7588054b29ef3bd8.md": "2d86f9d9b24240e05ea85fa5ae20bfa2cb80a0ac0d65403de012ccd56b9d702f",
   "dossier_in_75e0c0e20ce46744.md": "b76031a47a2e8185bcf557a876f8aed9390cbe4c2f15a70aeeba5bc43f777d62",
   "dossier_in_76b7d5ad127ffb1f.md": "94f6c6a9d277cfea35b671ba34ac47460a1afd620e0ca30e31a84112db203103",
   "dossier_in_7762d1d17feb7542.md": "e13e375946e59ee33aef6d9ed6f631b2e9595a03b4827c3f333f0370465ae3ef",
   "dossier_in_78ca4e9ea72abbe6.md": "6a66e3a22d0d561ca616e58e6556cd88141849a29e23c67423e92b12e955d507",
   "dossier_in_79aafdbd08030480.md": "c32e6528cbf3221cb6bc43fa23b52c1599794744cbd1bc0ecab644d4348e77a7",
   "dossier_in_79dd85e653cf7014.md": "25065b532fa234fefab7af7e344144caf8e5fce92d1cc96b02e68f931a3de57b",
   "dossier_in_7a3ce9a222036d7d.md": "580ccb1e0ba08ece1c1ff75917ffa12a50c5c84b9e5dc7e379ae1ae2d23689e9",
   "dossier_in_7ac132da9d210410.md": "89b42be5e980590175007ed56b44fc7adbe9eb6e3511d838f03886086be0d502",
   "dossier_in_7b7fa0d4dbbb16b1.md": "4817372382d5b5ea8d06c16cb72bb946f3a4c2fb748241f2525fe90ef263d0ec",
   "dossier_in_7cce2fbcaf718583.md": "015d1483a564af0a071c37b7e1616fb4fde614717a12c4eb331301ab25c5171c",
   "dossier_in_7cddf92c7a8414b4.md": "14efe2e292dc77c143e8a5b859978e1219c6495d509a2ab976ce9523c4ed4f3c",
   "dossier_in_7d4824d25f78ef0b.md": "4b1faaefc870c263e9bba5ee43610d673e796ffe672431f87598f136ba578fc5",
   "dossier_in_7d64af28ae699ac1.md": "65cac8d80790768e2475e590ec1670b08a638444209c15922fbdbd514eb16769",
   "dossier_in_7d6c1a509544da8a.md": "cd656a4b19e08d442625ba4409fa8b947d2b0d5c8e2ffd26492fb94cb1c06e09",
   "dossier_in_7dd05e2d32221f60.md": "ed9a72d32dce07a7e5a8b6c7829da59b7fbf5e43f174b256a5eb2e679b675817",
   "dossier_in_7dd986187ac026b4.md": "df909c85e7ae7b0b16b96176b8315d3c73ffa7ff4bf7d4aad3e75dc332189857",
   "dossier_in_7df2d9eec9b87282.md": "b45c42d1df4968456827244bc053b3d811d23720916796b6299f20efe5ad56b5",
   "dossier_in_7e9358ac48041a6a.md": "8e403319cee6541ddd811b59681273858ca44ff3e245b81b5285198f6a073bf1",
   "dossier_in_8061ee1b0621a79a.md": "8ae38d5b161d6c913c4c1e3cecba3602faec289efe604f3f8887539442d0498d",
   "dossier_in_8198e16cfea88109.md": "239485b9ebf59ca751585c69c0e3bbf645cc5f9dfafd213ad656f154c0f71515",
   "dossier_in_827fb64894a6a2ef.md": "a139d6e3737e9577446bc8a6df760c113a1458edab5974575f8c199d47853eeb",
   "dossier_in_834aea25df20b4ea.md": "a26ab34ec516cebfe9393bbf0017154480f962f1b94afc733195325a901206c5",
   "dossier_in_83ad6e3c65d75bed.md": "8724ef38f197232cafb2f45d653f696d1f2d9211331c0e309b0ef8184379ba6d",
   "dossier_in_83d36d08b14453ec.md": "ae25b26d4c4fa7495c82cbcf113c3a894dc12943edc097144772e9256129c0c0",
   "dossier_in_83ec0b583795eca3.md": "8fa23abd4215d435b0a28b7bcf1f307a98d2873a8f258a513f3f81ca363da1f7",
   "dossier_in_844c621a81f97078.md": "05e9b5847d20adb2086761e0b2ec38c8486d80f4197ca80d96bf54f7ba5fc897",
   "dossier_in_844d4c5c455c4557.md": "a3565635fbb3dc4f1b5cafca034fd5f50c346e5e0f07fc7cd24b1c3af790f7fe",
   "dossier_in_8537d4576a28c027.md": "88f98987e586cccd9624f8e988a80304673b1dd9dbf619db553f2ff823704cac",
   "dossier_in_85f6fd85fd809e56.md": "60b7fcff734b5f9d98d80e0ae5aec4f09bd7adf1e2105d9958fdaab0c3b2e085",
   "dossier_in_860b217dd95768dd.md": "d3becee7c532817a68336a8ef7d55b2b850944afd73885170628be6e8db2f5b5",
   "dossier_in_86654146a0023183.md": "5ea23799a26067ed5f82eb2a29131f07352f12019bb6405ddcb50aed25298555",
   "dossier_in_87497381b5872c0a.md": "5e7fa2e9961756bc77cbf7a99eab332249f0d916adc7494a0fa8d857285f6daa",
   "dossier_in_8864ccd6ea39204c.md": "0674c83f82fa6f742d133a78a23b2aa0d9ba8df11daa7c47d1626fe901add4de",
   "dossier_in_886845c73e271558.md": "0adb71922edbc867a00d32cfd957f1b511e0f48acb0980a203364ce7429174c2",
   "dossier_in_8891ff046b6a4f04.md": "8dd53cb91bdbf1f509a42e03918d42480728964016d73263014ec1f7dec716fc",
   "dossier_in_8910436ffcece3fd.md": "e3cca6aeda68443bf7d82f433281259bfa7749884b6263dcec325267383da23d",
   "dossier_in_89e2d9d2be8398e1.md": "6fa80fbe0087fe836a3662d4445e83a74b5ebf4d0e36310d7a8dee4d9dc8dd44",
   "dossier_in_8a2b10537c0af845.md": "c5e742e5edbc0a7c9eca7967b156ef2813e18be33cc9c159cb94fce58d23dd66",
   "dossier_in_8a511a80dac5b020.md": "b07312bd89d571a6e34f37809a2331bea2e60045e38c5849a718ee384a091e12",
   "dossier_in_8ab1aa11668d4d7e.md": "283e87558aee77a008089ea7cf34df6eedf41057cfb5fa24fd1a6117d67597cd",
   "dossier_in_8b32af0554186249.md": "fd38344afe38a2fd57e9fd0788a4b09790e7075a533775bd6ee67cd7cda04935",
   "dossier_in_8b462e3b294da476.md": "1a628f1eddf4e14d564de953dab1ca0f7c688c8b15d88c7b4f5a80e32ffc35e3",
   "dossier_in_8b59d7034c50a01c.md": "6478bc9f08d99fc5331351fe0e260e7fdb728dd2b4ee28b4cc924014856bae55",
   "dossier_in_8b9a339432bc188e.md": "b4a941531812bd93c5ea9c28c63188e4e989c10bf4f80ed0781efb8838fbbe00",
   "dossier_in_8cb747aca050e3cd.md": "b7e49e6e887910d478346f2a7e23ffb1a91ce5fdcd657b93e9d4512185f28c0d",
   "dossier_in_8cf2e62f8c237879.md": "4f8192c0408ac017d373cb7c0d00562b9bcfa4c22191762fb0f96d0ad4498b68",
   "dossier_in_8dba3a0b0a2cb6a1.md": "6a36d7a06265e4a7a0a118085a6b8d02244e5efbea1820491317c588d1bfbb58",
   "dossier_in_8ebeb20393602abb.md": "1af230848b0b85967f106b8ed062df96708dd22890807358c52f7c16f03658fd",
   "dossier_in_8ef4f90ad55edd5e.md": "e18bf4e1064eea3d9b92c0c63f5baa2b5daf826d1be8b2c56e687049aea9e00a",
   "dossier_in_8f008d3477240b9e.md": "d90257f66980ecc1c31d7e24b208f08b6b890afb7866814a93f3a9bff8139859",
   "dossier_in_8f1f797cc5cca8fa.md": "9af401bafe10415d8630b932cb71970949658c501f623a3c78ae252858f4e267",
   "dossier_in_8fa52491e6d93572.md": "ae8fb67703b2c7748cb19c63f9134a097b344e6562b8c8bad6c424bb3ed29e32",
   "dossier_in_9034172be7ee541c.md": "7a0114bc63c405b91a13416507cffa6d5ee573065b077dfc1f445166ec1fac8a",
   "dossier_in_90a8c34a596b9ce6.md": "45cfde2d81a1b2c4386170726330ba567ab2e1eb54341b29d988b63428198777",
   "dossier_in_90b48548dc10f8f9.md": "a6a5e0b6fce00a1b40c42ed38de3df45616e12f9031b6fa09803140047db79c2",
   "dossier_in_90e268a0e954af45.md": "b92e911ef0e51a1e4362a961d5d331d64f52b4a255611c80f20f420a900b5ac4",
   "dossier_in_90ea2e14eba0755b.md": "749a4404e173f4dfc579229ed55ea732d79e052097b94b49aa2b4b9f3fc00a9d",
   "dossier_in_910c61f9ea9f6600.md": "f314a224bd9f59ebea662faadacbd6d46e83239cdb0d96bca26bfb4cfeb1f6a5",
   "dossier_in_918490f6f731e7eb.md": "73eaf516fff9e7feac1c2ef7bc92f80eeac7f5b0cf6c5a2ee979fc49feca8521",
   "dossier_in_91ec8588a8499c46.md": "ef3b7230640939f0416fce86336efa3d73a06fad1bc1f2ba51850ed348f242e5",
   "dossier_in_927cfe44cf937d6b.md": "0b782e6c5315232e8ec1aacf90162777df15ff777fe612b1e8a64ad6be394a3e",
   "dossier_in_933b8bccff03b18f.md": "a96cc8142f5fba6d82a9c350a6219fe99c2e3453b72b0bc905d3b344fb03559d",
   "dossier_in_939b1580f1092938.md": "e277d556ece5aba0b21dc7c069e811d07e896526e2a3d50eca35bf0a2e403bfc",
   "dossier_in_9675563800eb6014.md": "b07f51dc441d6fab4013501bd72d1d904e884c79d28864d8c3a4bca24be5a596",
   "dossier_in_96dd74d3fb478b31.md": "16a2fc608e57c087ba077b1615d88d5f6999baad1907fe532e33dd219b0a6393",
   "dossier_in_9759155d0e77331e.md": "862d6fb5f78d06cb2e60216305214b6f3e0598967cfd0aa68578e935150ed922",
   "dossier_in_98074ee46b045498.md": "749a4404e173f4dfc579229ed55ea732d79e052097b94b49aa2b4b9f3fc00a9d",
   "dossier_in_989677c8268de647.md": "afd83190d41736b0b2d0c6720d6977b12376a49abb8e31cca1b06435a3474613",
   "dossier_in_99844ff2502e9247.md": "b015ec69df8533fdd25774c247e8039ba7d3339f9b8cd9db765bb7de3a787b7d",
   "dossier_in_9a4bf7a9bdb7ee11.md": "0580a255ab7e628566c4dc7c9c45ca361079f2a7a295ce4bdc7f5d0e2f32aa65",
   "dossier_in_9b37b6cf89bd1dce.md": "f408742a3dfc48022a1918dead2c466b255d39693c8a65b71873b25224b4a300",
   "dossier_in_9c4ea0e3cca4f495.md": "cfafbcc7223368a4d6ef26b132ce6b8c3092bf729b6354352e6cf9805c47bf40",
   "dossier_in_9c6923a9d86bcdc1.md": "c87aa48cf699fc0bf33135a94483a16db641e1b8b8e5f54fd66349abb1b36420",
   "dossier_in_9c69a3fb724b50c8.md": "0bbc219d7afb79a25f7d864df63b17234393e35d144234d0f9be5a09e82ada85",
   "dossier_in_9e2afc6a0769e561.md": "a03b5e36438a99578fb76f0664ca755f89efaace7442773f644cf38a8c438d30",
   "dossier_in_9f18901eeabf13f4.md": "603b0fc2827803db37562cb0e755415077851627cfe1a9f16e00fc75f2e4dad1",
   "dossier_in_9f3d64eb150a5de7.md": "d4997f84fbee9f65a112d6e7e7db0f4c9a02014ae6ec65cc1765f2e9b36a5855",
   "dossier_in_9f4755eabdde86f8.md": "d751b882ca5722ecb9b618906a1c5c21887c66ac0250ecf8519eb3e8f68680ff",
   "dossier_in_9fc138be8c3c8ca0.md": "2f58ab8902f8ff7f959f76a201070415d1eca00a55e8d5abbbdd10fc1f279882",
   "dossier_in_9fd761c6d95fa491.md": "9df663e93bc69bfbb810face331002a9a8c22d486fded33967a7918df2c4bc1f",
   "dossier_in_a0f561401ac93ff8.md": "48f2ef7529961741db015224256b18a7f02dffe02bd067979999d850d80d56e4",
   "dossier_in_a0fed96cf539158b.md": "3c1c199cb2997660baf740b4faaf90aeca61a74ad5a59bb7e1fc808771211730",
   "dossier_in_a14a438b63f1f209.md": "bedd0507f7375f2e60bea880b540de2023b6fb08327490ffcc5ddf786814311f",
   "dossier_in_a176afd004a94ae6.md": "11c85b6f8d9a6c0c3f199aee50e49aac42de771659cdef7935a0aaeeef66bb40",
   "dossier_in_a1a952e51cf15977.md": "1f78b577704fc3fcc8b226c09035d13cc4dd1140617796dc9c5d63101fd7dc22",
   "dossier_in_a284ba67c95b290f.md": "a7a8df2d5bc3244940f9725dc39758265de289e71924f9e01d2c54a945434928",
   "dossier_in_a2ba2605ea35028c.md": "b5346d582c3d1e307c65401b0eca80b2a1cfa26cc17a0eb36af33e5524893a9b",
   "dossier_in_a2fdc507342694d5.md": "d1240192c7c5c2fc059ad1d756e289f9f59c099046c974d39e7aa300ec934b3b",
   "dossier_in_a43299dc3be16306.md": "3e26e0e61a9a45c04c5c2a1b22535f7b9732fb4e650311fb3536b8cc2b76851d",
   "dossier_in_a484b0261cbd1a40.md": "50c288dda64dede3860fd9b6db3d22cb2826313be15a607a8d46fa548e31e1e4",
   "dossier_in_a4fae13679569fd0.md": "ea987c8fefcbd28dbbaa9e3bf00bab7e14b85cb13540e949834b5aec2c478ae3",
   "dossier_in_a52ee266b287538e.md": "bae4c73632f2e64942c5f5951bfa9189e5a17b8328382651c103fad755dce102",
   "dossier_in_a53ac80ce97293af.md": "722249583e33a4d2343729ace83b3471119c3cd7ccc8094fbb1a20fbd6c584ce",
   "dossier_in_a5f21ca01f94fbe8.md": "3661a2653752e2de8b6a5223e85a03c3cc9a2d13780e18b68103920b3819266e",
   "dossier_in_a62898e154b1ee2c.md": "d91fb81d5b365bbd623e66f7d8512e90ed1fc1ce9a0625ca380330eb8534fb7a",
   "dossier_in_a74f74615905a372.md": "54de36e5ccbb5a41bb732e82f31a8de38c83937cb35e76f1752aef17e1e4074e",
   "dossier_in_a844a971e8a02501.md": "8c813ccf82d7e0bc52cb7ee01109d8f1eef576330d5a4394d02530a06f9b691c",
   "dossier_in_a9bffbf57e834ddd.md": "76a1d7e1ea66a85457601cd1d73245d45febb194764981953bc117794aacc2d1",
   "dossier_in_aa651cdd1983f617.md": "b9cea8c7a746b4eb9fb58ba457cc16d30e80a0c2c5a51031c7d039d06160d949",
   "dossier_in_aa8c27d893e8158f.md": "548483836af44809f74510d7dba909551c9f3ee82147d8622492376455ec0ebf",
   "dossier_in_aae7fc5a281cbec5.md": "74d6a9cd4d6d8a7ca792c92f7e671e7792be34fb66be3aedb7525a6be437c945",
   "dossier_in_ab33a858979bf724.md": "61d664f6e36788bb3adab54383a8fd33659a0bb921e10e5ed0e7e50438d1e10a",
   "dossier_in_ab8b47d7ed838ae1.md": "862b734e399e2d7cf864ade2281c400daf4db40b08f83feb685f4f659d91d282",
   "dossier_in_ab8de7558b7f57e7.md": "74cc3c9880581fa8c564c0fecee47b21b0f5473a79360e86bc9a0638a161d5a1",
   "dossier_in_abb03532c22040e7.md": "d53ee02dd7b8c3dfa31175eb5dfe8a7a3029ed1d753ef558f4884909a8622580",
   "dossier_in_ac1b969c4b9133f3.md": "6294ba70d36093d1a72f516bef0af3d0d7ce88b1fd0aaba1262edb4b5ad255fd",
   "dossier_in_acad8456489cafbd.md": "c008dfe69a8acd88cda7eba9c03460b6bc46d470b4a817139a81c1e67fb19412",
   "dossier_in_acf2652c65317ba2.md": "c2dcda49bf39c8024b44b0762375892ca76991b561efc2a41dc7eada57c38db5",
   "dossier_in_ad3606ce60fd670b.md": "d5c04371be96e8d16acd4abbe8f316f668da5f9f34e5ba5f9efc8c8c7a61ac76",
   "dossier_in_ada5bc6abee95dad.md": "b0c3306d91158be469077d8d3b1ef2d89dbe8b141f0c277a38acdaba660e5a20",
   "dossier_in_adaeb58f992a960b.md": "490186cdaf1a84a6ede0498a354b0b4fb73ab5b7134a3ce9c0238e29c315663d",
   "dossier_in_ae3c6506ded0a1ef.md": "aa33aaa7301ffdbb87c2abfe6ca3cd064f47624e5909a2bbbd1d8199cd7665ce",
   "dossier_in_af3d9850d6e30af3.md": "8ff4c02311620067afea8b077ac87d0a0a608497030a4b45ad3df178529eeeba",
   "dossier_in_b11adb0ecee4432b.md": "124ee0a5fc324bd8c4ef6fd0855bd1ab8142730586bbbb9dd9c0d43ba039583b",
   "dossier_in_b12a8aee0bd57888.md": "6689c912197c68522a2f6ad659ef60b84ac30d3bcdd830527352bfd27e3af5c0",
   "dossier_in_b25c1fcda0da8642.md": "c9b4b965d21519e0c0825a6f397ab0938820f933220f35f69413b383d629b43d",
   "dossier_in_b325a4f324e28653.md": "4a45f69f3aa02d52d6b0070bf6edeaa02e835575776c2f2d0355addedb83ea2d",
   "dossier_in_b3760ada801c1ba6.md": "ec02b0c65acd376e2a26afdae57f67a8710e02ecfc819a5ed0af730a004cf200",
   "dossier_in_b429a3f3d4de68b6.md": "b489305149171b65ceca84bbfe316c7abf70a42273230287183d9009aba36948",
   "dossier_in_b54664216d0759c5.md": "e3b4d12954d655e326f10b25ba65cbed8380312a128e61acaf5aeffeb4ffa88c",
   "dossier_in_b58787844ff4f903.md": "49de9dfc67befd99082e703d48a2c4bfdd6537fd132418a587f4e507a2cf8894",
   "dossier_in_b5db89d0c139cc6d.md": "0c1592b090eaa503c215ee5a321c9c100c600587b9896ceeee29ce0f343abb43",
   "dossier_in_b6085a1af6cb3023.md": "87e5fb5d34389193c074a36c4c5ac6b8414874ee0e1ad68aa7624863648f452f",
   "dossier_in_b6892a113238ad53.md": "48da02d491b5691b9bd16590bd965e358866fa9ce51b79ccc46f8c144eff22b6",
   "dossier_in_b706b2f00c5f5855.md": "ca113e7cbac0fbf3a16038fe00abf0cd52bd7e9ae7256061aaa92199eac7c9c2",
   "dossier_in_b7b0ce378a8a9483.md": "1bb986c5007998543aa4e098c713992628c3f5da55c337bb068f8fdbf1188380",
   "dossier_in_b7e6ed9ebaf8af79.md": "aa0e07b2b939b1bce3f2a91c99b5d6c72eb006d81c0f36a7b96627cd45ac601b",
   "dossier_in_b80c6970e12b028e.md": "84a6c28298507c11b62eead74b56812060868cd4c94f28b46668a02470ea693d",
   "dossier_in_b8b470e49d984c0a.md": "456ff22b3378866eae1d7066764613c138a4208e1da7a595f625438d0538fa60",
   "dossier_in_b8e10695c4cac278.md": "8ff0559c50b226f72e4fda40ac3be271cea279d5b5c1e37f8574cf764ad395d7",
   "dossier_in_b930f54f201cf45b.md": "5dbcc6dac7d72e0a7df35587b58468d6962bf54f005a788d776bec880509fa8a",
   "dossier_in_b961120f1b45d13c.md": "50efbbecc5a14df98068d7830422079a8f35d72ec83e612fa03d82e819fc7e50",
   "dossier_in_b98b699ec9dc2a10.md": "90a72c196ffb5e5b872c2de7bed05baa3f91fdbff80e1d6f36cc9bd5a2bb448a",
   "dossier_in_b9a3e68af3182ba0.md": "4ee03f69954a81aa1bfd9b27914d4617945d4066456d57cdb5590c0852276104",
   "dossier_in_bb07fa2f41093bd9.md": "f46a7888994bc7cea7dc9fd030e6771a1ab19ef53ed33e81f3413a09a13c321b",
   "dossier_in_bb61ab28bf974f35.md": "1c5b892be8e86d646ce6ec403ed96fb47fcfffebd61557ad65f931f79cce0dec",
   "dossier_in_bba6924a0a1a73ce.md": "50a2d49eb088e23ad04494eb1672eb17bbfc072e501c3f1a178dbd61f33f6331",
   "dossier_in_bc867b34d584d8f0.md": "761d8b109a1ce5645c749f20b41de3249423c3ca790cd57e69b09d7c568c684f",
   "dossier_in_bdf4a64fe04d041f.md": "8261fac3fc6206cdbc07b6ab59015928023c23b6cb22f2166ee9058fda551550",
   "dossier_in_be8e56e8a1025c73.md": "c1a044de7e178b6418918deb7605447e356cb5410b09fecfd88609c5fbcd741f",
   "dossier_in_bf1e58054cae6ac5.md": "8c97cd87ae9d329ca76e4234e9cd86cc1d394087fb1614d9de3af1d31365cfe7",
   "dossier_in_c16c15e7364d65aa.md": "59e81c73aa5770e94414326b04024157e5f902d8acd65beb93052f642d6ed453",
   "dossier_in_c1904842e523f4e1.md": "4c4d702ffbcfcede1a48c693dd6f71fcc21fccd6f23b152cab5f5924f8207bbe",
   "dossier_in_c2ddf30985e92807.md": "7acdf545135598b95c133cca7ecf1b43fb9c64d27ec8e55e38104606545a313c",
   "dossier_in_c34d5c21abffbc3a.md": "b17473cfb4a57e074095a31742149f930305e8f7280b5578443363e7a691a1cf",
   "dossier_in_c495810a19669428.md": "d1e0a6e55dbad1bfa5eea3ebc9bd644fcb698621bba418feedefe2217d41b53c",
   "dossier_in_c4dcf136280780db.md": "e34bfbfcfef812f2e78e36f65e84a0bedc8f0b1d30a8ff08257f4a0c473748c5",
   "dossier_in_c5f93e1ceb484010.md": "1b1cdc218d477cc345087799791825effbd51f2cb4f970847f00cdefb8d6910a",
   "dossier_in_c630283af9aec024.md": "9cc5c7ef8716cfb051bae4440751404de579dbd6718a40b82ad0fcf93e89d9b5",
   "dossier_in_c6560d15d3bf9009.md": "933beb74aa26b41c43d8dfc9651bf58e8c9a85bbd7050816d6f239f73f6e3a9c",
   "dossier_in_c71353f083b47f4c.md": "f649ecad089369dbda790fee2a8ddfc5fe02e28142315483d7793bb4790b6190",
   "dossier_in_c718f6020cd30b25.md": "3d81bffdc457fa6bc406c4956b7d40c22e9d3647c9e47125447b7d77145d7360",
   "dossier_in_c7d14516252197eb.md": "3cdc71015943724613e3e2f2449dcef93886dbe05d3d511f584d63ffd1ae1d2b",
   "dossier_in_c80f0009445b28e5.md": "1380bbd11ccb13aabd5369e6975bfe894514964884d0987398e6514759d0679a",
   "dossier_in_c903237331a2285b.md": "e5574771038c46396af3e79f19014e46e3dd1d24dfbb5e9d0f788627e65275be",
   "dossier_in_c938dc44ec554ffa.md": "853eeed45d92513be4525c9e2576b73024987fb2599b574ac6a4500a53b1c393",
   "dossier_in_c977544c7be1e5e5.md": "8d40393581d3ab96559f8b23e5a862e8da891281f3656a36ca166f004dc15b95",
   "dossier_in_ca2d6c8c694bb5bc.md": "5a77bcf4b06f0e6ab6263521f011ef5887ffd1b79539964b2232c44d6ca06dcf",
   "dossier_in_cb00b682c9f42bbc.md": "5b7ce3cf1e99befad8c95785cdc5d99c316fcab6546c2bd08c5b89f38c4aede4",
   "dossier_in_cc288a72341a4cc7.md": "3e5d3cedb8aee7cda3e7bd406d4756eb9b7008f95034b04a3a10d9aa1d952696",
   "dossier_in_cc54ba98e218882d.md": "a1f84f784e56ed9b152ac8e46808a7e0bd66ac2f9f5656cac71c8d582d863136",
   "dossier_in_cd383dbbffc4ba1c.md": "8e159bd8013d84016dd1fbf75f7ff392f32d1b0fc958121aa57b4c5a1cad02a4",
   "dossier_in_ce0f3a48a6c9b0d4.md": "3269a7d3a920c7e33b85fb07429715b65cdd5ca89fac5793a9f4f0f20056efb3",
   "dossier_in_ce7d6b1483e95ebf.md": "f38b44c68da7f72dfa68c7c1c35cb192ba459fddded09c7e109a3fe20bab634f",
   "dossier_in_ce9f6eb8b460eb32.md": "013c8a0858b45081356394d2e180050af0030191ba94116cc120a0d81b7a686a",
   "dossier_in_cef6f28f393b7f0b.md": "e409f60064fb6044dc04c3b620942032b6591e840a5b4c504c5f5605bf4d4237",
   "dossier_in_cfdce8cd30032c56.md": "7b38e5d44ba3af0ea8c46878b9246c6d70741b1894ab127ba9422f03bfe65ffe",
   "dossier_in_d090699b60dabbf9.md": "fc6cb2396309a8cff10d2c9d6b21f3427111322f9f2686628f1954fbba888dbe",
   "dossier_in_d12bbe215a91d9d3.md": "7850f7ff24d0cdcaa7b3a7b600d085c1fc0420ac4dc626c323bc1a4956de0139",
   "dossier_in_d145f604392bc0eb.md": "bbf05a7f7c74c81d6d7e8dabc3248e95eddc250715b2e21bfd80c52fa3909a28",
   "dossier_in_d22c195db5e66518.md": "7500c3d105d6da202254f605d0883689390ce53daf76576d1c6ce6d36c7b06f2",
   "dossier_in_d2680188eadf07f1.md": "fa76f553ccf92da44637f264655bd65dae015366eb08cfe4bfade7d2582ef33b",
   "dossier_in_d2783dc9c5ace68a.md": "b8073bdc2dcea315b763ebce801c0cefd443ba80d0455a4fca9e192bd99f427f",
   "dossier_in_d2933b1ee1796f7d.md": "63f34de5551fd09a4f94ea3a7dc3bbdc7ec5826b623bf59bf4959cd789e0b9cf",
   "dossier_in_d2c20bd2de28204d.md": "05ac78d75996dc08959b39457cf153c30ed25eae5d1f38d2b03758438d86e240",
   "dossier_in_d33d6573ea03f5c7.md": "591a2b72a745df7dbe7e8ce0065de5610ce315d94eac8d6f739e6b4d048f9cb3",
   "dossier_in_d396f67d1629abc7.md": "7b52c8df32adca17b955c7eb27eb7f5abdc7e0b8fec3eb9526bdf34a9d2eebf9",
   "dossier_in_d5a295e9f576a92a.md": "17918801938fed6838b18fbe302364a394d8935f138cb5e92f8d3cd253e3ae11",
   "dossier_in_d5c823bbb80e0408.md": "ffaa993db0bb86e90ed9656be9ec04cea14227cb294d6b215f1941b5eea3bd47",
   "dossier_in_d66718bf218d5ee2.md": "369337a95d5e08447b8a906520d92a919140eecb210be535a6ef76cb2042bb8b",
   "dossier_in_d7ad9e419f3d15fb.md": "a00cbb1a3e268934f4ac09932ea05c031020fc1545620eef28d75da9b4b74e90",
   "dossier_in_d7e77ffa49a1b885.md": "1065756d7bac0af07ca929f924c737e3ea55e5dd027e46cf5651e01080ec8231",
   "dossier_in_d9183cc15ee29d22.md": "2899affc1528ee3894df1ad8f7e27254117aeabc8ac5198dd87acfc1fe37577c",
   "dossier_in_d92a3125d346f24f.md": "4d039d6b572235890107ae9de7b2b43aa65b461314c1064d81b050da4689e2d7",
   "dossier_in_d9725cbbaa3b6e46.md": "aa1fd4cda09a7bb1a8faa4045744805147b16da3403bf039706b699d233f325d",
   "dossier_in_d9b90a7b6cce28c6.md": "d2a9ceae3665c622d7c4aebf93c097d60ed187fa4a1e14aaaba1f1b5e15d2ce3",
   "dossier_in_da0a777ded81ba57.md": "38bc27998d2aed5a475259af810970b96f8f1b36f437caa40cf06b8fa44db39a",
   "dossier_in_da0abae3963f7c23.md": "994ade7b6da900aad0240270b2e24ccb97979c4eb731164aa4d6495559518d9d",
   "dossier_in_dae12eac48350e35.md": "fbabed9a51edbb065c4773ec58b5b37c8c3e396df909880fb02895cf4e7619ed",
   "dossier_in_dbcd1cc68712c444.md": "a5b1affd9e382f816c13b8f87a8d13ee834b8884f8dad336760461983892617f",
   "dossier_in_dd1f277a4dade729.md": "decbcd67b69bb22c7f9c838a1df97b0f6317fce067605c7b4d9175001bd14b7b",
   "dossier_in_de2379bcc9c31514.md": "09a8a1a5b43c222b737eb879617f22d1f652930c11e7a4331ff1b2a87e866874",
   "dossier_in_de7ffbd4dba26293.md": "e8c13fd002046f74e9adb2a9c3ae250bc8108e760659c5cd3be10235763fddb0",
   "dossier_in_de8b02945b5c6a87.md": "16af4ab6dc1eee2759b88bd0b8e1bef28b39e279867500611950334d39263dca",
   "dossier_in_debb70211e1a805f.md": "10ac7e822a81a01bd873aa40a00fc249984e07a070b828b60dfce01e54ac63ee",
   "dossier_in_dec7f2d78d60c5ad.md": "519d62621c1e76eea1e84bcd98270982edcf0048032a4cc78d21047c75aed723",
   "dossier_in_dee3ffc211467cbf.md": "b924cb832682a332aa8e1980e1c8f70568bd84e0f1c07d2210da766b4604c685",
   "dossier_in_dfef2f83693d4be8.md": "bbfda1195086753247292ca3c3dd2503fb1d16a3dada523623c5f607c4da71c8",
   "dossier_in_e00d01dbe6360440.md": "5b1d7412222369d19b914c1c8fa3133231d0724514626529231403d565ca4b65",
   "dossier_in_e0a061e88ab06391.md": "7e54096571a30f387e648ed0b97f7876de962bc85a07d780778e677ca77c06c5",
   "dossier_in_e0d8b7e1929d1ad9.md": "806c4d3f316fc1c20dc692fb7f73afa7cae159ff077d1e89f25365d13a0955df",
   "dossier_in_e1808cacb9ef5ca1.md": "666e30c0d5f382ff58f954564e3e23678ff1c3c930216d7824c87d6a5ee949d3",
   "dossier_in_e21fdcf51aafeb78.md": "13ae6cf47e929205f438e040337fad6b6339af2e7f670207804cede401d041e8",
   "dossier_in_e2ecffcb3d78e548.md": "2d8d77b299c0e0d2fcccafccde6655ed37f4e3e1cf4373b254696c761683e10d",
   "dossier_in_e337164cf7996b29.md": "1370de088454297a4c719cb7300b1333ed763e6964840fe55d174d03d650bfd8",
   "dossier_in_e3e8af468a2bb522.md": "90b9eb3da052893dfc659147c560a9ff8b57d93cd0280573333a85b7b7b6570a",
   "dossier_in_e47b181d9fb5ccc3.md": "09a0c689180f3643f46b19031df7a1f0ea3c95195a98c7b279c6e6071351eb32",
   "dossier_in_e4a832e71104fbd6.md": "fc76a474e38a804ddebe2d05cee38dc7aaa42f1ca6f1bdab04013618497d8dec",
   "dossier_in_e5289e153d730107.md": "54ff65e5262da79020b18bea02b36f3c040d080195b18383a4dd4c722a5f3c2c",
   "dossier_in_e5f09d715932d2a5.md": "a9704c9f1e49b77646c42d34477557bc2ec8101d7b84c0c153186e00896b0c79",
   "dossier_in_e7490f859a8d5005.md": "9e238fe42b75f457828d67ef19646b1342460140cfd8322c8782a098bf1257d7",
   "dossier_in_e7a825391a09d732.md": "146f020274566f631f8887d4323956ca9289f49bcf049a51afd49e6450b5b0b1",
   "dossier_in_e7e78d421b2f009f.md": "e0ab9f57c392ae49c6e708ee410fc288243f4a58d6f4b9e7d9305bb89b80c154",
   "dossier_in_e7ff85f3c2d7e0e6.md": "e6ba4cd05071f0ad08a59055e77a56ce8f9bccc13007b9b3aae27e25f17c4775",
   "dossier_in_e8833709279a8a58.md": "a81d254f56ac90177a64a77e5f0fb88aa65a27d5c0c0f4a6da10596107295e51",
   "dossier_in_e8895aa2c2d999a6.md": "8fa4dc34ae3d88da3f3e5afeb078e52963ca4c8d4d0e3e3eebac311be26043b1",
   "dossier_in_e8c197be45b361cc.md": "0da417bb5c626ead5364043b188ed686a778b4733b1191ff2eb57d3f37f08b44",
   "dossier_in_e8de6bbe3b9197d6.md": "1ca65db0aa65445daf62e02c1bcc204bf110558117c827fd4f9523805d4b2032",
   "dossier_in_e8ff0daef2a100b1.md": "caee5d7d319e49ea806fb083f66084311368c9478c77d524da2d92fae8c83654",
   "dossier_in_e90fc72df16062fb.md": "755460e455689ca6b33f3c8ad5e752374971993c6a3f414ada3b9fc505e124d4",
   "dossier_in_e967e85c965125d5.md": "3f8e19f1cf012bce355e61414980840f292471911d898ab967d53affaa7accf2",
   "dossier_in_ea855a0fc5985ab3.md": "fde9b9575e9b74942f1e0780c484b42e0f1dc8fe3c73f9d22a14b6a2da732369",
   "dossier_in_eb41eb14d4960567.md": "5f91fc033779af6ea54825c146d23dcae918820ac20bcf326de6687710eb283b",
   "dossier_in_ebaaca672115a3c9.md": "09d58e3a4161bf44bf52a86c263f76c42dda59967fc9b95d06fe20fe6d6b2f04",
   "dossier_in_ebdd19aedc3eeed8.md": "dc10728680320cf4e497cf0368ba4baa51affc73efeff87b3ad02270393d70b1",
   "dossier_in_ec0de72a65527f79.md": "32124da98eefa5d462673e239f71a75d5e9efd69b37e710375765b04693d65b2",
   "dossier_in_edaa416494e15bdb.md": "20cd9615d95cc5bf17b5f998edd5b436bc7c1af858489340d447f49aa4296896",
   "dossier_in_ee056a2805f040a8.md": "aac2cee95fbbff6d566a172a62725dbeb0a0693cf04097d2c726f4910e44aa4b",
   "dossier_in_ee449d6b00b6289c.md": "3fb299255f498cc0d6fbe6250b263895f5e58b3332b9a7b05e7d8e1aae800a0f",
   "dossier_in_f0085c1e9296eed4.md": "69268a01bb183ac3f577c5e2a6440deda10b8092fe5f3b905661fc52dbf2f848",
   "dossier_in_f0fdb284a62b031d.md": "2423cd084e17d25e0b3f97f0978839f666d84e10243b80eadae86dbaf047b502",
   "dossier_in_f105a1768d2183fc.md": "4f6bb59a4f1982d8c9dea010d8f70e7a7c7697c80cdb53ae162b0dcba0a14065",
   "dossier_in_f1bed755d04997e3.md": "08f1ddd50a44b34f6ae972a33ff64e00cb0eadb6ced0c61c658af377a86ee362",
   "dossier_in_f1d62457ede9fce2.md": "956b5719ae6e97162e137b143f3e51581e3b3c9c526cf2f95be7adf0d80f599a",
   "dossier_in_f1dd15890b8ee401.md": "116f1442f7fa23bd1c6338a0b90fa38485b2023388a4d0047685427d34a0b875",
   "dossier_in_f23e4712dc360290.md": "c58906470a98bd9668ab93217e543b26a3b456a271f306c2a15cf9339375f6b4",
   "dossier_in_f2857d7cc6e9d746.md": "3a2d94c5569c6121e7187107e6d0a67d334959054b57b7048a9617c3534b8844",
   "dossier_in_f28caa087fb86fd5.md": "a052d46fd27736901d38593b103295723ccd77ad07f2d069abd18b230aa29c4f",
   "dossier_in_f2b155904e5ac3f9.md": "c57e3368d77b1cd144a04363912e4b64bc7a423d6265824fb4f9bba278832602",
   "dossier_in_f359a9121b7b2625.md": "ff4210ac17b161d3dbf5e50d34a317b69b10e8b6c04f00852fa4c327f708be44",
   "dossier_in_f36a7a9c57a48278.md": "da331253e8492487828ead6c4bf431d7f37c23a107e40dd8e73a02482e6e9d9e",
   "dossier_in_f36b79f42d56dcee.md": "12bf7918df007887ac4f669985fda509c7165694c66cf061f1ebfc4032845148",
   "dossier_in_f3aba6bff901bad7.md": "4d84aa07d52c508fc2256d284d28b2cacbdf55aacb447b43e889526563c4be0e",
   "dossier_in_f5a53bf1e27824c4.md": "f8da88945846c44e97d3976d07df7883556330c1656718d5a654f3fdcb68d8f5",
   "dossier_in_f6043ecc4257f155.md": "26cdc965bc0d8f3f34b429e57da0aad25bea4d1df10bb6f336edd3006f072fdf",
   "dossier_in_f68e521960f423ba.md": "e85f0540a5cf706241b9312f1625565e6f174af8252eaf9f82b9187478a8a73b",
   "dossier_in_f6955456ad00616f.md": "149a52d2eb8d98abd8bd391be85cd6da3f725ef3e23a79508e4ad8e695be89ce",
   "dossier_in_f6ae4c51a574a12b.md": "2f479cd225104d89c0670e911e3dd62b9cfc90164170a51d5e780f0a186824aa",
   "dossier_in_f6b25ab29ffec1e3.md": "0afce77bc4303d4e213cb94b3b3da4e931976bf221d6edd17ff41dcb7216b32a",
   "dossier_in_f738196596547bd3.md": "907a39b36c210f7e708a82b569b74695d8c830ae383315fc7d1c5660b7ce5995",
   "dossier_in_f7502d4691f36809.md": "1e64d7473f908bc9f1a78bb9d36228f588103cdddb56a866d209972c13f66326",
   "dossier_in_f78194f3f0d0148c.md": "b141d5f02ecc48c8ffe54f0cc486fa09d395366547ccac75f97593363c1b1cc1",
   "dossier_in_f78aea56ce3b1b1d.md": "bd256c16584ae89a62075ceb4acc2829e25841233b0bece599bceedf1bbeaf3f",
   "dossier_in_f7b28524c7ae8eff.md": "6cf00e03b66842657b1926cc96d4c91dc12699104f68e74306889cb300ac835e",
   "dossier_in_f801eb6de68249a3.md": "3971a6a69a4d5ae702ecb5e675084ee45d48dc5761938497055e44c2db072686",
   "dossier_in_f81b1141d0fe31d3.md": "f3b89ce164d531afedd085763d7934d67e7b10e707d8903e87d9cba7d527c625",
   "dossier_in_f88979e695cb32f9.md": "58ab390fcf5306282975b828fdd450ed27aa69dba77762a10397f59aa2b856d5",
   "dossier_in_f9502be5d0870967.md": "77bd704159d8bac431031f698a31c7066dd4f7d5238ba4300d80331086325f79",
   "dossier_in_f9fdef7de1d31ce4.md": "208f3dc91d50deb3647fdb8f70edd63b41feb127e64fb053b80c8af1b5b9a896",
   "dossier_in_fa1d78a3a7a378e5.md": "c3be3f4d9a120a6396c2d42b840d78b1b17553c7e124d6ebbaa942ee83cb4912",
   "dossier_in_fa2edb2b41f56b32.md": "05c940029717f6f01a5fb14bdf0a54f55582ae125695055e7d4f8819f0f7fed3",
   "dossier_in_fa74afd2a1b5f2aa.md": "4c63ccb91a3303d15e666276a3679bf705fc0444e52e2a2c1e21347150768d21",
   "dossier_in_fb3dfda226e10e69.md": "b74a0f6c44f5482a246f1b98d3fc982c9c7cdf8cd2e4c202e6f8a0cfb8017227",
   "dossier_in_fc2629fe65d09642.md": "3991a28bcd3967113f250514ef629c3e72ed68b115749ea3f16e30ea4419a84e",
   "dossier_in_fcb1c5a4d3402a53.md": "9d618a821e48ee4fee8823dfbbb42e6af15f1df5da57b7c1cfd676e8c951db93",
   "dossier_in_fe252fe46c0694bf.md": "91d3695c33447195f7dbc70db0c4a524b59b0e036e3e4aa6479c80ff3d5e95df",
   "dossier_in_fe3a638f95ab8b59.md": "b85b5e41f7960d9f7c4fa8939bff5b1b72122cc9a335a1c725f5086a718f13e1",
   "dossier_in_fe545696400ae80b.md": "4d940d1b4fccfe23722dab9d1dd12245b2fd4e0560afd0cd9cfeef08d96c9280",
   "dossier_in_fe86f7c3f828e52d.md": "f6f2df81ad8f4b919f1eb3f48f67bf5fb12cf83e98e3acb79d482a62e50ec313",
   "dossier_in_ff7ed636460b2ccc.md": "0587b6ede5c3cfb08343d5f30fc9fc96f286264bfd1059f1d9aada10716cd277",
   "dossier_in_ffc294d95bf1fcb9.md": "d1b640ee0e800eec1008c5994f8cf9c74fe0b3efbe6e2424f9158f4b5165f1a8",
   "dossier_in_ffd48a6c5d98d8cb.md": "4493c6dc81bac643de8752dded830302c21fa4d1c560a063a4417a363dd4ef66",
   "extrapolated_in_0026162272593cc4.md": "dda69f30cba8c01ae09177f69042e288c1336049a43ca0db032afc58064de9a8",
   "extrapolated_in_013c081f98831b12.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_01f37d3b848c6c61.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_023518b65456ecb3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_02575581d0ede86a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_02622e540fb9b9b4.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_0274f98af64e2795.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_027ea0e3da321447.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_02eb6b6ac560997e.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_031ec517c6b73b69.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_04e4ea15fdd7f357.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_058ad3f9740154ed.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_05eb7f9b57c0a291.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_062da72a2e3586ff.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_06694cfc633237ae.md": "184fb78042e8e7a1de54a157cc8429f66138abce1bbfc3d8bdac571367553cc9",
   "extrapolated_in_066bd4ae2b2235d6.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_06726548fbb905a5.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_0697bc02afd1da02.md": "0333c4443ba2a778c86666ca8b1991442317a1e8359e70d11b02ca3cadbb2135",
   "extrapolated_in_06e7c3427bc6329c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_06f6520c952f56b9.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_073bfbd860371ff9.md": "184fb78042e8e7a1de54a157cc8429f66138abce1bbfc3d8bdac571367553cc9",
   "extrapolated_in_075b04630573cca3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_07ab28b8b67275a9.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_07be289fca280fe5.md": "dae64af147fdf9d08ef5d662d82611a49937081af7d63e0d9bff5443a8285f6c",
   "extrapolated_in_07f4506756ed61bd.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_07f94b41166b5125.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_08484bf85fe6a4a5.md": "44763d727ab46920848585e5380327186e35e5df7963e047a1c208a35c8635de",
   "extrapolated_in_0941aeb4eb810f0b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_0952a3d8ffd12734.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_09682da555139b03.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_0accd247ff889bce.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_0bc4b4e36d8fe878.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_0c17e3dc46e12795.md": "7441326eea5de0ff908d46e4aef675aecdc53c8008a048bcb7ab3af84630b617",
   "extrapolated_in_0d178b7d32efae15.md": "c684445a714174d30f03aa04ed93780902e14e1a8c81506154559d4ed12b03aa",
   "extrapolated_in_0dff362d9672e155.md": "63133d6302d9c0b04536286c37010f55de9cb816993c97136105843d30146e42",
   "extrapolated_in_0ee72e6fed85b72b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_0ffe7c42d12ed0b2.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_100f3811f2627b4c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_101da3eb16a3ca55.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1051b65ab29e18fd.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_11299b273770e8a8.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_113e279b7bbdffc3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1166c67282b8478a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_11f7e70cac1ca142.md": "7391ffea4b5aea59d41592f19c34fae60b52a4d502c044a758bd745ee6d75aeb",
   "extrapolated_in_120d7ac1cf88e79d.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_123e3dc3424b83da.md": "792c138a656a178f577d56a3b873c223e93311f3aab532e115ca793d8968b83d",
   "extrapolated_in_124ac8c06c14c05b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1331ff07041d8c18.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_13ee1293e2c76c86.md": "d4d687513a5c1d704a4c993dc346121f71836146bbdff6e12afaa14234ff4154",
   "extrapolated_in_148a6be15f50370d.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_153f92aa786c5e59.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_15fde14bf12d85f8.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1669b84323c01bc1.md": "3dba53b9100a92ad7aa36c79b0d73186c49234259209fdd7acaa744dddc927b2",
   "extrapolated_in_1677bb654640fa03.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1682848b3c021fae.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_170e11e068b0c55e.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_17f8885215472d8c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1872378afb60aee2.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_191fab9b3de52ed4.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_19558cbb04548669.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_199191dceae1e331.md": "4bf8a86e8c6bdcc47de8fcb4fa66c84d55d038ea106ae861ada36bff55787676",
   "extrapolated_in_19c37698889ab195.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1a2a73ae686ff915.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1a46d462a1e589f2.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1a6519bcbf50d20f.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1a78c4d7ca2904c1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1b29d7d9067f8fe6.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1b645f8cbf741237.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1c26356f95e26193.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1cca47f3a0d7589c.md": "b1886a6faf8849d8f0b0a752b80b2048469b96f5b20a123b4906dcc41c6e3e94",
   "extrapolated_in_1dd3ec7378f4f741.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1ddefa02433b6328.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1e3aa11375b0029b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1e85d3d81e10785a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_1efb3f841ed68f2f.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_2423100566953142.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_242c3e4d12729091.md": "eab02b4c341aa0b5cf87049814f94b36dd8636b1f0066b78bc76d70fd5f18569",
   "extrapolated_in_2456d72c1efc6e28.md": "54a388eef6c96c97c137a0352e39dcaa1b19bc983a83c53dba9bd9f1c72de833",
   "extrapolated_in_247f5e7806e14a91.md": "79f4bc8e27a5ed5acd5d6e40138a42033ad9afbe8695352a27d2d42b1fbb83a7",
   "extrapolated_in_256405e2f067092d.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_258c67251b2fb1fa.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_26cd97167a86ab2d.md": "e0803afeff7b8bc67ab44197cf5cad1cc718f288b056767d25a1c907da70720d",
   "extrapolated_in_2749711e98c02f1c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_2802febb83347740.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_282eb8628c21cdc6.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_289a24d239ed7df1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_28a9b2cba9e3edb2.md": "43da61dd2a392a508c66cdb2b90ccb9cb225ab0adb6557f0c9c36df198aa42aa",
   "extrapolated_in_28f7ff3b4c5d12a1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_299244ef22a3cd70.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_29ed9d359c37b513.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_2a89a333a941c116.md": "3409427a7a48b7b4db033bc30626654ff573745056981459175855446952e0c4",
   "extrapolated_in_2bd5d0cf7b557cfa.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_2cccb617b592628c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_2ddfbaa645f52562.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_2de616af1bdb1d3a.md": "6212172f03d978246209843400575238609bd9b2350f270f8799ee731c1213f6",
   "extrapolated_in_2dfb10d3b3e093ee.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_2f5401f3c0f1dbf8.md": "dae64af147fdf9d08ef5d662d82611a49937081af7d63e0d9bff5443a8285f6c",
   "extrapolated_in_2feaf2f515e4b9aa.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3004aab1769f9842.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3032637ea5e58c7a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_30a096b928fd82f6.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3139a6326a607d88.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3171a851dd10fc60.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_32ddd9891bc7d2c4.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_33a37bb3cf24d799.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_33bf855c7eb30203.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_351d7fd9dbe35dec.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_352c8765e69793c1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_35cf050ebeb8c4e2.md": "ce35ab9e5eca3b9b439abd3124a1f8c7e6834e77dad5d67c7e89149d6b814fe2",
   "extrapolated_in_36bda3d72e664b3d.md": "a0815a02a22ccd3381e636b6a21a60f778f6772698dfc57d57d69eed3805985a",
   "extrapolated_in_37419f3c1c7b8ae5.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_381a2f4af1f7b1b0.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_386fb556b34f47a5.md": "1e803e873c0e9d0e93349d7f278aad502f51ae441233e86d80af874305c8f895",
   "extrapolated_in_38f2897e6b1ff340.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_394d15bb29383ca5.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_39e02f3b2700adda.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3a5cd13c78628442.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3d6ea502155c6da7.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3d706f289cb8e6c4.md": "54a0d6f1b5c105bad5f13eeb150e2404128b18eb9b212590a1fba294a8da8a2b",
   "extrapolated_in_3dfbe1e6ab56e933.md": "1f158aa75f8bb724cfe725d6569ba2ab247489ddacf3e7ddd1f1dff755ba893c",
   "extrapolated_in_3e53a18d4cb2aeff.md": "1430bffd4921907616464ee6a5e9fc48b35726f3dbfdc54ce41ae3ab9e1154c6",
   "extrapolated_in_3e6f8f6040b3c94c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3e8f73756574f677.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3ef258320c6fce0c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3f56ed70a45fb8c1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3f90e55c21382f1b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_3f96c955361d154b.md": "ca2b11d450f97a676c2c56253dfe8e2882b14bf92caa6d4c8290269cdcf3af60",
   "extrapolated_in_404bf91d2a9b1239.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_409494562383fbf1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_40b00fd6aeac3847.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_416fd06afd93a5f5.md": "599396fb321a9ea8982928fac8f1cf3c0cd229b43705e076ac5967bb5b703aa0",
   "extrapolated_in_41740d4abbd03e37.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_41b46fb346923603.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_41d7776f4f341efb.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_4220aeb754f711a7.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_4275ea8f42aa6f00.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_45278f72dc0c7362.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_457ca2925db0c133.md": "599396fb321a9ea8982928fac8f1cf3c0cd229b43705e076ac5967bb5b703aa0",
   "extrapolated_in_45d0c754083c48a3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_462167360ebd21ae.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_463b2e98ce0a4b57.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_46954b4a93c30adb.md": "c264eb63ce2c8fcf67055bf023c16674c9dfea1a4e7bd41db1eb5850769f9acb",
   "extrapolated_in_469dfaeca94101a0.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_46d0f429abc89e8b.md": "c8efec72c37355dd747cc466420eaa915bbcdea23542f2ac947e0682dd22891e",
   "extrapolated_in_47327a0d5ff19355.md": "f30df37583f0afb1f8a5c30ade44e1fab011f112a962903022a37ed25563f7d0",
   "extrapolated_in_47ed9578befb0f79.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_48019d7238bc3c8e.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_489274750d1dda34.md": "2a65b1e6fa5481d754df68c2ed1d76cc1df17c560fc9c188e2038a86fee331b9",
   "extrapolated_in_493a55c85bcb0c8c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_497c8bbebb1aa6be.md": "3409427a7a48b7b4db033bc30626654ff573745056981459175855446952e0c4",
   "extrapolated_in_49be39fa241181c5.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_4a03037d07f7d5ec.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_4bfbef367d03e866.md": "3d7b5e3e73d9c3b9782beef16df6da87d6f5b5e37f6561ac417bdb189f2273a6",
   "extrapolated_in_4c0f39039abf65a6.md": "f5da73a22af47995b856f4548d8db9a1f17ba25f52b172b65b6cb287616165d4",
   "extrapolated_in_4cb2d2b8df14a684.md": "857a313b7d96958356f9e5b2101b969e073aa72450676a7bbf919ebdcbd7c304",
   "extrapolated_in_4d6cc47fc31e4aa5.md": "d6e092ed9b1e134a8b36652a2494146fb8a8c990a7dd1bf7d897c1a7d0efc1ea",
   "extrapolated_in_4db2dd1c8fa50846.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_4f8c5a3683cde4cf.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_4fbb1702a6eaad73.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_501d95f30476d979.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5038e69347cbcb88.md": "9357cb38c90f093f4f8381cfc1d562ce6290b886a4a8a89d3a4c8bb4e6512bbc",
   "extrapolated_in_504ae54ed6b53fd9.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5073b724ead05666.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_50d26a03de3051eb.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_510a44a949d52753.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_517125b1101c89c1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5187a87da755b16d.md": "f7e480a7aae1cda1cd742593a925dd25288c4f13475a74d1b83f836a28a5480b",
   "extrapolated_in_51cc919b886ce146.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_52064d62541d133e.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5224c56a68940b97.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_53ff22befba31a63.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5497a9cde9016f73.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_54efff9de13db555.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5510be7ecaed0af7.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_551a9dd0acb7efd5.md": "dd505ba70291d173bd350707f53eedac71bbcf545dd01c21e1e864d0828788fe",
   "extrapolated_in_560e1eb6b10801a9.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_562ed79277156ff8.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_56aa2d2d80ea71a7.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_573a7c118f985289.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_58847c03bce522ce.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_59e5b5b17a6cf72a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5b50e1ee84ab5a26.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5c24928675d4467f.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5c6d75f40613e741.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5c8db826c5f68b5c.md": "3409427a7a48b7b4db033bc30626654ff573745056981459175855446952e0c4",
   "extrapolated_in_5c9feda898e59c3c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5d83a74245acc6e3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5da4795d7e632fba.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5fe0fb7931e8fb7c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5ff36a389cfce72f.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_5ff3ed0fdc240405.md": "b91dd972ce39cba3b52d3de9ebb4e3cb706be4be87a5c107d229ab8e5a63a5ff",
   "extrapolated_in_61233b7beb48815b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_6149cab91d409313.md": "47b79ff0b65101480a069cf8c32750b87e2d2d17ab3ca93335481a1534c59ef9",
   "extrapolated_in_619232fa0fc3e362.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_63b70c6edd29b0be.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_640b9750133f5884.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_65ebbdf744a6817c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_6675888e3c53ebd7.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_6697833edfe831ae.md": "11f00e4532a58fa5c8588e92919124d9bb4a59bb1473677ffe2ad0941cb3ca02",
   "extrapolated_in_68183951b6e3fbd0.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_68538b70c7bed9de.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_68542789dc746d39.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_695101ce7fd71dd8.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_69aa0204fbb6079f.md": "54a388eef6c96c97c137a0352e39dcaa1b19bc983a83c53dba9bd9f1c72de833",
   "extrapolated_in_6a2f0bef8f639b71.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_6ac102447e1148d7.md": "792c138a656a178f577d56a3b873c223e93311f3aab532e115ca793d8968b83d",
   "extrapolated_in_6adec1773b1ae43c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_6b88387753838b1c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_6b9a6110d47121bb.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_6bca218f1e251559.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_6cc03da768b979c3.md": "1109209c76438e84b01b7203c3fa109eb33815c146cf2a63d5fe1b3678a865cd",
   "extrapolated_in_6e5c3d4bcda5c493.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_6f1dd50c4bc51465.md": "084dbad29d42ed047c9a62413de35cefde680c59bb9078cbad525833b79751fa",
   "extrapolated_in_6f6328f5c2eaf6fc.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_6f78628d953cac3d.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_6feea031c7aa47cc.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_712f0d21ffa22a4a.md": "f36f4af6f4f09a80d01232c568291d4d6708e4d42512c684b7126ba74e630271",
   "extrapolated_in_713b6f62d8b57ae0.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_71b8d196c6eb34c1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_723ea883248ec983.md": "7fb104dcbef4c986442c3ff8ba6817943e5de7c26fde43c3a01ab271b1b89343",
   "extrapolated_in_73e607c5071aaa1d.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_73f065d03aec20d9.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_74273c7c5df1bb31.md": "f538fdd101517d3d6f3bfc29e90088bb0873687f44f0e5f551b3bd178b2b7cc3",
   "extrapolated_in_74331233ec6fcb59.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_747495db2a127593.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_7588054b29ef3bd8.md": "1ad1941e223df523795e169280de981911c220e1a7d30e16f83744c324a8f35e",
   "extrapolated_in_75e0c0e20ce46744.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_76b7d5ad127ffb1f.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_7762d1d17feb7542.md": "55ecd6ef24cdda3a53b5fa4357cb1b42cebc55c9a5c1e7d1b287c962a0013ebe",
   "extrapolated_in_78ca4e9ea72abbe6.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_79aafdbd08030480.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_79dd85e653cf7014.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_7a3ce9a222036d7d.md": "280baba3bad640cc4b01e18e2302fa8007ab40c4e42cf91bac7a6dec306880d3",
   "extrapolated_in_7ac132da9d210410.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_7b7fa0d4dbbb16b1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_7cce2fbcaf718583.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_7cddf92c7a8414b4.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_7d4824d25f78ef0b.md": "eab02b4c341aa0b5cf87049814f94b36dd8636b1f0066b78bc76d70fd5f18569",
   "extrapolated_in_7d64af28ae699ac1.md": "ddc76aaab5a8eacf7be34123c1acf891ed191f5dbb5579d1156f2120b2dfa1dd",
   "extrapolated_in_7d6c1a509544da8a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_7dd05e2d32221f60.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_7dd986187ac026b4.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_7df2d9eec9b87282.md": "9ecf6400dd4dd95a456f86769cfdac7513ec80b153025e52015b8da299641f60",
   "extrapolated_in_7e9358ac48041a6a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_8061ee1b0621a79a.md": "7c3d716445ccf81c9ffe04ac2cd94571520809363b5cfd4723e0d8f1ea6c0ea0",
   "extrapolated_in_8198e16cfea88109.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_827fb64894a6a2ef.md": "97470988c3ab3ff901acd77620794e7ec6588ffa7206be5121e6e43c5750b186",
   "extrapolated_in_834aea25df20b4ea.md": "c75641125dfcfb4c5349d0871a60c50cc6d9509402c9801ee0cb469b69db4371",
   "extrapolated_in_83ad6e3c65d75bed.md": "c684445a714174d30f03aa04ed93780902e14e1a8c81506154559d4ed12b03aa",
   "extrapolated_in_83d36d08b14453ec.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_83ec0b583795eca3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_844c621a81f97078.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_844d4c5c455c4557.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_8537d4576a28c027.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_85f6fd85fd809e56.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_860b217dd95768dd.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_86654146a0023183.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_87497381b5872c0a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_8864ccd6ea39204c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_886845c73e271558.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_8891ff046b6a4f04.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_8910436ffcece3fd.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_89e2d9d2be8398e1.md": "50ea733d4ab4b970c36dd2fbd49e9df857e6e4686eb275d35b6321d7a0c1b55e",
   "extrapolated_in_8a2b10537c0af845.md": "352a43d0a75b4a3403eb16cdd27dcf15ab42fba6070a682e13eb25d316d3a58e",
   "extrapolated_in_8a511a80dac5b020.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_8ab1aa11668d4d7e.md": "ac978de74da881dea0a66514bd029dfda0053a95eb6f076764818d8e111a02d3",
   "extrapolated_in_8b32af0554186249.md": "3409427a7a48b7b4db033bc30626654ff573745056981459175855446952e0c4",
   "extrapolated_in_8b462e3b294da476.md": "3177fbcd1f5c6afda6680cca2af3425b1d955cee6906ec0fb167d1515ef13164",
   "extrapolated_in_8b59d7034c50a01c.md": "3409427a7a48b7b4db033bc30626654ff573745056981459175855446952e0c4",
   "extrapolated_in_8b9a339432bc188e.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_8cb747aca050e3cd.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_8cf2e62f8c237879.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_8dba3a0b0a2cb6a1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_8ebeb20393602abb.md": "ae60cc736ff18e17cff1815ba26643186e0f4f120bd41da8c10cf90d99d51ff3",
   "extrapolated_in_8ef4f90ad55edd5e.md": "3409427a7a48b7b4db033bc30626654ff573745056981459175855446952e0c4",
   "extrapolated_in_8f008d3477240b9e.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_8f1f797cc5cca8fa.md": "3fa98e6beee696c02e6fbd3487c829dc962b319590be19b840fcc27d422faebf",
   "extrapolated_in_8fa52491e6d93572.md": "d4d687513a5c1d704a4c993dc346121f71836146bbdff6e12afaa14234ff4154",
   "extrapolated_in_9034172be7ee541c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_90a8c34a596b9ce6.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_90b48548dc10f8f9.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_90e268a0e954af45.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_90ea2e14eba0755b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_910c61f9ea9f6600.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_918490f6f731e7eb.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_91ec8588a8499c46.md": "0ae321337c325decca7a00b9e51cb2d37a6c9a41ab341f907de55340d6ee3e35",
   "extrapolated_in_927cfe44cf937d6b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_933b8bccff03b18f.md": "3dba53b9100a92ad7aa36c79b0d73186c49234259209fdd7acaa744dddc927b2",
   "extrapolated_in_939b1580f1092938.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_9675563800eb6014.md": "e0803afeff7b8bc67ab44197cf5cad1cc718f288b056767d25a1c907da70720d",
   "extrapolated_in_96dd74d3fb478b31.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_9759155d0e77331e.md": "68b22adf8eea2499a7baa27000365722b68b3e39ea4509a7b10a9279e234189e",
   "extrapolated_in_98074ee46b045498.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_989677c8268de647.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_99844ff2502e9247.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_9a4bf7a9bdb7ee11.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_9b37b6cf89bd1dce.md": "9357cb38c90f093f4f8381cfc1d562ce6290b886a4a8a89d3a4c8bb4e6512bbc",
   "extrapolated_in_9c4ea0e3cca4f495.md": "1430bffd4921907616464ee6a5e9fc48b35726f3dbfdc54ce41ae3ab9e1154c6",
   "extrapolated_in_9c6923a9d86bcdc1.md": "e8486d4791a2282dda269cb1dd0bcf0180a635939e0d60c85da422169d304c3f",
   "extrapolated_in_9c69a3fb724b50c8.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_9e2afc6a0769e561.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_9f18901eeabf13f4.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_9f3d64eb150a5de7.md": "b658fb7ac472ec1f1e4b1d5996a8705817b8e1d9d85aabaafbb2d4f6b4b7a600",
   "extrapolated_in_9f4755eabdde86f8.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_9fc138be8c3c8ca0.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_9fd761c6d95fa491.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a0f561401ac93ff8.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a0fed96cf539158b.md": "bc237c571f7c7b73379369f1bec16287625e5c27b29a76b126184629a68b35e0",
   "extrapolated_in_a14a438b63f1f209.md": "33e0330586cd1091e19b8aa032e40e92e18c3af9bb48e7e334793d8043a4130b",
   "extrapolated_in_a176afd004a94ae6.md": "a0f7b55e67a998ec98666aed170655b002b6253531e847eb859c665dc5b1f595",
   "extrapolated_in_a1a952e51cf15977.md": "b7cf7871c760e34ce6dc0d918744d73a59fc630da787ffbcad1eac3f77930df6",
   "extrapolated_in_a284ba67c95b290f.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a2ba2605ea35028c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a2fdc507342694d5.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a43299dc3be16306.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a484b0261cbd1a40.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a4fae13679569fd0.md": "6ad6d5881fb062cb9783b3860ac0694571525e03b160e99437b76225f53d5ecc",
   "extrapolated_in_a52ee266b287538e.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a53ac80ce97293af.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a5f21ca01f94fbe8.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a62898e154b1ee2c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a74f74615905a372.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a844a971e8a02501.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_a9bffbf57e834ddd.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_aa651cdd1983f617.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_aa8c27d893e8158f.md": "c62b13c9ec6471cb7c1a59ed9effc523111b5a935154cd80792a67e8f689fc58",
   "extrapolated_in_aae7fc5a281cbec5.md": "c684445a714174d30f03aa04ed93780902e14e1a8c81506154559d4ed12b03aa",
   "extrapolated_in_ab33a858979bf724.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_ab8b47d7ed838ae1.md": "bd86cfa6d6226cd461a18304832560414cfeb7e9ec65cbad18c69e424ab677e8",
   "extrapolated_in_ab8de7558b7f57e7.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_abb03532c22040e7.md": "b03090aa7701ecb3f6f5651685f9b14beea0e14ba7d3436ceffedc5230b91174",
   "extrapolated_in_ac1b969c4b9133f3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_acad8456489cafbd.md": "39660fff6c7af4dc60468f3bd67454f1f1891aea670de0ef9ae985ed05518863",
   "extrapolated_in_acf2652c65317ba2.md": "7ab0814c409d94c97bf5026d8a1042b5054c57cde67d93fd4db6edec4b797f5e",
   "extrapolated_in_ad3606ce60fd670b.md": "41e6540fdfd2fee774a149d650b163195de2d8a7ff0bfc70e5305c784c302938",
   "extrapolated_in_ada5bc6abee95dad.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_adaeb58f992a960b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_ae3c6506ded0a1ef.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_af3d9850d6e30af3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b11adb0ecee4432b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b12a8aee0bd57888.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b25c1fcda0da8642.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b325a4f324e28653.md": "0f28aea6b516deeec7c2363417db69539879d73db8b0d65af7676553b2ded4cd",
   "extrapolated_in_b3760ada801c1ba6.md": "237951bc3780f5d39ecb715daa924d6761999ddb2f6255b41fe3583d8430ec30",
   "extrapolated_in_b429a3f3d4de68b6.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b54664216d0759c5.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b58787844ff4f903.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b5db89d0c139cc6d.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b6085a1af6cb3023.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b6892a113238ad53.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b706b2f00c5f5855.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b7b0ce378a8a9483.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b7e6ed9ebaf8af79.md": "1430bffd4921907616464ee6a5e9fc48b35726f3dbfdc54ce41ae3ab9e1154c6",
   "extrapolated_in_b80c6970e12b028e.md": "f31e6a2563d3c053b9963117dbfbc6719b0bbea3fd78c1c93748897ef2488191",
   "extrapolated_in_b8b470e49d984c0a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b8e10695c4cac278.md": "49dac82411c7137a859864b946150c2fbb34ac4da2e0b6611a700d9ee4773c23",
   "extrapolated_in_b930f54f201cf45b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b961120f1b45d13c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b98b699ec9dc2a10.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_b9a3e68af3182ba0.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_bb07fa2f41093bd9.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_bb61ab28bf974f35.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_bba6924a0a1a73ce.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_bc867b34d584d8f0.md": "9ecf6400dd4dd95a456f86769cfdac7513ec80b153025e52015b8da299641f60",
   "extrapolated_in_bdf4a64fe04d041f.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_be8e56e8a1025c73.md": "6c9eae622d251b27f864a2c37cced50df0427663987266b0a4c0090ffd4b50a4",
   "extrapolated_in_bf1e58054cae6ac5.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_c16c15e7364d65aa.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_c1904842e523f4e1.md": "6552c0596b9091b564c4981c2cb64ee11356de7f77da6890a8d6bc9852ba6466",
   "extrapolated_in_c2ddf30985e92807.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_c34d5c21abffbc3a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_c495810a19669428.md": "f30df37583f0afb1f8a5c30ade44e1fab011f112a962903022a37ed25563f7d0",
   "extrapolated_in_c4dcf136280780db.md": "b9ec6d626cccccd3e9db44ca0e91bc6487ef8b3f3c146c9ba5410850400a0ee2",
   "extrapolated_in_c5f93e1ceb484010.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_c630283af9aec024.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_c6560d15d3bf9009.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_c71353f083b47f4c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_c718f6020cd30b25.md": "3409427a7a48b7b4db033bc30626654ff573745056981459175855446952e0c4",
   "extrapolated_in_c7d14516252197eb.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_c80f0009445b28e5.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_c903237331a2285b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_c938dc44ec554ffa.md": "37065e571062ad54b72a0e012b58ea032705d7fd8e10702b68bee0a3a768ceed",
   "extrapolated_in_c977544c7be1e5e5.md": "3d7b5e3e73d9c3b9782beef16df6da87d6f5b5e37f6561ac417bdb189f2273a6",
   "extrapolated_in_ca2d6c8c694bb5bc.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_cb00b682c9f42bbc.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_cc288a72341a4cc7.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_cc54ba98e218882d.md": "b658fb7ac472ec1f1e4b1d5996a8705817b8e1d9d85aabaafbb2d4f6b4b7a600",
   "extrapolated_in_cd383dbbffc4ba1c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_ce0f3a48a6c9b0d4.md": "1b5a1ad5cb5020aa7339d28d0cbc25a57b3111396668d5b957b24db96b9a5adb",
   "extrapolated_in_ce7d6b1483e95ebf.md": "9ecf6400dd4dd95a456f86769cfdac7513ec80b153025e52015b8da299641f60",
   "extrapolated_in_ce9f6eb8b460eb32.md": "580083585f25b7c02ef64dbe3e629c7253dcb452de9174a0d8c4426b218aa4c4",
   "extrapolated_in_cef6f28f393b7f0b.md": "0ae321337c325decca7a00b9e51cb2d37a6c9a41ab341f907de55340d6ee3e35",
   "extrapolated_in_cfdce8cd30032c56.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d090699b60dabbf9.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d12bbe215a91d9d3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d145f604392bc0eb.md": "64bb2321079d93c79c2ce95c7398b6fdb34611b5ce4e7f8a73614c643867b6de",
   "extrapolated_in_d22c195db5e66518.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d2680188eadf07f1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d2783dc9c5ace68a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d2933b1ee1796f7d.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d2c20bd2de28204d.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d33d6573ea03f5c7.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d396f67d1629abc7.md": "64bb2321079d93c79c2ce95c7398b6fdb34611b5ce4e7f8a73614c643867b6de",
   "extrapolated_in_d5a295e9f576a92a.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d5c823bbb80e0408.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d66718bf218d5ee2.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d7ad9e419f3d15fb.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d7e77ffa49a1b885.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d9183cc15ee29d22.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_d92a3125d346f24f.md": "f538fdd101517d3d6f3bfc29e90088bb0873687f44f0e5f551b3bd178b2b7cc3",
   "extrapolated_in_d9725cbbaa3b6e46.md": "3122751e5ccdf9c4a09fc2bd6f676719de0b4ca26ef0f52a8bf1826e96bb5012",
   "extrapolated_in_d9b90a7b6cce28c6.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_da0a777ded81ba57.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_da0abae3963f7c23.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_dae12eac48350e35.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_dbcd1cc68712c444.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_dd1f277a4dade729.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_de2379bcc9c31514.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_de7ffbd4dba26293.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_de8b02945b5c6a87.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_debb70211e1a805f.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_dec7f2d78d60c5ad.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_dee3ffc211467cbf.md": "4f600a5b0460ac06d38a11646b966f6bf5d1536209c2dad2ab0fec9a11fe454e",
   "extrapolated_in_dfef2f83693d4be8.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e00d01dbe6360440.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e0a061e88ab06391.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e0d8b7e1929d1ad9.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e1808cacb9ef5ca1.md": "e5750ce7abab6adc372a2630bcf29424858f5a4341684b449ca0fcea5252139c",
   "extrapolated_in_e21fdcf51aafeb78.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e2ecffcb3d78e548.md": "423941ea68707e1e9ac2bea3d1ad6deb98ac6d2102ace9f4f29623b8a304b8c8",
   "extrapolated_in_e337164cf7996b29.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e3e8af468a2bb522.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e47b181d9fb5ccc3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e4a832e71104fbd6.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e5289e153d730107.md": "ac978de74da881dea0a66514bd029dfda0053a95eb6f076764818d8e111a02d3",
   "extrapolated_in_e5f09d715932d2a5.md": "41e16b1ab80d002d46250b283a7717307ba0269a5847b1f65792d778416fee45",
   "extrapolated_in_e7490f859a8d5005.md": "eab02b4c341aa0b5cf87049814f94b36dd8636b1f0066b78bc76d70fd5f18569",
   "extrapolated_in_e7a825391a09d732.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e7e78d421b2f009f.md": "e070c2c738c30ad03f2fa875bb010c40fdfcca9a5c3e5fbb06c8bcb881002efd",
   "extrapolated_in_e7ff85f3c2d7e0e6.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e8833709279a8a58.md": "c357504868311bdc7e95eb417150844e708bb3f8320717669c52399f6a6aa736",
   "extrapolated_in_e8895aa2c2d999a6.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e8c197be45b361cc.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e8de6bbe3b9197d6.md": "82c186ed1c8bc52c744a3fe93d2e5091bf082e0eb8c8df1b992949b5edbe56cd",
   "extrapolated_in_e8ff0daef2a100b1.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_e90fc72df16062fb.md": "3409427a7a48b7b4db033bc30626654ff573745056981459175855446952e0c4",
   "extrapolated_in_e967e85c965125d5.md": "79f4bc8e27a5ed5acd5d6e40138a42033ad9afbe8695352a27d2d42b1fbb83a7",
   "extrapolated_in_ea855a0fc5985ab3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_eb41eb14d4960567.md": "0ae8aa0b1e109061eeb59dddf951ee37a6fe5255d3c27798bb21294016148043",
   "extrapolated_in_ebaaca672115a3c9.md": "11f00e4532a58fa5c8588e92919124d9bb4a59bb1473677ffe2ad0941cb3ca02",
   "extrapolated_in_ebdd19aedc3eeed8.md": "e0803afeff7b8bc67ab44197cf5cad1cc718f288b056767d25a1c907da70720d",
   "extrapolated_in_ec0de72a65527f79.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_edaa416494e15bdb.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_ee056a2805f040a8.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_ee449d6b00b6289c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f0085c1e9296eed4.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f0fdb284a62b031d.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f105a1768d2183fc.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f1bed755d04997e3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f1d62457ede9fce2.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f1dd15890b8ee401.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f23e4712dc360290.md": "ebd5cd43e6773d9d8df085beb555bec89392032b6a527fd47fb3cfff14e50f31",
   "extrapolated_in_f2857d7cc6e9d746.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f28caa087fb86fd5.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f2b155904e5ac3f9.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f359a9121b7b2625.md": "3409427a7a48b7b4db033bc30626654ff573745056981459175855446952e0c4",
   "extrapolated_in_f36a7a9c57a48278.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f36b79f42d56dcee.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f3aba6bff901bad7.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f5a53bf1e27824c4.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f6043ecc4257f155.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f68e521960f423ba.md": "3177fbcd1f5c6afda6680cca2af3425b1d955cee6906ec0fb167d1515ef13164",
   "extrapolated_in_f6955456ad00616f.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f6ae4c51a574a12b.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f6b25ab29ffec1e3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f738196596547bd3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f7502d4691f36809.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f78194f3f0d0148c.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f78aea56ce3b1b1d.md": "bd86cfa6d6226cd461a18304832560414cfeb7e9ec65cbad18c69e424ab677e8",
   "extrapolated_in_f7b28524c7ae8eff.md": "4f600a5b0460ac06d38a11646b966f6bf5d1536209c2dad2ab0fec9a11fe454e",
   "extrapolated_in_f801eb6de68249a3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f81b1141d0fe31d3.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f88979e695cb32f9.md": "3fa98e6beee696c02e6fbd3487c829dc962b319590be19b840fcc27d422faebf",
   "extrapolated_in_f9502be5d0870967.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_f9fdef7de1d31ce4.md": "fb85fd37cff7db20a7aa7570e94cc6eef2b5f6497f03eba143ea6bb27a2d10f3",
   "extrapolated_in_fa1d78a3a7a378e5.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_fa2edb2b41f56b32.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_fa74afd2a1b5f2aa.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_fb3dfda226e10e69.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_fc2629fe65d09642.md": "73bbcca0280d940e59ed4f7f9fc7448e8343684ec6ecd624465cbcee3bc66305",
   "extrapolated_in_fcb1c5a4d3402a53.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_fe252fe46c0694bf.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_fe3a638f95ab8b59.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_fe545696400ae80b.md": "c10a37d08886ea84682c182d5628808255e7087181f4e1f7f9bd5f6e80f7b616",
   "extrapolated_in_fe86f7c3f828e52d.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_ff7ed636460b2ccc.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417",
   "extrapolated_in_ffc294d95bf1fcb9.md": "1e803e873c0e9d0e93349d7f278aad502f51ae441233e86d80af874305c8f895",
   "extrapolated_in_ffd48a6c5d98d8cb.md": "59ad0d197d115122494b182f15640e165806a917742e5c70f72bdf5d924ad417"
  }
 },
 "work_dir": "/tmp/chatadhd-precision-bench-after/after-ygkd8g3s"
}
```
