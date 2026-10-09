#!/usr/bin/env python3
import argparse,os,pathlib,subprocess,tempfile,sys
p=argparse.ArgumentParser();p.add_argument('--repo',type=pathlib.Path,required=True);p.add_argument('--sha',required=True);p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args();a.repo=a.repo.resolve();a.out=a.out.resolve();a.out.parent.mkdir(parents=True,exist_ok=True)
assert subprocess.check_output(['git','-C',str(a.repo),'rev-parse','HEAD'],text=True).strip()==a.sha
subprocess.run(['git','-C',str(a.repo),'diff','--exit-code',a.sha,'--'],check=True,stdout=subprocess.DEVNULL)
with tempfile.TemporaryDirectory(prefix='audit-wd-resources-') as tmp:
 env={**os.environ,'AUDIT_RESOURCE_REPO':str(a.repo),'AUDIT_RESOURCE_SHA':a.sha,'AUDIT_RESOURCE_OUT':str(a.out),'AUDIT_RESOURCE_TMP':tmp,'DB_PATH':str(pathlib.Path(tmp)/'incidental.sqlite'),'WATCHDOG_DIAGNOSTICS_DIR':str(pathlib.Path(tmp)/'diagnostics')}
 sys.exit(subprocess.run(['node','--import',str(a.repo/'node_modules/tsx/dist/loader.mjs'),str(pathlib.Path(__file__).with_name('probe.ts'))],cwd=a.repo,env=env).returncode)
