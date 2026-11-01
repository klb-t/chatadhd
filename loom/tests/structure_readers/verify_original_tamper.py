"""One extra fault gate on a NEW copy of the closed synthetic native fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import shutil


def run(library, source, target, result_file):
    api=runpy.run_path(str(Path(__file__).with_name('verify_native.py')))
    result=json.loads(result_file.read_text());receipt=result['csv_receipt']
    shutil.copytree(source,target)
    native=api['Native'](library,target)
    digest=receipt['source_sha256']
    files=[p for p in target.rglob('*') if p.is_file() and p.name==digest]
    assert len(files)==1
    path=files[0];raw=path.read_bytes();mode=path.stat().st_mode&0o777
    assert hashlib.sha256(raw).hexdigest()==digest
    def replace(data):
        tmp=path.with_name(path.name+'.synthetic-fault');tmp.write_bytes(data);tmp.chmod(mode);tmp.replace(path)
    try:
        before=api['counts'](target)
        replace(raw+b'changed-source')
        read=native.invoke('client.blob_text_read',{'node_id':result['selected_cell']})
        write=native.invoke('client.blob_text',{'hash':digest,'format':'csv','name':'żółć.csv','source_key':'synthetic:host-csv'})
        assert 'error' in read and 'error' in write
        assert api['counts'](target)==before
    finally:
        replace(raw);native.close()
    assert path.read_bytes()==raw
    print(json.dumps({'source_corruption_refused_on_read':True,'source_corruption_refused_before_materialization':True,'database_counts_unchanged':True,'restored_hash':digest,'original_fixture_unchanged':True},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--target',type=Path,required=True);p.add_argument('--result',type=Path,required=True);a=p.parse_args()
    run(a.library.resolve(),a.source.resolve(),a.target.resolve(),a.result.resolve())
