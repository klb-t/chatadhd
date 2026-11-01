"""stdlib AST adapter over immutable UTF-8 bytes; never evaluates source.

Quoted text is observed; declaration kinds and parents are derived syntax,
not runtime facts. Limits and labels are data. Zero disables a limit.
"""
from __future__ import annotations
import ast
import codecs
from copy import deepcopy
import hashlib
import io
import json
from pathlib import Path
import re
import sys
import tokenize

SCHEMA = 'loom.python_ast_basic.result/1'
PARSER = 'loom.python_ast_basic/1'
_LIMITS = ('max_input_bytes', 'max_ast_nodes', 'max_declarations', 'max_quoted_bytes')
_DECLARATIONS = {ast.ClassDef: 'class', ast.FunctionDef: 'function',
                 ast.AsyncFunctionDef: 'async_function'}


def canonical(value):
    """Loom canonical JSON subset used here: UTF-8, sorted keys, no floats."""
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(',', ':'), allow_nan=False)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def _stable(prefix, *parts):
    # model::Unit/Observation::make_id; not a new graph identity scheme.
    return prefix + sha('\x1f'.join(parts).encode('utf-8'))[:16]


class PythonAstError(ValueError):
    def __init__(self, code, locator=None):
        self.code, self.locator = code, locator
        super().__init__(code)

    def to_json(self):
        return {'schema': 'loom.python_ast_basic.error/1', 'error': {
            'code': self.code, 'locator': self.locator}}


def profile(values=None):
    data = (json.loads(Path(__file__).with_name('profile.json').read_text(encoding='utf-8'))
            if values is None else deepcopy(values))
    if not isinstance(data, dict) or data.get('schema') != 'loom.python_ast_basic.profile/1':
        raise PythonAstError('python_ast_configuration_invalid')
    limits = data.get('limits')
    if (not isinstance(limits, dict) or set(limits) != set(_LIMITS) or
            any(type(limits[k]) is not int or limits[k] < 0 for k in _LIMITS)):
        raise PythonAstError('python_ast_configuration_invalid')
    version = data.get('feature_version')
    if version is not None and (type(version) is not int or not 7 <= version <= sys.version_info.minor):
        raise PythonAstError('python_ast_feature_version_unavailable')
    kinds = data.get('kinds')
    if (not isinstance(kinds, dict) or set(kinds) != {'module', *_DECLARATIONS.values()} or
            any(not isinstance(v, str) or not v for v in kinds.values())):
        raise PythonAstError('python_ast_configuration_invalid')
    fmt = data.get('format')
    if (not isinstance(fmt, dict) or fmt.get('level') != 'python_declaration' or
            any(not isinstance(fmt.get(k), str) or not fmt[k] for k in ('mime', 'part_kind', 'reason')) or
            fmt.get('parse') != {'segment': []}):
        raise PythonAstError('python_ast_configuration_invalid')
    return data


def _check(settings, key, value):
    ceiling = settings['limits'][key]
    if ceiling and value > ceiling:
        raise PythonAstError('python_ast_limit_' + key)


def _locator(source, start, end, line=None):
    return {'source': source, 'member': '', 'json_pointer': '',
            'byte_start': start, 'byte_len': end - start,
            'time_start': None, 'time_end': None, 'line': line}


def _lines(raw, bom):
    # splitlines() also splits form-feed/Unicode separators, unlike Python.
    starts = [bom]
    for match in re.finditer(rb'\r\n|\r|\n', raw[bom:]):
        starts.append(bom + match.end())
    return starts


def parse(raw, *, source_key, name, settings=None, expected_sha256=None):
    """Return Source/Unit/Observation JSON without writes or code execution.

    Byte ends are exclusive; line ends inclusive. Decorators start at '@'.
    ast_range separately addresses def/class through the last AST token.
    Trailing comments/newlines belong to the module, not the declaration.
    A module always covers every input byte, including BOM/empty files.
    """
    if not isinstance(raw, bytes):
        raise PythonAstError('python_ast_bytes_required')
    if any(not isinstance(v, str) or not v for v in (source_key, name)):
        raise PythonAstError('python_ast_source_identity_required')
    try:
        source_key.encode('utf-8')
        name.encode('utf-8')
    except UnicodeError:
        raise PythonAstError('python_ast_source_identity_invalid') from None
    settings = profile(settings)
    _check(settings, 'max_input_bytes', len(raw))
    original = sha(raw)
    if expected_sha256 is not None and expected_sha256 != original:
        raise PythonAstError('python_ast_original_hash_mismatch')
    source_id = 'src_text_' + sha(canonical([source_key, original]).encode('utf-8'))
    bom = 3 if raw.startswith(codecs.BOM_UTF8) else 0
    starts = _lines(raw, bom)

    def position(line, column):
        if not 1 <= line <= len(starts) or column < 0:
            raise PythonAstError('python_ast_position_invalid')
        offset = starts[line - 1] + column
        line_end = starts[line] if line < len(starts) else len(raw)
        if offset > line_end:
            raise PythonAstError('python_ast_position_invalid')
        return offset

    try:
        text = raw[bom:].decode('utf-8', errors='strict')
    except UnicodeDecodeError as exc:
        raise PythonAstError('python_ast_invalid_utf8', _locator(
            source_id, bom + exc.start, bom + exc.end)) from None
    try:
        encoding, _ = tokenize.detect_encoding(io.BytesIO(raw).readline)
        if codecs.lookup(encoding).name not in ('utf-8', 'utf-8-sig'):
            raise PythonAstError('python_ast_non_utf8_cookie')
        tree = ast.parse(text, filename='<source>', mode='exec',
                         feature_version=settings['feature_version'])
    except SyntaxError as exc:
        line, loc = exc.lineno, None
        if line is not None and 1 <= line <= len(starts):
            # SyntaxError uses 1-based character columns; AST uses UTF-8 bytes.
            end = starts[line] if line < len(starts) else len(raw)
            value = raw[starts[line - 1]:end].decode('utf-8')
            column = max(0, (exc.offset or 1) - 1)
            offset = position(line, len(value[:column].encode('utf-8')))
            loc = _locator(source_id, offset, offset, line)
        raise PythonAstError('python_ast_syntax_error', loc) from None
    except PythonAstError:
        raise
    except (LookupError, ValueError):
        raise PythonAstError('python_ast_encoding_error') from None
    except (RecursionError, MemoryError):
        raise PythonAstError('python_ast_resource_unavailable') from None

    method = {'id': PARSER, 'python_implementation': sys.implementation.name,
              'python_version': list(sys.version_info[:3]),
              'reader_sha256': sha(Path(__file__).read_bytes()),
              'profile': settings, 'coordinate': 'source_utf8_bytes',
              'structural_evidence_class': 'derived'}
    method_hash = sha(canonical(method).encode('utf-8'))
    declarations, units, observations, provenance = [], [], [], []
    quoted = 0

    def emit(kind, symbol, parent, start, stop, line, end_line, ast_range, ast_column, end_column):
        nonlocal quoted
        _check(settings, 'max_declarations', len(declarations) + 1)
        quoted += stop - start
        _check(settings, 'max_quoted_bytes', quoted)
        value = raw[start:stop].decode('utf-8')
        # Module line=null distinguishes it from a sole one-line declaration.
        locator = _locator(source_id, start, stop, None if kind == 'module' else line)
        uid = _stable('un_', source_id, canonical(locator))
        oid = _stable('ob_', uid, canonical(locator), sha(raw[start:stop]))
        attrs = {'recognized_kind': kind, 'name': symbol, 'parent': parent,
                 'line_range': [line, end_line], 'ast_range': ast_range,
                 'ast_col_offset': ast_column, 'ast_end_col_offset': end_column,
                 'coordinate': 'source_utf8_bytes', 'original_sha256': original,
                 'structural_evidence_class': 'derived', 'method_sha256': method_hash}
        ordinal = len(declarations)
        declarations.append({'kind': kind, 'name': symbol, 'parent': parent,
                             'byte_range': [start, stop], 'line_range': [line, end_line],
                             'ast_range': ast_range, 'unit_id': uid, 'observation_id': oid,
                             'text_sha256': sha(raw[start:stop])})
        units.append({'id': uid, 'source': source_id, 'kind': settings['kinds'][kind],
                      'locator': locator, 'title': symbol, 'date': '', 'artifact_type': 'python',
                      'lang': '', 'bytes': stop - start, 'attrs': deepcopy(attrs)})
        observations.append({'id': oid, 'unit': uid, 'kind': 'code_block', 'text': value,
                             'locator': deepcopy(locator), 'lang': '', 'date': '', 'ordinal': ordinal,
                             'artifact_type': 'python', 'speaker': '',
                             'attrs': {**attrs, 'evidence_class': 'observed'}})
        provenance.append({'subject_id': uid, 'subject_kind': 'unit', 'source_id': source_id,
                           'locator': deepcopy(locator), 'transform': PARSER + ':' + method_hash,
                           'evidence_class': 'derived'})
        return ordinal

    physical_lines = len(starts) - (1 if raw.endswith((b'\n', b'\r')) else 0)
    emit('module', name, None, 0, len(raw), 1, max(1, physical_lines),
         [0, len(raw)], 0, None)
    pending, visited = [(tree, 0)], 0
    while pending:
        node, parent = pending.pop()
        visited += 1
        _check(settings, 'max_ast_nodes', visited)
        kind = _DECLARATIONS.get(type(node))
        if kind is not None:
            start = ast_start = position(node.lineno, node.col_offset)
            stop = position(node.end_lineno, node.end_col_offset)
            first_line = node.lineno
            if node.decorator_list:
                decorator = min(node.decorator_list, key=lambda d: (d.lineno, d.col_offset))
                # Parenthesized decorators can place the first AST expression
                # several lines after '@'. Locate the introducer, not its name.
                first_line = decorator.lineno
                while first_line:
                    end = starts[first_line] if first_line < len(starts) else len(raw)
                    value = raw[starts[first_line - 1]:end]
                    stripped = value.lstrip(b' \t\f')
                    if stripped.startswith(b'@'):
                        start = starts[first_line - 1] + len(value) - len(stripped)
                        break
                    first_line -= 1
                if not first_line:
                    raise PythonAstError('python_ast_decorator_position_invalid')
            parent = emit(kind, node.name, parent, start, stop, first_line,
                          node.end_lineno, [ast_start, stop], node.col_offset, node.end_col_offset)
        pending.extend((child, parent) for child in reversed(list(ast.iter_child_nodes(node))))
    return {'schema': SCHEMA, 'parser': PARSER, 'coordinate': 'source_utf8_bytes',
            'source': {'id': source_id, 'kind': 'file', 'uri': source_key,
                       'title': name, 'blob_hash': original, 'size': len(raw),
                       'mime': settings['format']['mime'], 'format': 'python',
                       'parser': PARSER, 'parser_version': '1'},
            'original_sha256': original, 'method': method, 'method_sha256': method_hash,
            'declarations': declarations, 'units': units, 'observations': observations,
            'provenance': provenance, 'recognition': {'syntax': 'recognized', 'semantics': 'unknown'}}


def validate(result, raw):
    """Reparse before the graph hook; reject even AST labels/parents drift."""
    try:
        source = result['source']
        expected = parse(raw, source_key=source['uri'], name=source['title'],
                         settings=result['method']['profile'], expected_sha256=source['blob_hash'])
        if canonical(result) != canonical(expected):
            raise PythonAstError('python_ast_receipt_mismatch')
    except PythonAstError:
        raise
    except (KeyError, TypeError, ValueError):
        raise PythonAstError('python_ast_receipt_invalid') from None
    return result


def source_parts_request(result, raw):
    """Existing source_parts request plus saved AST receipt; needs integrator hook.

    No native library/model is loaded. Revalidation is mandatory.
    """
    validate(result, raw)
    return {'hash': result['original_sha256'], 'source_key': result['source']['uri'],
            'name': result['source']['title'], 'format': 'python_ast',
            'python_ast_receipt': deepcopy(result),
            'pack': {'source_parts': {'formats': {
                'python_ast': deepcopy(result['method']['profile']['format'])}}}}


def main(argv=None):
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source')
    parser.add_argument('--source-key', required=True)
    parser.add_argument('--name', required=True)
    parser.add_argument('--profile')
    parser.add_argument('--sha256')
    parser.add_argument('--source-parts-request', action='store_true')
    args = parser.parse_args(argv)
    try:
        settings = None if args.profile is None else json.loads(Path(args.profile).read_text(encoding='utf-8'))
        chosen = profile(settings)
        ceiling = chosen['limits']['max_input_bytes']
        with open(args.source, 'rb') as source:
            raw = source.read(ceiling + 1) if ceiling else source.read()
        result = parse(raw, source_key=args.source_key, name=args.name,
                       settings=chosen, expected_sha256=args.sha256)
        if args.source_parts_request:
            result = source_parts_request(result, raw)
        print(canonical(result))
        return 0
    except PythonAstError as exc:
        print(canonical(exc.to_json()))
        return 2
    except (OSError, UnicodeError, ValueError):
        print(canonical(PythonAstError('python_ast_input_unavailable').to_json()))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
