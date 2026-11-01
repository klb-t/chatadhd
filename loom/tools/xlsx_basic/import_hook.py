"""Thin syntax-registry hook and a pure plan for the shared native source pipeline.

This module never assigns native IDs, writes a DB, or implements another genesis.
The integrator binds these drafts using the existing model factories/transaction.
"""
import hashlib
import json

from .container import check, fail
from .reader import read, settings


def parse_for_import(data, options):
    """Signature of resource_graph.parsers.register_parser; no format guessing."""
    return read(data, limits=options.get('xlsx_limits'), sheet=options.get('xlsx_sheet'),
                selected_range=options.get('xlsx_range'),
                expected_sha256=options.get('xlsx_original_sha256'))


def register_with_resource_graph():
    """Explicit extension point; registration collisions keep existing handlers."""
    from ..resource_graph.parsers import register_parser
    register_parser('xlsx', parse_for_import, descriptor={
        'version': 'loom.xlsx_basic/1', 'semantics': 'spreadsheet_syntax_only'})


def prepare_source_parts(data, *, expected_sha256, source_key, name,
                         sheet, selected_range, limits=None):
    """Reproducible Unit/Observation drafts with derived UTF-8 byte coordinates.

Original locators live in attrs.xlsx, because native Locator has no sheet/range
ABI fields. Persist the original first and the UTF-8 as a separate derived blob.
Use model::Unit::make_id / Observation::make_id after source IDs are assigned.
Pass derived text into existing genesis::compute_chain/materialize exactly once
in the source transaction; this plan is not a materialization receipt.
"""
    if not all(isinstance(v, str) and v for v in
               (expected_sha256, source_key, name, sheet, selected_range)):
        fail('xlsx_import_identity_required')
    policy = settings(limits)
    result = read(data, limits=policy, sheet=sheet, selected_range=selected_range,
                  expected_sha256=expected_sha256)
    chunks, units, unavailable, offset = [], [], [], 0
    for cell in result['sheets'][0]['cells']:
        attrs = {'xlsx': cell['locator'], 'status': cell['status'], 'cell_kind': cell['kind'],
                 'evidence': 'derived', 'method': result['extractor']}
        unit = {'kind': 'cell', 'title': cell['ref'], 'attrs': attrs}
        if cell['status'] == 'observed':
            raw = cell['text'].encode('utf-8')
            unit['locator'] = {'byte_start': offset, 'byte_len': len(raw)}
            unit['observation'] = {'kind': 'field', 'text': cell['text'],
                                   'ordinal': len(units), 'attrs': attrs}
            chunks.append(raw + b'\n')
            offset += len(raw) + 1
        else:
            # No fabricated text/value/genesis fragment for a missing/formula cell.
            if 'code' in cell:
                attrs['code'] = cell['code']
            unit['locator'] = None
            unavailable.append({'ref': cell['ref'], 'attrs': attrs})
            continue
        units.append(unit)
    derived = b''.join(chunks)
    derived_hash = hashlib.sha256(derived).hexdigest()
    for unit in units:
        if unit['locator'] is not None:
            unit['locator']['source'] = 'sha256:' + derived_hash
            unit['observation']['locator'] = dict(unit['locator'])
    plan = {'schema': 'loom.xlsx_basic.source_plan/1',
            'original': result['source'],
            'source_key': source_key, 'name': name, 'selection': result['selection'],
            'derived': {'sha256': derived_hash, 'size': len(derived), 'format': 'text',
                        'text': derived.decode('utf-8')},
            'method': {'extractor': result['extractor'], 'limits': policy},
            'units': units, 'unavailable_cells': unavailable,
            'warnings': result['warnings'], 'partial': True}
    check(policy, 'max_output_bytes', len(json.dumps(plan, ensure_ascii=False).encode('utf-8')))
    return plan
