#!/usr/bin/env python3
"""Independent synthetic import evidence; never imports repository test helpers."""
import argparse
import hashlib
import json
import pathlib
import sqlite3
import subprocess
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
BASE = ROOT / 'docs/research/w6_evidence_2026-09-30'
FIX = BASE / 'import_fixtures'

def digest(data):
    return hashlib.sha256(data).hexdigest()

def encoded(obj):
    return (json.dumps(obj, ensure_ascii=False, indent=2) + '\n').encode()

def leaves(obj, path=''):
    if isinstance(obj, dict):
        if not obj:
            yield path, 'empty_object', '{}'
        for key, value in obj.items():
            yield from leaves(value, path + '/' + key.replace('~', '~0').replace('/', '~1'))
    elif isinstance(obj, list):
        if not obj:
            yield path, 'empty_array', '[]'
        for i, value in enumerate(obj):
            yield from leaves(value, path + '/' + str(i))
    else:
        yield path, type(obj).__name__, json.dumps(obj, ensure_ascii=False)

def make_zip(path, members):
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_STORED) as archive:
        for name, raw in members.items():
            info = zipfile.ZipInfo(name, date_time=(2026, 9, 30, 0, 0, 0))
            archive.writestr(info, raw)

def generate():
    if (FIX / 'manifest.json').exists():
        raise SystemExit('Refusing to overwrite frozen fixture manifest')
    FIX.mkdir(parents=True, exist_ok=True)
    odd = {'tilde~/key': [False, 0, None, {}, [], 'Zażółć 🧪 e\u0301 é \x00 końcówka']}
    def msg(key, role, text, metadata=None):
        return {'id': key, 'author': {'role': role}, 'content': {'content_type': 'text', 'parts': [text]},
                'create_time': 1700000000, 'metadata': metadata or {}, 'future': odd}
    openai = {'id': 'oa-independent', 'title': 'W6 źródło', 'current_node': 'a/current~1', 'create_time': 1700000000,
              'future': odd, 'mapping': {
        'root': {'id': 'root', 'parent': None, 'children': ['u/~0'], 'message': None, 'future': odd},
        'u/~0': {'id': 'u/~0', 'parent': 'root', 'children': ['a/rejected', 'a/current~1'],
                 'message': msg('mu', 'user', 'Nie przyjmuj odrzuconego wariantu.',
                   {'attachments': [{'id': 'file-W6audit', 'name': 'dane.bin', 'mime_type': 'application/octet-stream'}]})},
        'a/rejected': {'id': 'a/rejected', 'parent': 'u/~0', 'children': [], 'message': msg('mr', 'assistant', 'ODRZUCONY')},
        'a/current~1': {'id': 'a/current~1', 'parent': 'u/~0', 'children': [], 'message': msg('mc', 'assistant', 'AKTUALNY 🧪')}
    }}
    def amsg(key, parent, sender, text):
        return {'uuid': key, 'parent_message_uuid': parent, 'sender': sender, 'text': text,
                'created_at': '2026-09-30T12:00:00Z', 'content': [{'type': 'text', 'text': text},
                {'type': 'future_widget', 'payload': odd}], 'attachments': [{'file_name': 'notatki.txt',
                'extracted_content': 'Cytat nie jest stanowiskiem autora.', 'future': odd}], 'future': odd}
    anthropic = {'uuid': 'an-independent', 'name': 'W6 kolejność', 'current_leaf_message_uuid': 'child/~current',
                 'future': odd, 'chat_messages': [amsg('child/~current', 'parent', 'assistant', 'Nowa decyzja'),
                 amsg('parent', None, 'human', 'Wyjątek: tylko syntetyczne dane.'),
                 amsg('child/rejected', 'parent', 'assistant', 'Poprzednia propozycja')]}
    cases = {'openai': [openai], 'anthropic': [anthropic]}
    for name, obj in cases.items():
        (FIX / (name + '.json')).write_bytes(encoded(obj))
    make_zip(FIX / 'openai.zip', {'conversations.json': encoded(cases['openai']),
        'file-W6audit-dane.bin': bytes(range(256)) + b'\x00\xffW6',
        'file-W6orphan.bin': b'orphan\x00\xfe', 'future-unknown.json': encoded({'x': odd})})
    make_zip(FIX / 'anthropic.zip', {'conversations.json': encoded(cases['anthropic']),
        'unknown-member.bin': b'unknown\x00\xff\x01', 'future-unknown.json': encoded({'x': odd})})
    (FIX / 'wrapped.json').write_bytes(encoded({'conversations': [openai], 'unknown_wrapper': odd}))
    files = [BASE / 'import_protocol.md', pathlib.Path(__file__)] + sorted(FIX.iterdir())
    manifest = {'baseline': 'b118c80e981c08ec6d7f9ab6aacc177979186cf2',
                'files': {str(p.relative_to(ROOT)): digest(p.read_bytes()) for p in files},
                'expectations': {'openai_messages': 3, 'anthropic_messages': 3,
                                 'openai_statuses': {'u/~0': 'active', 'a/rejected': 'version', 'a/current~1': 'active'},
                                 'anthropic_statuses': {'parent': 'active', 'child/rejected': 'version', 'child/~current': 'active'}}}
    (FIX / 'manifest.json').write_bytes(encoded(manifest))
    print(json.dumps({'manifest_sha256': digest((FIX / 'manifest.json').read_bytes())}))

def evaluate(binary, out):
    manifest = json.loads((FIX / 'manifest.json').read_text())
    for name, expected in manifest['files'].items():
        if digest((ROOT / name).read_bytes()) != expected:
            raise SystemExit('Frozen input changed: ' + name)
    out.mkdir(parents=True, exist_ok=False)
    checks = []
    def check(case, name, passed, evidence):
        checks.append({'case': case, 'check': name, 'pass': bool(passed), 'evidence': evidence})
    # Scorer controls exist independently of native output.
    check('oracle', 'changed_scalar_detected', list(leaves({'x': False})) != list(leaves({'x': 0})), 'bool != integer')
    check('oracle', 'changed_byte_detected', digest(b'abc') != digest(b'abd'), 'single-byte mutation')
    observed = []
    for case, filename, provider in [('openai', 'openai.zip', 'openai'), ('anthropic', 'anthropic.zip', 'anthropic'), ('wrapped', 'wrapped.json', 'openai')]:
        inp = FIX / filename
        work = out / case
        cmd = [str(binary), '--data-dir', str(work), '--json', 'import', str(inp), '--export-mode', 'on']
        run = subprocess.run(cmd, capture_output=True)
        (out / (case + '.stdout.json')).write_bytes(run.stdout)
        (out / (case + '.stderr.txt')).write_bytes(run.stderr)
        check(case, 'native_exit', run.returncode == 0, run.returncode)
        if run.returncode:
            continue
        report = json.loads(run.stdout)
        db = sqlite3.connect('file:' + str(work / 'chatadhd.db') + '?mode=ro', uri=True)
        db.row_factory = sqlite3.Row
        conversations = list(db.execute('select * from conversations'))
        rows = [dict(row) for row in db.execute('select rowid, * from messages order by rowid')]
        for row in rows:
            row['metadata'] = json.loads(row['metadata'])
            row['attachments'] = json.loads(row['attachments'])
        check(case, 'conversation_count', len(conversations) == 1, len(conversations))
        check(case, 'message_count', len(rows) == 3, len(rows))
        cex = json.loads(conversations[0]['metadata'])['export']
        by_key = {r['metadata']['export']['key']: r for r in rows}
        source = json.loads((FIX / (provider + '.json')).read_text())[0]
        reconstruction = dict(cex['fields'])
        if provider == 'openai':
            mapping = {key: dict(row['metadata']['export']['node'], message=row['metadata']['export']['raw']) for key, row in by_key.items()}
            mapping.update({n['id']: n['node'] for n in cex['null_nodes']})
            reconstruction['mapping'] = mapping
        else:
            reconstruction['chat_messages'] = [r['metadata']['export']['raw'] for r in rows]
        before, after = set(leaves(source)), set(leaves(reconstruction))
        lost = sorted(before - after)
        scalar = [x for x in before if not x[1].startswith('empty_')]
        empty = [x for x in before if x[1].startswith('empty_')]
        matched = before & after
        check(case, 'structured_leaf_positions', not lost, {'scalar_preserved': sum(x in matched for x in scalar), 'scalar_total': len(scalar),
              'empty_preserved': sum(x in matched for x in empty), 'empty_total': len(empty), 'missing_or_changed': lost})
        for key, expected in manifest['expectations'][provider + '_statuses'].items():
            check(case, 'status:' + key, by_key[key]['status'] == expected, by_key[key]['status'])
        parent = 'u/~0' if provider == 'openai' else 'parent'
        for key, row in by_key.items():
            if key != parent:
                check(case, 'parent:' + key, row['parent_id'] == by_key[parent]['id'], row['parent_id'])
        raw_messages = ([(key, val['message']) for key, val in source['mapping'].items() if val['message'] is not None] if provider == 'openai'
                        else [(m['uuid'], m) for m in source['chat_messages']])
        for key, raw in raw_messages:
            check(case, 'raw_message:' + key, set(leaves(raw)) == set(leaves(by_key[key]['metadata']['export']['raw'])), key)
        blobs = [p for p in (work / 'blobs').rglob('*') if p.is_file()]
        blob_bytes = {digest(p.read_bytes()): p.read_bytes() for p in blobs}
        def has_bytes(raw):
            return blob_bytes.get(digest(raw)) == raw
        check(case, 'complete_source_bytes', has_bytes(inp.read_bytes()), {'bytes': inp.stat().st_size, 'sha256': digest(inp.read_bytes())})
        if inp.suffix == '.zip':
            with zipfile.ZipFile(inp) as archive:
                for name in archive.namelist():
                    raw = archive.read(name)
                    check(case, 'member_bytes:' + name, has_bytes(raw), {'bytes': len(raw), 'sha256': digest(raw)})
        if provider == 'openai':
            attachments = by_key[parent]['attachments']
            if case == 'openai':
                expected = bytes(range(256)) + b'\x00\xffW6'
                check(case, 'attachment_readable_bytes', any(pathlib.Path(p).is_file() and pathlib.Path(p).read_bytes() == expected for p in attachments), attachments)
        # Exact locators: independently resolve source ordinals or explicit pointers.
        prov = [dict(r) for r in db.execute('select * from loom_provenance')]
        for key, row in by_key.items():
            records = [p for p in prov if p['subject_id'] == row['id']]
            found = False
            details = []
            for record in records:
                loc = json.loads(record['locator'])
                details.append(loc)
                pointer = loc.get('json_path')
                if pointer:
                    try:
                        value = json.loads((FIX / (provider + '.json')).read_text())
                        for part in pointer.lstrip('/').split('/'):
                            part = part.replace('~1', '/').replace('~0', '~')
                            value = value[int(part)] if isinstance(value, list) else value[part]
                        found |= value == row['metadata']['export']['raw']
                    except (KeyError, ValueError, TypeError, IndexError):
                        pass
                if provider == 'anthropic' and isinstance(loc.get('message_index'), int):
                    idx = loc['message_index']
                    found |= 0 <= idx < len(source['chat_messages']) and source['chat_messages'][idx] == row['metadata']['export']['raw']
            check(case, 'exact_source_locator:' + key, found, details)
        if case == 'wrapped':
            needle = json.loads(inp.read_text())['unknown_wrapper']
            def contains(value):
                if value == needle:
                    return True
                return (any(contains(v) for v in value.values()) if isinstance(value, dict) else
                        any(contains(v) for v in value) if isinstance(value, list) else False)
            # This sentinel value is also inside the conversation: wrapper path itself must survive.
            def has_wrapper(value):
                if isinstance(value, dict):
                    return 'unknown_wrapper' in value or any(has_wrapper(v) for v in value.values())
                return isinstance(value, list) and any(has_wrapper(v) for v in value)
            metadata = [json.loads(c['metadata']) for c in conversations] + [r['metadata'] for r in rows]
            metadata += [json.loads(r['metadata']) for r in db.execute('select metadata from nodes')]
            check(case, 'wrapper_structured_field', any(has_wrapper(m) for m in metadata), 'unknown_wrapper key in persisted metadata')
        observed.append({'case': case, 'command': cmd, 'source_sha256': digest(inp.read_bytes()),
                         'importer_report_not_oracle': report.get('export_report'), 'message_rows': rows, 'provenance': prov})
        db.close()
    result = {'schema': 'w6.independent_import.v1', 'baseline_requested': manifest['baseline'],
              'binary': str(binary), 'binary_sha256': digest(binary.read_bytes()),
              'manifest_sha256': digest((FIX / 'manifest.json').read_bytes()), 'checks': checks,
              'passed': sum(c['pass'] for c in checks), 'total': len(checks), 'observations': observed,
              'limitations': ['Synthetic only', 'Binary provenance supplied by integrator; binary hash identifies execution',
                              'Array-order failure cannot be rescued by raw-byte success', 'No model understanding measured']}
    (out / 'receipt.json').write_bytes(encoded(result))
    print(json.dumps({k: result[k] for k in ('passed', 'total', 'binary_sha256', 'manifest_sha256')}))
    print(json.dumps([{'case': c['case'], 'check': c['check']} for c in checks if not c['pass']]))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['generate', 'evaluate'])
    parser.add_argument('--binary', type=pathlib.Path)
    parser.add_argument('--out', type=pathlib.Path)
    args = parser.parse_args()
    if args.action == 'generate':
        generate()
    else:
        if args.binary is None or args.out is None:
            parser.error('evaluate requires --binary and --out')
        evaluate(args.binary.resolve(), args.out.resolve())
