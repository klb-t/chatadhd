# Wątek 5 — drugi przyrost, 2026-10-05

**GOTOWY DO ODBIORU WYBRANEGO PRZYROSTU.** Gałąź `gpt/archive-import-2-2026-10-05`; kod zamrożony w `156a82080a7abb8a68d249747dfa167b3045effe`. Poprzednia gałąź `gpt/archive-import-2026-10-04` pozostaje nietknięta.

Baza: dozwolony `gpt/integrator-state-2026-10-04`, `0a81480bd1a70b394bac72e98f6b5e7cb49f5d0b`, z przyjętym poprzednim W5 oraz 3/4. Świeży fetch potwierdził nadal ten tip i `main=e4109df7e4af22b461def5f7d62e268d9b9a8825`. Natywny punkt odniesienia `66da570d3b5379492e128d940ad474467082c59f` ma identyczne źródła produkcyjne; późniejszy commit bazy dodaje dowody integratora.

## Co zrobiono

Wybrano z [inwentarza W5](https://github.com/klb-t/chatadhd/blob/753858ea00f0bf7966e4dbc2e0b6a12bf101d1cf/docs/reports/data-in-code/thread-5.md) DIC-0424, DIC-0428 oraz DIC-0682: wartości parsera/importu i audytu. To część 52 grup z aktualnego INDEX, nie ukończenie całego inwentarza.

| Miara | Przed | Po |
|---|---|---|
| Domyślne ustawienia importu | 5 liczb w kodzie, część powtórzona | Jeden `loom/data/presets/import.pack` |
| Domyślne ustawienia audytu | Rozproszone C++/Python | 20 pól w `import_audit.pack`; zachowane różnice native/API/CLI |
| Warstwy R40 | Brak deskryptora tych ustawień | 25 trwałych wpisów wyprowadzonych z obu packów |
| Audyt Python | 19 zwykłych testów | 32, wszystkie stare zachowane |
| Zarejestrowane wpisy CTest | 117 | 121, żadnego nie usunięto |

Generator osadza dokładne bajty dwóch kanonicznych packów w C++. `import_layers.spec.pack` opisuje wiązania, a wygenerowany `import_layers.pack` ma kontrakt `loom.default_layers_pack/1`. W specyfikacji nie ma kopii wartości. Nazwy presetów są danymi; kod nie wymaga konkretnego ID.

Import wiąże cache/wznowienie z efektywną projekcją semantyczną (inline i generic). Zmiany ustawień zasobów zachowują tożsamość starego checkpointu. Pokwitowanie W2 wiąże wszystkie 5 ustawień, więc zmiana któregokolwiek pod tym samym operation-id nie przyjmuje starego potwierdzenia. Domyślne formaty źródeł i pokwitowań pozostają zgodne z historycznymi.

R40: adapter przyjmuje już rozstrzygnięte wartości; brak/null używanego pola kończy się błędem, bez ponownego uzupełnienia defaults. Kompletność dotyczy konkretnego konsumenta: **import 5 / audyt natywny 4 / audyt Python 20 pól**. Nie tworzymy drugiego trwałego magazynu wykluczeń.

R42 odczytano z autoryzowanej gałęzi właściciela `claude/chataddhd-cpp-loom-core-IRGRN`, `3cd5848e7484d50d00f19f0855bc2cd228ad7f3b`; tekst nie jest jeszcze na naszej bazie. W kodzie pozostają kontrakty pól/schematów, operacje, diagnostyka i granice reprezentacji typów. Wartości i ID presetów są w danych; pozytywne rewizje danych nie są ograniczone do 1.

## Weryfikacja

Wszystkie wykonania offline, wyłącznie publiczne/syntetyczne fixtures: **0 prywatnych wejść, 0 płatnych wywołań**.

| Bramka | Wynik |
|---|---|
| Pełny build GCC/vendored SQLite, WERROR, shared/CLI/server/tests | PASS, exit 0 |
| Pełny CTest | **121/121 PASS**, exit 0, 352,65 s |
| Niezmieniony strażnik W8 `f64e0ef…` | 120 wykonanych wpisów; **725 native / 26 841 asercji / 1 345 Python / 0 pominięć Python** |
| Istniejący opt-in `catalog_scale` | Jawne 0/0; jedyny niewykonany zestaw |
| Nowe zwykłe testy natywne | 15 przypadków / 442 asercje PASS; także niezależne wykonanie świeżych obiektów |
| Audyt Python / generator | 32/32 i 10/10 PASS w tym samym pełnym przebiegu |
| Rzeczywisty CLI | 3 nowe scenariusze i 12 odrzuceń błędnego presetu przed zmianą importowanych danych/blobów |
| Web: `tsc` + Vite | PASS, 85 modułów, 5,13 s; instalacja z offline cache |

Strażnik sprawdził pełne, nieprzycięte strumienie; nie rekonstruowano XML. `knowledge` i `context_engine` rzeczywiście wykonały po 18 przypadków; `import_resume` 23/696 i `import_screenshot` 6/872. Niezależny audyt potwierdził 1274/1274 hashe źródeł względem Git oraz 7/7 hashe artefaktów względem zapisanych snapshotów i kontroli po bramkach. Snapshot wykonano w trakcie CTest, potem ponownie sprawdzono; nie deklarujemy pełnego snapshotu przed startem. Obie strony natywnego porównania użyły GCC 13.3, Release `-O0 -DNDEBUG`; nie deklarujemy macierzy Clang/ASan ani izolowanego benchmarku.

Zgodność domyślnych wyników:

- Python: **13 pełnych wyników API/CLI identycznych bajtowo** z bazą; surowe wyniki i kolektor w dowodzie bramek.
- Native: rzeczywiste wykonania przed/po, po 11/11 sprawdzeń; **6 pełnych sekcji / 704 wartości zgodnych**. Fixture: 3 rozmowy, 7 wiadomości, 86 znaków Unicode, 10 wpisów pochodzenia. Zachowano raw stdout; normalizacja zmienia jedynie generowane ID/datę/URI kopii, nie usuwa kluczy. To dowód SQLite/audit, nie nowy pomiar wielogigabajtowy ani live-WAL.
- Rzeczywisty silnik warstw W12 `3c0bc365` i decoder W5: **55/55 sprawdzeń, 25/25 wpisów**; wykluczenia, zmiany rewizji, propozycje nowych defaults i brak fallbacku. Powtórzenie na finalnym `156a820` daje ten sam wynik; nie jest to integracja produkcyjnego OnboardingStore.

Pełne publiczne dowody:

- [Bramki, source/binary hashes, XML, logi i odtwarzalny runner](archive-import-2-2026-10-05-gates.zip): 51 plików, 216373 B; SHA256 `4f6ce2b4928081ea330894a1d170d813dc11f62076106609a7f229e8dc1a93f2`. Zawiera również pełny oryginalny dowód W12 przed zmianą ID.
- [Porównanie natywne przed/po](archive-import-2-2026-10-05-native-parity.zip).
- [Niezależne wykonanie 15/442](archive-import-2-2026-10-05-focused-native.zip).
- [Powtórzenie konsumenta warstw po zmianie ID](archive-import-2-2026-10-05-layers-evidence.zip).

Pełne przechwycone próby narzędziowe/przerwane buildy i historyczne dowody pozostają oddzielnie na [archiwalnym przyroście](https://github.com/klb-t/chatadhd/blob/ec18cdd3262fa850eb39dfe6d57f8d03fca507ea/docs/reports/archive-import-2-2026-10-05-build-attempts.md). Nie są negatywnym wynikiem funkcjonalnym. Zmniejszono współbieżność kompilacji i przeniesiono wyłącznie ukończone własne artefakty bajtowo identycznie do tmpfs; źródła, flagi i hashe wynikowych binariów pozostały bez zmian. Nie luzowano progów.

## API i instrukcja dla właściciela

Python `archive_stats`, `project_stats`, `estimate`: `audit_preset=` przyjmuje deskryptor lub kompletne Mapping; jawne argumenty mają pierwszeństwo. `load_audit_preset` waliduje, `inspect_audit_preset` pokazuje źródło/ID/wersję/hash/wartości. Import modułu nie czyta domyślnego packa; kompletny jawny preset działa także bez niego. Błędny wybrany preset nie otwiera DB.

CLI: `--import-preset PATH` / `--audit-preset PATH` przyjmują deskryptory `loom.import_preset/1` / `loom.import_audit_preset/1` z kompletnym `values`. Istniejące pojedyncze flagi mają pierwszeństwo. Native API projekcji: `import_preset_from_values`, `apply_import_preset_values`, `inspect_import_preset` oraz odpowiedniki `import_audit_*`. Zastosowanie atomowe; callbacki, proweniencja i jawne ceny wywołującego pozostają zachowane.

```sh
# Na własnych danych; nie dodawać eksportu ani DB do publicznego repo.
python3 loom/tools/eval/archive_cost.py --db archive.sqlite --audit-preset /path/audit.pack --json
loom import export.zip --audit --import-preset /path/import.pack --audit-preset /path/audit.pack
python3 loom/src/import/gen_import_presets.py --check
```

`import --audit` wykonuje import i raportuje jego wynik. Stawki natywne są nieznane bez jawnego podania. Historyczny snapshot Python pozostaje datowanym wejściem; nie pobieramy aktualnych cen ani nie wysyłamy danych do modelu. CLI Python zachowuje stare flagi ujemne i dodaje dodatnie `--include-versions`, `--include-unknown-status`, `--include-tools`.

Zera mają poprzednią semantykę (np. depth 0 = bez limitu); chunk jest dodatni i reprezentowalny bez sztucznego maksimum. Przy zmianie packa uruchomić generator bez `--check`; podnieść wersję źródła oraz odpowiednią rewizję wpisu/pakietu warstw. Generator nie ma historii: konflikty wersji rozstrzyga W12. Tokeny `compatibility` opisują historyczne formaty, nie aktualne defaults; nie aktualizować ich automatycznie.

## Czego nie zrobiono

Pozostałe formaty/role, DB/FTS oraz pełne UI/profile/graf metod zostają kolejnymi przyrostami właściwych właścicieli. Nie zmieniono migracji DB ani adnotacji `recorded|model|user` i ich zakresów. Ten most danych nie kończy R40/R41 i nie reklamuje wszystkich 52 grup jako przeniesionych.

## Do wątku 2

W5 już wiąże wszystkie 5 ustawień z pending receipt; 6 zwykłych regresji cache/wznowienia i rzeczywistego potwierdzenia W2 przechodzi. Estymacja nadal mierzy bajty źródła. CPU/RAM/rozmiar projekcji wymaga osobnego modelu zużycia, nie wymyślonych cen.

## Do wątku 11

Włączyć te 2 zasoby do wspólnego rejestru bez kopiowania wartości. Przyrost `5f9774c` (obserwowany tip `1b7379c`) już zawiera niewpięte `loom/data/runtime/import.pack`, `import_audit.pack`, `import_formats.pack`; nakładające się 5+20 defaults wyprowadzić z kanonicznych packów W5 przed aktywacją. Projekcje mają 5/4/20 wymaganych pól: brak/null używanego pola po warstwach jest błędem; pole należące wyłącznie do innego konsumenta nie jest przywracane. Formatów/ról nie deklarujemy jako podłączonych tym przyrostem.

## Do wątku 12

`import_layers.pack` ma 25 trwałych id/key/area/revision. **Granica:** DefaultLayers przypina jeden pack_id, a OnboardingStore ma już `user.pack`. Nie podmieniać tej tożsamości na `loom.defaults.archive-import`. Złożyć wpisy do autorytatywnego packa z podniesioną rewizją albo uzgodnić wspólne API kompozycji. Respektować suppressed/wykluczenia przed walidacją, bez ponownego merge. Drugi magazyn wykluczeń oraz most HTTP/UI nie są pracą W5.

## Do wątku 9

**GOTOWY:** odebrać wyłącznie drugi przyrost z `gpt/archive-import-2-2026-10-05` po własnym świeżym fetch/rebase i mixed gates. Kod `156a820` ma pełny pozytywny dowód; kolejne commity zapisują wyłącznie raporty/dowody. Baza to aktualny dozwolony fallback, będący potomkiem `main`; poprzedni W5 nietknięty. Archiwalnego przyrostu `ec18cdd…` nie przenosić na main. Zachować wymagania R42 z `3cd5848…` w kolejce dokumentacji. Przekazać konkretne Do11/12; pozostałe formaty/role/DB/FTS z 52 grup pozostają otwarte.
