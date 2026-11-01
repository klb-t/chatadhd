"""Graph constructor arguments only. Identity/storage remain in existing native APIs."""
from .contract import canonical, digest


def _walk(value, path, pointer=''):
    if not path:
        yield pointer, value
        return
    token, *rest = path
    if token == '*':
        if not isinstance(value, list):
            raise ValueError('mapping_expected_array')
        for index, child in enumerate(value):
            yield from _walk(child, rest, pointer + '/' + str(index))
    else:
        if not isinstance(value, dict) or token not in value:
            raise ValueError('mapping_key_missing')
        escaped = token.replace('~', '~0').replace('/', '~1')
        yield from _walk(value[token], rest, pointer + '/' + escaped)


def native_arguments(result, compiled):
    if result['descriptor_sha256'] != compiled.sha256 or result['status'] != 'partial':
        raise ValueError('usable_result_required')
    from .xls import projection
    replay_document, replay_text, replay_blocks = projection(bytes.fromhex(result['stdout_hex']))
    if (replay_document != result['document'] or replay_text.decode('utf-8') != result['text']
            or replay_blocks != result['blocks']):
        raise ValueError('saved_conversion_projection_mismatch')
    mapping = compiled.plan()['mapping']
    document, text = result['document'], result['text'].encode('utf-8')
    if digest(canonical(document)) != result['json_sha256'] or digest(text) != result['text_sha256']:
        raise ValueError('derived_content_hash_mismatch')
    receipt = result['tool_receipt']
    if (digest(bytes.fromhex(result['stdout_hex'])) != receipt['stdout_sha256']
            or digest(bytes.fromhex(result['stderr_hex'])) != receipt['stderr_sha256']
            or receipt['returncode'] != 0):
        raise ValueError('saved_tool_receipt_mismatch')
    source = 'sha256:' + result['json_sha256']
    text_source = 'sha256:' + result['text_sha256']
    units = []
    for rule in mapping['unit_rules']:
        for pointer, value in _walk(document, rule['path']):
            parent = max((u['locator']['json_pointer'] for u in units
                          if pointer.startswith(u['locator']['json_pointer'] + '/')), key=len, default=None)
            units.append({'source': source, 'kind': rule['kind'],
                          'locator': {'source': source, 'json_pointer': pointer},
                          'title': pointer, 'date': '', 'artifact_type': compiled.plan()['format_id'],
                          'lang': '', 'bytes': len(canonical(value)),
                          'attrs': {'evidence_class': 'derived', 'confidence': None,
                                    'parent_pointer': parent, 'original_address': None}})
    observations = []
    for ordinal, block in enumerate(result['blocks']):
        start, end = block['text_range']
        if (type(start) is not int or type(end) is not int or not 0 <= start <= end <= len(text)
                or text[start:end].decode('utf-8') != block['text']):
            raise ValueError('observation_range_mismatch')
        observations.append({'unit_pointer': block['json_pointer'], 'kind': mapping['observation_kind'],
                             'text': block['text'], 'ordinal': ordinal,
                             'locator': {'source': text_source, 'byte_start': start, 'byte_len': end-start},
                             'attrs': {'evidence_class': 'derived', 'confidence': None,
                                       'coordinate': mapping['coordinate'],
                                       'original_sha256': result['original']['sha256'],
                                       'original_address': None}})
    # Observations address a text Unit in the UTF8 Source. JSON unit_pointer is
    # a binding to its structural sibling; host creates the actual text Unit ID.
    text_units = [{'source': text_source, 'kind': mapping['text_unit_kind'],
                   'locator': observation['locator'],
                   'attrs': {'structural_unit_pointer': observation['unit_pointer']}}
                  for observation in observations]
    return {'schema': 'loom.unknown_structure.native_arguments/1',
            'identity_assignment': 'native_Unit_make_id_Observation_make_id',
            'sources': [{'role': role, 'blob_hash': hash_value}
                        for role, hash_value in [('original', result['original']['sha256']),
                                                ('derived_json', result['json_sha256']),
                                                ('derived_utf8', result['text_sha256'])]],
            'units': units, 'text_units': text_units, 'observations': observations,
            'provenance': {'source_sha256': result['original']['sha256'],
                           'transform_sha256': digest(canonical({'descriptor_sha256': compiled.sha256,
                                                                'tool_receipt': receipt})),
                           'tool_receipt': result['tool_receipt']},
            'native_materialized': False}
