"""Read-only native/current-source CTest audit; verification artifacts outside repo."""
import argparse, hashlib, json, re, shutil, subprocess, sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
import xml.etree.ElementTree as ET

REPO=Path('/workspace/scratch/407fe6f6ff42/chatadhd')
BUILD=Path('/workspace/scratch/407fe6f6ff42/build')
BASE=Path('/workspace/scratch/407fe6f6ff42/native-logs')
OUT=BASE/'final-current'
POLICIES=('docs/research/model_research_2026-10-04/billing/runner-policy.json','docs/research/model_research_2026-10-04/billing/endpoint-pricing-policy.json')
PREFIXES=('loom/include/','loom/src/','loom/tests/','loom/third_party/','loom/data/','loom/cli/','loom/server/','loom/tools/','engine/','core/')
SINGLE=('loom/CMakeLists.txt','loom/CMakePresets.json')
COMMAND='env -u PYTHONPATH -u TMPDIR /root/.local/bin/ctest --test-dir /workspace/scratch/407fe6f6ff42/build -j2 --output-on-failure --output-junit /workspace/scratch/407fe6f6ff42/native-logs/final-current/ctest.junit.xml'

def git(*args):
    return subprocess.check_output(['git',*args],cwd=REPO).decode().strip()

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def snapshot():
    prov=json.loads((BASE/'build-provenance.json').read_bytes())
    source={}
    listed=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=REPO).decode().split('\0')
    protected=[]
    for name in sorted(set(listed)):
        if not name or not (name in SINGLE or name.startswith(PREFIXES)):
            continue
        if 'real-holdout-key' in name:
            protected.append(name);continue
        p=REPO/name
        if p.suffix.lower() in ('.md','.rst') and not name.startswith('loom/tests/fixtures/'):
            continue
        if p.is_file(): source[name]=sha(p)
    binaries={}
    for name in prov['binaries']:
        p=BUILD/name
        binaries[name]={'size':p.stat().st_size,'sha256':sha(p),'mode':oct(p.stat().st_mode&0o777)}
    objects={name:git('rev-parse','HEAD:'+name) for name in prov['native_git_objects']}
    native_changes=git('diff','--name-only','HEAD','--',*prov['native_git_objects'])
    if objects != prov['native_git_objects'] or native_changes or binaries!=prov['binaries']:
        raise RuntimeError('native source/binary equivalence failed')
    return {'at':datetime.now(timezone.utc).isoformat(),'head':git('rev-parse','HEAD'),
        'git_status':git('status','--short'),'functional_files_sha256':source,'file_count':len(source),
        'scope_rule':'Tracked and untracked functional/native/tool/data files; md/rst excluded except fixtures; explicit public policies included; real-holdout-key paths excluded without reading',
        'protected_paths_excluded_without_reading':len(protected),
        'native_equivalent_to_configured_commit':prov['configured_commit'],
        'native_git_objects':objects,'ctest_command':COMMAND,'binaries':binaries,
        'public_policy_sha256':{name:sha(REPO/name) for name in POLICIES if (REPO/name).is_file()}}

def counts():
    text=(OUT/'LastTest.log').read_text()
    blocks=re.findall(r'^\d+/\d+ Testing: ([^\n]+)\n(.*?)(?=^\d+/\d+ Testing: |\Z)',text,re.M|re.S)
    records=[]
    for name,block in blocks:
        rec={'name':name,'status':'passed' if 'Test Passed.' in block else 'failed'}
        m=re.search(r'\[doctest\] test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed\s*\|\s*(\d+) skipped',block)
        if m:
            rec.update(zip(('native_cases','native_passed','native_failed','native_filtered_or_skipped'),map(int,m.groups())))
            a=re.search(r'\[doctest\] assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed',block)
            if a: rec.update(zip(('native_assertions','native_assertions_passed','native_assertions_failed'),map(int,a.groups())))
        py=re.findall(r'^Ran (\d+) tests? in ',block,re.M)
        if py:
            rec['unittest_ran']=int(py[-1]);rec['skips']=re.findall(r'skipped=(\d+)',block)
        if name=='cli.smoke': rec['custom_completed']='cli smoke: ok' in block
        if name=='server.smoke':
            rec['custom_completed']=all(x in block for x in ('OK: all loom-server smoke checks passed','OK: loom-server auth checks passed'))
            rec['custom_completion_markers']=[line for line in block.splitlines() if line.startswith('OK:')]
        rec['unexpected_skip_markers']=[line for line in block.splitlines() if re.search(r'\bSKIP\b',line)]
        records.append(rec)
    root=ET.parse(OUT/'ctest.junit.xml').getroot()
    cases=root.findall('.//testcase')
    skips=sum(len(case.findall('skipped')) for case in cases)
    fails=sum(len(case.findall('failure'))+len(case.findall('error')) for case in cases)
    return {'ctest_entries':len(records),'statuses':dict(Counter(r['status'] for r in records)),
        'junit_entries':len(cases),'junit_skipped':skips,'junit_failures':fails,
        'native_cases':sum(r.get('native_cases',0) for r in records),
        'native_assertions':sum(r.get('native_assertions',0) for r in records),
        'native_suites':sum('native_cases' in r for r in records),
        'unittest_ran':sum(r.get('unittest_ran',0) for r in records),
        'unittest_suites':sum('unittest_ran' in r for r in records),
        'skip_summary_events':sum(sum(map(int,r.get('skips',[]))) for r in records),
        'custom_smoke_suites':sum('custom_completed' in r for r in records),'records':records}

ap=argparse.ArgumentParser();ap.add_argument('mode',choices=('start','finish'));ap.add_argument('--exit-code',type=int,default=0)
args=ap.parse_args();OUT.mkdir(parents=True,exist_ok=True)
if args.mode=='start':
    d=snapshot();(OUT/'ctest-source-freeze.json').write_text(json.dumps(d,indent=2)+'\n')
    print(json.dumps({'head':d['head'],'file_count':d['file_count'],'native_equivalent':d['native_equivalent_to_configured_commit'],'command':COMMAND},indent=2))
else:
    for name in ('LastTest.log','LastTestsFailed.log'):
        p=BUILD/'Testing/Temporary'/name
        if p.exists(): shutil.copy2(p,OUT/name)
        elif (OUT/name).exists(): (OUT/name).unlink()
    before=json.loads((OUT/'ctest-source-freeze.json').read_bytes());after=snapshot()
    allpaths=set(before['functional_files_sha256'])|set(after['functional_files_sha256'])
    changed=sorted(p for p in allpaths if before['functional_files_sha256'].get(p)!=after['functional_files_sha256'].get(p))
    (OUT/'ctest-source-after.json').write_text(json.dumps(after,indent=2)+'\n')
    c=counts();(OUT/'guard-counts.json').write_text(json.dumps(c,indent=2)+'\n')
    st={'head_at_end':after['head'],'functional_paths_changed_since_start':changed,
        'public_policy_unchanged':before['public_policy_sha256']==after['public_policy_sha256'],
        'binaries_unchanged':before['binaries']==after['binaries'],'ctest_exit_code':args.exit_code,
        'elapsed_seconds':ET.parse(OUT/'ctest.junit.xml').getroot().get('time')}
    st['gate_green']=args.exit_code==0 and not changed and st['public_policy_unchanged'] and st['binaries_unchanged'] and c['ctest_entries']==108 and c['statuses']=={'passed':108} and c['junit_entries']==108 and c['junit_failures']==0 and c['junit_skipped']==0 and c['skip_summary_events']==0 and all(r.get('custom_completed',True) and not r['unexpected_skip_markers'] for r in c['records'])
    (OUT/'run-status.json').write_text(json.dumps(st,indent=2)+'\n')
    print(json.dumps({'status':st,'counts':{k:v for k,v in c.items() if k!='records'}},indent=2))
    if not st['gate_green']: sys.exit(1)
