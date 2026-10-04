#!/usr/bin/env python3
"""Isolated thread-2 integration check; no repository edits/provider calls.

Build the native core first. Fetch the policy branch before this offline check.
A fixed policy ref makes source provenance explicit. Config's override is set
by the synthetic harness because the complete thread-2 Config patch is not
installed in this isolated link check.
"""
import argparse
import hashlib
import subprocess
import tempfile
from pathlib import Path

repo=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser()
p.add_argument('--build-dir',type=Path,default=repo/'loom/build/dev')
p.add_argument('--policy-ref',default='34cc920dd3cdb0c0fca0a514569b19111429583f')
args=p.parse_args()
loom=repo/'loom'
with tempfile.TemporaryDirectory(prefix='loom-packet-policy-') as t:
    tmp=Path(t);inc=tmp/'include/loom';inc.mkdir(parents=True)
    references=[('loom/include/loom/usage_policy.h',inc/'usage_policy.h'),('loom/src/policy/usage_policy.cpp',tmp/'usage.cpp'),('loom/src/core/config_usage_policy.cpp',tmp/'config_usage.cpp')]
    for source,dest in references:
        data=subprocess.check_output(['git','show',f'{args.policy_ref}:{source}'],cwd=repo)
        dest.write_bytes(data)
        print(f'{source} SHA-256 {hashlib.sha256(data).hexdigest()}',flush=True)
    include=['-I'+str(tmp/'include'),'-I'+str(loom/'include'),'-I'+str(loom/'src'),'-isystem',str(loom/'third_party/nlohmann'),'-isystem',str(loom/'third_party/sqlite')]
    objects=[]
    for source in [tmp/'usage.cpp',tmp/'config_usage.cpp',loom/'src/capi/capi_packet.cpp',loom/'src/packet/packet.cpp']:
        obj=tmp/(source.stem+'.o');objects.append(obj)
        subprocess.run(['g++','-std=c++20','-fPIC','-fvisibility=hidden',*include,'-c',str(source),'-o',str(obj)],check=True)
    exe=tmp/'check'
    subprocess.run(['g++','-std=c++20',*include,str(loom/'src/packet/tests/policy_integration.cc'),*map(str,objects),str(args.build_dir/'libloom_core.a'),str(args.build_dir/'libloom_sqlite3_amalgamation.a'),str(args.build_dir/'libloom_miniz.a'),'-lssl','-lcrypto','-lpthread','-ldl','-o',str(exe)],check=True)
    subprocess.run([str(exe),str(tmp/'data')],check=True)
