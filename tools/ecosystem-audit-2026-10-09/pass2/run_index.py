#!/usr/bin/env python3
"""Executable audit index. JSON job file contains [{suite,args:[...]},...]."""
import argparse,ast,hashlib,json,pathlib,subprocess,sys
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--list',action='store_true');p.add_argument('--validate',action='store_true');p.add_argument('--jobs',type=pathlib.Path);p.add_argument('--output-dir',type=pathlib.Path);p.add_argument('--python',default=sys.executable);p.add_argument('--timeout',type=int,default=600);a=p.parse_args()
 root=pathlib.Path(__file__).resolve().parent;catalog=json.loads((root/'catalog.json').read_text());suites={x['id']:x for x in catalog['suites']}
 if a.list:print(json.dumps(catalog,indent=2));return 0
 if a.validate:
  for suite in suites.values():
   path=root/suite['script']
   if root not in path.resolve().parents:raise SystemExit('invalid catalog path')
   ast.parse(path.read_text(),filename=str(path))
  print(json.dumps({'status':'PASS','executable_scripts':len(suites),'scope':'catalog paths and Python syntax, not product tests'}));return 0
 if not a.jobs or not a.output_dir:p.error('--jobs and --output-dir required for execution')
 jobs=json.loads(a.jobs.read_text())
 if not isinstance(jobs,list) or not jobs or not all(isinstance(job,dict) for job in jobs):raise SystemExit('Jobs must be a nonempty list of objects')
 out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
 if any(out.iterdir()):raise SystemExit('Output directory must be empty; never overwrite historical receipts')
 results=[]
 for i,job in enumerate(jobs):
  entry=suites.get(job.get('suite'))
  if not entry or not isinstance(job.get('args'),list) or not all(isinstance(x,str) for x in job['args']):
   results.append({'job':i,'suite':job.get('suite'),'status':'INVALID_JOB'});continue
  script=root/entry['script'];cmd=[a.python,str(script),*job['args']]
  try:
   run=subprocess.run(cmd,capture_output=True,text=True,timeout=a.timeout)
   (out/f'{i:02d}-{entry["id"]}.log').write_text(run.stdout+run.stderr)
   results.append({'job':i,'suite':entry['id'],'argv':cmd,'exit_code':run.returncode,'status':'RUNNER_PASS' if run.returncode==0 else 'NOT_PASS','script_sha256':hashlib.sha256(script.read_bytes()).hexdigest()})
  except subprocess.TimeoutExpired:
   results.append({'job':i,'suite':entry['id'],'status':'TIMEOUT','timeout_seconds':a.timeout})
 result={'schema':'klbt.audit.index-execution/1','jobs':results,'all_selected_runners_passed':all(x['status']=='RUNNER_PASS' for x in results),'caution':'Runner PASS for reproduction-only jobs means reproduced bug, not product acceptance. Inspect individual receipts.'}
 (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));return 0 if result['all_selected_runners_passed'] else 1
if __name__=='__main__':sys.exit(main())
