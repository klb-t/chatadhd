#!/usr/bin/env python3
"""Offline factorial request materializer; all study choices are design data."""
import argparse,hashlib,itertools,json
from pathlib import Path

def canonical(x):return json.dumps(x,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode()
def digest(x):return hashlib.sha256(x).hexdigest()
def read_bound(path,expected):
 raw=path.read_bytes()
 if digest(raw)!=expected:raise ValueError('source_hash_mismatch')
 return json.loads(raw),raw
def write_exact(path,raw):
 path.parent.mkdir(parents=True,exist_ok=True)
 if path.exists() and path.read_bytes()!=raw:raise ValueError('immutable_artifact_collision')
 path.write_bytes(raw)
def prepare(design_path,out):
 design_path=Path(design_path);root=design_path.parent;out=Path(out)
 design_raw=design_path.read_bytes();d=json.loads(design_raw);s=d['source']
 _,inputs_raw=read_bound(root/s['inputs_file'],s['inputs_sha256'])
 _,gold_raw=read_bound(root/s['gold_file'],s['gold_sha256'])
 source,source_raw=read_bound(root/s['prepared_manifest_file'],s['prepared_manifest_sha256'])
 queries={}
 for op in source['operations']:
  if op['metadata']['arm_id']!=s['prepared_arm']:continue
  query_id=op['metadata']['context_preparation']['case_id']
  if query_id not in d['selection']['query_ids']:continue
  body,raw=read_bound((root/s['prepared_manifest_file']).parent/op['request_file'],op['request_sha256'])
  queries[query_id]={'text':body['state']['text'],'source_request_sha256':digest(raw)}
 if set(queries)!=set(d['selection']['query_ids']):raise ValueError('selected_query_missing')
 configurations=[];operations=[];crosswalk=[]
 for strategy,temp,maximum,pref in itertools.product(d['prompt_strategies'],d['temperatures'],d['max_tokens'],d['user_preferences']):
  config={'strategy_id':strategy['id'],'temperature':temp,'max_tokens':maximum,'preference_id':pref['id']}
  cid='cfg.'+digest(canonical(config))[:20]
  prompt='\n\n'.join([d['prompt_common'],strategy['instructions'],pref['instructions']])
  configurations.append({'configuration_id':cid,**config,'prompt_sha256':digest(prompt.encode()),'prompt_version':'express-prompt-parameters-v1','system_prompt':prompt})
  for qid in d['selection']['query_ids']:
   body={**d['request_defaults'],'temperature':temp,'max_tokens':maximum,'messages':[{'role':'system','content':prompt},{'role':'user','content':queries[qid]['text']}]}
   raw=canonical(body);rh=digest(raw);name='requests/'+rh+'.json';write_exact(out/name,raw)
   oid=d['study_id']+'.'+cid+'.'+qid
   units=d['units_presets'];metadata={'configuration_id':cid,'query_id':qid,'arm_id':cid,'prompt_sha256':digest(prompt.encode()),'prompt_version':'express-prompt-parameters-v1','design_sha256':digest(design_raw),'source_inputs_sha256':digest(inputs_raw),'source_gold_sha256':digest(gold_raw),'source_request_sha256':queries[qid]['source_request_sha256'],'input_text_sha256':digest(queries[qid]['text'].encode())}
   operations.append({'operation_id':oid,'route_id':'chat','request_file':name,'request_sha256':rh,'model_id':body['model'],'provider_id':body['provider']['only'][0],'units_upper_bounds':{'prompt':len(raw)+units['prompt_fixed_allowance']+units['prompt_per_message_allowance']*len(body['messages']),'completion':maximum,'request':units['request'],'reasoning':units['reasoning']},'minimum_reservation_usd':units['minimum_reservation_usd'],'metadata':metadata})
   crosswalk.append({'operation_id':oid,'configuration_id':cid,'query_id':qid,'request_sha256':rh,**config})
 manifest={'schema':'loom.research_programme_manifest/1','programme_id':d['programme_id'],'stage_id':d['stage_id'],'operations':operations,'metadata':{'design_sha256':digest(design_raw),'paid_calls':0,'full_factorial':True,'configuration_count':len(configurations),'query_count':len(queries),'first_response_only':True}}
 for name,value in [('manifest.json',manifest),('configurations.json',configurations),('crosswalk.json',crosswalk)]:write_exact(out/name,canonical(value)+b'\n')
 return manifest
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--design',type=Path,default=Path(__file__).with_name('design.json'));a.add_argument('--output',type=Path,required=True);args=a.parse_args();m=prepare(args.design,args.output);print(json.dumps({'operations':len(m['operations']),'paid_calls':0}))
