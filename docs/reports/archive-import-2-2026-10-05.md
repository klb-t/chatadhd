# Wątek 5 — drugi przyrost, 2026-10-05

Status: W TRAKCIE. Gałąź `gpt/archive-import-2-2026-10-05`; poprzednia `gpt/archive-import-2026-10-04` jest zamrożona i nie jest zmieniana.
Baza to dozwolony przez właściciela `gpt/integrator-state-2026-10-04` (`0a81480bd1a70b394bac72e98f6b5e7cb49f5d0b`), zawierający odebrany przyrost5 oraz3/4. `main` podczas fetch: `e4109df7e4af22b461def5f7d62e268d9b9a8825`. Natywny punkt odniesienia66da570 ma identyczne źródła produkcyjne; późniejszy0a81480 dodaje tylko dowody integratora.

## Wybrany zakres

Inwentarz [wątku5](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-5.md): DIC0424 (inline), DIC0428 (głębokość) i DIC0682 (audyt); wraz z pozostałymi ustawieniami parsera tworzą komplet5 ustawień importu i20 współdzielonych pól audytu. To część52 grup, nie deklaracja przeniesienia całego inwentarza.

Jedno źródło wartości: `loom/data/presets/import.pack` i `import_audit.pack`. Generator zachowuje dokładne oryginalne bajty w natywnym osadzeniu. Nie ma drugiej ręcznej tabeli wartości ani cichego fallbacku. Domyślne różnice native/API Python/CLI (zakres i prefix) są zachowane w danych. Stawki nie są automatycznie pobierane; audyt i cały przyrost są offline.

R40: kompletne już rozstrzygnięte wartości są walidowane przed zastosowaniem, brak/null nie jest ponownie uzupełniany z defaults. `import_layers.spec.pack` wiąże pola z trwałymi identyfikatorami/obszarami kontraktu warstw12, bez powielania wartości. Most jest transformacją danych; nie zastępuje mechanizmu wykluczeń ani API profili11/12.
R42 sprawdzono w autoryzowanej gałęzi właściciela `claude/chataddhd-cpp-loom-core-IRGRN`, commit3cd5848; tekst nie jest jeszcze na naszej bazie. Pozostają dozwolone nazwy pól/schematów, reprezentacyjne granice typów, komunikaty błędów i mechanika; wartości i nazwy presetów pochodzą z danych; także zmiana ID bez zmiany algorytmu jest sprawdzona. ID ma być niepustym tekstem, nie konkretną nazwą zaszytą w kodzie.

## Weryfikacja i liczby

Przed:5 domyślnych wartości importu i audyt rozproszone w C++/Python. Po:2 kanoniczne pliki danych,5+20 pól; domyślne zachowanie pozostaje wymaganiem bramki.
Python:13 pełnych wyników API/CLI identycznych bajt po bajcie z bazą;19 starych testów zachowane,13 nowych przypadków, zwykły suite32/32 PASS. Root niezależnie ponownie wykonał32/32,0skip; wcześniejszy przegląd31 przypadków wykrył także potrzebę usunięcia ograniczenia rewizji danych do1, co jest poprawione i objęte32. testem. Generator:10/10 PASS, kontrola dokładnych bajtów i25 wpisów warstw; nowa natywna regresja15 przypadków (preset/audyt/cache/pokwitowania) czeka na rzeczywisty build. Niezależny rzeczywisty konsument W12 + decoder W5:55/55 sprawdzeń PASS,25/25 wpisów; sprawdza wykluczenia/aktualizacje wersji, bez produkcyjnego OnboardingStore. Powtórzenie po zmianie ID jako danych:55/55 i wynik identyczny; [źródła, runner, oba pokwitowania](archive-import-2-2026-10-05-layers-evidence.zip), SHA2561ec05099e4f16043115a9e0ddee2dbb7db958f616cda975552d84ba93915490c. W5=156a820, rzeczywisty silnik W12=3c0bc365. Build web PASS (85 modułów,5,13s). Natywny punkt odniesienia66da570 rzeczywiście wykonany:3 rozmowy,7 wiadomości,86 znaków Unicode;11/11 bramek,10 wpisów pochodzenia. Pełne JSON-y zachowane do porównania. Natywna zgodność po zmianie i pełny CTest: jeszcze w toku; nie zgłaszam gotowości przed zakończeniem.

## API i uruchomienie u właściciela

Python: `archive_stats`, `project_stats` i `estimate` przyjmują `audit_preset=` (ścieżka deskryptora albo kompletne Mapping wartości); pominięty argument używa danych kanonicznych. Jawne argumenty pojedynczego wywołania mają pierwszeństwo. `load_audit_preset` waliduje, `inspect_audit_preset` podaje wartości, wersję, hash źródła i pochodzenie. Import modułu nie czyta packa, więc kompletny jawny preset działa również przy uszkodzonym/brakującym domyślnym pliku. Wybrany błędny preset kończy się błędem przed otwarciem DB.

Przykłady offline, na własnych danych właściciela (nie dodawać eksportu do repo):

```sh
python3 loom/tools/eval/archive_cost.py --help
python3 loom/tools/eval/archive_cost.py --db archive.sqlite --audit-preset /path/audit.pack --json
```

CLI audytu zachowuje stare flagi ujemne i dodaje jawne `--include-versions`, `--include-unknown-status`, `--include-tools`, żeby ustawienie z pliku można było także ponownie włączyć w wywołaniu. Historyczny plik cen pozostaje datowaną opcją; nie jest aktualnym cennikiem ani automatycznym wydatkiem.

Natywny CLI otrzymuje `--import-preset PATH` i `--audit-preset PATH`: odpowiednio deskryptory `loom.import_preset/1` i `loom.import_audit_preset/1` z kompletnym `values`. Istniejące pojedyncze flagi importu/audytu mają pierwszeństwo. Nie są to pliki utrwalonego stanu wykluczeń.

```sh
loom import export.zip --audit --import-preset /path/import.pack --audit-preset /path/audit.pack
```

Prywatne API konsumenta: `import_preset_from_values`, `apply_import_preset_values`, `inspect_import_preset` oraz odpowiedniki `import_audit_*`. Zastosowanie jest atomowe, nie zmienia callbacków/proweniencji/cen wywołującego. Zachowane zera parsera mają poprzednią semantykę, np. głębokość0 oznacza brak limitu; rozmiar chunk musi być dodatni i reprezentowalny, bez wymyślonego maksimum.

Format `schema` ma numer kontraktu `/1`, a `version` jest dowolną dodatnią całkowitą rewizją danych; nie blokujemy kolejnych rewizji.

Regeneracja danych:

```sh
python3 loom/src/import/gen_import_presets.py
python3 loom/src/import/gen_import_presets.py --check
```

Zmiana wartości wymaga zmiany wersji źródła oraz odpowiedniej rewizji wpisu i pakietu w specyfikacji warstw. Trwałych id/key/area nie używać ponownie dla innego znaczenia. Generator jest transformacją bez historii; konflikty rewizji/wykluczenia sprawdza silnik12. Tokeny `compatibility` opisują historyczny format źródeł i pokwitowań, nie aktualny preset; nie aktualizować ich automatycznie przy nowym domyślnym ustawieniu.

## Czego ten przyrost nie robi

Nie przenosi jeszcze profili formatów/ról, FTS ani wszystkich ustawień DB. Zachowuje `recorded|model|user` i zakresy adnotacji z poprzedniego przyrostu. Nie modyfikuje grafu metod ani UI. Pełne R40/R41 wymagają wspólnego konsumenta11/12 i późniejszego powiązania z grafem; kompletne dane nie udają stanu wykluczeń.

## Do wątku 2

Zmiana5 ustawień musi wiązać treść oczekującego potwierdzenia importu; domyślne legacy pokwitowania pozostają identyczne. Sześć zwykłych natywnych regresji obejmuje cache pełny/częściowy oraz zmianę każdego pola przy realnym pokwitowaniu W2. Szacunek istniejącej operacji pozostaje liczony z bajtów źródła. Przewidywanie pamięci/CPU/rozmiaru projekcji wymaga osobnego modelu kosztu, nie zmyślonych cen.

## Do wątku 11

Po odbiorze foundation włączyć te2 zasoby do wspólnego rejestru profili; nie kopiować ich wartości. Natywne/Python API przyjmują kompletne efektywne wartości, także po usunięciu pola przez warstwy; brak jest błędem.

## Do wątku 12

Wygenerowany `loom.default_layers_pack/1` jest poprawnym źródłem25 wpisów; id/key/area/revision są trwałe. **Granica integracji:** `DefaultLayers` przypina jeden pack_id, a OnboardingStore ma już swój `user.pack`. Nie podmieniać tożsamości tego stanu na `loom.defaults.archive-import`. Złożyć wpisy z zachowaniem ich tożsamości do autorytatywnego packa z podniesioną rewizją albo użyć uzgodnionego wspólnego API kompozycji wielu packów. Nie tworzyć drugiego trwałego magazynu wykluczeń w5. Konsument ma respektować suppressed/wykluczenia przed walidacją, bez ponownego merge. Most HTTP/UI nie jest w zakresie5.

## Do wątku 9

Przyrost jest oddzielny od poprzedniego5. Obecnie NIEGOTOWY; gotowość zostanie wpisana po pełnych bramkach. Zachować wymagania R42 z3cd5848 w kolejce dokumentacji. Otwarte52 grup5 z INDEX są częściowo pokrywane w tym przyroście; pozostałe formaty/role/DB/FTS zostają kolejnym zadaniem.
