"""Integration diagnostic: byte consistency is NOT AST provenance.

Run with an explicitly built check_receipt executable (source in the integration
report). It must accept coherent forgeries; that result blocks exposing the
candidate receipt hook. The pinned Python reparse must refuse the same forgeries.
Only generated synthetic UTF-8 bytes are used; no source execution or providers.
"""
from copy import deepcopy
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from loom.tools.python_ast_basic.reader import canonical, parse, sha, validate, PythonAstError


def check(checker, scratch):
    raw = b'\xef\xbb\xbf' + '# syntetyczna Łódź\r\ndef żółw():\r\n    return "gęślą"\r\n'.encode()
    receipt = parse(raw, source_key='synthetic:trust-boundary', name='syntetyczny.py')
    validate(receipt, raw)
    declaration = receipt['declarations'][1]
    assert declaration['byte_range'] == [raw.index(b'def '), len(raw)-2]
    assert receipt['observations'][0]['text'].encode() == raw
    assert receipt['observations'][1]['text'].encode() == raw[slice(*declaration['byte_range'])]
    forgeries = {}
    renamed = deepcopy(receipt)
    renamed['declarations'][1]['name'] = 'not_in_source'
    renamed['units'][1]['title'] = 'not_in_source'
    for collection in ('units', 'observations'):
        renamed[collection][1]['attrs']['name'] = 'not_in_source'
    forgeries['coherent_name'] = renamed
    retyped = deepcopy(receipt)
    retyped['declarations'][1]['kind'] = 'class'
    retyped['units'][1]['kind'] = retyped['method']['profile']['kinds']['class']
    for collection in ('units', 'observations'):
        retyped[collection][1]['attrs']['recognized_kind'] = 'class'
    forgeries['coherent_kind'] = retyped
    repinned = deepcopy(receipt)
    repinned['method']['reader_sha256'] = '0'*64
    repinned['method_sha256'] = sha(canonical(repinned['method']).encode())
    for collection in ('units', 'observations'):
        for value in repinned[collection]:
            value['attrs']['method_sha256'] = repinned['method_sha256']
    for value in repinned['provenance']:
        value['transform'] = repinned['parser'] + ':' + repinned['method_sha256']
    forgeries['coherent_method'] = repinned
    results = {}
    with tempfile.TemporaryDirectory(prefix='ast-trust-', dir=scratch) as temporary:
        root = Path(temporary)
        source = root/'source.py'; source.write_bytes(raw)
        for name, value in [('genuine', receipt), *forgeries.items()]:
            candidate = root/(name+'.json'); candidate.write_text(canonical(value), encoding='utf-8')
            native = subprocess.run([str(checker), str(source), str(candidate)],
                                    capture_output=True, text=True, timeout=10,
                                    env={'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8'})
            assert native.returncode == 0, (name, native.returncode)
            assert native.stdout == 'byte_contract_accepted\n'
            assert native.stderr == ''
            refused = False
            try:
                validate(value, raw)
            except PythonAstError as error:
                assert error.code == 'python_ast_receipt_mismatch'
                refused = True
            assert refused == (name != 'genuine')
            results[name] = {'byte_contract_accepted': True, 'pinned_reparse_refused': refused}
    return {'schema': 'loom.python_ast_basic.trust_probe/1', 'cases': results,
            'original_sha256': sha(raw), 'bom_crlf_original_bounds': True,
            'byte_checker_is_not_ast_attestation': True, 'public_hook_accepted': False,
            'native_graph_accepted': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--byte-checker', required=True, type=Path)
    parser.add_argument('--scratch-root', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.byte_checker.resolve(), args.scratch_root.resolve()), indent=2))


if __name__ == '__main__':
    main()
