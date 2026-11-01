"""Basic OPC spreadsheet syntax. No evaluation, external access or source writes.

Numbers are lexical strings, not floats. Styles/date serials are not interpreted.
All coordinates address worksheet XML; none purport to be raw ZIP byte offsets.
"""
import copy
import hashlib
import json
import posixpath
import re
import urllib.parse
import zipfile
import zlib
from pathlib import Path

from .container import XlsxError, XmlBudget, check, fail, read_parts, safe_name

S = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
SS = 'http://purl.oclc.org/ooxml/spreadsheetml/main'
R = 'http://schemas.openxmlformats.org/package/2006/relationships'
CT = 'http://schemas.openxmlformats.org/package/2006/content-types'
OFFICE = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships/'
STRICT_OFFICE = 'http://purl.oclc.org/ooxml/officeDocument/relationships/'
MAIN = 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml'
SHEET = 'application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml'
STRINGS = 'application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml'
LIMIT_KEYS = ('max_input_bytes', 'max_total_uncompressed_bytes', 'max_entry_bytes',
              'max_entries', 'max_ratio', 'max_xml_depth', 'max_xml_nodes',
              'max_text_chars', 'max_sheets', 'max_cells', 'max_range_cells',
              'max_output_bytes')
NUMBER = re.compile(r'[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?\Z')
CELL = re.compile(r'\$?([A-Z]{1,3})\$?([1-9][0-9]{0,6})\Z')


def settings(values=None):
    if values is None:
        values = json.loads(Path(__file__).with_name('preset.json').read_text())
    if not isinstance(values, dict) or values.get('schema') != 'loom.xlsx_basic.limits/1':
        fail('xlsx_configuration_invalid')
    for key in LIMIT_KEYS + ('read_chunk_bytes',):
        if type(values.get(key)) is not int or values[key] < 0:
            fail('xlsx_configuration_invalid')
    if not values['read_chunk_bytes']:
        fail('xlsx_configuration_invalid')
    return dict(values)


def address(value):
    m = CELL.fullmatch(value) if isinstance(value, str) else None
    if m is None:
        fail('xlsx_invalid_cell')
    column = 0
    for c in m[1]:
        column = column * 26 + ord(c) - ord('A') + 1
    row = int(m[2])
    # SpreadsheetML A1 grid bounds, not a deployment resource policy.
    if column > 16384 or row > 1048576:
        fail('xlsx_invalid_cell')
    return m[1] + m[2], row, column


def column_name(column):
    out = ''
    while column:
        column, remainder = divmod(column - 1, 26)
        out = chr(65 + remainder) + out
    return out


def extent(value):
    if not isinstance(value, str) or value.count(':') > 1:
        fail('xlsx_invalid_range')
    try:
        parts = value.split(':')
        first, last = address(parts[0]), address(parts[-1])
    except XlsxError:
        fail('xlsx_invalid_range')
    if first[1] > last[1] or first[2] > last[2]:
        fail('xlsx_invalid_range')
    return first, last


def _single(node, tag, required=False):
    found = node.findall(tag)
    if len(found) > 1 or (required and not found):
        fail('xlsx_invalid_structure')
    return found[0] if found else None


def _ns(root, name):
    for ns in (S, SS):
        if root.tag == '{' + ns + '}' + name:
            return '{' + ns + '}'
    fail('xlsx_namespace_unsupported')


def _rich(node, q):
    """Visible plain/rich text, excluding phonetic guides and run properties."""
    texts = []
    plain = node.findall(q + 't')
    if len(plain) > 1 or (plain and node.findall(q + 'r')):
        fail('xlsx_invalid_structure')
    for child in node:
        if child.tag == q + 't':
            if len(child):
                fail('xlsx_invalid_structure')
            texts.append(child.text or '')
        elif child.tag == q + 'r':
            _single(child, q + 't', True)
            for item in child:
                if item.tag == q + 't':
                    if len(item):
                        fail('xlsx_invalid_structure')
                    texts.append(item.text or '')
                elif item.tag != q + 'rPr':
                    fail('xlsx_string_markup_unsupported')
        elif child.tag not in (q + 'rPh', q + 'phoneticPr'):
            fail('xlsx_string_markup_unsupported')
    return ''.join(texts)


class _Workbook:
    def __init__(self, data, limits, expected_sha256):
        if not isinstance(data, bytes):
            fail('xlsx_bytes_required')
        self.limits = settings(limits)
        check(self.limits, 'max_input_bytes', len(data))
        self.hash = hashlib.sha256(data).hexdigest()
        if expected_sha256 is not None and expected_sha256 != self.hash:
            fail('xlsx_original_hash_mismatch')
        self.parts = read_parts(data, self.limits)
        budget = XmlBudget(self.limits)
        # Parse every XML/relationship part so hidden DTDs cannot be overlooked.
        self.roots = {name: budget.parse(raw) for name, raw in self.parts.items()
                      if name.endswith(('.xml', '.rels')) or name == '[Content_Types].xml'}
        self.warnings = {'xlsx_styles_not_applied', 'xlsx_non_cell_features_not_projected'}
        self.text_chars = self.cell_count = 0
        self.types = self._types()
        self.links = self._relationships()
        mains = self._targets('', 'officeDocument')
        if len(mains) != 1 or self.types.get(mains[0]) != MAIN:
            fail('xlsx_workbook_type_unsupported')
        main = mains[0]
        workbook = self._root(main)
        q = _ns(workbook, 'workbook')
        sheets = _single(workbook, q + 'sheets', True)
        strings = self._targets(main, 'sharedStrings')
        if len(strings) > 1:
            fail('xlsx_invalid_shared_strings')
        self.strings, self.strings_member = [], strings[0] if strings else None
        if strings:
            if self.types.get(strings[0]) != STRINGS:
                fail('xlsx_invalid_shared_strings')
            sst = self._root(strings[0])
            sq = _ns(sst, 'sst')
            for si in sst:
                if si.tag != sq + 'si':
                    fail('xlsx_invalid_shared_strings')
                self.strings.append(self._text(_rich(si, sq)))
        self.sheets = []
        names, ids, members = set(), set(), set()
        for sheet in sheets:
            check(self.limits, 'max_sheets', len(self.sheets) + 1)
            if sheet.tag != q + 'sheet':
                fail('xlsx_sheet_type_unsupported')
            name, sid = sheet.get('name', ''), sheet.get('sheetId', '')
            rids = [sheet.get('{' + ns + '}id') for ns in (OFFICE[:-1], STRICT_OFFICE[:-1])]
            rids = [rid for rid in rids if rid]
            if (not name or not re.fullmatch(r'[1-9][0-9]*', sid) or len(rids) != 1
                    or name.casefold() in names or sid in ids):
                fail('xlsx_invalid_sheet')
            link = self.links.get(main, {}).get(rids[0])
            if not link or not self._kind(link[0], 'worksheet'):
                fail('xlsx_sheet_type_unsupported')
            member = link[1]
            if member in members or self.types.get(member) != SHEET:
                fail('xlsx_invalid_sheet')
            names.add(name.casefold()); ids.add(sid); members.add(member)
            descriptor = {'name': name, 'sheet_id': sid, 'member': member,
                          'state': sheet.get('state', 'visible')}
            descriptor['cells'] = self._cells(descriptor)
            self.sheets.append(descriptor)
        self.source = {'sha256': self.hash, 'size': len(data), 'format': 'xlsx'}
        if not self.sheets:
            fail('xlsx_invalid_sheet')

    def _root(self, name):
        if name not in self.roots:
            fail('xlsx_missing_xml_part')
        return self.roots[name]

    def _text(self, value):
        self.text_chars += len(value)
        check(self.limits, 'max_text_chars', self.text_chars)
        return value

    def _types(self):
        root = self._root('[Content_Types].xml')
        if root.tag != '{' + CT + '}Types':
            fail('xlsx_invalid_content_types')
        overrides, defaults = {}, {}
        for child in root:
            content = child.get('ContentType', '')
            if not content:
                fail('xlsx_invalid_content_types')
            if any(token in content.lower() for token in ('macroenabled', 'vba', 'activex')):
                fail('xlsx_active_content_unsupported')
            if child.tag == '{' + CT + '}Override':
                name = child.get('PartName', '')
                if not name.startswith('/'):
                    fail('xlsx_invalid_content_types')
                name = name[1:]
                safe_name(name)
                if name in overrides or name not in self.parts:
                    fail('xlsx_invalid_content_types')
                overrides[name] = content
            elif child.tag == '{' + CT + '}Default':
                ext = child.get('Extension', '')
                if not ext or ext in defaults:
                    fail('xlsx_invalid_content_types')
                defaults[ext] = content
            else:
                fail('xlsx_invalid_content_types')
        return {name: overrides.get(name, defaults.get(name.rsplit('.', 1)[-1]))
                for name in self.parts}

    @staticmethod
    def _kind(kind, tail):
        return kind in (OFFICE + tail, STRICT_OFFICE + tail)

    def _targets(self, owner, tail):
        return [target for kind, target in self.links.get(owner, {}).values() if self._kind(kind, tail)]

    def _relationships(self):
        links = {}
        for name, root in self.roots.items():
            if not name.endswith('.rels'):
                continue
            if root.tag != '{' + R + '}Relationships':
                fail('xlsx_invalid_relationships')
            if name == '_rels/.rels':
                base, owner = '', ''
            else:
                parent, filename = posixpath.split(name)
                if posixpath.basename(parent) != '_rels':
                    fail('xlsx_invalid_relationships')
                base = posixpath.dirname(parent)
                owner = posixpath.join(base, filename[:-5])
                if owner not in self.parts:
                    fail('xlsx_invalid_relationships')
            linked = links.setdefault(owner, {})
            for rel in root:
                rid, kind, target = (rel.get(k, '') for k in ('Id', 'Type', 'Target'))
                if (rel.tag != '{' + R + '}Relationship' or not rid or not kind
                        or not target or rid in linked):
                    fail('xlsx_invalid_relationships')
                if rel.get('TargetMode', 'Internal') != 'Internal':
                    fail('xlsx_external_relationship_unsupported')
                if any(token in kind.lower() for token in ('vba', 'activex', 'oleobject')):
                    fail('xlsx_active_content_unsupported')
                parsed = urllib.parse.urlsplit(target)
                decoded = urllib.parse.unquote(parsed.path, errors='strict')
                if (parsed.scheme or parsed.netloc or parsed.query or parsed.fragment
                        or '\\' in decoded or any(ord(c) < 32 for c in decoded)):
                    fail('xlsx_unsafe_relationship')
                resolved = posixpath.normpath(decoded[1:] if decoded.startswith('/')
                                             else posixpath.join(base, decoded))
                safe_name(resolved)
                if resolved not in self.parts:
                    fail('xlsx_missing_part')
                linked[rid] = (kind, resolved)
        return links

    def locator(self, sheet, ref, xml_path=None):
        loc = {'original_sha256': self.hash, 'sheet': sheet['name'],
               'sheet_id': sheet['sheet_id'], 'member': sheet['member'], 'range': ref}
        if xml_path is not None:
            loc['xml_path'] = xml_path
        return loc

    def _cells(self, sheet):
        root = self._root(sheet['member'])
        q = _ns(root, 'worksheet')
        data = _single(root, q + 'sheetData', True)
        cells, rows = {}, set()
        for row in data:
            raw_row = row.get('r', '')
            if row.tag != q + 'row' or not re.fullmatch(r'[1-9][0-9]{0,6}', raw_row):
                fail('xlsx_invalid_row')
            row_number = int(raw_row)
            if row_number > 1048576 or row_number in rows:
                fail('xlsx_invalid_row')
            rows.add(row_number)
            for cell in row:
                if cell.tag != q + 'c':
                    fail('xlsx_cell_markup_unsupported')
                ref, ri, ci = address(cell.get('r'))
                if ri != row_number or ref in cells:
                    fail('xlsx_duplicate_or_misplaced_cell')
                self.cell_count += 1
                check(self.limits, 'max_cells', self.cell_count)
                loc = self.locator(sheet, ref, "worksheet/sheetData/row[@r='" + raw_row + "']/c[@r='" + ref + "']")
                item = {'ref': ref, 'row': ri, 'column': ci, 'locator': loc,
                        'status': 'observed', 'kind': cell.get('t', 'n'), 'value': None, 'text': None}
                if 's' in cell.attrib:
                    item['style_index'] = cell.get('s')
                formula = _single(cell, q + 'f')
                v = _single(cell, q + 'v')
                inline = _single(cell, q + 'is')
                if any(node is not None and len(node) for node in (formula, v)):
                    fail('xlsx_invalid_cell_value')
                if formula is not None:
                    item.update(status='unsupported', code='xlsx_formula_unsupported',
                                formula=self._text(formula.text or ''), formula_attrs=dict(formula.attrib))
                    self.warnings.add('xlsx_formula_unsupported')
                elif any(child.tag not in (q + 'v', q + 'is') for child in cell):
                    item.update(status='unsupported', code='xlsx_cell_markup_unsupported')
                elif item['kind'] == 'inlineStr':
                    if inline is None or v is not None:
                        fail('xlsx_invalid_inline_string')
                    item['value'] = item['text'] = self._text(_rich(inline, q))
                elif inline is not None:
                    fail('xlsx_invalid_cell_value')
                elif item['kind'] == 's':
                    raw = v.text if v is not None else None
                    if not raw or not re.fullmatch(r'[0-9]+', raw) or len(raw) > 10:
                        fail('xlsx_invalid_shared_string_index')
                    index = int(raw)
                    if index >= len(self.strings):
                        fail('xlsx_invalid_shared_string_index')
                    item['value'] = item['text'] = self._text(self.strings[index])
                    item['string_origin'] = {'original_sha256': self.hash,
                                             'member': self.strings_member, 'index': index}
                elif item['kind'] in ('n', 'b', 'd'):
                    raw = v.text if v is not None else None
                    if raw is None and item['kind'] == 'n':
                        item.update(kind='blank', value='', text='')
                    elif raw is None or (item['kind'] == 'n' and not NUMBER.fullmatch(raw)):
                        fail('xlsx_invalid_cell_value')
                    elif item['kind'] == 'b' and raw not in ('0', '1'):
                        fail('xlsx_invalid_cell_value')
                    else:
                        item['text'] = self._text(raw)
                        item['value'] = (raw == '1') if item['kind'] == 'b' else raw
                else:
                    item.update(status='unsupported', code='xlsx_cell_type_unsupported')
                if item['status'] == 'unsupported':
                    self.warnings.add(item['code'])
                cells[ref] = item
        return sorted(cells.values(), key=lambda c: (c['row'], c['column']))

    def result(self, sheet, selected_range):
        result = {'schema': 'loom.xlsx_basic.result/1', 'extractor': 'loom.xlsx_basic/1',
                  'source': self.source, 'partial': True, 'warnings': sorted(self.warnings)}
        if sheet is None:
            if selected_range is not None:
                fail('xlsx_sheet_required')
            result['sheets'] = self.sheets
        else:
            found = [s for s in self.sheets if s['name'] == sheet]
            if not found:
                fail('xlsx_sheet_not_found')
            selected = found[0]
            if selected_range is None:
                result['sheets'] = [selected]
            else:
                first, last = extent(selected_range)
                count = (last[1] - first[1] + 1) * (last[2] - first[2] + 1)
                check(self.limits, 'max_range_cells', count)
                by_ref = {c['ref']: c for c in selected['cells']}
                cells = []
                for ri in range(first[1], last[1] + 1):
                    for ci in range(first[2], last[2] + 1):
                        ref = column_name(ci) + str(ri)
                        cells.append(by_ref.get(ref, {'ref': ref, 'row': ri, 'column': ci,
                                     'status': 'absent', 'kind': 'absent', 'value': None,
                                     'text': None, 'locator': self.locator(selected, ref)}))
                canonical = first[0] if first == last else first[0] + ':' + last[0]
                result['selection'] = self.locator(selected, canonical)
                result['sheets'] = [{**selected, 'cells': cells}]
        check(self.limits, 'max_output_bytes', len(json.dumps(result, ensure_ascii=False).encode('utf-8')))
        return copy.deepcopy(result)


def read(data, *, limits=None, sheet=None, selected_range=None, expected_sha256=None):
    """Read bytes or a named sheet/range; explicit absent cells are never invented."""
    try:
        return _Workbook(data, limits, expected_sha256).result(sheet, selected_range)
    except XlsxError:
        raise
    except (zipfile.BadZipFile, zlib.error, ValueError, UnicodeError, OverflowError, EOFError):
        fail('xlsx_invalid_container')


def read_cell(data, sheet, cell, **options):
    ref, _, _ = address(cell)
    return read(data, sheet=sheet, selected_range=ref, **options)


def read_range(data, sheet, selected_range, **options):
    return read(data, sheet=sheet, selected_range=selected_range, **options)
