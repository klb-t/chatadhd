#!/usr/bin/env python3
"""Validate source projections and embed runtime .pack data; no domain dispatch."""
import argparse
import copy
import hashlib
import json
import math
import os
import re
import tempfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'src/model/runtime_profiles_embedded.inc'
MANIFEST = 'runtime_sources.pack'
TYPES = {'object', 'array', 'string', 'integer', 'number', 'boolean', 'null'}
ASSERTIONS = {'type', 'required', 'properties', 'items', 'additionalProperties',
              'minimum', 'maximum', 'minLength', 'minItems', 'enum'}
ANNOTATIONS = {'description', 'title', 'x-setting', 'x-unit', 'x-consumer'}


def unique_object(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ValueError(f'duplicate JSON key: {key}')
        out[key] = value
    return out


def finite_tree(value):
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError('nonfinite JSON number')
    if isinstance(value, str):
        value.encode('utf-8')  # Reject unpaired Unicode surrogates.
    elif isinstance(value, list):
        for child in value:
            finite_tree(child)
    elif isinstance(value, dict):
        for key, child in value.items():
            finite_tree(key)
            finite_tree(child)


def read_document(path):
    raw = path.read_bytes()
    def invalid_constant(value):
        raise ValueError(f'nonfinite JSON number: {value}')
    def native_integer(token):
        number = int(token)
        # nlohmann JSON retains signed/unsigned 64-bit integer tokens and
        # parses wider finite tokens as doubles. This is representation,
        # not an upper bound on resource quantities or open extensions.
        return number if -(1 << 63) <= number < (1 << 64) else float(token)
    document = json.loads(raw.decode('utf-8-sig'), object_pairs_hook=unique_object,
                          parse_constant=invalid_constant, parse_int=native_integer)
    finite_tree(document)
    return document, raw


def members(value, allowed, required, label):
    if not isinstance(value, dict) or not required <= value.keys() or value.keys() - allowed:
        raise ValueError(f'invalid {label} fields')


def contained_path(data_dir, relative):
    if not isinstance(relative, str) or not relative or '\\' in relative:
        raise ValueError('source/output path must be a relative POSIX path')
    path = PurePosixPath(relative)
    if path.is_absolute() or any(part in ('', '.', '..') for part in relative.split('/')):
        raise ValueError(f'source/output path escapes data root: {relative}')
    resolved = (data_dir / relative).resolve()
    if not resolved.is_relative_to(data_dir.resolve()):
        raise ValueError(f'source/output path escapes data root: {relative}')
    return data_dir / relative


def pointer_tokens(pointer):
    if not isinstance(pointer, str) or (pointer and not pointer.startswith('/')):
        raise ValueError('pointer must use RFC 6901')
    if re.search(r'~(?![01])', pointer):
        raise ValueError('invalid RFC 6901 escape')
    return [] if not pointer else [s.replace('~1', '/').replace('~0', '~')
                                   for s in pointer[1:].split('/')]


def pointer_child(value, token):
    if isinstance(value, dict) and token in value:
        return value[token]
    if isinstance(value, list) and re.fullmatch(r'0|[1-9][0-9]*', token):
        index = int(token)
        if index < len(value):
            return value[index]
    raise ValueError(f'RFC 6901 pointer member is absent: {token}')


def project(document, pointer):
    for token in pointer_tokens(pointer):
        document = pointer_child(document, token)
    return document


def replace(document, pointer, value):
    tokens = pointer_tokens(pointer)
    if not tokens:
        return copy.deepcopy(value)
    parent = document
    for token in tokens[:-1]:
        parent = pointer_child(parent, token)
    token = tokens[-1]
    pointer_child(parent, token)  # A projection replaces a declared slot.
    if isinstance(parent, list):
        parent[int(token)] = copy.deepcopy(value)
    else:
        parent[token] = copy.deepcopy(value)
    return document


def type_matches(value, name):
    return {'object': isinstance(value, dict), 'array': isinstance(value, list),
            'string': isinstance(value, str), 'integer': type(value) is int,
            'number': type(value) in (int, float), 'boolean': type(value) is bool,
            'null': value is None}[name]


def exact_equal(a, b):
    if type(a) in (int, float) and type(b) in (int, float):
        return a == b
    if type(a) is not type(b):
        return False
    if isinstance(a, dict):
        return a.keys() == b.keys() and all(exact_equal(a[k], b[k]) for k in a)
    if isinstance(a, list):
        return len(a) == len(b) and all(exact_equal(x, y) for x, y in zip(a, b))
    return a == b


def validate_schema(schema):
    members(schema, ASSERTIONS | ANNOTATIONS, {'type'}, 'value schema')
    names = schema['type'] if isinstance(schema['type'], list) else [schema['type']]
    if not names or any(not isinstance(name, str) or name not in TYPES for name in names):
        raise ValueError('schema needs supported type(s)')
    for key in ANNOTATIONS:
        if key in schema and not isinstance(schema[key], str):
            raise ValueError(f'schema {key} must be a string')
    if 'required' in schema and (not isinstance(schema['required'], list) or
                                any(not isinstance(k, str) for k in schema['required'])):
        raise ValueError('schema required must be an array of names')
    if 'properties' in schema:
        if not isinstance(schema['properties'], dict):
            raise ValueError('schema properties must be an object')
        for child in schema['properties'].values():
            validate_schema(child)
    if 'items' in schema:
        validate_schema(schema['items'])
    if 'additionalProperties' in schema:
        additional = schema['additionalProperties']
        if isinstance(additional, dict):
            validate_schema(additional)
        elif type(additional) is not bool:
            raise ValueError('schema additionalProperties must be boolean or schema')
    for key in ('minimum', 'maximum'):
        if key in schema and type(schema[key]) not in (int, float):
            raise ValueError(f'schema {key} must be a finite number')
    for key in ('minLength', 'minItems'):
        if key in schema and (type(schema[key]) is not int or schema[key] < 0):
            raise ValueError(f'schema {key} must be a nonnegative integer')
    if 'enum' in schema and (not isinstance(schema['enum'], list) or not schema['enum']):
        raise ValueError('schema enum must be a nonempty array')


def validate_value(value, schema):
    names = schema['type'] if isinstance(schema['type'], list) else [schema['type']]
    if not any(type_matches(value, name) for name in names):
        raise ValueError('profile value does not match declared type')
    if 'enum' in schema and not any(exact_equal(value, item) for item in schema['enum']):
        raise ValueError('profile value is outside declared enum')
    if type(value) in (int, float):
        if 'minimum' in schema and value < schema['minimum']:
            raise ValueError('profile value below declared minimum')
        if 'maximum' in schema and value > schema['maximum']:
            raise ValueError('profile value above declared maximum')
    if isinstance(value, str) and len(value) < schema.get('minLength', 0):
        raise ValueError('profile string shorter than declared minimum')
    if isinstance(value, list):
        if len(value) < schema.get('minItems', 0):
            raise ValueError('profile array shorter than declared minimum')
        if 'items' in schema:
            for item in value:
                validate_value(item, schema['items'])
    if isinstance(value, dict):
        if not set(schema.get('required', [])) <= value.keys():
            raise ValueError('profile required field is absent')
        for key, child in value.items():
            properties = schema.get('properties', {})
            additional = schema.get('additionalProperties', True)
            if key in properties:
                validate_value(child, properties[key])
            elif isinstance(additional, dict):
                validate_value(child, additional)
            elif not additional:
                raise ValueError(f'unknown profile setting: {key}')


def validate_definition(document, domain):
    members(document, {'schema', 'domain', 'revision', 'defaults', 'value_schema',
                       'description', 'title'},
            {'schema', 'domain', 'revision', 'defaults', 'value_schema'}, 'profile descriptor')
    if not re.fullmatch(r'[a-z0-9_-]+', domain) or document['domain'] != domain or document['schema'] != 'loom.runtime_profile/1':
        raise ValueError(f'invalid profile identity: {domain}')
    if type(document['revision']) is not int or document['revision'] < 1:
        raise ValueError('profile revision must be a positive integer')
    if not isinstance(document['defaults'], dict):
        raise ValueError('profile defaults must be an object')
    for key in ('description', 'title'):
        if key in document and not isinstance(document[key], str):
            raise ValueError(f'profile {key} must be a string')
    validate_schema(document['value_schema'])
    validate_value(document['defaults'], document['value_schema'])


def source_metadata(path, pointer, raw, target):
    return {'path': 'loom/data/' + path, 'pointer': pointer,
            'raw_sha256': hashlib.sha256(raw).hexdigest(), 'target': target}


def literal_entries(name, documents):
    lines = [f'constexpr std::pair<std::string_view, std::string_view> {name}[] = {{']
    for domain, doc in sorted(documents.items()):
        text = json.dumps(doc, ensure_ascii=False, allow_nan=False, separators=(',', ':'))
        if ')LPROFILE"' in text:
            raise ValueError(f'raw-string delimiter in {domain}')
        lines.append('  {"' + domain + '",')
        # Adjacent literals concatenate; split Unicode characters, never bytes.
        for i in range(0, len(text), 4000):
            lines.append('    R"LPROFILE(' + text[i:i + 4000] + ')LPROFILE"')
        lines[-1] += '},'
    lines.append('};')
    return lines


def plan(data_dir=ROOT / 'data', output=OUT):
    """Read and validate the complete transaction before opening any output."""
    data_dir, output = Path(data_dir), Path(output)
    manifest_path = contained_path(data_dir, MANIFEST)
    manifest, manifest_raw = read_document(manifest_path)
    members(manifest, {'schema', 'description', 'recipes'}, {'schema', 'recipes'}, 'source manifest')
    if manifest['schema'] != 'loom.runtime_profile_sources/1' or not isinstance(manifest['recipes'], list):
        raise ValueError('invalid source manifest schema/recipes')
    if 'description' in manifest and not isinstance(manifest['description'], str):
        raise ValueError('source manifest description must be a string')
    outputs, definitions, provenance = {}, {}, {}
    output_names = []
    for recipe in manifest['recipes']:
        members(recipe, {'output', 'definition', 'projections'}, {'output', 'definition', 'projections'}, 'source recipe')
        path = contained_path(data_dir, recipe['output'])
        if path.parent != data_dir / 'runtime' or path.suffix != '.pack':
            raise ValueError('recipe output must be runtime/<domain>.pack')
        if path.is_symlink():
            raise ValueError('recipe output cannot be a symlink')
        if recipe['output'] in output_names:
            raise ValueError('duplicate recipe output')
        output_names.append(recipe['output'])
    # Inputs must be canonical sources, never generated outputs (no cycles).
    for index, recipe in enumerate(manifest['recipes']):
        path = contained_path(data_dir, recipe['output'])
        if not isinstance(recipe['definition'], dict) or not isinstance(recipe['projections'], list) or not recipe['projections']:
            raise ValueError('recipe requires a definition object and nonempty projections')
        document = copy.deepcopy(recipe['definition'])
        sources = [source_metadata(MANIFEST, f'/recipes/{index}/definition', manifest_raw, '')]
        targets = []
        for projection in recipe['projections']:
            members(projection, {'source', 'pointer', 'target'}, {'source', 'pointer', 'target'}, 'projection')
            source = projection['source']
            source_path = contained_path(data_dir, source)
            generated_paths = [contained_path(data_dir, name) for name in output_names]
            if any(source_path.resolve() == candidate.resolve() or
                   (source_path.exists() and candidate.exists() and source_path.samefile(candidate))
                   for candidate in generated_paths):
                raise ValueError('projection source is a generated output')
            pointer_tokens(projection['target'])
            if any(projection['target'] == target or projection['target'].startswith(target + '/') or
                   target.startswith(projection['target'] + '/') for target in targets):
                raise ValueError('projection targets overlap')
            targets.append(projection['target'])
            source_doc, raw = read_document(source_path)
            document = replace(document, projection['target'], project(source_doc, projection['pointer']))
            sources.append(source_metadata(source, projection['pointer'], raw, projection['target']))
        validate_definition(document, path.stem)
        definitions[path.stem], provenance[path.stem] = document, sources
        outputs[path] = (json.dumps(document, ensure_ascii=False, allow_nan=False, indent=2) + '\n').encode('utf-8')
    for path in sorted((data_dir / 'runtime').glob('*.pack')):
        contained_path(data_dir, path.relative_to(data_dir).as_posix())
        if path.stem in definitions:
            continue  # Generated wrappers are outputs, never authorities.
        document, raw = read_document(path)
        validate_definition(document, path.stem)
        definitions[path.stem] = document
        provenance[path.stem] = [source_metadata(path.relative_to(data_dir).as_posix(), '', raw, '')]
    if not definitions:
        raise ValueError('no runtime profile definitions')
    if output.resolve() in {path.resolve() for path in outputs} or output.resolve().is_relative_to(data_dir.resolve()):
        raise ValueError('embedding output must be outside data root')
    lines = ['// Generated from loom/data/runtime/*.pack and runtime_sources.pack; do not edit.',
             '// Regenerate: python3 loom/src/model/gen_runtime_profiles.py']
    lines += literal_entries('kRuntimeProfiles', definitions)
    lines += literal_entries('kRuntimeProfileSources', provenance)
    outputs[output] = ('\n'.join(lines) + '\n').encode('utf-8')
    return outputs


def apply_plan(outputs, check=False):
    if check:
        stale = [str(path) for path, raw in outputs.items() if not path.is_file() or path.read_bytes() != raw]
        if stale:
            raise ValueError('runtime profile generated outputs are stale: ' + ', '.join(stale))
        return
    # All parsing, projection, schema and literal validation has finished.
    # Stage every output first; replace files only after staging succeeds.
    staged = []
    try:
        for path, raw in outputs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.' + path.name + '.', delete=False) as temporary:
                staged.append((Path(temporary.name), path))
                temporary.write(raw)
            os.chmod(temporary.name, path.stat().st_mode & 0o777 if path.exists() else 0o644)
        for temporary, path in staged:
            os.replace(temporary, path)
    finally:
        for temporary, _ in staged:
            temporary.unlink(missing_ok=True)


def generate(data_dir=ROOT / 'data', output=OUT):
    """Backward-compatible in-memory embedding accessor."""
    return plan(data_dir, output)[Path(output)].decode('utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--data-dir', type=Path, default=ROOT / 'data')
    parser.add_argument('--output', type=Path, default=OUT)
    args = parser.parse_args()
    try:
        apply_plan(plan(args.data_dir, args.output), args.check)
    except (OSError, ValueError, UnicodeError) as error:
        raise SystemExit(str(error)) from error


if __name__ == '__main__':
    main()
