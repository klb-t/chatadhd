"""One default-package resolver sentinel after extracting the shared transport."""
import argparse
import json
from pathlib import Path
import runpy

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--fixture',type=Path,required=True);a=p.parse_args()
    Native=runpy.run_path(str(Path(__file__).with_name('verify_native.py')))['Native']
    a.data.mkdir(mode=0o700)
    n=Native(a.library.resolve(),a.data.resolve())
    try:
        digest=n.upload(a.fixture.read_bytes())
        result=n.ok('client.blob_text',{'hash':digest,'source_key':'synthetic:default-docx','name':'synthetic.docx','format':'docx'})
        assert len(result['occurrence_nodes'])==11
        assert result['derivation']['original_sha256']==digest
        read=n.ok('client.blob_text_read',{'node_id':result['occurrence_nodes'][2]})
        assert read['text']=='Wewnątrz\nakapitu.\n'
        assert read['provenance']['coordinate']=='derived_utf8_bytes'
    finally:n.close()
    print(json.dumps({'default_docx_package_resolver':True,'source_sha256':digest,'parts':11,'polish_derived_read':True}))
