"""Independent full-output LastTest/JUnit/stdout/source completeness audit."""
import argparse,hashlib,json,re,xml.etree.ElementTree as ET
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--gate',type=Path,required=True);ap.add_argument('--python-cases',type=int,required=True);args=ap.parse_args();folder=args.gate
text=(folder/'LastTest.log').read_text();blocks=re.findall(r'^\d+/\d+ Testing: ([^\n]+)\n(.*?)(?=^\d+/\d+ Testing: |\Z)',text,re.M|re.S)
def summaries(name,out):
    record={'name':name};native=re.search(r'\[doctest\] test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed\s*\|\s*(\d+) skipped',out)
    if native:record['native']=list(map(int,native.groups()))
    assertions=re.search(r'\[doctest\] assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed',out)
    if assertions:record['assertions']=list(map(int,assertions.groups()))
    py=re.findall(r'^Ran (\d+) tests? in ',out,re.M)
    if py:record['python']=int(py[-1])
    record['python_skips']=sum(map(int,re.findall(r'skipped=(\d+)',out)))
    if name=='cli.smoke':record['smoke_complete']='cli smoke: ok' in out
    if name=='server.smoke':record['smoke_complete']=all(m in out for m in ['OK: all loom-server smoke checks passed','OK: loom-server auth checks passed'])
    record['explicit_skip_markers']=bool(re.search(r'\bSKIP\b',out))
    return record
last={name:summaries(name,out) for name,out in blocks};assert len(blocks)==len(last)==108
assert all('Test Passed.' in out for _,out in blocks)
junit=ET.parse(folder/'ctest.junit.xml').getroot();cases=junit.findall('.//testcase');assert len(cases)==108
xml={case.attrib['name']:summaries(case.attrib['name'],case.findtext('system-out') or '') for case in cases}
assert last==xml
assert all(not case.findall('failure') and not case.findall('error') and not case.findall('skipped') for case in cases)
assert not any('removed because' in (case.findtext('system-out') or '') for case in cases)
native=[r for r in last.values() if 'native' in r];python=[r for r in last.values() if 'python' in r];smokes=[r for r in last.values() if 'smoke_complete' in r]
assert len(native)==81 and sum(r['native'][0] for r in native)==659 and sum(r['native'][1] for r in native)==659 and sum(r['native'][2] for r in native)==0
assert sum(r['assertions'][0] for r in native)==24465 and sum(r['assertions'][1] for r in native)==24465 and sum(r['assertions'][2] for r in native)==0
assert len(python)==25 and sum(r['python'] for r in python)==args.python_cases
assert len(smokes)==2 and all(r['smoke_complete'] for r in smokes)
assert not any(r['python_skips'] or r['explicit_skip_markers'] for r in last.values())
raw=(folder/'ctest.raw.log').read_bytes();capture=json.loads((folder/'stdout-capture-proof.json').read_bytes());assert len(raw)==capture['bytes'] and hashlib.sha256(raw).hexdigest()==capture['sha256']
console=re.findall(rb'^\s*\d+/108 Test\s+#\d+: ([^ ]+)\s+.*?Passed',raw,re.M);assert len(console)==len(set(console))==108
before=json.loads((folder/'ctest-source-freeze.json').read_bytes());after=json.loads((folder/'ctest-source-after.json').read_bytes())
assert before['functional_files_sha256']==after['functional_files_sha256'] and before['public_policy_sha256']==after['public_policy_sha256'] and before['binaries']==after['binaries']
elapsed=float(re.search(rb'Total Test time \(real\) = ([0-9.]+) sec',raw).group(1))
proof={'verdict':'pass','ctest_entries':108,'complete_LastTest_JUnit_per_entry_summaries_match':True,
 'native_suites':81,'native_cases':659,'native_assertions':24465,'unittest_suites':25,'unittest_cases':args.python_cases,
 'custom_smoke_suites':2,'skip_events':0,'junit_truncated_outputs':0,'complete_stdout_entries':108,
 'stdout_stream_saved_bytes_and_sha256_match':True,'functional_files_unchanged':len(before['functional_files_sha256']),
 'public_policies_unchanged':len(before['public_policy_sha256']),'binaries_unchanged':True,
 'raw_ctest_elapsed_seconds':elapsed,'junit_elapsed_integer_metadata':junit.get('time'),'per_entry_summaries':last}
(folder/'independent-count-audit.json').write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps({k:v for k,v in proof.items() if k!='per_entry_summaries'},indent=2))
