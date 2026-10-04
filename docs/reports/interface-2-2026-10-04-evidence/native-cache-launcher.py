#!/opt/codex/runtimes/codex-primary-runtime/dependencies/python/bin/python3
import os,sys,json,shlex,hashlib,shutil
from pathlib import Path
root=Path('/workspace/scratch/308e2340bae7/chatadhd')
reference=Path('/workspace/scratch/2a6e71927cf3/chatadhd')
args=sys.argv[1:]
manifest=json.load(open('/workspace/scratch/308e2340bae7/native-source-cache.json'))
entries=json.load(open(reference/'loom/build/dev/compile_commands.json'))
byfile={entry['file']:entry for entry in entries}
def normal(command, base):
    out=[];i=0
    while i<len(command):
        arg=command[i]
        if arg in ['-o','-MT','-MF','-MQ']:
            i+=2;continue
        if arg in ['-MD','-MMD','-O0']:
            i+=1;continue
        out.append(arg.replace(str(base),'<SOURCE>'))
        i+=1
    return out
source=Path(args[args.index('-c')+1]) if '-c' in args else None
relative=source.relative_to(root) if source and source.is_relative_to(root) else None
entry=byfile.get(str(reference/relative)) if relative else None
if entry and str(relative).startswith('loom/src/') and normal(args,root)==normal(shlex.split(entry['command']),reference):
    obj=Path(entry['output'])
    valid=obj.exists() and obj.stat().st_size>0 and all(hashlib.sha256((root/p).read_bytes()).hexdigest()==sha for p,sha in manifest.items())
    if valid:
        output=Path(args[args.index('-o')+1]);shutil.copyfile(obj,output)
        if '-MF' in args:
            Path(args[args.index('-MF')+1]).write_text(str(output)+': '+ ' '.join(str(root/p) for p in manifest)+'\n')
        with open('/workspace/scratch/308e2340bae7/native-cache-hits.log','a') as f:f.write(str(relative)+'\n')
        sys.exit(0)
os.execv(args[0],args)
