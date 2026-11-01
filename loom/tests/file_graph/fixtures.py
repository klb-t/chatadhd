"""One small reproducible application batch. No parser execution or user data."""
from pathlib import Path
import importlib.util,io,zipfile,json,hashlib

def module(path):
    s=importlib.util.spec_from_file_location('fixture',path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m

def archive(entries):
    out=io.BytesIO()
    with zipfile.ZipFile(out,'w') as z:
        for name,data in entries:
            info=zipfile.ZipInfo(name,(2026,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=(0o40755 if name.endswith('/') else 0o100644)<<16;z.writestr(info,data)
    return out.getvalue()

def pdf():
    # Deterministic text-layer PDF: Type0/Identity-H + explicit UTF16 ToUnicode.
    # Glyph appearance uses a system substitute; the fixture tests text/structure,
    # not font fidelity. No script/actions/external references.
    texts=[['Rozdział 1','Zażółć gęślą jaźń. PDF_WYBRANY.','Drugi akapit strony pierwszej.'],['Rozdział 2','Łódź i żółw. Druga strona.']]
    chars=sorted(set(''.join(sum(texts,[]))));cmap='/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n/CMapName /Synthetic def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n'+str(len(chars))+' beginbfchar\n'+''.join(f'<{ord(c):04X}> <{ord(c):04X}>\n' for c in chars)+'endbfchar\nendcmap\nCMapName currentdict /CMap defineresource pop\nend\nend'
    stream=lambda data:b'<< /Length '+str(len(data)).encode()+b' >>\nstream\n'+data+b'\nendstream'
    objects=[b'<< /Type /Catalog /Pages 2 0 R >>',b'<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>']
    for i in range(2):objects.append(f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /Font << /F1 5 0 R >> >> /Contents {8+i} 0 R >>'.encode())
    objects += [b'<< /Type /Font /Subtype /Type0 /BaseFont /DejaVuSans /Encoding /Identity-H /DescendantFonts [6 0 R] /ToUnicode 7 0 R >>',b'<< /Type /Font /Subtype /CIDFontType2 /BaseFont /DejaVuSans /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /DW 600 >>',stream(cmap.encode())]
    for lines in texts:
        commands='BT /F1 18 Tf 50 740 Td '+(' Tj 0 -50 Td '.join('<'+x.encode('utf-16-be').hex()+'>' for x in lines))+' Tj ET'
        objects.append(stream(commands.encode()))
    out=bytearray(b'%PDF-1.4\n');offsets=[0]
    for i,obj in enumerate(objects,1):offsets.append(len(out));out.extend(f'{i} 0 obj\n'.encode()+obj+b'\nendobj\n')
    x=len(out);out.extend(f'xref\n0 {len(objects)+1}\n0000000000 65535 f \n'.encode())
    for offset in offsets[1:]:out.extend(f'{offset:010} 00000 n \n'.encode())
    out.extend(f'trailer << /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{x}\n%%EOF\n'.encode());return bytes(out)

def create(destination):
    destination.mkdir(mode=0o700);tests=Path(__file__).resolve().parents[1]
    x=module(tests/'xlsx_basic/fixtures.py');parts=x.parts();parts['xl/book.xml']=parts['xl/book.xml'].replace('Pusty','Drugi')
    parts['xl/worksheets/empty.xml']=f'<worksheet xmlns="{x.S}"><sheetData><row r="2"><c r="B2" t="inlineStr"><is><t>Żółw WYBRANY</t></is></c></row><row r="7"><c r="D7" t="inlineStr"><is><t>INNY_ZAKRES</t></is></c></row></sheetData></worksheet>'
    book=x.package(parts);doc=pdf();epub=module(tests/'epub_basic/fixtures.py').archive()
    odt=archive([('mimetype',b'application/vnd.oasis.opendocument.text'),('content.xml',(tests/'odt_basic/fixtures/content.xml').read_bytes())])
    entries={'document.pdf':doc,'workbook.xlsx':book,'simple.zip':archive([('folder/',b''),('folder/notatka.txt','Żółć — plik w folderze.'.encode())]),'documents.zip':archive([('pakiet/',b''),('pakiet/document.pdf',doc),('pakiet/workbook.xlsx',book)]),'document.odt':odt,'publication.epub':epub,'unsafe.zip':archive([('../outside',b'no')])}
    for name,data in entries.items():(destination/name).write_bytes(data)
    (destination/'manifest.json').write_text(json.dumps({name:hashlib.sha256(data).hexdigest() for name,data in entries.items()},indent=2)+'\n')
if __name__=='__main__':
    import sys;create(Path(sys.argv[1]))
