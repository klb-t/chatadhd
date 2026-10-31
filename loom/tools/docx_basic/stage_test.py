#!/usr/bin/env python3
"""Stage the runtime component locally and test it with checkout access denied.

Run from the repository root. Requires the already built own loom/server targets
and Linux setpriv with Landlock. Never installs to the host prefix.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',required=True)
    parser.add_argument('--stage',required=True)
    args=parser.parse_args()
    root=Path.cwd().resolve();build=Path(args.build).resolve();stage=Path(args.stage).resolve()
    if not build.is_relative_to(root) or not stage.is_relative_to(root) or stage==root:
        parser.error('build and new staging directory must be inside this checkout')
    if stage.exists():parser.error('use a new staging directory; old evidence is preserved')
    stage.mkdir(parents=True)
    subprocess.run(['cmake','--install',str(build),'--prefix',str(stage),'--component','runtime'],check=True)
    support=['server_flow_harness.py','test_server_docx_flow.py','test_server_guest_credentials.py',
             'test_server_guests.py','test_server_security.py','test_docx_package.py']
    for name in support:
        target=stage/'loom/server/tests'/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(root/'loom/server/tests'/name,target)
    for name in ['tests/fixtures.py','chat_context_additions.json']:
        target=stage/'loom/tools/docx_basic'/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(root/'loom/tools/docx_basic'/name,target)
    (stage/'tmp').mkdir()
    # Relocation is part of the test: neither configured prefix nor checkout paths
    # are consulted by the native resolver.
    moved=stage.with_name(stage.name+'-relocated')
    if moved.exists():raise RuntimeError('relocated staging destination already exists')
    stage.rename(moved);stage=moved
    env={k:os.environ[k] for k in ['PATH','LANG','LC_ALL'] if k in os.environ}
    env.update(TMPDIR=str(stage/'tmp'),PYTHONDONTWRITEBYTECODE='1',OMP_THREAD_LIMIT='1',
        LOOM_PACKAGE_ROOT=str(stage),LOOM_SERVER_BIN='bin/loom-server',
        LOOM_FLOW_EVIDENCE_DIR=str(stage/'evidence'),
        LOOM_DENIED_SOURCE_PATHS=json.dumps([str(root/'loom/tools/docx_basic/reader.py'),
            str(root/'loom/src/chat/docx_basic/adapter.cpp'),str(build/'server/loom-server')]))
    command=['setpriv','--no-new-privs','--landlock-access','fs',
        '--landlock-rule','path-beneath:execute,read-file,read-dir:/usr',
        '--landlock-rule','path-beneath:read-file,read-dir:/proc',
        '--landlock-rule','path-beneath:read-file,write-file:/dev/null',
        '--landlock-rule','path-beneath:execute,write-file,read-file,read-dir,remove-dir,remove-file,make-dir,make-reg,make-sym,refer,truncate:'+str(stage),
        '/usr/bin/python3','-B',str(stage/'loom/server/tests/test_docx_package.py')]
    (stage/'test-command.json').write_text(json.dumps({'command':command,'scope':'default package; checkout denied; synthetic HTTP'},indent=2)+'\n')
    result=subprocess.run(command,cwd=stage,env=env)
    (stage/'test-result.json').write_text(json.dumps({'exit_code':result.returncode})+'\n')
    return result.returncode

if __name__=='__main__':raise SystemExit(main())
