#!/usr/bin/env python3
"""Capture unchanged old-API probes against actual whole-core baseline/current archives."""
import argparse, datetime, hashlib, json, os, shlex, shutil, subprocess
from pathlib import Path
NAMES = ('memory_selector','semantics_graph_materialize','worker_media_github','net_model','util','regex')
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1048576),b''): h.update(chunk)
    return h.hexdigest()
def record(path):
    return {'path':str(path),'bytes':path.stat().st_size,'sha256':sha(path)}
def source(name, repo_root): return repo_root/f'loom/tests/compat/profile_{name}_parity.cc'
def discover_repo_root():
    # The saved runner lives inside the repository; scratch copies may sit
    # beside a checkout named chatadhd. No execution-workspace ID is fixed.
    for parent in Path(__file__).resolve().parents:
        for candidate in (parent, parent/'chatadhd'):
            if (candidate/'loom/tests/compat').is_dir():
                return candidate
    return None
def command(src, root, build, executable):
    loom=root/'loom'
    return ['c++','-DCPPHTTPLIB_OPENSSL_SUPPORT=1','-DJSON_USE_IMPLICIT_CONVERSIONS=1','-DLOOM_BUILDING=1',
            '-DLOOM_HAVE_OPENSSL=1','-DLOOM_VENDORED_SQLITE=1','-DLOOM_VERSION_STRING="0.1.0"',
            '-I'+str(loom/'include'),'-I'+str(loom/'src'),
            *[a for d in ('nlohmann','cpp-httplib','sqlite','miniz') for a in ('-isystem',str(loom/'third_party'/d))],
            '-std=c++20','-O0','-g0',str(src),str(build/'libloom_core.a'),str(build/'libloom_miniz.a'),
            str(build/'libloom_sqlite3_amalgamation.a'),'-pthread','-ldl','-lm','-lssl','-lcrypto','-o',str(executable)]
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--stage',choices=('baseline-extra','after'),required=True)
    ap.add_argument('--repo-root',type=Path,help='current checkout; default: discover from this script ancestors')
    ap.add_argument('--baseline-root',type=Path,help='frozen checkout; default: sibling <repo-name>-baseline')
    ap.add_argument('--baseline-build',type=Path,help='frozen full build; default: sibling baseline-build')
    ap.add_argument('--current-build',type=Path,help='current full build; default: sibling current-build')
    ap.add_argument('--scratch-root',type=Path,help='probe executables/run dirs; default: repository parent')
    ap.add_argument('--only',choices=NAMES)
    args=ap.parse_args()
    selected_root = args.repo_root or discover_repo_root()
    if selected_root is None: ap.error('cannot discover a checkout; provide --repo-root')
    ROOT = selected_root.expanduser().resolve()
    TASK = (args.scratch_root or ROOT.parent).expanduser().resolve()
    BASE_ROOT = (args.baseline_root or ROOT.parent/(ROOT.name+'-baseline')).expanduser().resolve()
    BASE_BUILD = (args.baseline_build or ROOT.parent/'baseline-build').expanduser().resolve()
    CURRENT_BUILD = (args.current_build or ROOT.parent/'current-build').expanduser().resolve()
    OUT = ROOT/'docs/reports/data-in-code/evidence/fullcore161'
    TASK.mkdir(parents=True,exist_ok=True)
    OUT.mkdir(parents=True,exist_ok=True)
    before=args.stage=='baseline-extra'
    variant='before' if before else 'after'
    root=BASE_ROOT if before else ROOT
    build=BASE_BUILD if before else CURRENT_BUILD
    names=(args.only,) if args.only else (NAMES[3:] if before else NAMES)
    original_receipt=json.loads((ROOT/'docs/reports/data-in-code/evidence/baseline161/receipt.json').read_text())
    old={p['name']:p for p in original_receipt['probes']}
    receipt={'schema':'loom.offline_fullcore_parity_receipt/1','created_at_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'variant':variant,'baseline_commit':original_receipt['baseline_commit'],
             'workspace_paths':{'repo_root':str(ROOT),'baseline_root':str(BASE_ROOT),'baseline_build':str(BASE_BUILD),'current_build':str(CURRENT_BUILD),'scratch_root':str(TASK)},
             'library_mode':'actual whole libloom_core.a; no changed-object/older-library fallback',
             'synthetic':True,'network':'Offline fixtures/ScriptedTransport only; Runtime start_workers=false.',
             'compiler':subprocess.check_output(['c++','--version']).decode(),
             'build':str(build),'libraries':[record(build/lib) for lib in ('libloom_core.a','libloom_miniz.a','libloom_sqlite3_amalgamation.a')],
             'build_metadata':[record(build/file) for file in ('compile_commands.json','CMakeCache.txt') if (build/file).exists()],
             'probe_receipts':[],'baseline_three_receipt':'../baseline161/receipt.json', 'capture_script':record(Path(__file__).resolve())}
    if not before:
        receipt['consumer_artifacts']=[record(build/file) for file in ('cli/loom','server/loom-server','libloom.so.0.1.0')]
        receipt['current_commit']=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()
        receipt['current_git_status_porcelain']=subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).decode()
    for name in names:
        original=source(name, ROOT)
        snapshot=OUT/original.name
        if snapshot.exists() and snapshot.read_bytes()!=original.read_bytes():
            raise ValueError('Probe source changed after snapshot: '+name)
        if name in old and sha(original)!=old[name]['source_sha256']:
            raise ValueError('Old-API probe source differs from exact161 receipt: '+name)
        if not snapshot.exists(): shutil.copyfile(original,snapshot)
        if name in old:
            frozen=ROOT/'docs/reports/data-in-code/evidence/baseline161'/f'{name}.json'
            saved_before=OUT/f'{name}.before.json'
            if saved_before.exists():
                if saved_before.read_bytes()!=frozen.read_bytes():
                    raise ValueError('Frozen before output differs from original receipt: '+name)
            else:
                shutil.copyfile(frozen,saved_before)
        executable=TASK/f'fullcore161-{name}-{variant}-probe'
        cmd=command(snapshot,root,build,executable)
        work=TASK/f'fullcore161-{name}-{variant}-work'
        work.mkdir(parents=True,exist_ok=True)
        row={'name':name,'source':record(snapshot),'compile_command':cmd,'compile_cwd':str(ROOT),
             'run_command':[str(executable)],'run_cwd':str(work),'core_only':False,'runtime_path':name=='semantics_graph_materialize'}
        script=['#!/usr/bin/env bash','set -euo pipefail','# Same old-API source; actual whole-core archive. No mixed objects.',shlex.join(cmd),
                'mkdir -p '+shlex.quote(str(work)),'cd '+shlex.quote(str(work))]
        compiled=subprocess.run(cmd,cwd=ROOT,capture_output=True)
        row['compile_exit']=compiled.returncode
        (OUT/f'{name}.{variant}.compile.log').write_bytes(compiled.stdout+compiled.stderr)
        receipt['probe_receipts'].append(row)
        if compiled.returncode:
            row['valid']=False
            (OUT/f'{name}.{variant}.command.sh').write_text('\n'.join(script)+'\n')
            continue
        runenv=os.environ.copy()
        # Avoid uncontrolled inherited runtime presets without enumerating any secrets.
        for key in ('LOOM_LOG_LEVEL','LOOM_LOG_STDERR','LOOM_HTTP_BACKEND'):
            runenv.pop(key,None)
        if name=='util':
            combined={}
            runs=[]
            for scenario,level in (('default',None),('debug','DEBUG'),('warning','warn'),('error','error'),('unknown','unrecognized'),('fallback',None)):
                env=runenv.copy();env['LOOM_LOG_STDERR']='0'
                if level is not None:env['LOOM_LOG_LEVEL']=level
                else:env.pop('LOOM_LOG_LEVEL',None)
                if scenario=='fallback':env['TMPDIR']=str(OUT/'absent-system-temp-fixture')
                output=OUT/f'util-{scenario}.{variant}.json';err=OUT/f'util-{scenario}.{variant}.stderr.log'
                p=subprocess.run([str(executable)],cwd=work,env=env,capture_output=True)
                output.write_bytes(p.stdout);err.write_bytes(p.stderr)
                r={'scenario':scenario,'exit':p.returncode,'environment_overrides':{k:env[k] for k in ('LOOM_LOG_STDERR','LOOM_LOG_LEVEL','TMPDIR') if k in env},'stdout':record(output),'stderr':record(err)}
                if not p.returncode: combined[scenario]=json.loads(p.stdout)
                runs.append(r)
                unsets='env -u LOOM_LOG_LEVEL -u LOOM_HTTP_BACKEND'
                env_args=[f'{k}={v}' for k,v in r['environment_overrides'].items()]
                script.append(unsets+' '+shlex.join(env_args+[str(executable)])+' > '+shlex.quote(str(output))+' 2> '+shlex.quote(str(err)))
            row['runs']=runs;row['run_exit']=max(r['exit'] for r in runs)
            output=OUT/f'{name}.{variant}.json'
            output.write_bytes((json.dumps(combined,ensure_ascii=False,separators=(',',':'))+'\n').encode())
            row['environment_cases']=len(combined)
        else:
            p=subprocess.run([str(executable)],cwd=work,env=runenv,capture_output=True)
            row['run_exit']=p.returncode
            output=OUT/f'{name}.{variant}.json';err=OUT/f'{name}.{variant}.stderr.log'
            output.write_bytes(p.stdout);err.write_bytes(p.stderr)
            script.append('env -u LOOM_LOG_LEVEL -u LOOM_LOG_STDERR -u LOOM_HTTP_BACKEND '+shlex.quote(str(executable))+' > '+shlex.quote(str(output))+' 2> '+shlex.quote(str(err)))
            row['stderr']=record(err)
        row['stdout']=record(output)
        row['valid']=row['run_exit']==0
        if row['valid']:
            data=json.loads(output.read_bytes());row['top_level_keys']=list(data)
            if name=='semantics_graph_materialize':row['products']=len(data.get('products',[]))
            if name=='regex':row['rows']=len(data['rows']);row['budgets']=len(data['budgets'])
            if not before:
                original_output=OUT/f'{name}.before.json'
                if not original_output.exists():raise ValueError('Missing frozen before output '+name)
                row['before_stdout']=record(original_output)
                row['byte_parity']=original_output.read_bytes()==output.read_bytes()
                row['valid']=row['byte_parity']
                if name=='util':
                    row['raw_environment_byte_parity']={s:(OUT/f'util-{s}.before.json').read_bytes()==(OUT/f'util-{s}.after.json').read_bytes() for s in combined}
                    row['valid']=row['valid'] and all(row['raw_environment_byte_parity'].values())
        (OUT/f'{name}.{variant}.command.sh').write_text('\n'.join(script)+'\n')
        (OUT/(f'receipt-{variant}.json' if not args.only else f'receipt-{variant}-{args.only}.json')).write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n')
        print(json.dumps({'probe':name,'variant':variant,'compile_exit':row['compile_exit'],'run_exit':row['run_exit'],'valid':row['valid'],'bytes':row['stdout']['bytes'],'sha256':row['stdout']['sha256'],'byte_parity':row.get('byte_parity')}),flush=True)
    receipt['valid']=all(row.get('valid',False) for row in receipt['probe_receipts'])
    filename=f'receipt-{variant}.json' if not args.only else f'receipt-{variant}-{args.only}.json'
    (OUT/filename).write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n')
    return 0 if receipt['valid'] else 1
if __name__=='__main__':raise SystemExit(main())
