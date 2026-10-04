#!/usr/bin/env python3
"""Verify an already configured native build against exact public source bytes."""
import argparse, hashlib, importlib.util, json, os, re, subprocess, time, shutil
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--out-dir', type=Path, required=True)
parser.add_argument('--code-commit', required=True)
parser.add_argument('--main-commit', required=True)
parser.add_argument('--build-jobs', type=int, default=2)
parser.add_argument('--test-jobs', type=int, default=1)
parser.add_argument('--cmake')
parser.add_argument('--ctest')
args = parser.parse_args()
if min(args.build_jobs, args.test_jobs) < 1: parser.error('job presets must be positive')
repo, build, out = args.repo.resolve(), args.build_dir.resolve(), args.out_dir.resolve()
out.mkdir(parents=True, exist_ok=False)
helper = repo / 'docs/reports/archive-import-2026-10-04-evidence/native/verify_native.py'
spec = importlib.util.spec_from_file_location('native_proof', helper)
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
cmake = m.discover_tool(args.cmake, 'cmake')
ctest = m.discover_tool(args.ctest, 'ctest', Path(cmake).with_name('ctest'))
env = os.environ.copy()
removed = [key for key in ('PYTHONPATH','TMPDIR') if key in env]
for key in removed: env.pop(key)
receipt = {'schema':'loom.mixed_import_verification/1','status':'running',
    'source_code_commit':args.code_commit,'main_commit':args.main_commit,
    'profile':'GCC Release -O0 -DNDEBUG WERROR vendored SQLite OpenSSL shared/CLI/server/tests',
    'removed_external_environment_keys':removed,'commands':[],'live_provider_calls':0,
    'build_was_configured_before_runner':True, 'failure':None,
    'executed_runner_sha256':m.sha256(Path(__file__))}
def save(): (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
def run(label, command):
    start=time.monotonic()
    with (out/(label+'.txt')).open('w') as log:
        result=subprocess.run(command,cwd=repo,env=env,stdout=log,stderr=subprocess.STDOUT)
    receipt['commands'].append({'label':label,'argv':command,'exit_code':result.returncode,
        'seconds':time.monotonic()-start,'log':label+'.txt','log_sha256':m.sha256(out/(label+'.txt'))})
    save(); print(label+': exit '+str(result.returncode),flush=True)
    if result.returncode: raise ValueError(label+' failed')
    return (out/(label+'.txt')).read_text()
try:
    cache=(build/'CMakeCache.txt').read_text()
    home=next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_HOME_DIRECTORY:'))
    if Path(home).resolve().parent != repo: raise ValueError('build is bound to a different checkout')
    current=m.source_manifest(repo)
    (out/'source-before.json').write_text(json.dumps(current,indent=2)+'\n')
    tracked=subprocess.check_output(['git','ls-tree','-r','-z',args.code_commit,'loom','core','engine','docs/contracts'],cwd=repo).split(b'\0')
    mismatch=[]
    for entry in tracked:
        if not entry: continue
        header,name=entry.split(b'\t',1); name=os.fsdecode(name)
        actual=(repo/name).read_bytes()
        identity=hashlib.sha1(b'blob '+str(len(actual)).encode()+b'\0'+actual).hexdigest()
        if identity != header.split()[2].decode(): mismatch.append(name)
    receipt['source_commit_mismatches']=mismatch
    if mismatch: raise ValueError('source bytes do not match requested code commit')
    run('build-final',[cmake,'--build',str(build),'--parallel',str(args.build_jobs)])
    receipt['cmake_cache_sha256']=m.sha256(build/'CMakeCache.txt')
    binary_names=['libloom_core.a','libloom_miniz.a','libloom_sqlite3_amalgamation.a',
        'libloom.so','loom_tests','loom_compat_tool','loom_candidate_graph_native_tool','cli/loom','server/loom-server']
    receipt['binary_sha256']={name:m.sha256(build/name) for name in binary_names}
    inventory=json.loads(run('ctest-registration',[ctest,'--test-dir',str(build),'--show-only=json-v1']))
    registered=inventory['tests']; receipt['registered_ctest_entries']=len(registered)
    required=['unit.test_import_screenshot','unit.test_import_aux_depth','unit.test_db_annotations',
        'unit.test_import_resume','unit.test_import_audit','unit.test_import_usage','compat.test_archive_cost']
    if any(name not in {test['name'] for test in registered} for name in required): raise ValueError('required new tests missing')
    logs=run('ctest-full',[ctest,'--test-dir',str(build),'-V','--output-on-failure','--no-tests=error','--parallel',str(args.test_jobs)])
    receipt['full_ctest']=m.ctest_counts(logs)
    if receipt['full_ctest']['failed'] or receipt['full_ctest']['executed'] != len(registered): raise ValueError('full execution count mismatch')
    native=re.findall(r'(?m)^(\d+): \[doctest\]\s+test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed',logs)
    assertions=re.findall(r'(?m)^\d+: \[doctest\]\s+assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed',logs)
    python=re.findall(r'(?m)^\d+: Ran (\d+) tests? in ',logs)
    skips=re.findall(r'(?m)^\d+: OK \(skipped=(\d+)\)',logs)
    receipt['native']={'groups':len(native),'cases':sum(int(x[1]) for x in native),
        'failed':sum(int(x[3]) for x in native),'assertions':sum(int(x[0]) for x in assertions),
        'zero_case_groups':[registered[int(x[0])-1]['name'] for x in native if int(x[1])==0]}
    receipt['python']={'unittest_groups':len(python),'cases':sum(map(int,python)),'skips':sum(map(int,skips))}
    receipt['new_group_counts']={}
    for name in required:
        index=next(i+1 for i,t in enumerate(registered) if t['name']==name)
        case_match=next((x for x in native if int(x[0])==index),None)
        if case_match:
            receipt['new_group_counts'][name]={'cases':int(case_match[1]),'failed':int(case_match[3])}
            assertion_match=re.search(r'(?m)^'+str(index)+r': \[doctest\]\s+assertions:\s*(\d+)', logs)
            if assertion_match: receipt['new_group_counts'][name]['assertions']=int(assertion_match[1])
        else:
            match=re.search(r'(?m)^'+str(index)+r': Ran (\d+) tests? in ',logs)
            if not match: raise ValueError('new group has no executed denominator: '+name)
            receipt['new_group_counts'][name]={'cases':int(match[1])}
        if receipt['new_group_counts'][name]['cases']==0: raise ValueError('new group executed zero cases')
    after=m.source_manifest(repo)
    (out/'source-after.json').write_text(json.dumps(after,indent=2)+'\n')
    receipt['source_manifest_sha256']=after['sha256'];receipt['source_files']=len(after['files'])
    receipt['sources_stable']=after['sha256']==current['sha256']
    receipt['binaries_stable']=all(m.sha256(build/name)==sha for name,sha in receipt['binary_sha256'].items())
    if not receipt['sources_stable'] or not receipt['binaries_stable']: raise ValueError('inputs changed during verification')
    if receipt['native']['failed'] or receipt['python']['skips']: raise ValueError('test cases failed or skipped')
    receipt['status']='passed'
except BaseException as error:
    receipt['status']='failed';receipt['failure']=str(error)
finally: save()
raise SystemExit(0 if receipt['status']=='passed' else 1)
