# Wątek 11 — R42: ścisły strażnik literałów produktu, 2026-10-05

Narzędzie jest odczytową inwentaryzacją leksykalną i bramką opartą na jawnych,
dokładnych wpisach przeglądu. Przechodzi tylko wtedy, gdy każdy rozpoznany literał
ma aktualny wpis z jedną z sześciu kategorii R42 i uzasadnieniem, a skan nie zgłosił
żadnej nieobsługiwanej formy. Pozytywne przypadki syntetyczne nie oznaczają odbioru
R42 dla repozytorium. Pełny wynik repo zostanie zachowany jako wynik negatywny.

Źródłem wymogu jest commit `3cd5848e7484d50d00f19f0855bc2cd228ad7f3b`,
`docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md:483–515`.
[Pełny wycinek](evidence/product-literal-guard-2026-10-05/R42-primary-excerpt.md)
i [tożsamość źródła](evidence/product-literal-guard-2026-10-05/R42-primary-excerpt-receipt.json)
pozostają w dowodach. Zmiany tego przyrostu obejmują wyłącznie nowe pliki w zakresie
wątku 11; nie zmieniają tekstów interfejsu, kodu innych właścicieli ani CI.

## Mechanizm i dane

`loom/src/util/product_literal_guard.py` odczytuje rejestr, zakresy, reguły
przypisania do wątków, jawne wykluczenia testów/fixtures, teksty diagnostyczne i
allowlistę z `loom/data/validation/product_literals.pack`. Kontrakt opisuje
`docs/contracts/product_literals.schema.json`. CLI akceptuje tylko schemat
bajtowo identyczny z kanonicznym schematem dostarczonym obok narzędzia. Duplikaty
kluczy JSON, nieznane pola, puste uzasadnienia, nielegalne kategorie, nieaktualne
kotwice, niejednoznaczne rejestracje i referencje do zewnętrznych schematów
blokują sprawdzian. Nie ma trybu pozwalającego automatycznie uznać dawny dług.

Wpis przeglądu wiąże względną ścieżkę, SHA-256 całego pliku, początek i koniec
zakresu bajtów UTF-8, SHA-256 dokładnego literału, kategorię oraz konkretne
uzasadnienie. Zmiana pliku unieważnia wpis nawet wtedy, gdy sam literał się nie
zmienił. Kod nie zgaduje, czy uzasadnienie jest prawdziwe: to decyzja człowieka,
którą trzeba osobno przejrzeć. Nie przyjmuje automatycznie całych plików ani
grup ze wcześniejszej inwentaryzacji.

Początkowy pack zawiera tylko sześć osobno przejrzanych rzeczywistych kotwic:
identyfikator schematu RuntimeProfile, standard MIME JSON, maszynową nazwę błędu,
format zgodnej serializacji UTC, klucz środowiska odkrywający dane i istniejący
log diagnostyczny chmod. Każda ma własny hash całego źródła, dokładny zakres,
hash literału i konkretne uzasadnienie. Ta lista nie przenosi żadnej wcześniej
zaklasyfikowanej grupy danych do automatycznego dopuszczenia.

Jedyny zaufany backend generacji to obecny `gen_runtime_profiles.py::plan`.
Wyłączenie osadzonego pliku wymaga zgodności bajtów źródła generatora z
kanoniczną implementacją, przypiętego hasha generatora, całej listy wejść,
hashy rzeczywistych wejść oraz identycznych bajtów wyniku planu obliczonego
w pamięci. Ścieżka importu i operacja nie są dowolnie wybierane przez pack.
Narzędzie nie zapisuje źródeł ani wyników generatora i nie wykonuje sieci.

## Granice rozpoznawania

Skaner obejmuje wszystkie pliki w zadeklarowanych korzeniach, także nieznane
rozszerzenia i pliki binarne. CSS, HTML, XML i inne nieobsługiwane formy zostają
w mianowniku jako `BLOCKED`. Jedynie jawne reguły segmentów `tests` i `fixtures`
wyłączają pliki nieproduktowe. Korzenie Androida mogą być jawnie opcjonalne;
ich brak jest odnotowany. Brak obowiązkowego korzenia blokuje wynik. Dokładne
korzenie R42 to `loom/src`, `loom/server`, `loom/cli`, `loom/web/src`, `android`,
`rootandroid` i `loom/android`. Dodatkowo skan obejmuje wszystkie publiczne/inline
nagłówki `loom/include`, gdzie mogą być ukryte wartości domyślne struktur. Ich
mianownik i przypisania właścicieli są jawne. Niepewne nagłówki oraz Android
trafiają do wątku 9 do rozdzielenia zakresu; reguła najdłuższego prefiksu nie
udaje semantycznego rozstrzygnięcia własności współdzielonego pliku.

To skan leksykalny, nie AST i nie dowód semantycznej kompletności. Rozpoznaje
łańcuchy, znaki, liczby oraz wskazane w danych standardowe literały boolean/null.
Komentarze nie są kandydatami. Nie rozwiązuje makr C++, warunków kompilacji ani
semantyki identyfikatorów. Literały we wszystkich gałęziach kodu są kandydatami.
Wyrażenia szablonów i formy regex w JavaScript/TypeScript, JSX, interpolacja
Pythona/Kotlina, bloki tekstowe Javy i zagnieżdżone komentarze Kotlina jawnie
blokują plik. Slash i znak `<` w JS/TS są konserwatywnie blokowane również tam,
gdzie oznaczają dzielenie lub porównanie. Nie ma twierdzenia o pewności AST.
Nieobsługiwane tłumaczenia przed analizą — escape Unicode Javy, łączenie linii
C++ przez backslash, samotne CR i separatory linii U+2028/U+2029 w JS — również
blokują pokrycie, ponieważ mogą zmienić granice komentarzy/literałów. Metadane
standardowych boolean/null są wymaganym kontraktem każdego obsługiwanego
backendu; ich usunięcie nie może ukryć kandydatów. Odczyt drzewa używa jawnego
`scandir` z zachowaniem błędów, zamiast funkcji glob, która mogłaby po cichu
pominąć niedostępny katalog. Błąd przeglądania drzewa blokuje wynik i zapis
raportu mogący nadpisać niezidentyfikowane źródło.

## Odtworzenie

Sprawdziany syntetyczne uruchamia istniejący glob CTest `compat/test_*.py`.
Można je uruchomić oddzielnie:

```sh
python3 -B loom/tests/compat/test_product_literal_guard.py -v
```

Pełny sprawdzian produktu (z katalogu głównego repo) wymaga dostępnego
`jsonschema`; brak tej zależności daje jawny błąd, bez luźniejszego fallbacku:

```sh
python3 -B loom/src/util/product_literal_guard.py --check \
  --report /tmp/product-literals-report.json \
  --manifest /tmp/product-literals-manifest.json
```

Kod wyjścia: `0` — pełny pozytywny wynik zadeklarowanych zakresów, `1` —
niezaklasyfikowane literały lub zablokowane formy, `2` — błąd danych/wejścia.
Bez `--check` narzędzie może służyć do inwentaryzacji: negatywny raport zachowuje
`valid:false`, a proces kończy się `0`; CI musi zawsze używać `--check`.
Jawnie wskazane pliki raportu i manifestu są jedynymi dozwolonymi zapisami.
Kolizja z danymi wejściowymi lub dowolnym zakresem produktu daje błąd przed zapisem.
Dotyczy to również aliasów hardlinków: porównywane są urządzenie i inode,
zarówno przy prawidłowych danych, jak i na ścieżce błędu konfiguracji. Raport
i manifest nie mogą wskazywać tego samego pliku przez różne nazwy.

## Wynik rzeczywisty i sprawdziany

Przed tym przyrostem nie było strażnika R42. Po zmianie niezależny przegląd
mechanizmu zakończył się PASS, a **41 sprawdzianów syntetycznych przeszło w
39,901 s**. [Surowy log końcowy](evidence/product-literal-guard-2026-10-05/synthetic-run-6.log)
obejmuje kotwice, podmiany schematu i generatora, błędy brakujących danych,
nieobsługiwane formy, nagłówki, hardlinki oraz symulowany błąd `scandir` przy
obecności innego w pełni dopuszczonego pliku. To wynik testów mechanizmu.

Pierwszy pełny sprawdzian aktualnego produktu, HEAD
`1b7379c80e62e30e58e97c4ba84c490d9ddbc6f4` z nowymi plikami związanymi hashami,
zakończył się **exit 1 / `valid:false`**:

| Miara | Rzeczywisty wynik |
|---|---:|
| Wszystkie pliki w zakresie | 363 |
| Pliki produktu / jawnie wyłączone tests i fixtures | 339 / 24 |
| Bajty źródeł produktu | 4 884 696 |
| Kandydaci leksykalni | 56 974 |
| Dokładnie dopuszczone / niezaklasyfikowane | 6 / 56 968 |
| Zablokowane pliki / nieaktualne kotwice | 85 / 0 |
| Zweryfikowane osadzenie wygenerowane z danych | 1 |
| Błędy odkrywania drzewa / nieznany rozmiar pliku | 0 / 0 |

Mianownik plików: `loom/src` 220, `loom/server` 8, `loom/cli` 3,
`loom/web/src` 44, `loom/android` 31 i dodatkowe `loom/include` 57.
Opcjonalne `android` oraz `rootandroid` nie istnieją; manifest zapisuje ten
fakt. Nie pominięto CSS, HTML, XML, plików binarnych ani nieznanych rozszerzeń.
Niezaklasyfikowany literał nie jest automatycznie dowodem zakazanych danych:
część to legalna gramatyka, kontrakty lub mechanizm, które nadal potrzebują
indywidualnego przeglądu. Zablokowany plik oznacza niepełne rozpoznanie,
dlatego liczba kandydatów nie jest pełną semantyczną liczbą danych produktu.

[Pełny surowy JSON](evidence/product-literal-guard-2026-10-05/strict-report.json.gz)
ma 31 468 048 bajtów przed kompresją i 2 635 423 bajty w deterministycznym gzip.
[Manifest](evidence/product-literal-guard-2026-10-05/strict-manifest.json),
[dług przypisany według jawnych prefiksów](evidence/product-literal-guard-2026-10-05/owner-debt.json)
i [receipt z komendą i hashami](evidence/product-literal-guard-2026-10-05/actual-capture-receipt.json)
zachowują wszystkie wiersze oraz statusy. Porównanie **390 źródeł i wejść**
[przed](evidence/product-literal-guard-2026-10-05/source-snapshot-before.json)
i [po](evidence/product-literal-guard-2026-10-05/source-snapshot-after.json) jest
identyczne: zero zaobserwowanych zmian bajtów źródeł, zero sieci i zapisów
generatora. To porównanie hashy i odczytowego kodu, nie ślad wszystkich syscalli.

Przenośny [skrypt odtworzenia](evidence/product-literal-guard-2026-10-05/capture_product_literals.py)
przyjmuje `--repo-root`, `--output` i opcjonalne przypięcie historycznego HEAD.
Po publikacji tego przyrostu HEAD zmieni się wraz z commitem; istotne wejścia
i nowe pliki są dodatkowo związane hashami w receipt. Powtórkę należy zapisać
w osobnym katalogu dowodów, zachowując pierwszy wynik negatywny:

```sh
python3 -B docs/reports/data-in-code/evidence/product-literal-guard-2026-10-05/capture_product_literals.py \
  --repo-root "$PWD" --output /tmp/product-literals-replay
```

Zachowano też [ujemny run 2](evidence/product-literal-guard-2026-10-05/synthetic-run-2.log):
33 przypadki, dwa błędy — rzeczywiste nadpisanie źródła przez ścieżkę raportu
po błędzie danych oraz błędne oczekiwanie testu, że dodatnia rewizja `2` jest
niedozwolona. Pierwszy przypadek poprawiono, drugi zmieniono na rewizję `0`.
[Dokładne wejście źródłowe i dane fixture](evidence/product-literal-guard-2026-10-05/negative-run-2-fixture/)
oraz [replay starej gałęzi zapisu](evidence/product-literal-guard-2026-10-05/replay_negative_run2.py)
odtwarzają nadpisanie w tym starym mechanizmie i brak nadpisania w obecnym.
[Surowy wynik replay](evidence/product-literal-guard-2026-10-05/negative-run2-replay.stdout)
potwierdza obie obserwacje. Jest to odtworzenie dokładnego przypadku i dawnej
gałęzi wyjścia z użyciem obecnej walidacji, zachowane osobno od późniejszego
odzyskania źródeł. Odtworzenie nie zastępuje oryginalnego ujemnego logu.

Przed publikacją odzyskano także **pełny guard run 3 bajtowo zgodny** z uprzednio
zapisanym SHA-256 `1e0768846130bc17ca252c309b19554435f6e61acd94c8dd0b1ad73e6c3abb72`
(27 978 bajtów) przez literalne odwrócenie późniejszych patchy na kopii evidence.
[Guard](evidence/product-literal-guard-2026-10-05/historical-recovery/guard-run3-verified.py)
oraz [pełny łańcuch odwróceń](evidence/product-literal-guard-2026-10-05/historical-recovery/source-recovery-receipt.json)
pozostają w archiwum. Usunięcie udokumentowanego patcha ochrony zapisu dodanego
po run 2 daje pełny [source run 2](evidence/product-literal-guard-2026-10-05/historical-recovery/guard-run2-derived.py),
SHA `6278d048a8c9004cdbadbb0d48b9bcea513587c9a718749291cebaffeabfade2`.
Dla run 2 nie było wcześniej niezależnie zapisanego hasha; jego pochodzenie
wynika z jawnej historii patcha i zweryfikowanego sąsiedniego snapshotu, nie
z nieistniejącego pierwotnego receipt. Stary schemat odzyskano bajtowo zgodnie
z uprzednio zapisanym SHA `fbdffaf662c94af95731430f466238bb9eb53f499e0be010066f46e7cda1843d`.
Cały [test run 3](evidence/product-literal-guard-2026-10-05/recovered-test-sources/test_product_literal_guard-run3.py)
także odzyskano przez jawne odwrócenie patchy: 31 633 bajty, zgodne z niezależnym
uprzednim SHA `d543631015aac8474f43592cf6c5da49da8ce6f76052091b48ba65bd32ab3bca`.
Jawne odwrócenie pięciu późniejszych hunków daje także pełny
[test run 2](evidence/product-literal-guard-2026-10-05/recovered-test-sources/test_product_literal_guard-run2-history-derived.py),
SHA `b9a7a1b08f03808edf6e4dcd0ae48e9940de25c79c2e7fffa74d7f3d235f38ab`,
bez niezależnego starego targetSHA. Obie linie tracebacków 498 i 435 odpowiadają
oryginalnemu ujemnemu logowi; wszystkie inverse hunks zachowano w receipt.
Te odzyskane kopie nie zmieniają bieżących źródeł ani oryginalnych logów.
[Osobny replay kompletnego historycznego guard](evidence/product-literal-guard-2026-10-05/historical-recovery/replay_full_guard_run2.py)
wywołuje go przez subprocess tylko na zachowanych syntetycznych wejściach;
[nowy wynik replay](evidence/product-literal-guard-2026-10-05/historical-recovery/full-guard-run2-replay.stdout)
odtwarza exit 2 i nadpisanie źródła oraz exit 1 dla poprawnej rewizji 2 w pustym
zakresie. Nie jest przedstawiany jako odzyskany oryginalny log.

Run 1 był przejściowym rozjazdem fixture/schematu. Jego **trzy kompletne pola
`output` narzędzia** odzyskano literalnie z kontekstu do
[połączonego zapisu](evidence/product-literal-guard-2026-10-05/recovered-run1/combined-tool-output.txt)
(40 897 bajtów), osobnych chunków i [receipt](evidence/product-literal-guard-2026-10-05/recovered-run1/receipt.json).
Zakres odzyskania to decoded combined tool output, bez rozdzielenia stdout/stderr
i bez deklaracji oryginalnych bajtów PTY/pipe. Narzędzie nie obcięło pól; skróty
`repr` emitowane przez sam unittest zachowano dokładnie. Tymczasowe źródła,
polityka, schematy i zmieniający się w trakcie run 1 stan plików nie zostały
utrwalone i nie są fabrykowane. Zielone runy 3–6 zachowano oddzielnie od obu
wyników ujemnych.

## Do wątku N

- **Do wątku 8:** gotowy CLI i syntetyczne testy stanowią wejście do CI. Nie
  włączaj zielonej bramki pełnego repo przez blanket-allowlistę: wynik negatywny
  wymaga osobnych decyzji właścicieli zakresów.
- **Do wątku 9:** dodatni wynik fixtures dotyczy działania strażnika, a nie
  akceptacji R42 całego produktu. Dowody pełnego wyniku negatywnego muszą pozostać
  odtwarzalne na gałęzi/archiwum. Przydziel niepewne nagłówki, Android oraz
  współdzielone pliki serwera na podstawie owner-debt; prefiks nie rozstrzyga
  własności poszczególnych handlerów.
- **Do wątków 1/2/3/4/5/6/10/11:** każdy `UNCLASSIFIED` wymaga przeniesienia danych
  albo indywidualnie uzasadnionej, dokładnej kotwicy jednej z sześciu kategorii.
  `BLOCKED` nie jest automatycznie dopuszczony: trzeba rozszerzyć rejestr/mechanizm
  rozpoznawania lub jawnie wykazać nieproduktową naturę pliku.
- **Do wątku 12:** ten przyrost egzekwuje klasyfikację literałów R42; nie
  implementuje rozwiązywania warstw R40 ani wizarda R41. Schematy i źródła paków
  pozostają wejściem do ich osobnych mechanizmów, bez drugiego loadera w UI.
