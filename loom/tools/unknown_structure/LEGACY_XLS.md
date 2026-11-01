# Karta legacy XLS → istniejący graf

Opracowanie: 2026-11-01; baza 4a82c0cbf543c14412b01e7491938384551b4c7a.
To Excel 97–2003 BIFF8, nie XLSX. Spec: MS-XLS 12.2 (2025-08-19),
https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-xls/cd03cb5f-ca02-4934-a391-bb674cb8aa06 .
Narzędzie już obecne: /usr/bin/xls2csv, Catdoc 0.95, -d utf-8 -q 3.
Dokumentacja autora: https://www.wagner.pp.ru/~vitus/software/catdoc/ .

Hierarchia formatu: workbook → sheet → row → cell; zakresy prostokątne,
scalenia, nazwy, formuły i style są dodatkowymi strukturami BIFF. xls2csv daje
projekcję wartości i granice arkuszy formfeed, bez nazw arkuszy, formuł,
scalonych zakresów i wiarygodnego oryginalnego adresowania BIFF. W pierwszej
recepturze zakres oznacza zakres wynikowej macierzy, nie potwierdzony zakres A1.
Formfeed wewnątrz cytowanej wartości musi pozostać wartością, nie granicą.

Minimalna wykonywalna recepta adaptera zaufanego: snapshot oryginału →
/usr/bin/xls2csv -d utf-8 -q 3 SNAPSHOT → zachowany stdout i stderr →
stdlib csv.reader → kanoniczny JSON workbook/sheets/rows/cells → UTF-8 wartości
z zakresami bajtowymi. Uruchomienie bez shell, w izolowanym pustym HOME;
recepta nie może określać argv, środowiska, sieci, limitów ani uprawnień.
Sygnatura OLE jest tylko kandydatem, nie dowodem XLS; sukces konwersji jest
częściowym dowodem kompatybilności. Makr nie uruchamiamy.

Mapowanie do kontraktów obecnych w bazie:
- Source oryginału: istniejący BlobStore/ProvenanceStore, hash wszystkich bajtów.
- Osobne Source pochodnych: exact stdout, kanoniczny JSON i UTF-8. Każda ma
  provenance do oryginału, hash narzędzia, deskryptora i receipt konwersji.
- Unit(kind otwarty): workbook/sheet/row/cell/range; locator JSON pointer do
  pochodnego JSON albo byte_start/byte_len do pochodnego UTF-8. Nigdy offset
  JSON/UTF-8 udający pozycję w oryginalnym OLE.
- Observation(kind field): dokładna wartość komórki, evidence_class derived,
  confidence null, attrs: sheet ordinal, output row/column, original_address null.
  Istnienie bajtów/sygnatury observed; semantyka kolumn bez dowodu nie inferred.
- Segmenter istniejący: dalszy podział tekstu; strukturalne granice komórek
  przekazane jak recognized_block w DOCX, bez nowego Segmentera/SourceID.
- genesis: istniejące compute_chain/materialize i wersje/fingerprint; powiązać
  occurrence z Unit i provenance jak src/text/source_parts.cpp. Nie kopiować ledger.
- Zakres prostokątny: Unit z JSON pointer do /sheets/N, attrs output_rectangle
  i lista cell pointers. Zaznaczenie zakresu nie twierdzi istnienia BIFF MERGECELLS.

Punkty integracji dla sole integratora: FormatRegistry::add (wybór kandydata,
nie walidacja), zaufany reader w structure_readers/runtime profile analogicznie
DOCX; src/text/source_parts.cpp: materialize oraz validate_occurrence/read;
model::Unit::make_id i Observation::make_id; genesis::materialize;
ProvenanceStore. tools/resource_graph/MappingAdapter jest istniejącym providerem
projekcji, a tools/python_ast_basic pokazuje pinning parsera. Nie ufamy client JSON
jako poświadczeniu wykonania. Brak structure_readers w tym sparse checkout:
konkretny hook wymaga rebase/audytu integratora. Nie zmieniono DB/HTTP/UI.

Syntetyczny oczekiwany graf: raw XLS(hash A) ←derived_from— JSON(hash B)
←derived_from— UTF8(hash C); Unit workbook → sheet ordinal0 → row0 → cell0
(value „A”, locator /sheets/0/rows/0/0, Observation field derived), cell1 („2”);
Unit range output rectangle [0,0,1,2] wiąże oba cell pointers. Strzałki są
projektem powiązań do istniejącego materializera, nie nowymi enum relacjami.
Unknown signature → retained_original + gap, istniejący ImportLedger::keep.

Luki: brak weryfikacji oryginalnych współrzędnych/nazw/formuł/dat/locale,
wykresów/obiektów/makr/hasła; nie claimujemy pełnej zgodności BIFF8.
PDF/MRI: następny provider tego samego kontraktu ma podać spec/tool/hash,
warstwy/locators/mapping i jawne gaps. Wolumen/region MRI nie oznacza segmentacji
ani diagnozy. Nie powielamy adapterów autorów PDF ani MRI.
