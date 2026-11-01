"""Synthetic OPC packages only. Fixed ZIP metadata makes hashes reproducible."""
import io
import zipfile

S = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
R = 'http://schemas.openxmlformats.org/package/2006/relationships'
O = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'
C = 'http://schemas.openxmlformats.org/package/2006/content-types'


def parts():
    return {
        '[Content_Types].xml': f'''<Types xmlns="{C}">
          <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
          <Override PartName="/xl/book.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
          <Override PartName="/xl/worksheets/table.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
          <Override PartName="/xl/worksheets/empty.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
          <Override PartName="/xl/strings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>
          </Types>''',
        '_rels/.rels': f'<Relationships xmlns="{R}"><Relationship Id="root" Type="{O}/officeDocument" Target="xl/book.xml"/></Relationships>',
        'xl/book.xml': f'''<workbook xmlns="{S}" xmlns:r="{O}"><sheets>
          <sheet name="Zażółć" sheetId="4" r:id="table"/>
          <sheet name="Pusty" sheetId="8" state="hidden" r:id="empty"/>
          </sheets></workbook>''',
        'xl/_rels/book.xml.rels': f'''<Relationships xmlns="{R}">
          <Relationship Id="empty" Type="{O}/worksheet" Target="worksheets/empty.xml"/>
          <Relationship Id="table" Type="{O}/worksheet" Target="worksheets/table.xml"/>
          <Relationship Id="strings" Type="{O}/sharedStrings" Target="strings.xml"/>
          </Relationships>''',
        'xl/strings.xml': f'''<sst xmlns="{S}" count="2" uniqueCount="2">
          <si><t>Nazwa</t></si><si><r><rPr><b/></rPr><t>żół</t></r><r><t>w</t></r><rPh sb="0" eb="1"><t>ignored</t></rPh></si></sst>''',
        'xl/worksheets/table.xml': f'''<worksheet xmlns="{S}">
          <dimension ref="A1:XFD1048576"/><sheetData>
          <row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" t="inlineStr"><is><t>Ilość</t></is></c></row>
          <row r="2"><c r="A2" t="s"><v>1</v></c><c r="B2" s="1"><v>9007199254740993</v></c><c r="C2" t="b"><v>1</v></c></row>
          <row r="3"><c r="A3" t="inlineStr"><is><r><t xml:space="preserve"> a </t></r><r><t>&amp;\nβ</t></r></is></c><c r="B3"/></row>
          <row r="5"><c r="A5"><f>HYPERLINK("https://invalid.example/", "never")</f><v>123456789</v></c></row>
          </sheetData></worksheet>''',
        'xl/worksheets/empty.xml': f'<worksheet xmlns="{S}"><sheetData/></worksheet>',
    }


def package(values=None, *, extra=(), compression=zipfile.ZIP_DEFLATED):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w') as archive:
        for name, value in list(sorted((values if values is not None else parts()).items())) + list(extra):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = compression
            archive.writestr(info, value.encode('utf-8') if isinstance(value, str) else value)
    return stream.getvalue()
