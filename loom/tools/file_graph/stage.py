"""Stage reviewed adapters beside the existing bounded transport; no installation/download."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path

def stage(source,destination,python):
    python=python.resolve(strict=True);destination.mkdir(parents=True,exist_ok=True)
    paths=['file_graph/worker.py','file_graph/profile.json','unknown_structure/contract.py',
      'xlsx_basic/__init__.py','xlsx_basic/reader.py','xlsx_basic/container.py','xlsx_basic/preset.json',
      'odt_basic/__init__.py','odt_basic/adapter.py','odt_basic/reader.py','odt_basic/container.py','odt_basic/limits.json',
      'epub_basic/reader.py']
    hashes={}
    for path in paths:
        target=path.removeprefix('file_graph/');p=destination/target;p.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source/path,p);hashes[target]=hashlib.sha256(p.read_bytes()).hexdigest()
    manifest={'schema':'loom.file_graph.runtime/1','python':str(python),'python_sha256':hashlib.sha256(python.read_bytes()).hexdigest(),'files':hashes}
    (destination/'runtime.json').write_text(json.dumps(manifest,indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--destination',type=Path,required=True);p.add_argument('--python',type=Path,required=True)
    a=p.parse_args();stage(a.source,a.destination,a.python)
