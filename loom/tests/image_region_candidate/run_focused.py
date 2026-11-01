#!/usr/bin/env python3
"""One small stdlib C++ fixture binary; no backend, dependency install or CMake.

Python only serializes the checked-in JSON fixture to C++ DTOs. It does not
reimplement validation. UTF-8/SHA implementations are the existing Loom code.
Unreferenced Unicode casing functions are discarded by function-section GC.
"""
import hashlib
import json
from pathlib import Path
import resource
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent

def capacity():
    if shutil.disk_usage(ROOT).free < 1024**3:
        raise SystemExit('BLOCKED_CAPACITY: repository filesystem free < 1 GiB')

def quoted(s):
    return json.dumps(s, ensure_ascii=False)

def array(xs):
    return '{' + ','.join(quoted(x) if isinstance(x, str) else str(x) for x in xs) + '}'

def main():
    capacity()
    raw = (HERE / 'representation.json').read_bytes()
    original = (HERE / 'original.txt').read_bytes()
    doc = json.loads(raw)
    assert hashlib.sha256(original).hexdigest() == doc['source_sha256']
    digest = hashlib.sha256(raw).hexdigest()
    source = 'src_ocr_' + digest
    regions = ','.join('Region{' + ','.join([
        quoted(r['id']), quoted(r['frame']), quoted(r['text']), array(r['origin']), array(r['size'])]) + '}' for r in doc['regions'])
    frames = ','.join('Frame{' + ','.join([
        quoted(f['id']), array(f['origin']), array(f['extent']), array(f['axes']), array(f['units']), quoted(f['orientation'])]) + '}' for f in doc['frames'])
    header = '''struct Fixture { Selection selection; Document document; Source representation, original; std::string bytes; };
Fixture fixture() {
Fixture f;
'''
    header += f'f.bytes={quoted(raw.decode())};\n'
    header += f'f.representation=Source{{{quoted(source)},{quoted(digest)},{len(raw)}}};\n'
    header += f'f.original=Source{{"src_original",{quoted(doc["source_sha256"])},{len(original)}}};\n'
    header += 'f.document=Document{' + ','.join([quoted(doc[k]) for k in ['schema','status','source_sha256','evidence_class']]) + ',true,{' + regions + '},{' + frames + '}};\n'
    text = quoted(doc['regions'][0]['text'])
    header += f'f.selection=Selection{{{text},{quoted(source)},{quoted(source)},"/regions/0",{quoted(source)},"/regions/0/text",{text},0,"src_original",{quoted(doc["source_sha256"])},"inferred",true}};\nreturn f;\n}}\n'
    with tempfile.TemporaryDirectory(prefix='ocr-region-focused-') as temp:
        out = Path(temp)
        (out / 'fixture_generated.h').write_text(header)
        # Per-process cap, well below the fleet RAM/output allowance.
        def limits():
            resource.setrlimit(resource.RLIMIT_AS, (512*1024**2, 512*1024**2))
            resource.setrlimit(resource.RLIMIT_FSIZE, (16*1024**2, 16*1024**2))
        cmd = ['c++','-std=c++20','-O0','-g0','-Wall','-Wextra','-Werror',
               '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
               '-I'+str(ROOT/'loom/include'),'-I'+str(ROOT/'loom/src'),'-I'+str(out),
               str(HERE/'contract_test.cc'), str(ROOT/'loom/src/context/image_region_candidate/contract.cpp'),
               str(ROOT/'loom/src/util/sha256.cpp'), str(ROOT/'loom/src/util/utf8.cpp'),
               '-o',str(out/'contract_test')]
        capacity()
        subprocess.run(cmd, check=True, timeout=60, preexec_fn=limits)
        capacity()
        subprocess.run([str(out/'contract_test')], check=True, timeout=10, preexec_fn=limits)
        print('temporary binary bytes:', (out/'contract_test').stat().st_size)

if __name__ == '__main__':
    main()
