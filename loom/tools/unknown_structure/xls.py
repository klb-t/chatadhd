"""Trusted local XLS value projection; no BIFF reader and no native graph writes."""
from dataclasses import dataclass
import csv
import io
import json
import os
from pathlib import Path
import resource
import selectors
import subprocess
import tempfile
import time

from .contract import canonical, digest

TOOL = '/usr/bin/xls2csv'
SIGNATURE = bytes.fromhex('d0cf11e0a1b11ae1')


@dataclass(frozen=True)
class HostLimits:
    # Independent safety preset for a local converter smoke, unrelated to
    # engineering onboarding targets. Host can replace all values, recipe cannot.
    input_bytes: int = 1024 * 1024
    output_bytes: int = 1024 * 1024
    diagnostic_bytes: int = 65536
    wall_seconds: float = 20
    structural_elements: int = 10000
    result_bytes: int = 8 * 1024 * 1024
    cpu_seconds: int = 5
    address_bytes: int = 128 * 1024 * 1024


def _pages(text):
    """Split converter sheet markers only outside CSV quoted fields.

    -q 3 quotes every value. A literal formfeed inside a quoted value stays data.
    A marker embedded mid-record is ambiguous and refused, never fabricated.
    """
    start, quoted, i = 0, False, 0
    while i < len(text):
        c = text[i]
        if c == '"':
            if quoted and i + 1 < len(text) and text[i + 1] == '"':
                i += 2
                continue
            quoted = not quoted
        elif c == '\f' and not quoted:
            if i > start and text[i - 1] not in '\r\n':
                raise ValueError('ambiguous_sheet_boundary')
            yield text[start:i]
            start = i + 1
        i += 1
    if quoted:
        raise ValueError('unterminated_quoted_cell')
    if start < len(text):
        yield text[start:]


def projection(stdout, structural_elements=10000):
    sheets = []
    elements = 0
    for page in _pages(stdout.decode('utf-8', errors='strict')):
        rows = []
        elements += 1
        for row in csv.reader(io.StringIO(page, newline=''), strict=True):
            elements += 1 + len(row)
            if elements > structural_elements:
                raise ValueError('host_structural_output_bound')
            rows.append(row)
        if elements > structural_elements:
            raise ValueError('host_structural_output_bound')
        sheets.append({'ordinal': len(sheets), 'name': None, 'rows': rows})
    if not sheets:
        raise ValueError('no_usable_sheet_output')
    document = {'sheets': sheets}
    blocks, text = [], bytearray()
    for sheet in sheets:
        for rowno, row in enumerate(sheet['rows']):
            for colno, value in enumerate(row):
                pointer = f"/sheets/{sheet['ordinal']}/rows/{rowno}/{colno}"
                start = len(text)
                text.extend(value.encode('utf-8'))
                end = len(text)
                text.extend(b'\n')
                blocks.append({'kind': 'field', 'text': value, 'json_pointer': pointer,
                               'text_range': [start, end], 'range': [start, end + 1],
                               'evidence_class': 'derived', 'confidence': None,
                               'original_address': None})
    return document, bytes(text), blocks


def range_binding(document, sheet, rectangle):
    if (type(sheet) is not int or not 0 <= sheet < len(document['sheets']) or
            not isinstance(rectangle, list) or len(rectangle) != 4 or
            any(type(x) is not int for x in rectangle)):
        raise ValueError('output_rectangle_invalid')
    top, left, bottom, right = rectangle
    rows = document['sheets'][sheet]['rows']
    if not (0 <= top < bottom <= len(rows) and 0 <= left < right):
        raise ValueError('output_rectangle_invalid')
    if any(right > len(rows[r]) for r in range(top, bottom)):
        raise ValueError('output_rectangle_ragged')
    return {'kind': 'range', 'locator': {'json_pointer': f'/sheets/{sheet}'},
            'attrs': {'coordinate': 'derived_output_matrix', 'output_rectangle': rectangle,
                      'cell_pointers': [f'/sheets/{sheet}/rows/{r}/{c}'
                                        for r in range(top, bottom) for c in range(left, right)],
                      'evidence_class': 'derived', 'confidence': None, 'original_address': None}}


def _convert(raw, limits):
    def bounds():
        resource.setrlimit(resource.RLIMIT_CPU, (limits.cpu_seconds, limits.cpu_seconds))
        resource.setrlimit(resource.RLIMIT_AS, (limits.address_bytes, limits.address_bytes))
        resource.setrlimit(resource.RLIMIT_FSIZE, (limits.output_bytes, limits.output_bytes))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    with tempfile.TemporaryDirectory(prefix='loom-xls-') as directory:
        source = Path(directory) / 'input.xls'
        source.write_bytes(raw)
        env = {'HOME': directory, 'LC_ALL': 'C', 'PATH': '/usr/bin:/bin'}
        with subprocess.Popen([TOOL, '-d', 'utf-8', '-q', '3', str(source)],
                              stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, cwd=directory, env=env,
                              preexec_fn=bounds) as process:
            chunks = {'stdout': bytearray(), 'stderr': bytearray()}
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ, 'stdout')
                selector.register(process.stderr, selectors.EVENT_READ, 'stderr')
                deadline = time.monotonic() + limits.wall_seconds
                try:
                    while selector.get_map():
                        remaining = deadline - time.monotonic()
                        if remaining <= 0:
                            raise ValueError('host_converter_time_bound')
                        for key, _ in selector.select(min(remaining, 0.1)):
                            chunk = os.read(key.fileobj.fileno(), 8192)
                            if not chunk:
                                selector.unregister(key.fileobj)
                                continue
                            cap = limits.output_bytes if key.data == 'stdout' else limits.diagnostic_bytes
                            if len(chunks[key.data]) + len(chunk) > cap:
                                raise ValueError('host_converter_output_bound')
                            chunks[key.data].extend(chunk)
                    code = process.wait(timeout=max(0.001, deadline - time.monotonic()))
                except BaseException:
                    process.kill()  # only this owned child, on independent host bounds
                    process.wait()
                    raise
            return code, bytes(chunks['stdout']), bytes(chunks['stderr'])


def run(raw, compiled, limits=HostLimits()):
    plan = compiled.plan()
    if plan['provider'] != 'xls2csv.local/1':
        raise ValueError('wrong_trusted_adapter')
    original = {'sha256': digest(raw), 'bytes': len(raw), 'retention': 'caller_input_unchanged_existing_blobstore_required'}
    base = {'schema': 'loom.unknown_structure.result/1', 'original': original,
            'descriptor_sha256': compiled.sha256, 'confidence': None,
            'native_materialized': False}
    if not raw.startswith(SIGNATURE):
        return dict(base, status='retained_original', evidence_class='observed',
                    gaps=['unknown_signature_use_existing_import_ledger'])
    if len(raw) > limits.input_bytes:
        return dict(base, status='gap', evidence_class='observed', gaps=['host_input_bound'])
    try:
        before = digest(Path(TOOL).read_bytes())
        code, stdout, stderr = _convert(raw, limits)
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        return dict(base, status='gap', evidence_class='observed',
                    gaps=['trusted_converter_unavailable_or_host_bound', type(error).__name__])
    if digest(Path(TOOL).read_bytes()) != before:
        raise ValueError('converter_changed_during_execution')
    receipt = {'tool_sha256': before, 'tool': 'xls2csv', 'argv_profile': 'utf8_quote_all/1',
               'returncode': code, 'stdout_sha256': digest(stdout), 'stderr_sha256': digest(stderr)}
    if code != 0 or not stdout:
        return dict(base, status='gap', evidence_class='observed', tool_receipt=receipt,
                    stdout_hex=stdout.hex(), stderr_hex=stderr.hex(), gaps=['converter_no_usable_output'])
    try:
        document, text, blocks = projection(stdout, limits.structural_elements)
    except (ValueError, csv.Error, UnicodeError) as error:
        return dict(base, status='gap', evidence_class='observed', tool_receipt=receipt,
                    stdout_hex=stdout.hex(), stderr_hex=stderr.hex(), gaps=[str(error)])
    result = dict(base, status='partial', evidence_class='derived', tool_receipt=receipt,
                document=document, json_sha256=digest(canonical(document)), text=text.decode('utf-8'),
                text_sha256=digest(text), blocks=blocks, stdout_hex=stdout.hex(), stderr_hex=stderr.hex(),
                gaps=plan['gaps'] + (['converter_diagnostic_present'] if stderr else []))
    if len(canonical(result)) > limits.result_bytes:
        return dict(base, status='gap', evidence_class='observed', tool_receipt=receipt,
                    stdout_hex=stdout.hex(), stderr_hex=stderr.hex(), gaps=['host_result_output_bound'])
    return result


def main():
    import argparse
    from .contract import compile_descriptor
    parser = argparse.ArgumentParser()
    parser.add_argument('input')
    args = parser.parse_args()
    root = Path(__file__).parent
    compiled = compile_descriptor(json.loads((root / 'legacy_xls.json').read_text()),
                                  json.loads((root / 'catalog.json').read_text()))
    # Bound read before allocation; original remains at caller's path.
    with open(args.input, 'rb') as stream:
        raw = stream.read(HostLimits().input_bytes + 1)
    if len(raw) > HostLimits().input_bytes:
        raise ValueError('host_input_bound_original_file_untouched')
    print(json.dumps(run(raw, compiled), ensure_ascii=False, sort_keys=True))


if __name__ == '__main__':
    main()
