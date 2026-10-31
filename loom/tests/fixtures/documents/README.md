Authored mechanism fixtures, no user documents or model output.

- unicode-compressed.pdf: two text pages, Flate-compressed content and ToUnicode mapping, Polish diacritics. Expected first page: `Zażółć gęślą jaźń — PDF PAPUGA. Strona 1.`; second: `Druga strona: Łódź, żółw, ąćęłńóśźż.`
- mixed-text-blank.pdf: the same two pages, plus a blank third page. This tests reporting pages without readable text; it is not an OCR test.
- no-text.pdf: blank page, no text layer.
- password.pdf: the first fixture encrypted with pypdf, password `mechanism-test-only` (test data only).

The first two files were authored as PDF 1.4 objects with Python stdlib zlib; pypdf 5.1.0, already available on the development VM, authored the blank/encrypted variants. Neither Python nor pypdf is a runtime dependency of the reader. No external PDF was redistributed. These fixtures test mechanism behavior and do not establish accuracy on owner documents, scanned pages, tables or visual layout.
