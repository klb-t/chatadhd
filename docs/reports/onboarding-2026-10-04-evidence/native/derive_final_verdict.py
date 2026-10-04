from pathlib import Path
import datetime,hashlib,json,re,subprocess,xml.etree.ElementTree as ET
folder=Path('/tmp/onboarding-verification-2026-10-04')
repo=Path('/workspace/scratch/4db3ee41b13b/chatadhd')
manifest=json.loads((folder/'native-source-hashes-final.json').read_text())
changed=[]; missing=[]
for relative,expected in manifest['sha256'].items():
 p=repo/relative
 if not p.is_file():missing.append(relative)
 elif hashlib.sha256(p.read_bytes()).hexdigest()!=expected:changed.append(relative)
comparison={'checked_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'published_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),'capture_head':manifest['head_at_capture'],'compared_inputs':len(manifest['sha256']),'source_manifest_sha256':manifest['manifest_sha256'],'changed':changed,'missing':missing,'identical':not changed and not missing}
(folder/'native-source-hashes-after-publication.json').write_text(json.dumps(comparison,indent=2)+'\n')
raw=(folder/'ctest-full-final-LastTest.log').read_text()
parts=re.split(r'^\d+/112 Testing: (.*?)\n',raw,flags=re.M)
records=[]
for name,body in zip(parts[1::2],parts[2::2]):
 out=body.split('Output:\n----------------------------------------------------------\n',1)[1].split('<end of output>',1)[0]
 entry={'ctest_name':name,'passed':'\nTest Passed.\n' in body}
 if name.startswith('unit.'):
  m=re.search(r'\[doctest\] test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed',out)
  a=re.search(r'\[doctest\] assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed',out)
  assert m and a,(name,out)
  entry.update(kind='native_doctest',executed_cases=int(m[1]),passed_cases=int(m[2]),failed_cases=int(m[3]),assertions=int(a[1]),failed_assertions=int(a[3]))
 else:
  ran=re.findall(r'Ran (\d+) tests? in ([0-9.]+)s',out)
  skips=re.findall(r'OK \(skipped=(\d+)\)',out)
  if ran:entry.update(kind='python_unittest',reported_cases=sum(int(x[0]) for x in ran),skipped_cases=sum(int(x) for x in skips))
  else:
   assert name in ['server.smoke','cli.smoke'],name
   assert 'SKIP' not in out and ('all loom-server smoke checks passed' in out or 'cli smoke: ok' in out),name
   entry.update(kind='python_smoke_script',reported_cases=None,count_note='Script success has no unittest case denominator; counted only as one executed CTest entry.')
 records.append(entry)
xml=ET.parse(folder/'ctest-full-final.xml').getroot()
ctest_log=(folder/'ctest-full-final.log').read_text()
elapsed=float(re.search(r'Total Test time \(real\) =\s*([0-9.]+) sec',ctest_log)[1])
native=[x for x in records if x['kind']=='native_doctest']
python=[x for x in records if x['kind']=='python_unittest']
onboarding=[x for x in native if '.test_onboarding' in x['ctest_name']]
count=int(re.search(r'unskipped test cases passing the current filters:\s*(\d+)',(folder/'doctest-final-count.log').read_text())[1])
assert sum(x['executed_cases'] for x in native)==count
assert len(records)==112 and all(x['passed'] for x in records)
assert comparison['identical']
verdict={
 'completed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
 'status':'PASS','build_exit_code':0,'ctest_exit_code':0,
 'base_head':'30ad7d37337d6641cb7714b03e9feff0e6e25d25',
 'source_comparison':comparison,
 'build_command':'/root/.local/bin/cmake --build /tmp/onboarding-build-2026-10-04 -j2',
 'ctest_command':'env -u PYTHONPATH /root/.local/bin/ctest --test-dir /tmp/onboarding-build-2026-10-04 --output-on-failure -j2 --output-junit /tmp/onboarding-verification-2026-10-04/ctest-full-final.xml',
 'configuration':'Ninja Debug -g0 -O0, LOOM_WERROR=ON, shared+CLI+server+tests ON, bundled SQLite3.47.2, OpenSSL ON',
 'ctest_entries':112,'ctest_failures':int(xml.attrib['failures']),'ctest_skipped_statuses':int(xml.attrib['skipped']),'ctest_elapsed_seconds':elapsed,
 'native_source_entries':len(native),'native_executed_cases':sum(x['executed_cases'] for x in native),'native_assertions':sum(x['assertions'] for x in native),'native_failed_cases':sum(x['failed_cases'] for x in native),'native_failed_assertions':sum(x['failed_assertions'] for x in native),'native_unskipped_count_query':count,
 'native_opt_in_not_executed':[x['ctest_name'] for x in native if x['executed_cases']==0],
 'native_opt_in_note':'unit.test_catalog_scale executes 0 cases/0 assertions by default; one slow ~1GB corpus case requires LOOM_RUN_SLOW_TESTS and was not opted in.',
 'onboarding_native_cases':sum(x['executed_cases'] for x in onboarding),'onboarding_native_assertions':sum(x['assertions'] for x in onboarding),'onboarding_suites':onboarding,
 'python_unittest_entries':len(python),'python_unittest_reported_cases':sum(x['reported_cases'] for x in python),'python_unittest_skipped_cases':sum(x['skipped_cases'] for x in python),
 'python_non_unittest_smoke_entries':[x['ctest_name'] for x in records if x['kind']=='python_smoke_script'],
 'count_evidence':'Full untruncated Testing/Temporary/LastTest.log copied to ctest-full-final-LastTest.log. JUnit successful stdout truncates at1024 bytes for several entries, so counts are derived from LastTest, not inferred from baseline.',
 'generator_check':json.loads((folder/'builtin-generated-check.json').read_text()),
 'historical_status':{'ctest-onboarding-initial.log':'FAIL3/4; corrected missing-vocabulary fixture revision without loosening assertion','build-test-type-initial.log':'COMPILE_FAIL; corrected typed string comparison in new regression, assertion unchanged','ctest-onboarding-multiversion-fixture-negative.log':'FAIL3/4; corrected new multiversion fixture revision without loosening assertion','ctest-full-interrupted-source-change.log':'INTERRUPTED exit130 after source change; no final verdict','ctest-full-interrupted-method-version-fix.log':'INTERRUPTED exit130 after source change; no final verdict'},
 'entries':records,
}
(folder/'final-verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
print(json.dumps({k:v for k,v in verdict.items() if k not in ['entries','onboarding_suites']},indent=2))
