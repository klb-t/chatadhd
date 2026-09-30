"""Generate exchange schemas; semantic/hash/replay checks remain in packet.py."""
from copy import deepcopy
import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
TEXT = {'type': 'string'}
NONEMPTY = {'type': 'string', 'minLength': 1}
NULL = {'type': 'null'}
NUMBER = {'type': 'number'}
UNIT = {'type': 'number', 'minimum': 0, 'maximum': 1}
TEXTS = {'type': 'array', 'items': TEXT}


def obj(properties):
    return {'type': 'object', 'properties': properties, 'required': list(properties), 'additionalProperties': False}


def ref(name):
    return {'$ref': '#/$defs/' + name}


def nullable(schema):
    return {'anyOf': [schema, NULL]}


def array(schema):
    return {'type': 'array', 'items': schema}


def schemas():
    enums = {'evidence': {'enum': ['observed', 'derived', 'inferred', 'extrapolated', 'absent', 'user']},
             'origin': {'enum': ['archive', 'repo', 'user', 'external_authority', 'model_knowledge', 'system']},
             'status': {'enum': ['active', 'contested', 'superseded', 'rejected']}}
    defs = deepcopy(enums)
    defs['instrument_origin'] = obj({'kind': {'enum': ['recorded', 'model', 'user', 'system']}, 'actor': NONEMPTY,
        'model': nullable(NONEMPTY), 'recipe_sha256': nullable({'type': 'string', 'pattern': '^[0-9a-f]{64}$'}),
        'response_sha256': nullable({'type': 'string', 'pattern': '^[0-9a-f]{64}$'})})
    defs['locator'] = obj({'source': TEXT, 'member': TEXT, 'json_pointer': TEXT,
        'byte_start': nullable({'type': 'integer', 'minimum': 0}), 'byte_len': nullable({'type': 'integer', 'minimum': 0}),
        'time_start': nullable(NUMBER), 'time_end': nullable(NUMBER), 'line': nullable({'type': 'integer', 'minimum': 0})})
    defs['support'] = obj({'observation': NONEMPTY, 'locator': ref('locator'), 'quote': TEXT, 'extractor': TEXT, 'quality': UNIT})
    defs['derivation'] = obj({'operator': NONEMPTY, 'operator_version': {'type': 'integer', 'minimum': 1},
                              'morphism': TEXT, 'depth': {'type': 'integer', 'minimum': 0}})
    defs['assessment'] = obj({'basis': obj({'support': array(ref('support')), 'derivation': nullable(ref('derivation'))}),
        'evidence_class': ref('evidence'), 'origin': ref('origin'), 'confidence': UNIT,
        'premises': obj({'claims': TEXTS, 'principles': TEXTS, 'assumptions': TEXTS}),
        'counter': obj({'observations': TEXTS, 'claims': TEXTS}), 'status': ref('status'),
        'consequences': obj({'claims': TEXTS, 'predictions': TEXTS, 'checks': TEXTS}),
        'open': obj({'slots': TEXTS, 'questions': TEXTS, 'fill_query': {}}),
        'expected_property': nullable(obj({'expr': {'type': 'object'}, 'rationale': TEXT, 'confirm_if': TEXTS, 'refute_if': TEXTS})),
        'check_state': {'enum': ['pending', 'holds', 'violated', 'n/a']},
        'alternatives': array(obj({'object': TEXT, 'value': {}, 'score': NUMBER}))})
    defs['claim'] = obj({'id': NONEMPTY, 'subject': NONEMPTY, 'predicate': NONEMPTY, 'object': TEXT, 'value': {},
        'qualifiers': obj({'valid_from': TEXT, 'valid_to': TEXT, 'version': TEXT, 'branch': TEXT, 'scope': TEXT,
                           'lang': TEXT, 'extra': {'type': 'object'}}), 'assessment': ref('assessment')})
    defs['entity'] = obj({'id': NONEMPTY, 'kind': NONEMPTY, 'canonical_key': NONEMPTY, 'label': TEXT,
        'labels': {'type': 'object', 'additionalProperties': TEXT},
        'aliases': array(obj({'key': NONEMPTY, 'surface': TEXT, 'lang': TEXT, 'method': TEXT, 'count': {'type': 'integer'}, 'confidence': UNIT})),
        'parent': TEXT, 'first_seen': TEXT, 'last_seen': TEXT, 'evidence_class': ref('evidence'), 'origin': ref('origin'),
        'confidence': UNIT, 'status': ref('status'), 'attrs': {'type': 'object'}})
    defs['observation'] = obj({'id': NONEMPTY, 'unit': NONEMPTY,
        'kind': {'enum': ['sentence', 'list_item', 'heading', 'code_block', 'utterance', 'table_row', 'field']},
        'text': TEXT, 'locator': ref('locator'), 'lang': TEXT, 'date': TEXT, 'ordinal': {'type': 'integer'},
        'artifact_type': TEXT, 'speaker': TEXT, 'attrs': {'type': 'object'}})
    defs['source'] = obj({'observation': ref('observation'), 'known_at': nullable(TEXT),
                           'text_sha256': {'type': 'string', 'pattern': '^[0-9a-f]{64}$'}})
    defs['definition'] = obj({'id': NONEMPTY, 'kind': NONEMPTY, 'description': TEXT, 'examples': array({}),
                               'origin': ref('instrument_origin'), 'attrs': {'type': 'object'}})
    defs['record_provenance'] = obj({'known_at': nullable(TEXT), 'origin': ref('instrument_origin'),
                                     'record_sha256': {'type': 'string', 'pattern': '^[0-9a-f]{64}$'}})
    definitions = {'definitions': 'definition', 'entities': 'entity', 'claims': 'claim', 'sources': 'source'}
    edits = {}
    for collection, typename in definitions.items():
        edits[collection] = obj({'add': array(ref(typename)),
            'update': array(obj({'id': NONEMPTY, 'before_sha256': NONEMPTY, 'after': ref(typename)})),
            'remove': array(obj({'id': NONEMPTY, 'before_sha256': NONEMPTY, 'reason': NONEMPTY}))})
    defs['diff'] = obj({'schema': {'const': 'loom.graph_packet_diff/1'}, 'base_packet_sha256': NONEMPTY,
        'proposal_id': NONEMPTY, 'origin': ref('instrument_origin'), 'known_at': nullable(TEXT), **edits,
        'task': nullable(obj({'before_sha256': NONEMPTY, 'after': {'type': 'object'}})),
        'annotations': obj({'critique': array({}), 'questions': array({}),
            'operations': array(obj({'kind': NONEMPTY, 'inputs': TEXTS, 'outputs': TEXTS, 'reason': TEXT}))})})
    defs['history_change'] = obj({'collection': {'enum': list(definitions)}, 'action': {'enum': ['add', 'update', 'remove']},
        'record_id': NONEMPTY, 'before': nullable({'type': 'object'}), 'after': nullable({'type': 'object'}),
        'before_provenance': nullable(ref('record_provenance')), 'after_provenance': nullable(ref('record_provenance'))})
    defs['history_event'] = obj({'application_id': NONEMPTY, 'base_packet_id': NONEMPTY, 'diff': ref('diff'),
        'changes': array(ref('history_change')), 'previous_task': {'type': 'object'},
        'previous_order': obj({name: TEXTS for name in definitions}), 'result_origin': ref('instrument_origin')})
    packet = obj({'schema': {'const': 'loom.graph_packet/1'}, 'packet_id': NONEMPTY,
        **{name: array(ref(typename)) for name, typename in definitions.items()}, 'task': {'type': 'object'},
        'provenance': obj({name: {'type': 'object', 'additionalProperties': ref('record_provenance')} for name in definitions}),
        'history': array(ref('history_event'))})
    packet.update({'$schema': 'https://json-schema.org/draft/2020-12/schema',
        '$id': 'https://loom.local/schemas/graph_packet/1', '$defs': defs,
        'description': 'Proposal exchange projection. Native JSON fields retained; hash/source/replay/epistemic checks run in packet.py; no native-store write or truth certification.'})
    diff = {'$schema': packet['$schema'], '$id': 'https://loom.local/schemas/graph_packet_diff/1', '$ref': '#/$defs/diff', '$defs': defs,
            'description': 'Same graph record shapes inside reversible add/update/tombstone diff. Open operation annotations perform no implicit graph rewrite.'}
    return packet, diff


def main():
    packet, diff = schemas()
    for filename, schema in [('graph_packet.schema.json', packet), ('graph_packet_diff.schema.json', diff)]:
        (HERE / filename).write_text(json.dumps(schema, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
