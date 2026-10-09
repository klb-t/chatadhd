#!/usr/bin/env python3
"""Execute selected audit suites; accept argv arrays, never shell text.
Use --list for the suite catalog. Exit codes describe the runner, not a claim
that reproduction PASS is product acceptance. Always inspect module receipts.
"""
import argparse, ast, hashlib, json, pathlib, subprocess, sys
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--list',action='store_true');p.add_argument('--validate',action='store_true')
 p.add_argument('--jobs',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path)
 p.add_argument('--timeout',type=int,default=600)
 a=p.parse_args();root=pathlib.Path(__file__).resolve().parent
 catalog=json.loads((root/'catalog.json').read_text());suites={x['id']:x for x in catalog['suites']}
 for row in suites.values():
  script=(root/row['script']).resolve()
  if root not in script.parents:raise SystemExit('catalog path outside audit tools')
  ast.parse(script.read_text(),filename=str(script))
 if a.list:print(json.dumps(catalog,indent=2));return 0
 if a.validate:print(json.dumps({'status':'PASS','scripts':len(suites),'scope':'catalog paths and Python syntax only'}));return 0
 if not a.jobs or not a.output:p.error('--jobs and --output required')
 jobs=json.loads(a.jobs.read_text())
 if not isinstance(jobs,list) or not jobs:raise SystemExit('jobs must be a nonempty list')
 if a.output.exists():raise SystemExit('use new output directory')
 a.output.mkdir(parents=True);results=[]
 for i,job in enumerate(jobs):
  row=suites.get(job.get('suite')) if isinstance(job,dict) else None
  if not row or not isinstance(job.get('args'),list) or not all(isinstance(x,str) for x in job['args']):
   results.append({'job':i,'status':'INVALID_JOB'});continue
  script=root/row['script'];command=[sys.executable,str(script),*job['args']]
  try:
   run=subprocess.run(command,capture_output=True,text=True,timeout=a.timeout)
   (a.output/f'{i:02d}.log').write_text(run.stdout+run.stderr)
   results.append({'suite':row['id'],'exit_code':run.returncode,'status':'RUNNER_PASS' if run.returncode==0 else 'NOT_PASS','script_sha256':hashlib.sha256(script.read_bytes()).hexdigest(),'argv':command})
  except subprocess.TimeoutExpired:
   results.append({'suite':row['id'],'status':'TIMEOUT','seconds':a.timeout})
 receipt={'jobs':results,'warning':'Reproduction PASS is not product PASS. BLOCKED is not repaired. Exact classifications are in module receipts.'}
 (a.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
 return 0 if all(r['status']=='RUNNER_PASS' for r in results) else 1
if __name__=='__main__':sys.exit(main())
