#!/usr/bin/env python3
import argparse,hashlib,json,subprocess,concurrent.futures
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);a=p.parse_args();a.deps.mkdir(parents=True,exist_ok=True)
items=json.loads(Path(__file__).with_name('dependencies.json').read_text())
def fetch(x):
 target=a.deps/x['file']
 if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest()==x['sha256']:return
 part=target.with_suffix('.part')
 subprocess.run(['curl','--fail','--silent','--show-error','--location','--max-time','180',x['url'],'-o',str(part)],check=True)
 if hashlib.sha256(part.read_bytes()).hexdigest()!=x['sha256']:raise RuntimeError('Digest mismatch '+x['file'])
 part.replace(target)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(fetch,items))
print('Dependencies verified: '+str(len(items)))
