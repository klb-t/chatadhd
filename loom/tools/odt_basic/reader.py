"""Bounded ODT content.xml reader. No network, subprocess, extraction or writes.

XML paths use zero-based *element child* indices from the document root ([]).
Projection byte ranges are half-open UTF-8 ranges, never offsets into ZIP/XML.
Repeated rows/cells are represented once, with observed repetition metadata.
"""

import hashlib
import json
import zipfile
import struct
import zlib
from dataclasses import dataclass, field
from xml.parsers import expat

from .container import OdtError, check, fail, read_parts, settings

OFFICE = 'urn:oasis:names:tc:opendocument:xmlns:office:1.0'
TEXT = 'urn:oasis:names:tc:opendocument:xmlns:text:1.0'
TABLE = 'urn:oasis:names:tc:opendocument:xmlns:table:1.0'
MIME = 'application/vnd.oasis.opendocument.text'
EXTRACTOR = 'loom.odt_basic/1'


def q(namespace, local):
    return '{' + namespace + '}' + local


def sha(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'))


def checked_json_size(value, limits):
    """Measure incrementally; do not build a potentially over-budget JSON string."""
    size = 0
    encoder = json.JSONEncoder(ensure_ascii=False, sort_keys=True, separators=(',', ':'))
    for chunk in encoder.iterencode(value):
        size += len(chunk.encode('utf-8'))
        check(limits, 'max_output_bytes', size)
    return size


@dataclass
class Element:
    name: str
    attrs: dict
    path: tuple
    children: list = field(default_factory=list)
    pieces: list = field(default_factory=list)


def parse_xml(data, limits, warnings):
    parser = expat.ParserCreate(namespace_separator='}')
    stack, root = [], None
    nodes = 0

    def expanded(name):
        return '{' + name if '}' in name else name

    def start(name, attrs):
        nonlocal root, nodes
        nodes += 1
        check(limits, 'max_xml_nodes', nodes)
        check(limits, 'max_xml_depth', len(stack) + 1)
        path = stack[-1].path + (len(stack[-1].children),) if stack else ()
        node = Element(expanded(name), {expanded(k): v for k, v in attrs.items()}, path)
        if stack:
            stack[-1].children.append(node)
            stack[-1].pieces.append(node)
        else:
            root = node
        stack.append(node)

    def characters(text):
        if stack:
            stack[-1].pieces.append(text)

    def forbidden(*_args):
        fail('odt_unsafe_xml')

    parser.StartElementHandler = start
    parser.EndElementHandler = lambda _name: stack.pop()
    parser.CharacterDataHandler = characters
    parser.StartDoctypeDeclHandler = forbidden
    parser.EntityDeclHandler = forbidden
    parser.ExternalEntityRefHandler = forbidden
    parser.SkippedEntityHandler = forbidden
    parser.ProcessingInstructionHandler = lambda *_: warnings.add('odt_processing_instructions_ignored')
    parser.SetParamEntityParsing(expat.XML_PARAM_ENTITY_PARSING_NEVER)
    try:
        parser.Parse(data, True)
    except expat.ExpatError:
        fail('odt_invalid_xml')
    return root


def positive(node, name, default=1):
    raw = node.attrs.get(name)
    if raw is None:
        return default
    if not raw or any(c not in '0123456789' for c in raw):
        fail('odt_invalid_structure')
    try:
        value = int(raw)
    except ValueError:
        fail('odt_invalid_structure')
    if value < 1:
        fail('odt_invalid_structure')
    return value


class Projection:
    def __init__(self, limits, original, member_hash, warnings):
        self.limits, self.original, self.member_hash = limits, original, member_hash
        self.warnings = warnings
        self.blocks, self.chunks, self.size = [], [], 0
        self.logical_cells = 0

    def append(self, text, count=1):
        # Check multiplication before allocating e.g. text:s text:c="1000000000".
        width = len(text.encode('utf-8')) * count
        check(self.limits, 'max_projection_bytes', self.size + width)
        self.chunks.append(text * count)
        self.size += width

    def begin(self, node, kind, parent, **extra):
        check(self.limits, 'max_units', len(self.blocks) + 1)
        index = len(self.blocks)
        block = {
            'kind': kind, 'parent': parent, 'byte_range': [self.size, self.size],
            'raw_locator': {'source': 'sha256:' + self.original, 'member': 'content.xml',
                            'member_sha256': self.member_hash, 'xml_path': list(node.path)},
            'observed': {'element': node.name, 'attributes': dict(node.attrs)},
            'evidence_class': 'derived', **extra,
        }
        self.blocks.append(block)
        return index

    def finish(self, index):
        self.blocks[index]['byte_range'][1] = self.size

    def inline(self, node):
        for piece in node.pieces:
            if isinstance(piece, str):
                self.append(piece)
            elif piece.name in (q(TEXT, 'span'), q(TEXT, 'a')):
                if piece.name == q(TEXT, 'a'):
                    self.warnings.add('odt_link_targets_not_followed')
                self.inline(piece)
            elif piece.name == q(TEXT, 's'):
                self.append(' ', positive(piece, q(TEXT, 'c')))
            elif piece.name == q(TEXT, 'tab'):
                self.append('\t')
            elif piece.name == q(TEXT, 'line-break'):
                self.append('\n')
            else:
                # Do not flatten annotations, notes, fields, deleted text, images
                # or unknown XML into an observed quotation of body text.
                self.warnings.add('odt_unsupported_inline_omitted')

    def block_nodes(self, container):
        for node in container.children:
            if node.name in (q(TEXT, 'p'), q(TEXT, 'h'), q(TABLE, 'table')):
                yield node
            elif node.name in (q(TEXT, 'section'), q(TEXT, 'list'), q(TEXT, 'list-item'),
                               q(TEXT, 'list-header')):
                self.warnings.add('odt_container_layout_omitted')
                yield from self.block_nodes(node)
            else:
                self.warnings.add('odt_unsupported_block_omitted')

    def render_container(self, node, parent):
        for ordinal, child in enumerate(self.block_nodes(node)):
            if ordinal:
                self.append('\n')
            self.render(child, parent)

    def rows(self, table):
        for node in table.children:
            if node.name == q(TABLE, 'table-row'):
                yield node
            elif node.name in (q(TABLE, 'table-header-rows'), q(TABLE, 'table-rows'),
                               q(TABLE, 'table-row-group')):
                yield from self.rows(node)
            else:
                self.warnings.add('odt_table_layout_omitted')

    def render(self, node, parent):
        if node.name in (q(TEXT, 'p'), q(TEXT, 'h')):
            kind = 'heading' if node.name == q(TEXT, 'h') else 'paragraph'
            extra = {}
            if kind == 'heading':
                extra['level'] = positive(node, q(TEXT, 'outline-level'), None)
                if extra['level'] is None:
                    self.warnings.add('odt_heading_level_unspecified')
            index = self.begin(node, kind, parent, **extra)
            self.inline(node)
            self.finish(index)
            return
        table_index = self.begin(node, 'table', parent)
        logical_row = 0
        for ri, row in enumerate(self.rows(node)):
            if ri:
                self.append('\n')
            row_repeat = positive(row, q(TABLE, 'number-rows-repeated'))
            if row_repeat != 1:
                self.warnings.add('odt_table_repetitions_not_expanded')
            row_index = self.begin(row, 'table_row', table_index,
                                   logical_row=logical_row, repeat=row_repeat)
            column, cell_count = 0, 0
            for cell in row.children:
                if cell.name not in (q(TABLE, 'table-cell'), q(TABLE, 'covered-table-cell')):
                    self.warnings.add('odt_unsupported_table_markup_omitted')
                    continue
                repeat = positive(cell, q(TABLE, 'number-columns-repeated'))
                self.logical_cells += repeat * row_repeat
                check(self.limits, 'max_table_cells', self.logical_cells)
                if repeat != 1:
                    self.warnings.add('odt_table_repetitions_not_expanded')
                if cell_count:
                    self.append('\t')
                covered = cell.name == q(TABLE, 'covered-table-cell')
                index = self.begin(cell, 'table_cell', row_index, logical_column=column,
                                   repeat=repeat, covered=covered)
                if not covered:
                    self.render_container(cell, index)
                elif cell.children or any(isinstance(p, str) and p.strip() for p in cell.pieces):
                    fail('odt_invalid_structure')
                self.finish(index)
                column += repeat
                cell_count += 1
            logical_row += row_repeat
            self.finish(row_index)
        self.finish(table_index)


def extract(data, limits):
    limits = settings(limits)
    if not isinstance(data, bytes):
        fail('odt_configuration_invalid')
    check(limits, 'max_input_bytes', len(data))
    try:
        parts, entries = read_parts(data, limits)
        if parts.get('mimetype') != MIME.encode('ascii') or 'content.xml' not in parts:
            fail('odt_invalid_package')
        warnings = {'odt_formatting_pagination_and_auxiliary_parts_omitted'}
        root = parse_xml(parts['content.xml'], limits, warnings)
        if root is None or root.name != q(OFFICE, 'document-content'):
            fail('odt_invalid_structure')
        bodies = [n for n in root.children if n.name == q(OFFICE, 'body')]
        if len(bodies) != 1 or len(bodies[0].children) != 1 or bodies[0].children[0].name != q(OFFICE, 'text'):
            fail('odt_invalid_structure')
        original, member_hash = sha(data), sha(parts['content.xml'])
        projection = Projection(limits, original, member_hash, warnings)
        projection.render_container(bodies[0].children[0], None)
        text = ''.join(projection.chunks)
        utf8 = text.encode('utf-8')
        result = {
            'schema': 'loom.odt_basic.result/1', 'extractor': EXTRACTOR,
            'source': {'sha256': original, 'size': len(data), 'member': 'content.xml',
                       'member_sha256': member_hash, 'zip_entries': entries},
            'text': text, 'blocks': projection.blocks,
            'projection': {'schema': 'loom.odt_basic.projection/1',
                           'coordinate': 'derived_utf8_bytes', 'sha256': sha(utf8), 'size': len(utf8)},
            'evidence': {'structure': 'observed', 'text': 'derived', 'inferred': []},
            'partial': True, 'warnings': sorted(warnings), 'llm_calls': 0,
        }
        checked_json_size(result, limits)
        return result
    except OdtError:
        raise
    except RecursionError:
        fail('odt_parser_depth_unsupported')
    except (zipfile.BadZipFile, zipfile.LargeZipFile, struct.error, zlib.error,
            EOFError, UnicodeError, ValueError, NotImplementedError, RuntimeError):
        fail('odt_invalid_zip')


def read_file(path, limits):
    limits = settings(limits)
    with open(path, 'rb') as stream:
        cap = limits['max_input_bytes']
        data = stream.read(cap + 1) if cap else stream.read()
    return extract(data, limits)
