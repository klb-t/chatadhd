"""Installed catdoc adapter; original bytes only, closed argv and filesystem admission.
No author runner, user executable/path/flags, network, or inherited HOME/config.
"""
import hashlib
import os
from pathlib import Path
import resource
import subprocess


def convert(raw, policy, root, manifest):
    if not raw.startswith(bytes.fromhex('d0cf11e0a1b11ae1')) or len(raw)>policy['max_input_bytes']:
        raise ValueError('xls_input_refused')
    work=Path.cwd()
    if str(work)!=os.environ.get('TMPDIR') or str(work)!=os.environ.get('HOME'):
        raise ValueError('xls_private_workspace_required')
    root=root.resolve(strict=True)
    for tool in ('/usr/bin/xls2csv','/usr/bin/setpriv'):
        if hashlib.sha256(Path(tool).read_bytes()).hexdigest()!=manifest['external_tools'][tool]:
            raise ValueError('xls_tool_changed')
    if any(c in str(root) for c in '\n\r:\t '): raise ValueError('xls_package_path_unsupported')
    # All charset tables are hashed by the host both before and after execution.
    # The local generated config is versioned code and points only at the package.
    (work/'.catdocrc').write_text('charset_path = '+str(root/'catdoc')+'\nmap_path = '+str(root/'catdoc')+'\nsource_charset = cp1252\ntarget_charset = utf-8\nuse_locale = no\nformat = ascii\n')
    (work/'input.xls').write_bytes(raw)
    command=['/usr/bin/setpriv','--no-new-privs','--landlock-access','fs']
    for rights,path in [('execute,read-file','/usr/bin/xls2csv'),('execute,read-file,read-dir','/usr/lib'),
                        ('read-file,read-dir',root/'catdoc'),('read-file,read-dir',work)]:
        command+=['--landlock-rule','path-beneath:'+rights+':'+str(path)]
    command+=['/usr/bin/xls2csv','-d','utf-8','-q','3','-g','15','-b','\f',str(work/'input.xls')]
    # Denied /etc/catdocrc and other homes; no fallback if Landlock is unavailable.
    # Already opened output FDs remain writable; no filename write permission.
    def bounds():
        resource.setrlimit(resource.RLIMIT_CPU,(policy['cpu_seconds'],policy['cpu_seconds']))
        resource.setrlimit(resource.RLIMIT_AS,(policy['memory_bytes'],policy['memory_bytes']))
        resource.setrlimit(resource.RLIMIT_FSIZE,(policy['max_output_bytes'],policy['max_output_bytes']))
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    with (work/'stdout').open('xb') as out,(work/'stderr').open('xb') as err:
        p=subprocess.run(command,stdin=subprocess.DEVNULL,stdout=out,stderr=err,cwd=work,
                         env={'HOME':str(work),'LC_ALL':'C','TZ':'UTC'},timeout=policy['timeout_seconds'],preexec_fn=bounds)
    if p.returncode!=0: raise ValueError('xls_tool_refused')
    output=(work/'stdout').read_bytes();diagnostic=(work/'stderr').read_bytes()
    if len(output)>=policy['max_output_bytes'] or len(diagnostic)>=policy['max_output_bytes']:raise ValueError('xls_output_limit')
    return output,hashlib.sha256(diagnostic).hexdigest()
