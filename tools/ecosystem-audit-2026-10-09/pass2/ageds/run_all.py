#!/usr/bin/env python3
"""Executable index: run every available independent AGEDS module, continue on fail."""
import argparse,json,pathlib,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--checkout',required=True);p.add_argument('--sha',required=True);p.add_argument('--compiler-dir');p.add_argument('--serialization-dir');p.add_argument('--output-dir',required=True);a=p.parse_args()
out=pathlib.Path(a.output_dir);out.mkdir(parents=True,exist_ok=True)
if any(out.iterdir()):raise SystemExit('Choose a new empty output directory; historical receipts are not overwritten.')
base=pathlib.Path(__file__).parent;results=[]
for name in ['python','kotlin','store','scan']:
 receipt=out/(name+'.receipt.json');cmd=[sys.executable,str(base/('run_'+name+'.py')),'--checkout',a.checkout,'--sha',a.sha,'--output',str(receipt)]
 if name!='python':
  if not a.compiler_dir:results.append({'name':name,'status':'BLOCKED','reason':'--compiler-dir required'});continue
  cmd+=['--compiler-dir',a.compiler_dir]
 if name=='store':
  if not a.serialization_dir:results.append({'name':name,'status':'BLOCKED','reason':'--serialization-dir required'});continue
  cmd+=['--serialization-dir',a.serialization_dir]
 proc=subprocess.run(cmd,capture_output=True,text=True);(out/(name+'.log')).write_text(proc.stdout+proc.stderr)
 status='PASS' if proc.returncode==0 else 'ACCEPTANCE_NOT_PASS' if proc.returncode==1 and receipt.exists() else 'BLOCKED_OR_ERROR'
 results.append({'name':name,'exit_code':proc.returncode,'status':status,'receipt':receipt.name if receipt.exists() else None})
(out/'index.receipt.json').write_text(json.dumps({'sha':a.sha,'modules':results,'product_pass':all(x['status']=='PASS' for x in results)},indent=2)+'\n')
print(json.dumps(results));sys.exit(0 if all(x['status']=='PASS' for x in results) else 1)
