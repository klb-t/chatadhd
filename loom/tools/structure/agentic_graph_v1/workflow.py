"""Compositional proposal/review workflows over the single graph packet codec.

Transport is an injected function. Methods/stages/models/acceptance/resources
are data; no fixed agent count, stage count, prompt family or paid capability is
hard-coded here. This module contains no provider, credential or network code.
"""
from __future__ import annotations

from copy import deepcopy
from pathlib import Path

try:
    from .. import openrouter_runner as safe
except ImportError:
    import openrouter_runner as safe
from . import packet as codec


def validate_method(method):
    safe._keys(method, {'schema', 'id', 'stages', 'authorization', 'resources'})
    if method['schema'] != 'loom.graph_packet_workflow/1' or not isinstance(method['id'], str) or not method['id']:
        raise ValueError('graph_workflow_identity_invalid')
    if not isinstance(method['stages'], list) or not method['stages']:
        raise ValueError('graph_workflow_stages_required')
    safe._keys(method['authorization'], {'paid_live', 'usage_jump_confirmed', 'reason'})
    if (type(method['authorization']['paid_live']) is not bool or
            type(method['authorization']['usage_jump_confirmed']) is not bool or
            not isinstance(method['authorization']['reason'], str)):
        raise ValueError('graph_workflow_authorization_data_invalid')
    safe._keys(method['resources'], {'expected_usage_multiplier', 'confirmation_multiplier', 'packet_limits'})
    for key in ('expected_usage_multiplier', 'confirmation_multiplier'):
        value = method['resources'][key]
        if type(value) not in (int, float) or value < 0:
            raise ValueError('graph_workflow_usage_multiplier_invalid')
    codec.validate_json_resources(method)
    limits = method['resources']['packet_limits']
    codec.validate_json_resources({}, limits)
    ids = set()
    for stage in method['stages']:
        safe._keys(stage, {'id', 'operation', 'model', 'recipe', 'apply_policy', 'on_failure', 'context'})
        for key in ('id', 'operation', 'model', 'recipe'):
            if not isinstance(stage[key], str) or not stage[key]:
                raise ValueError('graph_workflow_stage_value_invalid')
        if stage['id'] in ids or stage['on_failure'] not in {'stop', 'continue'}:
            raise ValueError('graph_workflow_stage_identity_or_failure_policy_invalid')
        ids.add(stage['id'])
        if stage['context'] not in {'original', 'current'}:
            raise ValueError('graph_workflow_context_policy_invalid')
        safe._keys(stage['apply_policy'], {'schema', 'acceptance', 'allow_source_tombstones'})
        if (stage['apply_policy']['schema'] != 'loom.graph_packet_apply_policy/1' or
                stage['apply_policy']['acceptance'] not in {'preview', 'auto'} or
                type(stage['apply_policy']['allow_source_tombstones']) is not bool):
            raise ValueError('graph_workflow_apply_policy_invalid')
    return method


def _write_new(path, value):
    path = Path(path)
    with path.open('xb') as stream:
        stream.write(safe.canonical(value) + b'\n')


def run_workflow(packet, method, transport, *, output_dir=None, explicit_acceptance=None):
    """Call each declared stage once, preserving its first response and decision.

    transport(request) returns {content: JSON string or parsed diff, origin:
    externally verified instrument origin, known_at: response availability,
    accounting: provider/ledger data}. `transport.paid_live` declares whether
    the injected implementation spends money. Config data, not this codec,
    authorizes paid operation and the owner's expected-usage jump rule.

    Each response is saved before parsing. Parsed model-supplied origin/time is
    retained, then a separate diff is bound to transport-verified origin/time.
    Native Claim assessment/support/source timestamps are never rebound.
    """
    validate_method(method)
    codec.validate_packet(packet, resource_limits=method['resources']['packet_limits'])
    paid = getattr(transport, 'paid_live', False)
    if type(paid) is not bool:
        raise ValueError('graph_workflow_transport_paid_declaration_invalid')
    if paid and not method['authorization']['paid_live']:
        raise ValueError('graph_workflow_paid_transport_not_authorized_by_data')
    if (method['resources']['expected_usage_multiplier'] >= method['resources']['confirmation_multiplier'] and
            not method['authorization']['usage_jump_confirmed']):
        raise ValueError('graph_workflow_expected_usage_jump_needs_recorded_confirmation')
    acceptance = {} if explicit_acceptance is None else explicit_acceptance
    if (not isinstance(acceptance, dict) or set(acceptance) - {s['id'] for s in method['stages']} or
            any(type(v) is not bool for v in acceptance.values())):
        raise ValueError('graph_workflow_explicit_acceptance_invalid')
    folder = None
    if output_dir is not None:
        folder = Path(output_dir); folder.mkdir(parents=True, exist_ok=False)
        _write_new(folder / 'input_packet.json', packet); _write_new(folder / 'method.json', method)
    original, current = deepcopy(packet), deepcopy(packet)
    stages, proposals = [], []
    for ordinal, stage in enumerate(method['stages'], 1):
        stage_packet = original if stage['context'] == 'original' else current
        request = {'schema': 'loom.graph_packet_workflow_request/1', 'workflow_id': method['id'],
                   'stage': deepcopy(stage), 'packet': deepcopy(stage_packet),
                   'prior_proposals': deepcopy(proposals), 'ordinal': ordinal}
        # No retry: an error is an observed stage outcome, governed by the
        # configured stop/continue policy and retained in the first transcript.
        row = {'stage_id': stage['id'], 'request_sha256': codec.digest(request), 'request': request,
               'state': 'unavailable', 'canonical_store_written': False}
        if folder is not None:
            _write_new(folder / f'{ordinal:04d}.request.json', request)
        try:
            raw = transport(deepcopy(request))
        except Exception as error:
            # Fixed error class only, never print a credential/remote message.
            raw = {'transport_failure_class': type(error).__name__}
            row['reason'] = 'transport_failed'
        row['first_transport_response'] = deepcopy(raw)
        if folder is not None:
            _write_new(folder / f'{ordinal:04d}.first_transport_response.json', raw)
        if 'reason' not in row:
            try:
                safe._keys(raw, {'content', 'origin', 'known_at', 'accounting'})
                codec.validate_origin(raw['origin']); codec._timestamp(raw['known_at'])
                if not isinstance(raw['accounting'], dict):
                    raise ValueError('graph_workflow_accounting_record_required')
                proposed = safe.parse_json(raw['content']) if isinstance(raw['content'], (str, bytes)) else deepcopy(raw['content'])
                codec.validate_diff(proposed, stage_packet, resource_limits=method['resources']['packet_limits'])
                row['model_proposed_diff'] = deepcopy(proposed); row['model_proposed_diff_sha256'] = codec.digest(proposed)
                bound = deepcopy(proposed); bound['origin'] = deepcopy(raw['origin']); bound['known_at'] = raw['known_at']
                codec.validate_diff(bound, stage_packet, resource_limits=method['resources']['packet_limits'])
                row['instrument_bound_diff'] = bound; row['instrument_bound_diff_sha256'] = codec.digest(bound)
                preview = codec.preview_diff(stage_packet, bound)
                result, receipt = codec.apply_diff(stage_packet, bound, stage['apply_policy'],
                                                  explicitly_accepted=acceptance.get(stage['id'], False))
                # A stage rooted at the original packet is an independent
                # alternative projection. It may be selected by explicit data;
                # it never silently merges with a competing current projection.
                if receipt['accepted']:
                    current = result
                row.update(state='completed', preview=preview, application_receipt=receipt,
                           selected_packet_id=current['packet_id'], accounting=deepcopy(raw['accounting']))
                proposals.append({'stage_id': stage['id'], 'model_proposed_diff': deepcopy(proposed),
                                  'instrument_bound_diff': deepcopy(bound), 'accepted': receipt['accepted']})
            except (KeyError, TypeError, ValueError, IndexError):
                row['reason'] = 'response_diff_or_provenance_rejected'
        stages.append(row)
        if folder is not None:
            _write_new(folder / f'{ordinal:04d}.outcome.json', row)
        if row['state'] != 'completed' and stage['on_failure'] == 'stop':
            break
    result = {'schema': 'loom.graph_packet_workflow_result/1', 'workflow_id': method['id'],
              'planned_stages': len(method['stages']), 'attempted_stages': len(stages),
              'completed_stages': sum(s['state'] == 'completed' for s in stages),
              'original_packet_id': original['packet_id'], 'selected_packet': current,
              'stages': stages, 'canonical_store_written': False,
              'model_quality_measured': False, 'acceptance_establishes_content_truth': False}
    if folder is not None:
        _write_new(folder / 'result_first.json', result)
    return result
