# Unknown format discovery contract + legacy XLS provider

Lokalny moduł stdlib, bez instalacji. Baza odbioru:
`4a82c0cbf543c14412b01e7491938384551b4c7a`.

```
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s loom/tests/unknown_structure -p 'test_*.py' -v
PYTHONDONTWRITEBYTECODE=1 python3 -m loom.tools.unknown_structure.xls /local/input.xls
```

`catalog.json` to zaufane metadane providerów kontrolowane przez host/integratora;
nie wolno przyjmować zamiennika katalogu od dokumentu/klienta. `legacy_xls.json`
to walidowane dane: publiczny format → primary spec/version/hash → provider →
warstwy → mapowanie na istniejący model → jawne luki. Kompilacja zamyka zestaw
kluczy i porównuje mapping do zatwierdzonego providera; wynik jest kanonicznym,
niemutowalnym planem. Dowolna zmiana przy tej samej wersji to konflikt;
zmiana wersji zostawia wcześniejszą interpretację. Resume sprawdza bajty wyniku,
oryginału i hash deskryptora. Nie tworzymy osobnego rejestru trwałości.

Publiczne URL wybierane wyłącznie przez znany identyfikator formatu. Moduł sam
nie wykonuje zapytań HTTP. `spec_evidence` sprawdza przypięty hash snapshotu i
bibliograficzne markery; zmiana treści jest konfliktem do ponownego przeglądu,
nie instrukcją do wykonania. HTML, skrypty i makra nie są wykonywane. Hashe
stron i datę dostępu utrwalono jako metadane źródeł w katalogu i deskryptorze.
Wpisy DOCX/AST wskazują istniejące implementacje, nie ich nową implementację;
AST ma jawnie nieodebrany native hook. Nie certyfikujemy nim specyfikacji DOCX/AST.

`xls.run` używa stałego zaufanego `xls2csv`; pola recepty nie określają shell,
argv, środowiska, uprawnień, privacy lub limitów. Konwerter ma pusty prywatny
HOME i stdin, lokalny snapshot, oddzielnie uzasadnione zasobowe presety hosta.
To nie sandbox syscall/network: przed podłączeniem produktu integrator musi
użyć istniejącego host-side reader confinement. Narzędzie czyta systemową
konfigurację/charsety catdoc; przypięcie binarki samo nie przypina tych zależności.
Ich snapshot/hash to jawny następny krok integracji dla odtwarzalności konwersji.
Presety nie wynikają z 180/600 s opracowania formatu. Moduł nie wywołuje
płatnych API, modeli, zewnętrznych konwerterów ani pobranego kodu.

`retained_original` znaczy decyzję przekazania oryginału do istniejącego
ImportLedger/BlobStore. Ten moduł pozostawia wejście bez zmian i podaje hash;
nie poświadcza zapisu oryginału do DB. `native_materialized` jest zawsze false.
Przy błędnej sygnaturze/braku narzędzia/pustym lub niepoprawnym wyjściu dostajemy
result z gap. Sygnatura OLE jest kandydatem także dla formatów innych niż XLS.
CSV stdout/diagnostyka zachowane są dokładnie w hex, z osobnymi hashami.
Pochodne JSON i tekst UTF-8 mają oddzielne hashe. Formfeed w cytowanej wartości
nie rozcina arkusza. Puste arkusze wynikowe zachowujemy, o ile narzędzie je emituje;
nie wnioskujemy z tego liczby wszystkich arkuszy oryginału. Daty, typy, nazwy,
formuły i mapowanie pozycji BIFF pozostają niezweryfikowane.

`bindings.native_arguments` generuje argumenty istniejących konstruktorów,
nie drugi graf ani SourceID. Weryfikuje replay z zachowanego stdout, tekst,
JSON, zakresy i receipt. Host nadaje identyfikatory przez Unit::make_id oraz
Observation::make_id, tworzy SourceRecord/ProvenanceRecord, wiąże jednostki
strukturalne JSON z jednostkami tekstowymi UTF-8 i occurrence/genesis.
`unit_pointer` jest odwołaniem do strukturalnego rodzeństwa; pole Observation.unit
ma otrzymać ID właściwego text_unit, bo Observation.locator adresuje tekst.
Relacje rodzic/dziecko i role sources w planie są argumentami do istniejącego
materializera, nie nową zamkniętą ontologią relacji.

`expected_bindings.json` to deterministyczny fixture oczekiwanych argumentów dla
lokalnie przypiętej binarki catdoc 0.95. Fixture generator pisze 5632 bajty CFB
z BIFF8 i komórkami A,2; nie zawiera czytnika BIFF. Test ma także niezależne
asercje wartości, hierarchii, liczby Unit i zakresów. Rzeczywisty stderr:
`Format code 0 is used before definition`. Status partial zachowuje ten brak.
Nie badano prawdziwego korpusu XLS ani GUI/native. To test mechanizmu, nie metryka
pokrycia wszystkich XLS. Output matrix nie jest oryginalnym adresem A1.

PDF/MRI next: provider wpisuje public spec/tool metadata, poziomy struktury,
mapowanie istniejących locatorów oraz evidence/gaps w ten kontrakt. Nie dokładamy
kolejnych readerów ani nie przejmujemy pracy aktywnych autorów. DICOM/NIfTI
wymagają własnego sprawdzonego providera: struktura wolumenu nie jest segmentacją.

Produkt określa tylko zamknięta lista w PRODUCT_MANIFEST.json. CHECKPOINT,
HTML snapshoty, PUBLIC_READ_EVIDENCE, TOOL_SMOKE, log testów i sam patch są
materiałem przekazania, nie plikami produktu. Nie stosować całego katalogu wildcardem.
Integrator ma ręcznie sprawdzić/rebase do własnego nowszego HEAD; brak zmian refs.
