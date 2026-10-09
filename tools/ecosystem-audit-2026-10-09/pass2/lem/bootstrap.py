#!/usr/bin/env python3
"""Download only pinned public JVM test dependencies; SHA-256 check before reuse."""
import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import urllib.request

BASE=pathlib.Path(__file__).resolve().parent

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dest',type=pathlib.Path,required=True)
    args=p.parse_args()
    records=json.loads((BASE/'dependencies.json').read_text())
    def fetch(record):
        target=args.dest/record['bucket']/record['filename']
        target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest()!=record['sha256']:
            with urllib.request.urlopen(record['url'],timeout=120) as response:
                content=response.read()
            if hashlib.sha256(content).hexdigest()!=record['sha256']:
                raise RuntimeError('SHA-256 mismatch: '+record['filename'])
            target.write_bytes(content)
        return target.name
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        completed=list(pool.map(fetch,records))
    print(json.dumps({'verified':len(completed),'directories':['compiler','deps']},indent=2))

if __name__=='__main__':main()
