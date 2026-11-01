"""Pure preparation for Loom's SourceRecord, Unit/Observation and genesis APIs.

No database, schema migration, second graph or implicit model egress. The sole
integrator supplies the existing blob/provenance/knowledge/genesis operations.
"""

from .container import check, fail, settings
from .reader import EXTRACTOR, MIME, canonical, checked_json_size, extract, sha


def stable_id(prefix, *parts):
    # model_core.cpp join_key + kb::stable_id's existing 16-hex default.
    return prefix + sha('\x1f'.join(parts).encode('utf-8'))[:16]


def locator(source, start=None, stop=None, pointer=''):
    # Byte offsets address only the derived UTF-8 source, not its raw ZIP member.
    return {'source': source, 'member': '', 'json_pointer': pointer,
            'byte_start': start, 'byte_len': stop - start if start is not None else None,
            'time_start': None, 'time_end': None, 'line': None}


def configuration(config):
    if not isinstance(config, dict) or config.get('schema') != 'loom.odt_basic.adapter/1':
        fail('odt_configuration_invalid')
    for key in ('original_source_kind', 'derived_source_kind'):
        if not isinstance(config.get(key), str) or not config[key]:
            fail('odt_configuration_invalid')
    kinds = config.get('kinds')
    if not isinstance(kinds, dict):
        fail('odt_configuration_invalid')
    for kind in ('heading', 'paragraph', 'table', 'table_row', 'table_cell'):
        if (not isinstance(kinds.get(kind), dict)
                or not isinstance(kinds[kind].get('unit'), str) or not kinds[kind]['unit']
                or kinds[kind].get('observation') not in
                ('sentence', 'list_item', 'heading', 'code_block', 'utterance', 'table_row', 'field')):
            fail('odt_configuration_invalid')
    return config


def prepare_import(data, limits, config, *, source_key, name, language=''):
    """Return executable import inputs. Caller stores *exact data* as original.

    All validation precedes any caller writes. Native consumers must recompute
    Unit/Observation IDs with their model APIs and compare them to these values.
    An Observation is a quotation from the derived source; attrs retain its
    original XML address and mark its evidence class explicitly.
    """
    limits = settings(limits)
    config = configuration(config)
    if (not isinstance(source_key, str) or not source_key or not isinstance(name, str)
            or not name or not isinstance(language, str)):
        fail('odt_configuration_invalid')
    extraction = extract(data, limits)
    raw = extraction['source']
    projection = extraction['projection']
    transform = {'extractor': EXTRACTOR, 'limits': limits, 'adapter': config,
                 'coordinate': projection['coordinate']}
    transform_hash = sha(canonical(transform).encode('utf-8'))
    raw_id = 'src_odt_' + raw['sha256']
    derived_id = 'src_odt_text_' + sha(canonical(
        [source_key, raw['sha256'], projection['sha256'], transform_hash]).encode('utf-8'))
    derivation = {'schema': 'loom.odt_basic.derivation/1', 'original_source_id': raw_id,
                  'original_sha256': raw['sha256'], 'original_size': raw['size'],
                  'member': raw['member'], 'member_sha256': raw['member_sha256'],
                  'transform': transform, 'transform_sha256': transform_hash,
                  'extraction_sha256': sha(canonical(extraction).encode('utf-8'))}
    extraction_id = 'src_odt_structure_' + derivation['extraction_sha256']

    def source_record(sid, kind, uri, digest, size, mime, fmt, metadata):
        return {'id': sid, 'kind': kind, 'uri': uri, 'blob_hash': digest,
                'size': size, 'mime': mime, 'format': fmt, 'title': name,
                'parser': 'loom.odt_basic', 'parser_version': '1', 'imported_at': '',
                'metadata': metadata}

    sources = [
        source_record(raw_id, config['original_source_kind'], 'blob:' + raw['sha256'],
                      raw['sha256'], raw['size'], MIME, 'odt',
                      {'evidence_class': 'observed', 'source_key': source_key}),
        source_record(derived_id, config['derived_source_kind'], source_key,
                      projection['sha256'], projection['size'], 'text/plain; charset=utf-8', 'text',
                      {'evidence_class': 'derived', 'source_key': source_key,
                       'coordinate': 'derived_utf8_bytes', 'derivation': derivation,
                       'warnings': extraction['warnings']}),
        source_record(extraction_id, config['derived_source_kind'], source_key,
                      derivation['extraction_sha256'], len(canonical(extraction).encode('utf-8')),
                      'application/json', 'json',
                      {'evidence_class': 'derived', 'derivation': derivation}),
    ]
    utf8 = extraction['text'].encode('utf-8')
    units, observations = [], []
    retained_size = checked_json_size(extraction, limits)
    for ordinal, block in enumerate(extraction['blocks']):
        start, stop = block['byte_range']
        loc = locator(derived_id, start, stop)
        # Nested table/row/cell/paragraph can have identical text spans. Locate
        # each Unit in the saved extraction JSON, and its quotation in the UTF-8
        # source. This preserves native make_id without fabricated coordinates.
        unit_loc = locator(extraction_id, pointer='/blocks/' + str(ordinal))
        uid = stable_id('un_', extraction_id, canonical(unit_loc))
        parent = block['parent']
        attrs = {'evidence_class': 'derived', 'coordinate': 'derived_utf8_bytes',
                 'recognized_kind': block['kind'], 'original_sha256': raw['sha256'],
                 'original_source_id': raw_id, 'original_locator': block['raw_locator'],
                 'observed_xml': block['observed'], 'structure_evidence_class': 'observed',
                 'transform_sha256': transform_hash,
                 'text_locator': loc,
                 'parent_unit': units[parent]['id'] if parent is not None else ''}
        for key in ('level', 'logical_row', 'logical_column', 'repeat', 'covered'):
            if key in block:
                attrs[key] = block[key]
        unit = {'id': uid, 'source': extraction_id, 'kind': config['kinds'][block['kind']]['unit'],
                'locator': unit_loc, 'title': '', 'date': '', 'artifact_type': 'odt',
                'lang': language, 'bytes': len(canonical(block).encode('utf-8')), 'attrs': attrs}
        retained_size += checked_json_size(unit, limits)
        # A lower bound catches nested tables quoting the same large span before
        # accumulating many copies. The exact final JSON budget is checked below.
        check(limits, 'max_output_bytes', retained_size + stop - start)
        units.append(unit)
        value = utf8[start:stop].decode('utf-8')
        observation = {
            'id': stable_id('ob_', uid, canonical(loc), sha(value.encode('utf-8'))),
            'unit': uid, 'kind': config['kinds'][block['kind']]['observation'],
            'text': value, 'locator': loc, 'lang': language, 'date': '',
            'ordinal': ordinal, 'artifact_type': 'odt', 'speaker': '', 'attrs': dict(attrs),
        }
        retained_size += checked_json_size(observation, limits)
        check(limits, 'max_output_bytes', retained_size)
        observations.append(observation)
    result = {
        'schema': 'loom.odt_basic.import/1', 'sources': sources, 'units': units,
        'observations': observations, 'derivation': derivation, 'extraction': extraction,
        'genesis': {'source': {'id': derived_id + ':' + transform_hash,
                               'kind': config['derived_source_kind'], 'conv_id': '',
                               'group': source_key, 'ordinal': 0, 'when': '', 'role': '',
                               'actor': EXTRACTOR}, 'text': extraction['text']},
        'request': {'source_key': source_key, 'name': name, 'language': language},
    }
    checked_json_size(result, limits)
    return result


def read_unit(data, prepared, unit_id, limits, config):
    """Verify raw version and the complete saved plan before returning a quotation.

    Replays this small reader rather than trusting caller-supplied offsets/hashes.
    Durable/native readers can use their existing pinned receipt verification.
    """
    try:
        if sha(data) != prepared['derivation']['original_sha256']:
            fail('odt_source_mismatch')
        replay = prepare_import(data, limits, config, **prepared['request'])
        if canonical(replay) != canonical(prepared):
            fail('odt_receipt_mismatch')
        for observation in replay['observations']:
            if observation['unit'] == unit_id:
                return {'text': observation['text'], 'locator': observation['locator'],
                        'provenance': observation['attrs']}
        fail('odt_unit_not_found')
    except (KeyError, TypeError, ValueError):
        fail('odt_receipt_mismatch')
