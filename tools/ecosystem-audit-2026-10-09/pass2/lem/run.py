#!/usr/bin/env python3
"""Compile immutable production Kotlin sources and run independent intercepted host tests.

No product sources are modified. A=bug reproduction; B=product acceptance.
Default exit status follows B, so a reproduced violation is never product PASS.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import subprocess
import sys

BASE = pathlib.Path(__file__).resolve().parent
FILES = [
    'api/GeminiApiService.kt', 'api/ResearchAgent.kt', 'api/ApiKeyStore.kt',
    'research/ExperimentRunner.kt', 'viewmodel/ResearchViewModel.kt',
    'data/repository/ResearchRepository.kt', 'data/local/AppDatabase.kt',
    'data/local/ConfigDao.kt', 'data/local/ExperimentDao.kt', 'data/local/LedgerDao.kt',
    'data/model/ExperimentConfig.kt', 'data/model/ExperimentResult.kt', 'data/model/LedgerEntry.kt',
]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', type=pathlib.Path, required=True)
    p.add_argument('--sha', required=True)
    p.add_argument('--compiler-dir', type=pathlib.Path, required=True)
    p.add_argument('--deps-dir', type=pathlib.Path, required=True)
    p.add_argument('--out', type=pathlib.Path, required=True)
    p.add_argument('--phase', choices=['all', 'reproduction', 'acceptance'], default='all')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    sha = subprocess.check_output(['git', '-C', str(args.repo), 'rev-parse', args.sha + '^{commit}'], text=True).strip()
    source = args.out / 'source'
    source.mkdir(exist_ok=True)
    manifest = []
    for rel in FILES:
        path = 'app/src/main/java/com/example/' + rel
        raw = subprocess.check_output(['git', '-C', str(args.repo), 'show', f'{sha}:{path}'])
        dest = source / rel
        dest.parent.mkdir(exist_ok=True, parents=True)
        dest.write_bytes(raw)
        manifest.append({'path': path, 'sha256': hashlib.sha256(raw).hexdigest(), 'lines': raw.count(b'\n')})
    compiler = sorted(args.compiler_dir.resolve().glob('*.jar'))
    jars = sorted(args.deps_dir.resolve().glob('*.jar'))
    jars += list(args.compiler_dir.resolve().glob('kotlin-stdlib-*.jar'))
    jars += list(args.compiler_dir.resolve().glob('annotations-*.jar'))
    cp = ':'.join(map(str, jars))
    sources = sorted(source.rglob('*.kt')) + sorted((BASE/'fixtures').glob('*.kt')) + [BASE/'HostHarness.kt']
    compile_cmd = ['java', '-cp', ':'.join(map(str, compiler)), 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
        '-no-stdlib', '-no-reflect', '-jvm-target', '17', '-classpath', cp, *map(str, sources), '-d', str(args.out/'host.jar')]
    c = subprocess.run(compile_cmd, capture_output=True, text=True, timeout=120)
    (args.out/'compile.log').write_text(c.stdout+c.stderr)
    receipt = {'schema':'klbt.audit.lem-receipt/1', 'repo':'klb-t/LEM-Workbench', 'sha':sha,
        'sources':manifest, 'source_count':len(manifest), 'compile_exit_code':c.returncode,
        'dependencies':[{'file':j.name,'sha256':hashlib.sha256(j.read_bytes()).hexdigest()} for j in jars],
        'compiler':[{'file':j.name,'sha256':hashlib.sha256(j.read_bytes()).hexdigest()} for j in compiler]}
    if c.returncode:
        receipt['status']='BLOCKED_COMPILE'
        (args.out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        print(c.stderr, file=sys.stderr)
        return 2
    r = subprocess.run(['java','-Djava.security.manager=allow','-cp',str((args.out/'host.jar').resolve())+':'+cp,'audit.lem.HostHarnessKt'],cwd=args.out.resolve(),capture_output=True,text=True,timeout=60)
    (args.out/'run.stderr.log').write_text(r.stderr)
    receipt['run_exit_code']=r.returncode
    if r.returncode:
        receipt['status']='BLOCKED_RUN'
        (args.out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        print(r.stderr, file=sys.stderr)
        return 2
    result=json.loads(r.stdout)
    # A separate strict decoder checks all U+0000..U+001F in actual rawMetrics
    # returned by production; this is a serialization contract, not an R42 literal hunt.
    controls=next(t for t in result['tests'] if t['id']=='LEM-009-JSON-CONTROL-CHAR' and t['phase']=='A')
    valid=True
    for item in controls['observed']['strictDecoderInputs']:
        try:
            decoded=json.loads(item['syntheticRawMetrics'])
            item['strict_json']={'valid':True,'preserved':decoded['error']=='synthetic'+chr(item['codepoint'])+'transport failure'}
            valid = valid and item['strict_json']['preserved']
        except json.JSONDecodeError as error:
            item['strict_json']={'valid':False,'error':str(error)}
            valid=False
    controls['status']='FAIL' if valid else 'PASS'
    next(t for t in result['tests'] if t['id']=='LEM-009-JSON-CONTROL-CHAR' and t['phase']=='B')['status']='PASS' if valid else 'FAIL'
    (args.out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    counts={}
    for phase in ['A','B','control']:
        counts[phase]=dict(collections.Counter(t['status'] for t in result['tests'] if t['phase']==phase))
    receipt['counts']=counts
    receipt['status']='EXECUTED_WITH_ACCEPTANCE_FAILURES' if counts['B'].get('FAIL') else 'EXECUTED'
    (args.out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'sha':sha,'counts':counts,'status':receipt['status']},indent=2))
    wanted={'A'} if args.phase=='reproduction' else {'B','control'} if args.phase=='acceptance' else {'A','B','control'}
    return 1 if any(t['phase'] in wanted and t['status'] in {'FAIL','BLOCKED'} for t in result['tests']) else 0

if __name__=='__main__':
    sys.exit(main())
