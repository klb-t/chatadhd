"""Small synthetic OOXML packages; no user/private documents."""
import io
import zipfile

W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
R = 'http://schemas.openxmlformats.org/package/2006/relationships'
CT = 'http://schemas.openxmlformats.org/package/2006/content-types'
MAIN_TYPE = 'application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml'


def p(text):
    return '<w:p><w:r><w:t>' + text + '</w:t></w:r></w:p>'


BODY = ('<w:p><w:pPr><w:pStyle w:val="Tytul"/></w:pPr><w:r><w:t>Zażółć gęślą jaźń</w:t></w:r></w:p>'
        + p('Pierwszy akapit: Łódź &amp; żółw.')
        + '<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr>'
          '<w:r><w:t>Lista: ąćęłńóśźż</w:t></w:r></w:p>'
        + '<w:tbl><w:tblPr/><w:tblGrid><w:gridCol w:w="2400"/><w:gridCol w:w="2400"/></w:tblGrid><w:tr><w:tc>' + p('Kolumna A') + '</w:tc><w:tc>' + p('Kolumna B') + '</w:tc></w:tr>'
          '<w:tr><w:tc>' + p('jeden') + '</w:tc><w:tc>' + p('dwa') + '</w:tc></w:tr></w:tbl>'
        + p('Koniec — po tabeli.'))
EXPECTED = ('# Zażółć gęślą jaźń\nPierwszy akapit: Łódź & żółw.\n- Lista: ąćęłńóśźż\n'
            'Kolumna A\tKolumna B\njeden\tdwa\nKoniec — po tabeli.')
STYLES = ('<w:styles xmlns:w="' + W + '"><w:style w:type="paragraph" w:styleId="Tytul">'
          '<w:pPr><w:outlineLvl w:val="0"/></w:pPr></w:style></w:styles>')


def parts(body=BODY):
    return {
        '[Content_Types].xml': '<Types xmlns="' + CT + '">'
          '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
          '<Default Extension="xml" ContentType="application/xml"/>'
          '<Default Extension="bin" ContentType="application/octet-stream"/>'
          '<Override PartName="/word/document.xml" ContentType="' + MAIN_TYPE + '"/>'
          '<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>'
          '<Override PartName="/word/numbering.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"/></Types>',
        '_rels/.rels': '<Relationships xmlns="' + R + '"><Relationship Id="r1" '
          'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" '
          'Target="word/document.xml"/></Relationships>',
        'word/document.xml': '<w:document xmlns:w="' + W + '"><w:body>' + body + '</w:body></w:document>',
        'word/styles.xml': STYLES,
        'word/numbering.xml': '<w:numbering xmlns:w="' + W + '"><w:abstractNum w:abstractNumId="0">'
          '<w:multiLevelType w:val="singleLevel"/><w:lvl w:ilvl="0"><w:start w:val="1"/>'
          '<w:numFmt w:val="bullet"/><w:lvlText w:val="•"/></w:lvl></w:abstractNum>'
          '<w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num></w:numbering>',
        'word/_rels/document.xml.rels': '<Relationships xmlns="' + R + '"><Relationship Id="s1" '
          'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" '
          'Target="styles.xml"/><Relationship Id="n1" '
          'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering" '
          'Target="numbering.xml"/></Relationships>',
    }


def package(entries=None, compression=zipfile.ZIP_DEFLATED):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=compression) as z:
        for name, value in (entries if entries is not None else parts()).items():
            info = zipfile.ZipInfo(name, (2026, 11, 1, 0, 0, 0))
            info.compress_type = compression
            z.writestr(info, value)
    return stream.getvalue()
