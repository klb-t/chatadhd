#!/usr/bin/env python3
"""Evaluate independently frozen embedding scores without changing the encoder."""
from datetime import datetime,timezone
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import methods_panel as panel

here=Path(__file__).resolve().parent
structure=here.parent
repo=structure.parents[2]
policy=json.loads((here/'policy.json').read_text())
artifact=structure/'local_embedding_panel_v1/scores.json'
expected='0b59dd984a254f6080edb74886d469436e3cb0bba98902b070d8e78f702bec3f'
raw=artifact.read_bytes()
if panel.sha_bytes(raw)!=expected:raise ValueError('frozen_embedding_artifact_hash_mismatch')
value=json.loads(raw)
if value['inputs_sha256']!=policy['input_sha256']:raise ValueError('embedding_pair_input_hash_mismatch')
pairs,gold,_=panel.load_fixture(repo/'loom/tests/fixtures/eval/jev_structure_pairs_v1',policy)
rows=[]
for row in value['rows']:
    if row['prediction']is not None:raise ValueError('unexpected_prediction_in_independent_raw_scores')
    rows.append({'case_id':row['case_id'],'scores':{value['method']:row['score']if row['available']else None}})
if len(rows)!=48 or len({r['case_id']for r in rows})!=48 or set(gold)!={r['case_id']for r in rows}:
    raise ValueError('embedding_pair_inventory_mismatch')
result={'schema':'loom.research.frozen_embedding_addendum/1',
    'evaluation_status':policy['evaluation_status'],'protocol_sha256':panel.sha_bytes((here/'EMBEDDING_ADDENDUM_PROTOCOL.md').read_bytes()),
    'raw_artifact_sha256':expected,'raw_artifact_prediction_fields_remain_null':True,
    'vectors_sha256':value['vectors_sha256'],'model_manifest_sha256':value['model_manifest_sha256'],
    'embedding_protocol_sha256':value['protocol_sha256'],'embedding_code_sha256':value['code_sha256'],
    'inputs_sha256':value['inputs_sha256'],'encoder_changed_after_label_read':False,
    'thresholds':policy['thresholds'],'primary_descriptive_threshold':policy['primary_descriptive_threshold'],
    'representation':'learned_text_embedding_not_argument_graph_embedding',
    'coverage':value['coverage'],'dimensions':value['dimensions'],
    **panel.summarize(rows,gold,policy['thresholds'],policy['primary_descriptive_threshold'])}
panel.write_new(here/'embedding_addendum_results.json',result)
print('Frozen learned embedding addendum preserved:48 exploratory pairs; generic thresholds unchanged.')
