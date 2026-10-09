#!/usr/bin/env python3
"""Read-only, externally witnessed recovery in the existing PrivateLedger.

No POST is reachable here. A recovery event and its optional billing projection
commit atomically in the payer's SQLite database. A digest binds bytes, not truth:
external capture attribution is an explicit reviewed trust boundary, never inferred
from generation metadata: the existing verifier does not attest the request hash.
"""
from __future__ import annotations
import argparse
import base64
from decimal import Decimal
from datetime import datetime
import json
from pathlib import Path
import re

try:
    from . import research_programme_runner as payer
except ImportError:
    import research_programme_runner as payer

PROOF_SCHEMA = 'loom.research_programme_external_recovery/1'
CAPTURE_SCHEMA = 'loom.research_programme_external_capture/1'
EVENT_SCHEMA = 'loom.research_programme_recovery_event/1'
_BINDINGS = ('operation_id', 'programme_id', 'stage_id', 'manifest_sha256',
             'key_fingerprint_sha256', 'request_sha256', 'reservation_usd',
             'started_at', 'receipt_operation', 'expected_cost_usd', 'billing_verification_timing')


# Exact diagnostic codes only. Arbitrary exception text may contain source data.
SAFE_REASONS = frozenset({
    'credential_in_recovery_data',
    'credential_in_recovery_response',
    'existing_private_ledger_required',
    'recovery_accounting_snapshot_changed',
    'recovery_actual_exceeds_reservation',
    'recovery_already_other_resolution',
    'recovery_attempt_campaign_mismatch',
    'recovery_attempt_hash_missing',
    'recovery_attempt_not_eligible',
    'recovery_billing_reads_missing',
    'recovery_campaign_key_policy_mismatch',
    'recovery_campaign_usage_unreconciled',
    'recovery_capture_binding_mismatch',
    'recovery_capture_provenance_missing',
    'recovery_capture_request_mismatch',
    'recovery_capture_review_missing',
    'recovery_capture_route_mismatch',
    'recovery_capture_schema_invalid',
    'recovery_capture_time_outside_attempt',
    'recovery_controlled_transport_required',
    'recovery_credential_reference_unavailable',
    'recovery_duplicate_generation',
    'recovery_event_binding_mismatch',
    'recovery_event_digest_mismatch',
    'recovery_event_invalid_action',
    'recovery_existing_key_fingerprint_required',
    'recovery_first_response_conflict',
    'recovery_first_response_incomplete',
    'recovery_frozen_manifest_mismatch',
    'recovery_key_reference_mismatch',
    'recovery_manifest_operation_missing',
    'recovery_manifest_programme_mismatch',
    'recovery_operation_changed',
    'recovery_original_attempt_changed',
    'recovery_original_evidence_changed',
    'recovery_original_file_required',
    'recovery_original_generation_changed',
    'recovery_original_journal_binding_mismatch',
    'recovery_original_journal_invalid',
    'recovery_original_request_changed',
    'recovery_original_response_changed',
    'recovery_other_cost_unknown',
    'recovery_policy_changed',
    'recovery_proof_binding_mismatch',
    'recovery_proof_event_missing',
    'recovery_proof_replay_mismatch',
    'recovery_read_binding_mismatch',
    'recovery_read_incomplete',
    'recovery_read_route_invalid',
    'recovery_request_encoding_invalid',
    'recovery_reservation_invalid',
    'recovery_reservation_missing',
    'recovery_resolved_capture_conflict',
    'recovery_response_bytes_required',
    'recovery_response_digest_mismatch',
    'recovery_response_encoding_invalid',
    'recovery_unknown_original_file',
})

def safe_reason(exc, fallback):
    reason = str(exc)
    return reason if isinstance(exc, payer.ProgrammeError) and reason in SAFE_REASONS else fallback


def require(condition, reason):
    if not condition:
        raise payer.ProgrammeError(reason)


def encode_response(response):
    require(isinstance(response, dict) and isinstance(response.get('raw'), bytes), 'recovery_response_bytes_required')
    return {name: response.get(name) for name in ('http_status', 'latency_seconds', 'transport_error',
            'response_cache_status', 'response_cache_disabled')} | {
            'raw_base64': base64.b64encode(response['raw']).decode(), 'raw_sha256': payer.sha(response['raw'])}


def decode_response(value):
    try:
        raw = base64.b64decode(value['raw_base64'], validate=True)
    except Exception:
        raise payer.ProgrammeError('recovery_response_encoding_invalid') from None
    require(payer.sha(raw) == value.get('raw_sha256'), 'recovery_response_digest_mismatch')
    return {**value, 'raw': raw}


def validate_incomplete(ledger, original):
    """Inspect only existing bytes; missing witnesses are never manufactured."""
    require(original.get('state') in ('reserved', 'uncertain') and original.get('actual_cost_usd') is None,
            'recovery_attempt_not_eligible')
    require(original.get('programme_id') == ledger.programme_id and
            original.get('key_fingerprint_sha256') == ledger.fingerprint,
            'recovery_attempt_campaign_mismatch')
    require(payer.gate.digest(original.get('manifest_sha256')) and payer.gate.digest(original.get('request_sha256')),
            'recovery_attempt_hash_missing')
    require(payer.gate.amount(original['reservation_usd']) > 0, 'recovery_reservation_invalid')
    token = payer.sha(original['operation_id'].encode())
    pattern = re.compile(re.escape(token) + r'\.(started\.json|request\.bin|response\.bin|result\.json|generation\.bin|generation-read-\d+\.bin)$')
    files = set()
    for path in ledger.records.glob(token + '.*'):
        require(pattern.fullmatch(path.name) is not None, 'recovery_unknown_original_file')
        payer.private_path(path, ledger.repo_root)
        require(path.is_file(), 'recovery_original_file_required')
        files.add(path.name)
        raw = path.read_bytes()
        if path.name.endswith(('.started.json', '.result.json')):
            try:
                saved = json.loads(raw)
            except Exception:
                raise payer.ProgrammeError('recovery_original_journal_invalid') from None
            require(payer.canonical(saved) == raw and all(saved.get(k) == original.get(k) for k in _BINDINGS),
                    'recovery_original_journal_binding_mismatch')
        if path.name.endswith('.request.bin'):
            require(payer.sha(raw) == original['request_sha256'], 'recovery_original_request_changed')
        if path.name.endswith('.response.bin') and original.get('response_sha256'):
            require(payer.sha(raw) == original['response_sha256'], 'recovery_original_response_changed')
        if path.name.endswith('.generation.bin') and original.get('generation_sha256'):
            require(payer.sha(raw) == original['generation_sha256'], 'recovery_original_generation_changed')
    return files


def snapshot(ledger, original):
    return [{'file': name, 'sha256': payer.sha((ledger.records / name).read_bytes())}
            for name in sorted(validate_incomplete(ledger, original))]


def validate_events(ledger, originals):
    for row in ledger.db.execute('SELECT * FROM recovery_events ORDER BY rowid'):
        event = json.loads(row['payload'])
        require(event.get('schema') == EVENT_SCHEMA and event.get('event_id') == row['event_id'] and
                event.get('operation_id') == row['operation_id'] and event.get('programme_id') == ledger.programme_id
                and event.get('key_fingerprint_sha256') == ledger.fingerprint,
                'recovery_event_binding_mismatch')
        original = originals.get(row['operation_id'])
        require(original is not None and payer.sha(payer.canonical(original)) == event.get('original_attempt_sha256'),
                'recovery_original_attempt_changed')
        require(event['original_evidence'] == snapshot(ledger, original), 'recovery_original_evidence_changed')
        core = {k: v for k, v in event.items() if k != 'event_id'}
        require(payer.sha(payer.canonical(core)) == row['event_id'], 'recovery_event_digest_mismatch')
        require(event.get('paid_calls') == 0 and event.get('post_retried') is False, 'recovery_event_invalid_action')
        for read in event.get('reads', []):
            decode_response(read['response'])
            require(read.get('method') == 'GET' and read.get('route') in ('key', 'generation'), 'recovery_read_route_invalid')


def evidence_response(evidence, original, ledger):
    require(evidence.get('schema') == CAPTURE_SCHEMA, 'recovery_capture_schema_invalid')
    fields = ('operation_id', 'programme_id', 'stage_id', 'manifest_sha256', 'key_fingerprint_sha256', 'request_sha256')
    require(all(evidence.get(name) == original.get(name) for name in fields), 'recovery_capture_binding_mismatch')
    require(evidence.get('route_id') == original['receipt_operation']['route_id'], 'recovery_capture_route_mismatch')
    try:
        request = base64.b64decode(evidence['request_base64'], validate=True)
    except Exception:
        raise payer.ProgrammeError('recovery_request_encoding_invalid') from None
    require(payer.sha(request) == original['request_sha256'], 'recovery_capture_request_mismatch')
    require(isinstance(evidence.get('capture_origin'), str) and evidence['capture_origin'] and
            isinstance(evidence.get('captured_at'), str), 'recovery_capture_provenance_missing')
    payer.transport._timestamp(evidence['captured_at'])
    response = decode_response(evidence['response'])
    require(response.get('http_status') == 200 and not response.get('transport_error'), 'recovery_first_response_incomplete')
    token = payer.sha(original['operation_id'].encode())
    first_path = ledger.records / (token + '.response.bin')
    if first_path.exists():
        require(first_path.read_bytes() == response['raw'], 'recovery_first_response_conflict')
    parsed = payer.transport.extract_receipt(response, original['receipt_operation'])
    payer.verify_bound_first_receipt(parsed, original['receipt_operation'], original['reservation_usd'])
    return response, parsed


def validate_capture_time(evidence, original, checked_at):
    times = [datetime.fromisoformat(payer.transport._timestamp(x).replace('Z', '+00:00'))
             for x in (original['started_at'], evidence['captured_at'], checked_at)]
    require(times[0] <= times[1] <= times[2], 'recovery_capture_time_outside_attempt')


def _verified(event, original, ledger):
    """Reconstruct facts from saved external capture and authenticated GETs."""
    evidence = event['external_evidence']
    trust = event['capture_review']
    require(trust.get('external_capture_sha256') == payer.sha(payer.canonical(evidence)) and
            trust.get('request_response_attribution_reviewed') is True and
            isinstance(trust.get('reviewer_ref'), str) and trust['reviewer_ref'] and
            isinstance(trust.get('basis_ref'), str) and trust['basis_ref'], 'recovery_capture_review_missing')
    require(event.get('policy_sha256') == payer.sha(payer.canonical(event['policy'])), 'recovery_policy_changed')
    response, parsed = evidence_response(evidence, original, ledger)
    validate_capture_time(evidence, original, event['checked_at'])
    reads = event['reads']
    require(len(reads) == 2 and [(x['method'], x['route']) for x in reads] == [('GET', 'generation'), ('GET', 'key')],
            'recovery_billing_reads_missing')
    require(reads[0].get('params') == {'id': parsed['generation_id']} and reads[1].get('params') is None,
            'recovery_read_binding_mismatch')
    generation, key_response = (decode_response(x['response']) for x in reads)
    require(not generation.get('transport_error') and not key_response.get('transport_error'), 'recovery_read_incomplete')
    verified = payer.transport.verify_generation_receipt(generation, parsed, original['receipt_operation'])
    require(payer.gate.amount(verified['actual_cost_usd']) <= payer.gate.amount(original['reservation_usd']),
            'recovery_actual_exceeds_reservation')
    metadata = payer.transport.normalize_key_metadata(key_response, ledger.fingerprint, event['checked_at'])
    policy = event['policy']
    require(policy['programme_id'] == ledger.programme_id and
            payer.gate.amount(metadata['limit_usd']) == payer.gate.amount(policy['usd_cap']) and
            metadata['limit_reset'] is None and metadata['is_management_key'] is False and
            metadata['is_provisioning_key'] is not True and metadata['include_byok_in_limit'] is False and
            payer.gate.amount(metadata['byok_usage_usd']) == 0, 'recovery_campaign_key_policy_mismatch')
    # Every other attempt must already carry validated billing; never absorb an
    # unexplained account delta into this one generation or release unknowns.
    current = {x['operation_id']: x for x in ledger.rows()}
    originals = {x['operation_id']: x for x in ledger.original_rows()}
    others = event['accounting_snapshot']
    require(all(x['operation_id'] in current and x['operation_id'] != original['operation_id'] and
                x['original_attempt_sha256'] == payer.sha(payer.canonical(originals[x['operation_id']])) and
                x['actual_cost_usd'] == current[x['operation_id']].get('actual_cost_usd') and
                x['billing_verified'] == current[x['operation_id']].get('billing_verified') for x in others) and
            len({x['operation_id'] for x in others}) == len(others), 'recovery_accounting_snapshot_changed')
    require(all(x.get('actual_cost_usd') is not None and x.get('billing_verified') is True for x in others),
            'recovery_other_cost_unknown')
    expected = sum((payer.gate.amount(x['actual_cost_usd']) for x in others), Decimal(0)) + payer.gate.amount(verified['actual_cost_usd'])
    require(payer.gate.amount(metadata['usage_usd']) == expected, 'recovery_campaign_usage_unreconciled')
    require(not any(x.get('generation_id') == parsed['generation_id'] for x in current.values() if x['operation_id'] != original['operation_id']), 'recovery_duplicate_generation')
    return {**verified, 'state': 'completed', 'http_status': 200, 'transport_error': None,
            'generation_sha256': payer.sha(generation['raw']), 'attempt_resolution_id': event['event_id'],
            'original_state': original['state'], 'latency_seconds': response.get('latency_seconds'),
            'response_cache_status': response.get('response_cache_status'),
            'response_source': 'external_capture', 'recovery_evidence_class': 'reviewed_capture_plus_authenticated_gets'}


def validate_proof(ledger, original, proof):
    require(proof.get('schema') == PROOF_SCHEMA and proof.get('operation_id') == original['operation_id'] and
            proof.get('programme_id') == ledger.programme_id and proof.get('key_fingerprint_sha256') == ledger.fingerprint and
            proof.get('original_attempt_sha256') == payer.sha(payer.canonical(original)), 'recovery_proof_binding_mismatch')
    row = ledger.db.execute('SELECT payload FROM recovery_events WHERE event_id=?', (proof.get('resolution_id'),)).fetchone()
    require(row is not None, 'recovery_proof_event_missing')
    event = json.loads(row[0])
    require(event['outcome'] == 'verified' and proof['projection'] == _verified(event, original, ledger),
            'recovery_proof_replay_mismatch')
    return validate_incomplete(ledger, original)


def recovered_response(ledger, proof):
    row = ledger.db.execute('SELECT payload FROM recovery_events WHERE event_id=?', (proof['resolution_id'],)).fetchone()
    require(row is not None, 'recovery_proof_event_missing')
    return decode_response(json.loads(row[0])['external_evidence']['response'])


def recover(policy, manifest_path, private_dir, key_file, repo_root, operation_id, *,
            external_evidence=None, capture_review=None, transport_fn=None, controlled_transport=False,
            key_fingerprint_sha256=None, fault=None):
    """Inspect/recover one existing reservation. Never invokes a write endpoint.

    external_evidence/capture_review are supplied data, not searched credentials.
    Reviewer attribution is a trust declaration: hashes are not signatures.
    Missing or contradictory evidence appends unknown, retaining full reservation.
    Passing fault is an offline crash-test hook, absent from the command line.
    """
    repo_root = Path(repo_root).resolve()
    directory = payer.private_path(private_dir, repo_root, directory=True)
    require((directory / 'ledger.sqlite3').is_file(), 'existing_private_ledger_required')
    require(transport_fn is None or controlled_transport is True, 'recovery_controlled_transport_required')
    key = payer.transport.load_key_file(key_file, repo_root) if key_file is not None else None
    fingerprint = payer.transport.key_fingerprint(key) if key is not None else key_fingerprint_sha256
    require(payer.gate.digest(fingerprint), 'recovery_existing_key_fingerprint_required')
    require(key_fingerprint_sha256 is None or key_fingerprint_sha256 == fingerprint, 'recovery_key_reference_mismatch')
    raw_manifest = Path(manifest_path).read_bytes()
    parsed_manifest = payer.manifests.read_manifest_bytes(raw_manifest)
    operations = payer.manifests.load_operations(parsed_manifest, base_dir=Path(manifest_path).resolve().parent)
    matching = [x for x in operations if x['operation_id'] == operation_id]
    require(len(matching) == 1, 'recovery_manifest_operation_missing')
    operation = matching[0]
    operation['provider_aliases'] = policy['provider_aliases'].get(operation['provider_id'], [operation['provider_id']])
    operation['model_aliases'] = policy.get('model_aliases', {}).get(operation['model_id'], [operation['model_id']])
    operation['api_type'] = policy['transport']['routes'][operation['route_id']]['api_type']
    require(parsed_manifest.get('programme_id') == policy['programme_id'], 'recovery_manifest_programme_mismatch')
    supplied = payer.canonical([policy, external_evidence, capture_review]) + raw_manifest
    require(key is None or (key.encode() not in supplied and key.encode() not in operation['request_bytes']), 'credential_in_recovery_data')
    ledger = payer.PrivateLedger(directory, repo_root, policy['programme_id'], fingerprint)
    with ledger.locked(allow_stopped=True, recovery_operation=operation_id):
        original = next((x for x in ledger.original_rows() if x['operation_id'] == operation_id), None)
        require(original is not None, 'recovery_reservation_missing')
        require(original['manifest_sha256'] == payer.sha(raw_manifest) and original['stage_id'] == parsed_manifest['stage_id'] and
                original['request_sha256'] == operation['request_sha256'], 'recovery_frozen_manifest_mismatch')
        require(all(original['receipt_operation'][k] == operation[k] for k in original['receipt_operation']), 'recovery_operation_changed')
        proof = ledger.late_proofs().get(operation_id)
        if proof:
            require(proof.get('schema') == PROOF_SCHEMA, 'recovery_already_other_resolution')
            validate_proof(ledger, original, proof)
            if external_evidence is not None:
                saved = json.loads(ledger.db.execute('SELECT payload FROM recovery_events WHERE event_id=?',
                    (proof['resolution_id'],)).fetchone()[0])
                require(payer.canonical(saved['external_evidence']) == payer.canonical(external_evidence),
                        'recovery_resolved_capture_conflict')
            return {'status': 'already_resolved', 'resolution_id': proof['resolution_id'], 'paid_calls': 0}
        event = {'schema': EVENT_SCHEMA, 'operation_id': operation_id, 'programme_id': ledger.programme_id,
                 'key_fingerprint_sha256': fingerprint, 'original_attempt_sha256': payer.sha(payer.canonical(original)),
                 'original_evidence': snapshot(ledger, original), 'external_evidence': external_evidence,
                 'capture_review': capture_review, 'policy': policy, 'policy_sha256': payer.sha(payer.canonical(policy)),
                 'checked_at': payer.utc().isoformat(), 'reads': [], 'paid_calls': 0, 'post_retried': False,
                 'outcome': 'unknown', 'reason': 'external_capture_required',
                 'accounting_snapshot': [{'operation_id': x['operation_id'], 'actual_cost_usd': x.get('actual_cost_usd'),
                     'billing_verified': x.get('billing_verified'), 'original_attempt_sha256': payer.sha(payer.canonical(
                        next(y for y in ledger.original_rows() if y['operation_id'] == x['operation_id'])))}
                    for x in ledger.rows() if x['operation_id'] != operation_id]}
        def persist_observation(reason):
            observation = {**event, 'reason': reason}
            observation.pop('event_id', None)
            observation['event_id'] = payer.sha(payer.canonical(observation))
            with ledger.db:
                ledger.db.execute('INSERT OR IGNORE INTO recovery_events VALUES (?, ?, ?)',
                    (observation['event_id'], operation_id, payer.canonical(observation).decode()))
        # Capture is durable before a metadata GET. A process death cannot erase
        # evidence which already reached this recovery procedure.
        if external_evidence is not None:
            candidate_response = decode_response(external_evidence.get('response', {}))
            require(key is None or key.encode() not in candidate_response['raw'], 'credential_in_recovery_response')
        persist_observation('recovery_opened_evidence_unverified')
        projection = None
        if external_evidence is not None:
            try:
                require(capture_review is not None and capture_review.get('external_capture_sha256') == payer.sha(payer.canonical(external_evidence))
                        and capture_review.get('request_response_attribution_reviewed') is True and capture_review.get('reviewer_ref')
                        and capture_review.get('basis_ref'), 'recovery_capture_review_missing')
                _, parsed = evidence_response(external_evidence, original, ledger)
                validate_capture_time(external_evidence, original, event['checked_at'])
                require(key is not None, 'recovery_credential_reference_unavailable')
                sender = transport_fn or payer.transport.OpenRouterTransport(policy['transport'], key).request
                for route, params in (('generation', {'id': parsed['generation_id']}), ('key', None)):
                    response = sender('GET', route, None, params)
                    require(key.encode() not in response['raw'], 'credential_in_recovery_response')
                    event['reads'].append({'method': 'GET', 'route': route, 'params': params, 'response': encode_response(response)})
                    persist_observation('read_saved_resolution_pending')
                    if fault:
                        fault('after_' + route + '_get')
                # Event identity is computed after the outcome; projection binds
                # that identity and is regenerated inside the atomic transaction.
                event['event_id'] = 'pending'
                projection = _verified(event, original, ledger)
                event.update(outcome='verified', reason='external_capture_and_credit_billing_verified')
            except Exception as exc:
                # Library exceptions may include arbitrary request/source text.
                # Only approved constant diagnostics cross into case metadata.
                event['reason'] = safe_reason(exc, 'recovery_evidence_unverified')
                projection = None
        event.pop('event_id', None)
        event['event_id'] = payer.sha(payer.canonical(event))
        if projection is not None:
            projection = _verified(event, original, ledger)
        if fault:
            fault('before_event_commit')
        # One SQLite transaction: no orphan proof file / no half-settled row.
        with ledger.db:
            ledger.db.execute('INSERT INTO recovery_events VALUES (?, ?, ?)',
                              (event['event_id'], operation_id, payer.canonical(event).decode()))
            if projection is not None:
                proof = {'schema': PROOF_SCHEMA, 'resolution_id': event['event_id'], 'operation_id': operation_id,
                         'programme_id': ledger.programme_id, 'key_fingerprint_sha256': fingerprint,
                         'original_attempt_sha256': event['original_attempt_sha256'], 'projection': projection,
                         'paid_calls': 0, 'no_post_retry': True}
                ledger.db.execute('INSERT INTO attempt_resolutions VALUES (?, ?)', (operation_id, payer.canonical(proof).decode()))
            if fault:
                fault('within_event_commit')
        if fault:
            fault('after_event_commit')
        return {'status': 'resolved' if projection is not None else 'unknown', 'reason': event['reason'],
                'event_id': event['event_id'], 'paid_calls': 0,
                'actual_cost_usd': projection['actual_cost_usd'] if projection else None,
                'reservation_released': projection is not None, 'campaign_dispatch_authorized': False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('policy', 'manifest', 'private-dir', 'repo-root', 'operation-id'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--key-file')
    parser.add_argument('--key-fingerprint')
    parser.add_argument('--external-evidence')
    parser.add_argument('--capture-review')
    args = parser.parse_args(argv)
    try:
        result = recover(payer.read_json(args.policy), args.manifest, args.private_dir, args.key_file,
                         args.repo_root, args.operation_id,
                         external_evidence=payer.read_json(args.external_evidence) if args.external_evidence else None,
                         capture_review=payer.read_json(args.capture_review) if args.capture_review else None,
                         key_fingerprint_sha256=args.key_fingerprint)
        print(json.dumps(result, sort_keys=True))
        return 0 if result['status'] in ('resolved', 'already_resolved') else 2
    except Exception as exc:
        reason = safe_reason(exc, 'recovery_input_or_evidence_invalid')
        print(json.dumps({'status': 'blocked', 'reason': reason, 'paid_calls': 0}))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
