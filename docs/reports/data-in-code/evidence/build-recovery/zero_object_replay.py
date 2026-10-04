#!/usr/bin/env python3
"""Deterministic copy-only replay of a zero-sized object; never build/link/edit originals."""
import argparse, hashlib, json, pathlib, shutil, tempfile
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("object",type=pathlib.Path)
parser.add_argument("--receipt",type=pathlib.Path,required=True)
args=parser.parse_args()
source=args.object.resolve()
before=source.read_bytes()
assert before[:4]==b"\x7fELF" and len(before)>64,"Provide an existing nonempty ELF object"
before_hash=hashlib.sha256(before).hexdigest()
with tempfile.TemporaryDirectory(prefix="copy-only-zero-object-") as temporary:
    copy=pathlib.Path(temporary)/"app.cpp.o"
    shutil.copyfile(source,copy)
    assert copy.read_bytes()==before
    copy.write_bytes(b"")
    corrupted=copy.read_bytes()
    assert len(corrupted)==0 and corrupted[:4]!=b"\x7fELF"
    after=source.read_bytes()
    assert after==before,"Readonly source unexpectedly changed"
    receipt={"schema":"loom.zero_object_copy_replay/1","classification":"Synthetic deterministic copy-only replay; not original failed server object","input_path":str(source),"input_bytes":len(before),"input_sha256":before_hash,"input_unchanged":True,"copied_object_bytes":len(corrupted),"copied_object_sha256":hashlib.sha256(corrupted).hexdigest(),"copied_object_is_elf":False,"expected_diagnostic_class":"empty object cannot supply App symbols or be a valid archive member","assertions":4,"compiler_executed":False,"linker_executed":False,"original_build_directory_mutated":False,"operations":["read+hash ELF input","copy into isolated temporary directory","truncate copy to zero","validate size/header and source bytes unchanged","remove temporary copies"]}
args.receipt.parent.mkdir(parents=True,exist_ok=True)
args.receipt.write_text(json.dumps(receipt,indent=2)+"\n")
print(json.dumps(receipt,sort_keys=True))
