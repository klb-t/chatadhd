"""Offline draft-2020-12 validation for T1 projections, plus relational guards.

No graph writes, provider calls, schema downloads, or canonical claim promotion.
Requires jsonschema >=4.18 and referencing >=0.30. See docs/contracts/README.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

try:
    from jsonschema import Draft202012Validator, FormatChecker
    from referencing import Registry, Resource
    from referencing.exceptions import NoSuchResource
except ImportError as exc:
    raise SystemExit('Missing local dependency: install loom/tools/contracts/requirements.txt before offline use.') from exc

SCHEMA_DIR = Path(__file__).resolve().parents[3] / 'docs' / 'contracts'
SCHEMAS = {
    'loom.history_event/1': 'history_event.schema.json',
    'loom.active_task_spec/1': 'active_task_spec.schema.json',
    'loom.request_snapshot/1': 'request_snapshot.schema.json',
    'loom.evaluation_packet/1': 'evaluation_packet.schema.json',
}
MAX_INPUT_BYTES = 10 * 1024 * 1024  # local CLI resource guard, not a model context limit
SECRET_KEYS = {'authorization', 'proxyauthorization', 'apikey', 'accesstoken',
               'refreshtoken', 'clientsecret', 'password', 'xapikey', 'cookie', 'setcookie'}


@dataclass(frozen=True)
class Issue:
    path: str
    code: str
    message: str


def _join(path: str, key: Any) -> str:
    return path + '/' + str(key).replace('~', '~0').replace('/', '~1')


def _pairs(pairs: list[tuple[str, Any]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON object key')
        result[key] = value
    return result


def _invalid_constant(_: str) -> None:
    raise ValueError('non-finite JSON number')


def read_json(path: Path) -> Any:
    with path.open('rb') as stream:
        raw = stream.read(MAX_INPUT_BYTES + 1)
    if len(raw) > MAX_INPUT_BYTES:
        raise ValueError('input exceeds local size guard')
    return json.loads(raw.decode('utf-8'), object_pairs_hook=_pairs,
                      parse_constant=_invalid_constant)


def canonical_bytes(value: Any) -> bytes:
    # Deliberately named Python canonical JSON, NOT RFC 8785 or native wire bytes.
    return json.dumps(value, sort_keys=True, ensure_ascii=False, allow_nan=False,
                      separators=(',', ':')).encode('utf-8')


def body_digest(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def _no_remote(uri: str) -> Resource:
    raise NoSuchResource(ref=uri)


def _time(text: str) -> datetime:
    return datetime.fromisoformat(text.replace('t', 'T').replace('z', '+00:00').replace('Z', '+00:00'))


def _walk(value: Any, path: str = ''):
    yield path, value
    if isinstance(value, dict):
        for key, child in value.items():
            yield from _walk(child, _join(path, key))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from _walk(child, _join(path, index))


class ContractValidator:
    """Loads ONLY explicitly local schemas; unknown refs fail closed."""

    def __init__(self, schema_dir: Path = SCHEMA_DIR):
        self.schemas = {}
        self.format_checker = FormatChecker()
        registry = Registry(retrieve=_no_remote)
        for path in sorted(schema_dir.glob('*.schema.json')):
            schema = read_json(path)
            Draft202012Validator.check_schema(schema)
            required_formats = {item['format'] for _, item in _walk(schema)
                                if isinstance(item, dict) and isinstance(item.get('format'), str)}
            if required_formats - self.format_checker.checkers.keys():
                raise RuntimeError('Required JSON Schema format checker unavailable; install '
                                   'loom/tools/contracts/requirements.txt before offline use.')
            registry = registry.with_resource(schema['$id'], Resource.from_contents(schema))
            self.schemas[path.name] = schema
        self.registry = registry

    def validate(self, value: Any, *, for_submission: bool = False) -> list[Issue]:
        issues: list[Issue] = []

        def add(path: str, code: str, message: str) -> None:
            issues.append(Issue(path or '/', code, message))

        # Reject Python-only values and duplicate/non-finite values before schema use.
        try:
            canonical_bytes(value)
        except (ValueError, TypeError, OverflowError, RecursionError):
            return [Issue('/', 'json', 'Not a finite UTF-8 JSON document.')]
        for path, item in _walk(value):
            if isinstance(item, dict) and not all(isinstance(k, str) for k in item):
                add(path, 'json', 'JSON object keys must be strings.')
        if issues:
            return issues
        kind = value.get('schema') if isinstance(value, dict) else None
        name = SCHEMAS.get(kind) if isinstance(kind, str) else None
        if name is None:
            return [Issue('/schema', 'schema', 'Unknown or missing projection schema.')]
        validator = Draft202012Validator(self.schemas[name], registry=self.registry,
                                         format_checker=self.format_checker)
        try:
            errors = sorted(validator.iter_errors(value),
                            key=lambda e: tuple(str(x) for x in e.absolute_path))
            for error in errors:
                path = ''.join(_join('', p) for p in error.absolute_path)
                # Do not echo source text, keys, credentials or payloads in diagnostics.
                add(path, 'schema.' + str(error.validator), 'Schema constraint failed.')
        except Exception as exc:
            # An invalid/unknown reference is a validation failure, never a network fallback.
            if 'referenc' in type(exc).__name__.lower() or isinstance(exc, NoSuchResource):
                add('/', 'schema.reference', 'Schema reference is not in the local registry.')
            else:
                raise
        if issues:
            return issues  # relational checks assume schema-valid shapes

        def not_future(date: str, cutoff: str, path: str) -> None:
            if _time(date) > _time(cutoff):
                add(path, 'future_knowledge', 'Timestamp exceeds the declared knowledge boundary.')

        def check_locator(item: dict, path: str) -> None:
            a, b = item.get('byte_start'), item.get('byte_len')
            if (a is None) != (b is None):
                add(path, 'locator.byte_pair', 'byte_start and byte_len must be supplied together.')
            a, b = item.get('time_start'), item.get('time_end')
            if (a is None) != (b is None):
                add(path, 'locator.time_pair', 'time_start and time_end must be supplied together.')
            elif a is not None and b < a:
                add(path, 'locator.time_order', 'time_end is before time_start.')

        def check_event(event: dict, path: str = '') -> None:
            check_locator(event['source'], path + '/source')
            if event['kind'] == 'attachment':
                check_locator(event['payload']['locator'], path + '/payload/locator')

        def span_check(text: str, span: dict, path: str) -> None:
            raw = text.encode('utf-8')
            start, end = span['byte_start'], span['byte_start'] + span['byte_len']
            if end > len(raw):
                add(path, 'span.range', 'UTF-8 byte span exceeds the text.')
                return
            try:
                raw[:start].decode('utf-8')
                raw[start:end].decode('utf-8')
            except UnicodeDecodeError:
                add(path, 'span.utf8', 'Span splits a UTF-8 code point.')

        def sources(refs: list[dict], cutoff: str, path: str) -> None:
            for i, source in enumerate(refs):
                not_future(source['known_at'], cutoff, _join(_join(path, i), 'known_at'))
                check_locator(source['locator'], _join(_join(path, i), 'locator'))

        def check_spec(spec: dict, path: str = '') -> None:
            sources(spec['source_refs'], spec['known_at'], path + '/source_refs')
            history_ids = set(spec['history_event_ids'])
            provenance_ids = {s['event_id'] for s in spec['source_refs']}
            if not provenance_ids <= history_ids:
                add(path + '/source_refs', 'spec.source_scope', 'Source event is outside declared history.')
            statements = {s['id']: s for s in spec['statements']}
            if len(statements) != len(spec['statements']):
                add(path + '/statements', 'spec.duplicate_id', 'Statement ids must be unique.')
            if not any(s['kind'] == 'goal' and s['status'] in ('active', 'contested')
                       for s in spec['statements']):
                add(path + '/statements', 'spec.goal', 'An active or contested goal is required.')
            for i, statement in enumerate(spec['statements']):
                sp = path + f'/statements/{i}'
                if not set(statement['source_event_ids']) <= provenance_ids:
                    add(sp, 'spec.ungrounded', 'Statement lacks a mapped source event.')
                for old in statement['supersedes']:
                    if old == statement['id'] or old not in statements:
                        add(sp, 'spec.supersedes_ref', 'Supersession must reference another local statement.')
                    elif statements[old]['status'] not in ('superseded', 'rejected'):
                        add(sp, 'spec.supersedes_status', 'Superseded target remains active/contested.')
            # Detect cycles even when each individual reference is valid.
            done, visiting = set(), set()

            def visit(key: str) -> bool:
                if key in visiting:
                    return True
                if key in done or key not in statements:
                    return False
                visiting.add(key)
                found = any(visit(k) for k in statements[key]['supersedes'])
                visiting.remove(key)
                done.add(key)
                return found
            if any(visit(k) for k in statements):
                add(path + '/statements', 'spec.supersedes_cycle', 'Cyclic supersession.')
            previous = spec['previous_product_ref']
            if previous and previous['id'] == spec['product_ref']['id']:
                add(path + '/previous_product_ref', 'spec.self_version', 'Previous product is the current product.')
            for i, mapping in enumerate(spec['compiled_instruction']['source_map']):
                mp = path + f'/compiled_instruction/source_map/{i}'
                span_check(spec['compiled_instruction']['text'], mapping['span'], mp)
                for key in mapping['statement_ids']:
                    if key not in statements:
                        add(mp, 'spec.map_ref', 'Instruction mapping references an unknown statement.')
                    elif statements[key]['status'] in ('superseded', 'rejected'):
                        add(mp, 'spec.stale_instruction', 'Instruction maps an inactive statement as active content.')

        def check_snapshot(snap: dict, path: str = '') -> None:
            sources(snap['source_refs'], snap['known_at'], path + '/source_refs')
            if snap['captured_at']:
                not_future(snap['captured_at'], snap['known_at'], path + '/captured_at')
            if snap['body'] is not None and body_digest(snap['body']) != snap['body_sha256']:
                add(path + '/body_sha256', 'request.hash', 'Hash differs from canonical redacted body.')
            if snap['request_provenance'] == 'unknown' and snap['redactions']:
                add(path + '/redactions', 'request.unknown_redactions', 'Unknown payload cannot declare body redactions.')
            for subpath, item in _walk(snap['body']):
                if isinstance(item, dict):
                    for key, entry in item.items():
                        compact = ''.join(c for c in key.lower() if c.isalnum())
                        if compact in SECRET_KEYS and entry not in (None, '[REDACTED]'):
                            add(path + '/body' + _join(subpath, key), 'request.secret', 'Credential-like field is not redacted.')
            for pointer in snap['redactions']:
                try:
                    item = snap['body']
                    for part in pointer.split('/')[1:] if pointer else []:
                        key = part.replace('~1', '/').replace('~0', '~')
                        item = item[int(key)] if isinstance(item, list) else item[key]
                    if item not in (None, '[REDACTED]'):
                        raise ValueError
                except (KeyError, IndexError, TypeError, ValueError):
                    add(path + '/redactions', 'request.redaction_pointer', 'Redaction pointer does not name a redacted value.')

        def check_packet(packet: dict) -> None:
            cutoff = packet['known_at']
            target = packet['target']
            events = packet['history_view']['events']
            by_id = {e['id']: e for e in events}
            if len(by_id) != len(events):
                add('/history_view/events', 'history.duplicate_id', 'Event ids must be unique.')
            previous_order = {}
            calls = set()
            complete = packet['history_view']['completeness'] == 'complete'
            for i, event in enumerate(events):
                ep = f'/history_view/events/{i}'
                not_future(event['known_at'], cutoff, ep + '/known_at')
                check_event(event, ep)
                if event['branch_id'] not in packet['history_view']['allowed_branch_ids']:
                    add(ep, 'history.branch_scope', 'Event branch is not explicitly included.')
                group = (event['conversation_id'], event['branch_id'])
                if event['ordinal'] <= previous_order.get(group, -1):
                    add(ep, 'history.order', 'Per-branch ordinals must be strictly increasing.')
                previous_order[group] = event['ordinal']
                if event['kind'] == 'tool_call':
                    key = (event['conversation_id'], event['payload']['call_id'])
                    if key in calls:
                        add(ep, 'history.call_id', 'Duplicate call identity.')
                    calls.add(key)
                if event['kind'] == 'tool_result':
                    if complete and (event['conversation_id'], event['payload']['call_id']) not in calls:
                        add(ep, 'history.tool_pair', 'Complete view contains result without a preceding call.')
            matched = by_id.get(target['event_id'])
            if matched is None:
                add('/target', 'packet.target', 'Analyzed event is missing from the explicit history view.')
            else:
                if any(matched[k] != target[k] for k in ('conversation_id', 'branch_id')):
                    add('/target', 'packet.target_scope', 'Target identity and scope disagree.')
                if target['span'] is not None:
                    text = matched['payload'].get('content')
                    if not isinstance(text, str):
                        add('/target/span', 'packet.span_text', 'Byte span requires an explicitly supplied text message.')
                    else:
                        span_check(text, target['span'], '/target/span')
            spec = packet['active_task_spec']
            if spec is not None:
                check_spec(spec, '/active_task_spec')
                not_future(spec['known_at'], cutoff, '/active_task_spec/known_at')
                if any(spec['scope'][k] != target[k] for k in ('conversation_id', 'branch_id')):
                    add('/active_task_spec/scope', 'packet.spec_scope', 'Active specification belongs to a different conversation/branch.')
            snap = packet['request_snapshot']
            if snap is not None:
                check_snapshot(snap, '/request_snapshot')
                not_future(snap['known_at'], cutoff, '/request_snapshot/known_at')
                if snap['target_event_id'] != target['event_id'] or any(snap[k] != target[k] for k in ('conversation_id', 'branch_id')):
                    add('/request_snapshot', 'packet.snapshot_scope', 'Snapshot does not belong to the analyzed turn/branch.')
            candidate_ids = {c['id'] for c in packet['candidates']}
            if len(candidate_ids) != len(packet['candidates']):
                add('/candidates', 'packet.candidate_id', 'Duplicate candidate id.')
            for i, candidate in enumerate(packet['candidates']):
                not_future(candidate['known_at'], cutoff, f'/candidates/{i}/known_at')
                for j, rep in enumerate(candidate['representations']):
                    sources(rep['source_refs'], candidate['known_at'], f'/candidates/{i}/representations/{j}/source_refs')
            question_ids = set()
            for i, question in enumerate(packet['questions']):
                if question['id'] in question_ids:
                    add(f'/questions/{i}', 'packet.question_id', 'Duplicate question id.')
                question_ids.add(question['id'])
                if not set(question['candidate_ids']) <= candidate_ids:
                    add(f'/questions/{i}', 'packet.candidate_ref', 'Question names a missing candidate.')
            not_future(packet['authorization']['evaluated_at'], cutoff, '/authorization/evaluated_at')
            if for_submission and packet['authorization']['decision'] != 'allow':
                add('/authorization', 'packet.not_authorized', 'Submission needs an explicit allow decision.')

        if kind == 'loom.history_event/1':
            check_event(value)
        elif kind == 'loom.active_task_spec/1':
            check_spec(value)
        elif kind == 'loom.request_snapshot/1':
            check_snapshot(value)
        elif kind == 'loom.evaluation_packet/1':
            check_packet(value)
        if for_submission and kind != 'loom.evaluation_packet/1':
            add('/', 'submission.packet_required', 'Submission validation requires an EvaluationPacket.')
        return issues


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('document', type=Path)
    parser.add_argument('--schema-dir', type=Path, default=SCHEMA_DIR)
    parser.add_argument('--for-submission', action='store_true')
    args = parser.parse_args(argv)
    try:
        issues = ContractValidator(args.schema_dir).validate(read_json(args.document), for_submission=args.for_submission)
    except (OSError, ValueError, TypeError, RecursionError):
        print(json.dumps({'valid': False, 'error': 'input_or_schema_load_error'}))
        return 2
    print(json.dumps({'valid': not issues, 'issues': [asdict(i) for i in issues]}, ensure_ascii=False, indent=2))
    return 1 if issues else 0


if __name__ == '__main__':
    raise SystemExit(main())
