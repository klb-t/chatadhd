#!/usr/bin/env python3
"""Offline DEV supplied-edge retrieval vectors from the immutable cached encoder."""
from datetime import datetime,timezone
import json
from pathlib import Path
import sys
import time

HERE=Path(__file__).resolve().parent
STRUCTURE=HERE.parent
CACHE=STRUCTURE/'local_embedding_panel_v1'
sys.path.insert(0,str(CACHE/'runtime_packages'))
sys.path.insert(0,str(STRUCTURE))
sys.path.insert(0,str(HERE))
import local_embedding_panel as instrument
import local_baselines as local


def run():
    output=HERE/'embedding';output.mkdir(exist_ok=False)
    prepared_path=HERE/'prepared_inputs.json';prepared=json.loads(prepared_path.read_text())
    if prepared['split']!='dev' or prepared['query_count']!=96:raise ValueError('DEV96queriesonly')
    old_env=json.loads((CACHE/'instrument_environment.json').read_text())
    protocol={'schema':'loom.research.graph_local_embedding_protocol/1','split':'dev',
        'prepared_input_sha256':local.file_sha(prepared_path),'instrument_protocol_sha256':local.file_sha(CACHE/'protocol.json'),
        'instrument_code_sha256':local.file_sha(STRUCTURE/'local_embedding_panel.py'),
        'model_manifest_sha256':local.file_sha(CACHE/'model_manifest.json'),
        'model_id':instrument.MODEL_ID,'revision':instrument.REVISION,'max_tokens':instrument.MAX_TOKENS,
        'dimensions':384,'threads':old_env['threads'],'score':'float64dotofL2normalizedfloat32vectors',
        'prediction':None,'source_judgment_available':False,'fine_tuning':False,'download':False,'paid_calls':0,
        'inputs':'only exact frozen query and eligible full-turn representation strings from prepared DEVpayloads',
        'cache':'stateless exact-string cache; eligible candidate identities selected independently perquery',
        'protocol_frozen_at':datetime.now(timezone.utc).isoformat()}
    local.write_new(output/'protocol.json',protocol)
    texts=sorted({q['query_text']for q in prepared['queries']}|{c['representation_text']for q in prepared['queries']for c in q['candidates']})
    # Existing instrument validates cached public weights/dependencies and immutable environment.
    # It never prepares/downloads in this path. Its already-existing environment must remain exact.
    np,embed,runtime=instrument.load_instrument(CACHE,old_env['threads'])
    begin=time.monotonic();vectors=[];index=[]
    for text in texts:
        vector,meta=embed(text);vectors.append(vector)
        index.append({'text':text,'text_sha256':local.sha_bytes(text.encode()),'tokenization':meta})
    matrix=np.stack(vectors).astype(np.float32)
    vector_path=output/'vectors.npy'
    with vector_path.open('xb')as f:np.save(f,matrix,allow_pickle=False)
    elapsed=time.monotonic()-begin
    lookup={text:i for i,text in enumerate(texts)}
    rows=[]
    for q in prepared['queries']:
        a=matrix[lookup[q['query_text']]].astype(np.float64)
        candidates=[{'turn_id':c['turn_id'],'source_id':c['source_id'],'known_at':c['known_at'],
            'representation_sha256':c['representation_sha256'],
            'score':float(np.dot(a,matrix[lookup[c['representation_text']]].astype(np.float64)))}for c in q['candidates']]
        rows.append({'query_id':q['query_id'],'case_id':q['case_id'],'prefix_sha256':q['prefix_sha256'],
            'query_text_sha256':q['query_text_sha256'],'candidates':candidates,'prediction':None,'source_judgment_available':False})
    local.write_new(output/'text_vector_index.json',{'schema':'loom.research.graph_local_text_vector_index/1','rows':index})
    local.write_new(output/'scores.json',{'schema':'loom.research.graph_local_embedding_scores/1','split':'dev',
        'prepared_input_sha256':protocol['prepared_input_sha256'],'protocol_sha256':local.file_sha(output/'protocol.json'),
        'model_manifest_sha256':protocol['model_manifest_sha256'],'instrument_code_sha256':protocol['instrument_code_sha256'],
        'instrument_protocol_sha256':protocol['instrument_protocol_sha256'],'embedding_code_sha256':local.file_sha(__file__),
        'vectors_sha256':local.file_sha(vector_path),'vector_index_sha256':local.file_sha(output/'text_vector_index.json'),
        'elapsed_inference_seconds':elapsed,'model_initialization_excluded':True,'unique_texts':len(texts),'dimensions':384,
        'truncated_unique_texts':sum(r['tokenization']['truncated']for r in index),'runtime':runtime,'queries':rows,
        'query_count':len(rows),'query_candidate_cosines':sum(len(r['candidates'])for r in rows),'prediction':None,'paid_calls':0})
    print(json.dumps({'queries':len(rows),'candidate_cosines':sum(len(r['candidates'])for r in rows),'unique_texts':len(texts),
        'truncated_texts':sum(r['tokenization']['truncated']for r in index),'elapsed_seconds':elapsed,'paid_calls':0}))


if __name__=='__main__':run()
