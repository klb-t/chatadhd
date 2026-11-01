"""Small relocated default-helper probe; native/browser semantics are tested separately."""
import json,runpy,sys
from pathlib import Path
helpers=runpy.run_path(str(Path(__file__).resolve().parents[1]/'structure_readers/verify_native.py'))
Native=helpers['Native']
stage,fixture,root=(Path(x).resolve() for x in sys.argv[1:]);root.mkdir(mode=0o700)
n=Native(stage/'lib/libloom.so',root)
try:
    digest=n.upload(fixture.read_bytes())
    made=n.ok('client.blob_text',{'hash':digest,'source_key':'synthetic:relocated','name':'documents.zip','format':'zip'})
    assert len(made['children'])==2
    pdf=next(x for x in made['children'] if x['method']['format']=='pdf')
    book=next(x for x in made['children'] if x['method']['format']=='xlsx')
    assert len(pdf['structure_nodes']['pages'])==2 and len(pdf['structure_nodes']['chapters'])==2
    i=next(i for i,o in enumerate(book['observations']) if o['text']=='Żółw WYBRANY')
    read=n.ok('client.blob_text_read',{'node_id':book['occurrence_nodes'][i]});assert read['text']=='Żółw WYBRANY'
    print(json.dumps({'relocated_default_helpers':'PASS','archive_sha256':digest,'children':['pdf','xlsx'],'coordinate':read['provenance']['coordinate'],'checkout_helpers_available':False}))
finally:n.close()
