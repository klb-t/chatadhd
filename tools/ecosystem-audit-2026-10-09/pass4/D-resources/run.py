#!/usr/bin/env python3
"""Independent D resource acceptance against original modules, no external transport.

PASS reproduction means the defect is observed, never product acceptance.
The JSON receipt is written even when acceptance fails. No private inputs.
"""
from __future__ import annotations
import argparse
from collections import Counter
from copy import deepcopy
import hashlib
import io
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
from unittest.mock import patch
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--sha', required=True)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--native-source', type=Path, required=True)
    parser.add_argument('--native-sha', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()
    actual = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
    native_actual = subprocess.check_output(['git', '-C', str(args.native_source), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != args.sha or native_actual != args.native_sha:
        raise SystemExit('checkout SHA mismatch')
    sys.path.insert(0, str(repo))
    from loom.tools.resource_graph.core import ResourceGraph, ResourceError
    from loom.tools.resource_graph.access import Access
    from loom.tools.resource_graph.parsers import recognize, register_parser
    from loom.tools.resource_graph.native import roundtrip
    from loom.tools.coordination.graph_store import NativeGraphStore

    # Only the final external transport is denied. Real local files, ZIP parsers,
    # the packet codec, C ABI, native SQLite and reopen/replay remain original.
    def denied(*_a, **_kw):
        raise AssertionError('audit forbids network transport')
    socket.socket.connect = denied
    socket.create_connection = denied
    cases = []

    def case(key, category, invariant, fn, finding=None):
        row = {'id': key, 'category': category, 'invariant': invariant}
        if finding:
            row['finding_id'] = finding
        try:
            observed = fn()
            row.update(status='PASS', observed=observed)
        except AssertionError as error:
            row.update(status='FAIL', reason=str(error))
        except Exception as error:
            # Do not copy exception text from source/parser/native machinery.
            row.update(status='ERROR', exception_type=type(error).__name__)
        cases.append(row)

    def blocked(key, invariant, reason):
        cases.append({'id': key, 'category': 'integration', 'status': 'BLOCKED',
                      'invariant': invariant, 'reason': reason})

    def check(condition, reason):
        if not condition:
            raise AssertionError(reason)

    def field_values(packet):
        return {e['attrs']['selector']: e['attrs'].get('value')
                for e in packet['entities'] if e['kind'] == 'resource_field' and 'value' in e['attrs']}

    def zip_bytes(member, content):
        output = io.BytesIO()
        with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr(member, content)
        return output.getvalue()

    with tempfile.TemporaryDirectory(prefix='audit-d4-') as tmp:
        directory = Path(tmp)
        expected = {'conversation': {'id': 'synthetic-conversation', 'messages': [
            {'id': 'm1', 'role': 'user', 'parent': None, 'text': 'synthetic first'},
            {'id': 'm2', 'role': 'assistant', 'parent': 'm1', 'text': 'synthetic reply'}]},
            'unknown': {'flag': True, 'empty': [], 'null': None}, 'integer': 9007199254740993}
        payload = json.dumps(expected, ensure_ascii=False).encode()
        source = directory / 'source.json'
        source.write_bytes(payload)

        def graph(path=source, logical='source', **kwargs):
            result = ResourceGraph(**kwargs)
            result.attach(path, logical_id=logical)
            return result

        def zero_read_attach():
            calls = []
            original = Access.read
            with patch.object(Access, 'read', lambda self, *a, **k: calls.append(True) or original(self, *a, **k)):
                g = graph(directory / 'missing.opaque')
                packet = g.reference_packet('source')
            check(not calls and g.metrics['opens'] == 0, 'attach performed source read')
            check(len(packet['entities']) == 1 and not packet['claims'], 'attach materialized fields')
            check(g.describe('source')['status'] == 'unloaded', 'unknown source not retained as unloaded')
            return {'transport_reads': 0, 'entities': 1, 'fields': 0, 'status': 'unloaded'}
        case('D4-01', 'contract', 'Attaching unknown/unavailable source requires neither byte copy nor field materialization.', zero_read_attach)

        def serialization_variants():
            reordered = dict(reversed(list(expected.items())))
            raws = [payload, json.dumps(reordered, indent=3).encode(), b'\xef\xbb\xbf' + json.dumps(expected, ensure_ascii=True).encode()]
            values, revisions = [], []
            for n, raw in enumerate(raws):
                file = directory / f'serialization-{n}.json'; file.write_bytes(raw)
                g = graph(file)
                values.append(g.select('source'))
                revisions.append(g.describe('source')['content_sha256'])
            check(all(value == expected for value in values), 'syntax-equivalent JSON changed structure')
            check(len(set(revisions)) == 3, 'raw byte versions incorrectly collapsed')
            return {'variants': 3, 'structural_values_equal': True, 'raw_versions_distinct': True}
        case('D4-02', 'contract', 'JSON key permutation, whitespace and BOM preserve structure and list order; raw versions need not match.', serialization_variants)

        def order_sensitive():
            g = graph(); original = g.select('source', '/conversation/messages')
            reversed_source = directory / 'reversed.json'
            transformed = deepcopy(expected); transformed['conversation']['messages'].reverse()
            reversed_source.write_text(json.dumps(transformed))
            reverse = graph(reversed_source).select('source', '/conversation/messages')
            check([m['id'] for m in original] == ['m1', 'm2'], 'baseline message order differs from fixture')
            check([m['id'] for m in reverse] == ['m2', 'm1'], 'list order was treated as semantically irrelevant')
            return {'list_order_preserved': True}
        case('D4-03', 'contract', 'Message list order is semantic and must not be normalized away.', order_sensitive)

        def demand_fragment():
            g = graph(); before = source.read_bytes()
            packet = g.project('source', '/unknown/flag', depth=0)
            check(field_values(packet) == {'/unknown/flag': True}, 'projection leaked unselected sibling values')
            check(packet['task']['materialized_fields'] == 1, 'unrequested fields materialized')
            check(not g.describe('source')['embedded_bytes'] and before == source.read_bytes(), 'reference copied or modified source')
            return {'materialized_fields': 1, 'embedded_bytes': False, 'io_scope': 'whole bounded JSON document'}
        case('D4-04', 'contract', 'Selecting one field materializes only that graph field; current syntax adapter may parse whole bounded document.', demand_fragment)

        def file_container_inline():
            archive = directory / 'outer.zip'
            archive.write_bytes(zip_bytes('inner.zip', zip_bytes('payload.json', payload)))
            g = graph(); g.attach(archive, logical_id='zip', members=['inner.zip', 'payload.json'])
            g.attach_inline(payload, logical_id='inline', format='json')
            values = [g.select(key) for key in ('source', 'zip', 'inline')]
            check(all(value == expected for value in values), 'transport/container altered syntax structure')
            check(len({g.describe(key)['content_sha256'] for key in ('source', 'zip', 'inline')}) == 1, 'leaf byte hash changed')
            return {'local_nested_zip_inline_equal': True, 'messages': 2, 'parent_links': 1, 'domain_import_not_claimed': True}
        case('D4-05', 'contract', 'Plain file, ZIP-in-ZIP member and explicit embedded bytes yield same source structure, ordered messages and parent values.', file_container_inline)

        def cache_and_eager():
            g = graph(policy={'cache': True, 'retention_seconds': 20, 'index': True})
            cold = g.select('source', '/conversation/messages/1')
            warm = g.select('source', '/conversation/messages/1')
            eager = graph().select('source')['conversation']['messages'][1]
            check(cold == warm == eager == expected['conversation']['messages'][1], 'eager/cold/warm semantics differ')
            check(g.metrics['opens'] == 1 and g.metrics['cache_hits'] == 1, 'actual cache not consumed')
            return {'opens': 1, 'cache_hits': 1, 'same_selected_value': True}
        case('D4-06', 'contract', 'Explicit full-tree read versus selective read and warm cache preserve selected field semantics on unchanged source.', cache_and_eager)

        def statuses():
            outcomes = {}
            for name, raw, fmt, budget in [('empty', b'{}', 'json', {}), ('corrupt', b'{', 'json', {}),
                                          ('unknown', b'opaque', 'no-parser', {}), ('partial', payload, 'json', {'max_bytes': 2})]:
                path = directory / (name + '.json'); path.write_bytes(raw)
                g = ResourceGraph(); g.attach(path, logical_id=name, format=fmt, parser_options=budget)
                try:
                    g.select(name)
                    outcomes[name] = g.describe(name)['status']
                except ResourceError as exc:
                    outcomes[name] = exc.status
            missing = graph(directory / 'absent.json')
            try: missing.select('source')
            except ResourceError as exc: outcomes['unavailable'] = exc.status
            check(outcomes == {'empty': 'empty', 'corrupt': 'corrupt', 'unknown': 'unsupported', 'partial': 'partial', 'unavailable': 'unavailable'}, 'states collapsed')
            return outcomes
        case('D4-07', 'contract', 'Empty, corrupt, unsupported, budget-partial and unavailable are distinguishable.', statuses)

        def fragment_and_resume():
            g = graph(policy={'index': True})
            g.select('source', '/conversation/id')
            try:
                g.select('source', '/missing')
                raise AssertionError('missing fragment became empty success')
            except ResourceError as exc:
                check(exc.code == 'fragment_not_found', 'missing selector error is not explicit')
            recovered = g.select('source', '/unknown/flag')
            index = g.search_index('source', 'synthetic-conversation')
            check(recovered is True and index['selectors'] == ['/conversation/id'], 'earlier index lost on interrupted traversal')
            check(index['status'] == 'partial', 'incomplete index claims completeness')
            return {'prior_index_retained': True, 'subsequent_read_available': True, 'coverage': index['coverage']}
        case('D4-08', 'contract', 'Failed fragment read does not erase prior index; later selected read resumes usable session, not durable cursor.', fragment_and_resume)

        def interruption():
            g = graph()
            import loom.tools.resource_graph.access as module
            original = module.os.open
            with patch.object(module.os, 'open', side_effect=InterruptedError('synthetic interruption')):
                try: g.select('source')
                except ResourceError as exc:
                    check(exc.status == 'unavailable', 'interrupted IO became successful empty source')
                else: raise AssertionError('interrupted IO accepted')
            check(g.select('source') == expected, 'source not recoverable after restored descriptor opening')
            check(original is module.os.open, 'fault injection not restored')
            return {'interruption': 'unavailable', 'retry': 'available', 'real_retry_read': True}
        case('D4-09', 'contract', 'Injected interruption at descriptor opening reports unavailable and subsequent actual file read succeeds.', interruption)

        def snapshot_history():
            path = directory / 'changing.json'; path.write_text('{"version":1}')
            g = graph(path, policy={'mode': 'snapshot'})
            old = g.project('source', '/version', depth=0)
            path.write_text('{"version":2}')
            try: g.select('source')
            except ResourceError as exc: check(exc.code == 'snapshot_source_changed', 'wrong snapshot drift outcome')
            else: raise AssertionError('snapshot accepted changed source')
            check(field_values(old) == {'/version': 1}, 'old projected value mutated')
            return {'drift': 'snapshot_source_changed', 'old_projection_unchanged': True}
        case('D4-10', 'contract', 'Snapshot reference detects source version drift and does not mutate earlier packet.', snapshot_history)

        def descriptor_and_unknown():
            g = graph(); packet = g.project('source', '/unknown', depth=3)
            fields = {e['attrs']['selector']: e['attrs'] for e in packet['entities'] if e['kind'] == 'resource_field'}
            check(fields['/unknown/flag']['value'] is True and fields['/unknown/null']['value'] is None, 'unknown values dropped')
            check(fields['/unknown/empty']['size'] == 0 and g.describe('source')['recognition'] == 'syntax_only', 'empty/unknown recognition wrong')
            check(all(e['evidence_class'] == 'derived' for e in packet['entities']), 'syntax projection presented as domain truth')
            return {'recognition': 'syntax_only', 'unknown_values_preserved': True}
        case('D4-11', 'contract', 'Unknown fields remain addressable without claiming domain interpretation.', descriptor_and_unknown)

        def ambiguity():
            result = recognize(b'{"a":1}', candidates=['json', 'csv'])
            check(len(result) == 2 and all(row['recognized'] and not row['semantic_recognition'] for row in result), 'syntax alternatives lost or promoted to semantics')
            g = graph(); before = g.describe('source')
            g.recognize('source', candidates=['json', 'csv'])
            after = g.describe('source')
            check(before['format'] == after['format'] and before['permissions'] == after['permissions'], 'recognition silently activated or widened access')
            return {'recognized_syntax_alternatives': 2, 'semantic_recognition': False, 'permissions_unchanged': True}
        case('D4-12', 'contract', 'Two plausible syntax interpretations remain alternatives; recognition neither activates another parser nor grants permissions.', ambiguity)

        def permissions():
            access = Access(policy={'allowed_origins': []}, transport=denied)
            g = ResourceGraph(access=access); g.attach('https://invalid.example/synthetic.json', logical_id='remote')
            try: g.select('remote')
            except ResourceError as exc: check(exc.code == 'origin_not_authorized', 'unapproved origin called transport')
            else: raise AssertionError('unapproved remote read succeeded')
            local = ResourceGraph(); local.attach(source, logical_id='denied', permissions={'read': False})
            try: local.select('denied')
            except ResourceError as exc: check(exc.code == 'read_permission_denied', 'read permission not enforced')
            else: raise AssertionError('denied read succeeded')
            return {'external_calls': 0, 'explicit_read_denial': True}
        case('D4-13', 'contract', 'Discovery-related recognition does not bypass transport or resource read authority.', permissions)

        def partial_projection():
            g = graph(); packet = g.project('source', depth=7, limit=3)
            check(packet['task']['status'] == 'partial' and packet['task']['materialized_fields'] == 3, 'bounded projection claims complete')
            check(g.select('source', '/conversation/messages/1/parent') == 'm1', 'not materializing field made it inaccessible')
            return {'partial_materialized_fields': 3, 'unmaterialized_selector_still_available': True}
        case('D4-14', 'contract', 'Projection granularity changes retained graph fields, not source structure or future access.', partial_projection)

        native_data = directory / 'native'
        def native_boundary():
            g = graph(); packet = g.project('source', '/conversation/messages', depth=4)
            result = roundtrip(packet, args.library, native_data)
            check(result['native_executed'] and result['reopened'] and result['packet'] == packet, 'native boundary mismatch')
            actual_fields = field_values(result['packet'])
            check(actual_fields['/conversation/messages/1/parent'] == 'm1', 'native packet lost parent source field')
            source.unlink()
            with NativeGraphStore(args.library, native_data) as store:
                read = store.replay(result['receipt_id'])
            source.write_bytes(payload)
            check(read['receipt']['packet'] == packet and read['row_drift']['matches'], 'native replay re-read deleted source or drifted')
            return {'native_executed': True, 'closed_reopened': True, 'replay_without_original': True, 'counts': result['selected_counts']}
        case('D4-15', 'integration', 'Real D demanded projection→existing packet→native C ABI→SQLite→close/reopen/replay preserves structural parent fields.', native_boundary)

        def native_reference():
            g = graph(directory / 'not-present.opaque'); packet = g.reference_packet('source')
            result = roundtrip(packet, args.library, directory / 'native-ref')
            check(result['packet'] == packet and result['selected_counts'] == {'entities': 1, 'claims': 0, 'sources': 0}, 'native required payload/materialization')
            return {'native_reference_retained': True, 'materialized_fields': 0, 'transport_reads': 0}
        case('D4-16', 'integration', 'Existing native store accepts an unknown unloaded reference without materializing source.', native_reference)

        # Minimal collision: same CSV bytes, same identity, same parser version,
        # different explicit delimiter. Do not normalize different interpretations.
        csv = directory / 'interpretation.csv'; csv.write_bytes(b'a;b\n1;2\n')
        variants = []
        for delim in (',', ';'):
            g = ResourceGraph(); g.attach(csv, logical_id='csv', parser_options={'csv_delimiter': delim})
            packet = g.project('csv', '/0/0', depth=0)
            field = next(e for e in packet['entities'] if e['kind'] == 'resource_field')
            method = next(e for e in packet['entities'] if e['kind'] == 'analysis_method')
            variants.append({'revision': packet['task']['source_revision'], 'field_id': field['id'], 'method_id': method['id'], 'value': field['attrs']['value']})
        def parser_repro():
            check(variants[0]['value'] == 'a;b' and variants[1]['value'] == 'a', 'fixture did not change interpretation')
            check(all(variants[0][k] == variants[1][k] for k in ('revision', 'field_id', 'method_id')), 'identity collision no longer reproduced')
            return variants
        def parser_accept():
            check(variants[0]['revision'] != variants[1]['revision'] and variants[0]['method_id'] != variants[1]['method_id'], 'changed parser options retain same recipe/revision/method identity')
            return variants
        case('D4-17A', 'reproduction', 'Observe distinct CSV interpretation under identical generated method/revision identity.', parser_repro, 'A4-D-001')
        case('D4-17B', 'repair_acceptance', 'Parser options affecting interpretation must be recorded and bound to method/result version.', parser_accept, 'A4-D-001')

        # Deterministic filesystem fault injection reproduces TOCTOU. Only
        # synthetic files exist; actual os.open and Access._local do the reading.
        def raced_read():
            safe = directory / 'allowed'; safe.mkdir(exist_ok=True)
            protected = directory / 'outside.json'; protected.write_bytes(b'{"outside_scope":true}')
            victim = safe / 'selected.json'
            if victim.exists() or victim.is_symlink(): victim.unlink()
            victim.write_bytes(b'{"inside_scope":true}')
            access = Access(policy={'local_roots': [str(safe)]})
            import loom.tools.resource_graph.access as module
            original_open = module.os.open
            called = []
            def swap_then_open(path, flags, *a, **kw):
                if Path(path) == victim and not called:
                    called.append(True); victim.unlink(); victim.symlink_to(protected)
                return original_open(path, flags, *a, **kw)
            with patch.object(module.os, 'open', swap_then_open):
                result = access.read(victim)
            victim.unlink()
            return result
        raced = raced_read()
        def race_repro():
            check(raced.get('status') == 'available' and raced.get('data') == b'{"outside_scope":true}', 'root authorization race no longer reproduced')
            return {'outside_root_bytes_read': True, 'returned_status': raced['status'], 'fixture_only': True}
        def race_accept():
            check(raced.get('status') not in ('available', 'empty') and 'data' not in raced, 'authorized path replacement exposes bytes outside allowed local root')
            return {'outside_root_bytes_read': False}
        case('D4-18A', 'reproduction', 'Swap authorized canonical local path to out-of-root symlink before actual descriptor open.', race_repro, 'A4-D-002')
        case('D4-18B', 'repair_acceptance', 'Authorization must apply to actual opened object; out-of-root symlink replacement is rejected before reading bytes.', race_accept, 'A4-D-002')

        def unknown_permission_options():
            g = ResourceGraph(); g.attach(source, logical_id='source', policy={'read_strategy': 'eager'})
            check(g.describe('source')['policy'].get('read_strategy') == 'eager', 'probe input rejected or altered')
            g.select('source', '/integer')
            return {'unrecognized_policy_retained': True, 'no_actual_eager_contract_claimed': True}
        case('D4-19', 'behavior', 'Record unrecognized policy retention without calling it supported eager behavior.', unknown_permission_options)

        def registration_unavailable():
            g = ResourceGraph(); g.attach(source, logical_id='declared', adapter='unimplemented')
            check(not g.describe('declared')['capabilities']['select'], 'declaration implies installed adapter')
            try: g.select('declared')
            except ResourceError as exc: check(exc.code == 'adapter_unavailable', 'missing executor not explicit')
            else: raise AssertionError('missing executor fabricated result')
            return {'unimplemented_adapter': 'adapter_unavailable', 'selection_capability': False}
        case('D4-20', 'contract', 'Declared adapter name does not imply implementation or successful execution.', registration_unavailable)

        def snapshot_offline():
            path = directory / 'snapshot.json'; path.write_bytes(payload)
            g = graph(path, policy={'mode': 'snapshot', 'index': False, 'cache': False})
            g.capture('source'); path.unlink()
            check(g.select('source') == expected, 'captured snapshot cannot read offline')
            desc = g.describe('source')
            check(desc['policy']['embedding'] and not desc['policy']['index'] and not desc['policy']['cache'], 'capturing forced cache/index')
            return {'embedding': True, 'cache': False, 'index': False, 'offline': True}
        case('D4-21', 'contract', 'Explicit captured snapshot works offline independently of cache and index.', snapshot_offline)

        def parser_extension():
            # Reuse existing JSON parser, only version the installed descriptor;
            # no replacement graph/parser implementation in the audit harness.
            from loom.tools.resource_graph import parsers
            original = parsers._REGISTRY['json'][0]
            parsers.register_parser('audit-json-v2', original, descriptor={'version': 'audit-wrapper/2'})
            g = graph(); baseline = g.project('source', '/integer', depth=0)
            g.select_format('source', 'audit-json-v2')
            changed = g.project('source', '/integer', depth=0)
            check(field_values(baseline) == field_values(changed) == {'/integer': 9007199254740993}, 'registered parser changed exact integer')
            check(baseline['task']['source_revision'] != changed['task']['source_revision'], 'parser descriptor version not bound to revision')
            return {'same_structural_value': True, 'parser_version_changes_revision': True, 'core_edits': 0}
        case('D4-22', 'contract', 'An explicitly trusted registry extension reuses existing parser without core/renderer edit; descriptor version changes lineage.', parser_extension)

        blocked('D4-B01', 'Full native conversation import equals D reference projection in domain records and relations.', 'D4 exposes generic syntax fields only; no public conversation mapping/import parity adapter is published. D source STATE explicitly lists parity as next work.')
        blocked('D4-B02', 'Native stored reference hydrates through B catalog and the same headless/UI resolver.', 'roundtrip persists/replays supplied packet; automatic catalog→D adapter dispatch and rehydration from reference packet are not published in D4.')
        blocked('D4-B03', 'Changing locator while preserving pinned logical version retains selector identity.', 'attach has no source-version/expected-hash or public relocate/restore-reference argument; do not mutate internal resource dictionary and call it integration.')
        blocked('D4-B04', 'Interrupted persistent index reopens at a durable checkpoint.', 'Only transient requested-fragment _index exists; no persisted index cursor/reopen contract. D4-08/09 prove narrower in-session recovery.')
        blocked('D4-B05', 'Domain discovery preserves mapping alternatives with evidence and explicit activation policy.', 'ResourceGraph.discover imports .discovery, absent from exact D4 tree; syntax recognize is available and tested separately. No domain executor installed.')
        blocked('D4-B06', 'Two external profiles project into graph fields consumed by actual B resolver.', 'D exposes generic fields and graph packet bridge; no B runtime binding in D4. Packet persistence is not runtime profile activation.')
        blocked('D4-B07', 'Same resource selector consumed through E headless and actual E UI.', 'D4 publishes no E consumer or perspective hook. Root tracks separately published E branch availability.')
        blocked('D4-B08', 'Seekable on-demand parser performs bounded fragment IO without whole document read.', 'Published D4 syntax adapter reads/parses whole bounded source/member upon demand. JSONL seekable adapter mentioned in report is not present at this SHA; zero-read attach and selective graph materialization remain proven.')

    counts = Counter(row['status'] for row in cases)
    for row in cases:
        row['evidence'] = row.get('observed', row.get('reason', row.get('exception_type')))
    units = ['loom/tools/resource_graph/core.py', 'loom/tools/resource_graph/access.py',
             'loom/tools/resource_graph/parsers.py', 'loom/tools/resource_graph/projection.py',
             'loom/tools/resource_graph/native.py', 'loom/tools/coordination/graph_store.py',
             'loom/tools/structure/agentic_graph_v1/packet.py']
    receipt = {'schema': 'audit.pass4.D/1', 'repo': 'klb-t/chatadhd', 'sha': actual,
        'native_sha': native_actual, 'native_library_sha256': hashlib.sha256(args.library.read_bytes()).hexdigest(),
        'source_hashes': {path: hashlib.sha256((repo / path).read_bytes()).hexdigest() for path in units},
        'no_external_transport': True, 'fixtures': 'synthetic disposable, no user archive',
        'implementation_replicated': False, 'counts': dict(counts), 'cases': cases}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'counts': dict(counts), 'receipt': str(args.output)}, ensure_ascii=False))
    return 1 if counts['FAIL'] or counts['ERROR'] or counts['BLOCKED'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
