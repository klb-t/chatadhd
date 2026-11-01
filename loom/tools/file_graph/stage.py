"""Stage reviewed adapters beside the existing bounded transport; no installation/download."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path

def stage(source,destination,python):
    python=python.resolve(strict=True);destination.mkdir(parents=True,exist_ok=True)
    paths=['file_graph/worker.py','file_graph/profile.json','unknown_structure/contract.py','unknown_structure/xls.py','file_graph/local_xls.py',
      'xlsx_basic/__init__.py','xlsx_basic/reader.py','xlsx_basic/container.py','xlsx_basic/preset.json',
      'odt_basic/__init__.py','odt_basic/adapter.py','odt_basic/reader.py','odt_basic/container.py','odt_basic/limits.json',
      'epub_basic/reader.py','video_basic/video_basic.py','video_basic/limited_exec.py','video_basic/defaults.json','file_graph/local_video.py','file_graph/local_bundle.py']
    hashes={}
    for path in paths:
        target=path.removeprefix('file_graph/');p=destination/target;p.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source/path,p);hashes[target]=hashlib.sha256(p.read_bytes()).hexdigest()
    for name in ['bundle.py','preset.json']:
        path=source.parent/'src/import/conversation_bundle'/name;target='conversation_bundle/'+name
        (destination/'conversation_bundle').mkdir(exist_ok=True);shutil.copyfile(path,destination/target)
        hashes[target]=hashlib.sha256(path.read_bytes()).hexdigest()
    policy=source.parent/'src/import/video_source_plan/policy.json'
    shutil.copyfile(policy,destination/'video-policy.json');hashes['video-policy.json']=hashlib.sha256(policy.read_bytes()).hexdigest()
    external={}
    for tool in ['/usr/bin/xls2csv','/usr/bin/setpriv','/usr/bin/ffmpeg','/usr/bin/ffprobe']:
        external[tool]=hashlib.sha256(Path(tool).read_bytes()).hexdigest()
    charsets=Path('/usr/share/catdoc')
    for file in sorted(charsets.iterdir()):
        if not file.is_file() or file.is_symlink(): raise ValueError('catdoc_charset_not_regular')
        target='catdoc/'+file.name;p=destination/target;p.parent.mkdir(exist_ok=True)
        shutil.copyfile(file,p);hashes[target]=hashlib.sha256(p.read_bytes()).hexdigest()
    manifest={'external_tools':external,'schema':'loom.file_graph.runtime/1','python':str(python),'python_sha256':hashlib.sha256(python.read_bytes()).hexdigest(),'files':hashes}
    (destination/'runtime.json').write_text(json.dumps(manifest,indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--destination',type=Path,required=True);p.add_argument('--python',type=Path,required=True)
    a=p.parse_args();stage(a.source,a.destination,a.python)
