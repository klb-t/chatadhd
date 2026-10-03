"""Opt-in deterministic evidence locator; no model, gold, network or promotion."""
from copy import deepcopy
import hashlib


def provenance_for_turn(case, turn):
    """Whole unchanged source, in its existing turn.text coordinate space."""
    text = turn['text']
    if not isinstance(text, str) or not text:
        raise ValueError('empty_or_invalid_source_turn')
    encoded = text.encode('utf-8')
    return {'source_id': case['source_id'], 'turn_id': turn['id'],
            'source_known_at': turn['known_at'], 'source_speaker': turn['speaker'],
            'coordinate_space': 'turn.text', 'char_start': 0, 'char_end': len(text),
            'byte_start': 0, 'byte_end': len(encoded),
            'source_text_sha256': hashlib.sha256(encoded).hexdigest(),
            'derivation': 'full_source_copied_by_turn_id',
            'semantic_support_established': False}


def quote_free_copy(value):
    """Comparison projection: remove only existing evidence quote values."""
    result = deepcopy(value)
    if not isinstance(result, dict):
        return result
    for collection in ('source_assertions', 'status_events'):
        if not isinstance(result.get(collection), list):
            continue
        for item in result[collection]:
            if not isinstance(item, dict) or not isinstance(item.get('evidence'), list):
                continue
            for entry in item['evidence']:
                if isinstance(entry, dict) and 'quote' in entry:
                    entry['quote'] = {'removed_quote_for_drift_comparison': True}
    return result


def bind_turn_references(value, case, *, raw_response_sha256):
    """Return derived input + sidecar. Change only quote under explicit policy.

    Malformed shapes/unknown IDs are left to the unchanged compiler, preserving
    its invalid-item counters. The caller must preserve the original raw bytes.
    """
    turns = {t['id']: t for t in case['turns']}
    if len(turns) != len(case['turns']):
        raise ValueError('duplicate_source_turn_id')
    derived = deepcopy(value); sidecar = []
    if not isinstance(derived, dict):
        return derived, sidecar
    for collection in ('source_assertions', 'status_events'):
        if not isinstance(derived.get(collection), list):
            continue
        for item_index, item in enumerate(derived[collection]):
            if not isinstance(item, dict) or not isinstance(item.get('evidence'), list):
                continue
            for evidence_index, entry in enumerate(item['evidence']):
                record = {'case_id': case['id'], 'raw_response_sha256': raw_response_sha256,
                          'evidence_pointer': f'/{collection}/{item_index}/evidence/{evidence_index}',
                          'model_evidence_entry': deepcopy(entry),
                          'model_hint': deepcopy(entry.get('quote')) if isinstance(entry, dict) else None,
                          'binding_state': 'refused', 'reason': 'malformed_evidence_shape'}
                if isinstance(entry, dict) and set(entry) == {'turn_id', 'quote'}:
                    turn_id = entry['turn_id']
                    if not isinstance(turn_id, str) or turn_id not in turns:
                        record['reason'] = 'unknown_or_invalid_turn_id'
                    else:
                        turn = turns[turn_id]
                        try:
                            source = provenance_for_turn(case, turn)
                        except ValueError:
                            record['reason'] = 'empty_or_invalid_source_turn'
                        else:
                            original = entry['quote']; text = turn['text']
                            start = text.find(original) if isinstance(original, str) and original else -1
                            record.update(binding_state='bound', reason=None, source=source,
                                model_hint_exact_unique_in_turn=(start >= 0 and text.find(original, start + 1) < 0),
                                quote_changed=original != text)
                            entry['quote'] = text
                sidecar.append(record)
    if quote_free_copy(derived) != quote_free_copy(value):
        raise AssertionError('non_quote_field_drift')
    return derived, sidecar
