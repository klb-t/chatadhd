# W12 — źródła, surowe wyniki i odtwarzanie drugiego przyrostu

Pełny CTest **125/125 PASS**, 306,35 s; wykonane native **775/29387**,
W12 **65/2988**, Python unittest **1340/0 skip**. Opt-in catalog scale: 0
przypadków w domyślnym trybie, zachowane ustawienie. UI **28/28**, build85,
generator **18/18**, rzeczywisty parytet i migracja starej bazy **15/15**: PASS.

Źródło produktu: `6adbf2ad00671da40a356b922a8c1a8091b1e5ea`.
Końcowy pin pełnego buildu/testów: `5115477fda183572acb901cfd2ea98979fb0f45d`.
Później zmieniły się tylko dwa pliki testów: jawne stringi w dwóch nowych
asercjach doctest oraz dwa fixture’y aktualizacji grafu, z twardej rewizji 2
na bieżącą + 1. Pin 6ea0772 jest pośrednim dowodem pierwszej poprawki, nie
końcową bramką. Wszystkie asercje pozostały; src/include/data i core są niezmienione. BEFORE: rzeczywisty, czysty pierwszy W12
`3c0bc36552ef9851f1174946cfb108549aae3228`. Surowe manifesty zachowują pin
obowiązujący w chwili danego przebiegu, bez przepisywania go na późniejszy commit.

[verification.zip](verification.zip) zawiera komplet surowych śladów, manifestów,
syntetycznych SQLite, narzędzi i zachowanych negatywów. [archive.json](archive.json)
podaje SHA256 ZIP i wewnętrznego manifestu. Po rozpakowaniu każdy plik jest
sprawdzalny według `ARCHIVE_MANIFEST.json`; manifest nie obejmuje sam siebie.
W archiwum nie ma executable, obiektów kompilacji ani cache Pythona.
Dane wejściowe są syntetyczne. Wywołania modeli/dostawców: 0.
Pole prepared_requests=21 w oryginalnym receipt oznacza 21 wyników przygotowania:
18 sukcesów i 3 oczekiwane odmowy/błędy, identyczne BEFORE/AFTER. Nie jest liczbą
21 udanych przygotowań. 120 wyników oceny polityki jest osobnym mianownikiem.

| Katalog w ZIP | Dowód i granica |
|---|---|
| `native-after/` | prawdziwy build, pełny CTest/LastTest, wykonywane przypadki, final source closure i błąd kompilacji nowego testu przed poprawką |
| `native-before/` | przypięty czysty source i build rzeczywistego starego kernelu; nie przenosi historycznego CTest na nową bazę |
| `source-parity/` | ten sam C++ probe z obiema bibliotekami, pełne raw traces, ścisły comparator, oryginalna baza starego użytkownika i konsument na kopii |
| `independent-presentation/` | niezależne helpery i źródła/negatywy; bez ReactDOM ani werdyktu native |
| `ui-development/` | poprawione błędy rekursji i malformed bootstrap, źródła reproducerów, końcowe 28 testów/build85 i wcześniejsze przebiegi |
| `root-point-checks/` | generator18, UI28, web85 oraz zachowana próba z niewłaściwym starym generatorem |

Odtwarzanie rachunku nie wymaga modeli, sieci ani prawdziwych danych użytkownika:

```sh
unzip verification.zip -d /tmp/w12-evidence
python3 /tmp/w12-evidence/native-after/derive_test_counts.py --folder /tmp/w12-evidence/native-after
python3 -B /tmp/w12-evidence/source-parity/compare.py --before /tmp/w12-evidence/source-parity/before/trace.json --after /tmp/w12-evidence/source-parity/after-final-pack3/trace.json --output /tmp/w12-comparison.json
python3 -B -m unittest discover -s /tmp/w12-evidence/source-parity -p test_compare.py -v
```

Do odtworzenia samych aplikacji/probe trzeba checkoutów przypiętych powyżej oraz
rzeczywistych bibliotek zbudowanych według zapisanych configuration manifests.
Szczegółowe polecenia i dozwolone różnice opisuje `source-parity/README.md` w ZIP.
Przy ponownym uruchamianiu probe na końcowym checkout5115477 użyj
`--production-revision 5115477fda183572acb901cfd2ea98979fb0f45d`; historyczne
polecenia w ZIP wskazują faktyczny pin6ad z chwili oryginalnego wykonania.
Produkcyjne src/include/data są identyczne między tymi pinami.
Same hashe biblioteki nie dowodzą jej kompilacji ze wskazanego źródła: dlatego
archiwum zawiera również raw build log, manifest wejść i closure po testach.

## Zachowane negatywy

Błąd strict build w dwóch nowych CHECK nie został zatajony: `std::string == Json`
nie przechodził dekompozycji doctest. Poprawka `.get<std::string>()` nie zmienia
kryterium równości; osobny strict object build i późniejszy pełny build są zachowane.
Focused CTest przed korektą fixture’ów: 7/8 PASS; pack 3 → 2 poprawnie dawał
Conflict. Korekta przywraca forward upgrade, aby faktycznie sprawdzić wykluczenie
i brak vocabulary; failed graph entry po poprawce ma 11/11 i 2172 asercje PASS.
Surowy negatyw, poprawka, recompile i rerun są zachowane.
Przerwania builderów exit130 były celowe: odzyskanie zasobów/przydział okna probe.
Globalne zdarzenia OOM innych procesów nie są przypisywane temu buildowi;
własny błąd kompilacji ma pełny log i nie był OOM.

Początkowy runner generatora miał nowy test z niewłaściwą starą wersją generatora:
18 testów / 19 failure. Zachowano drugi pełny negatyw `generator-tests.log`; pierwszy
pełny raw log nie został zapisany. Ponowne kopiowanie bajtów i kontrola hashy
przywróciły właściwy plik; nie zmieniono progów ani asercji. Mechanizm przywrócenia
starego pliku po wcześniejszym copy z mtime jest podejrzeniem, nie ustalonym faktem.

Niezależny helper początkowo błędnie zakładał 150 wpisów zamiast faktycznych 153.
Negatyw i poprawiony runner zachowano. Reprodukcje produktu wykazały RangeError
przy błędnym szablonie diagnostyki oraz TypeError przy błędnej strukturze osadzonego
bootstrapu. Oba błędy poprawiono i objęto regresjami; manualne obserwacje nie są
sumowane z przypadkami frameworka. Comparator początkowo nie odrzucał dwóch
adwersarialnych modyfikacji; po poprawieniu przeszedł kontrolę jednostkową i
niezależne pięć mutacji. Na rzeczywistych raw traces początkowo wymagał korekty
ścieżki wrappera effective_privacy i kształtu DTO assessment/basis/support.
Negatywy zachowano; kompletność pól, tożsamości i źródeł nadal jest wymagana.
