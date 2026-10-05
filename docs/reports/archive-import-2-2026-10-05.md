# Wątek 5 — drugi przyrost, 2026-10-05

Status: W TRAKCIE. Gałąź `gpt/archive-import-2-2026-10-05`; poprzednia `gpt/archive-import-2026-10-04` jest zamrożona i nie jest zmieniana.
Baza to dozwolony przez właściciela `gpt/integrator-state-2026-10-04` (`0a81480bd1a70b394bac72e98f6b5e7cb49f5d0b`), zawierający odebrany przyrost5 oraz3/4. `main` podczas fetch: `e4109df7e4af22b461def5f7d62e268d9b9a8825`. Natywny punkt odniesienia66da570 ma identyczne źródła produkcyjne; późniejszy0a81480 dodaje tylko dowody integratora.

## Wybrany zakres

Inwentarz [wątku5](https://github.com/klb-t/chatadhd/blob/c21e664c8145568e0505f9218fda67a75616056f/docs/reports/data-in-code/thread-5.md): DIC0424 (inline), DIC0428 (głębokość) i DIC0682 (audyt); wraz z pozostałymi ustawieniami parsera tworzą komplet5 ustawień importu i20 współdzielonych pól audytu. To część52 grup, nie deklaracja przeniesienia całego inwentarza.

Jedno źródło wartości: `loom/data/presets/import.pack` i `import_audit.pack`. Generator zachowuje dokładne oryginalne bajty w natywnym osadzeniu. Nie ma drugiej ręcznej tabeli wartości ani cichego fallbacku. Domyślne różnice native/API Python/CLI (zakres i prefix) są zachowane w danych. Stawki nie są automatycznie pobierane; audyt i cały przyrost są offline.

R40: kompletne już rozstrzygnięte wartości są walidowane przed zastosowaniem, brak/null nie jest ponownie uzupełniany z defaults. `import_layers.spec.pack` wiąże pola z trwałymi identyfikatorami/obszarami kontraktu warstw12, bez powielania wartości. Most jest transformacją danych; nie zastępuje mechanizmu wykluczeń ani API profili11/12.
R42 sprawdzono w autoryzowanej gałęzi właściciela `claude/chataddhd-cpp-loom-core-IRGRN`, commit3cd5848; tekst nie jest jeszcze na naszej bazie. Pozostają dozwolone nazwy pól/schematów, reprezentacyjne granice typów, komunikaty błędów i mechanika; wartości presetów pochodzą z danych.

## Weryfikacja i liczby

Przed:5 domyślnych wartości importu i audyt rozproszone w C++/Python. Po:2 kanoniczne pliki danych,5+20 pól; domyślne zachowanie pozostaje wymaganiem bramki.
Python:13 pełnych wyników API/CLI identycznych bajt po bajcie z bazą;19 starych testów zachowane,12 nowych przypadków, zwykły suite31/31 PASS. Niezależny przegląd ponownie wykonał31/31,0skip. Natywna zgodność, pełny CTest i build web: jeszcze w toku; nie zgłaszam gotowości przed zakończeniem.

## Czego ten przyrost nie robi

Nie przenosi jeszcze profili formatów/ról, FTS ani wszystkich ustawień DB. Zachowuje `recorded|model|user` i zakresy adnotacji z poprzedniego przyrostu. Nie modyfikuje grafu metod ani UI. Pełne R40/R41 wymagają wspólnego konsumenta11/12 i późniejszego powiązania z grafem; kompletne dane nie udają stanu wykluczeń.

## Do wątku 2

Zmiana5 ustawień musi wiązać treść oczekującego potwierdzenia importu; domyślne legacy pokwitowania pozostają identyczne. Szacunek istniejącej operacji pozostaje liczony z bajtów źródła. Przewidywanie pamięci/CPU/rozmiaru projekcji wymaga osobnego modelu kosztu, nie zmyślonych cen.

## Do wątku 11

Po odbiorze foundation włączyć te2 zasoby do wspólnego rejestru profili; nie kopiować ich wartości. Natywne/Python API przyjmują kompletne efektywne wartości, także po usunięciu pola przez warstwy; brak jest błędem.

## Do wątku 12

Przyjąć wygenerowany `loom.default_layers_pack/1` jako źródło25 wpisów; id/key/area/revision są trwałe. Konsument ma respektować suppressed/wykluczenia przed walidacją, bez ponownego merge. Most HTTP/UI nie jest w zakresie5.

## Do wątku 9

Przyrost jest oddzielny od poprzedniego5. Obecnie NIEGOTOWY; gotowość zostanie wpisana po pełnych bramkach. Zachować wymagania R42 z3cd5848 w kolejce dokumentacji. Otwarte52 grup5 z INDEX są częściowo pokrywane w tym przyroście; pozostałe formaty/role/DB/FTS zostają kolejnym zadaniem.
