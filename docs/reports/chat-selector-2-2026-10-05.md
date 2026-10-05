# Wątek 3 — drugi przyrost, 2026-10-05

Gałąź: `gpt/chat-selector-2-2026-10-05`. Baza: aktualniejszy stan integratora
`66da570d3b5379492e128d940ad474467082c59f`; `main` podczas startu był na `e4109df`.
Po kolejnym fetch baza przesunięta liniowo na `0a81480` (wyłącznie dokumentacja
integratora; źródła produktu identyczne z `66da570`). Poprzedni przyrost
`gpt/chat-selector-2026-10-04` pozostaje bez zmian.

**Status: W TOKU. Nie zgłaszam gotowości przed pełnym CTest i porównaniem wyników.**

## Zakres i wynik

Z inwentarza wątku 11 wybrane: DIC-0336/0337 (frazy i fallback celu) oraz
DIC-0368/0369/0370 (rozpoznawanie reasoning, wersje i budżety). Dwa kanoniczne
deskriptory `loom.runtime_profile/1`, osadzenie generowane z danych, 13 stabilnych
wpisów warstw grafu wyprowadzanych z tych samych wartości. Bez ręcznej drugiej
tabeli, osobnego resolvera, loadera plików ani kopii walidatora schematów.

Kontrakt: [RUNTIME_PRESETS.md](../../loom/src/context/RUNTIME_PRESETS.md).
Wspólny kontrakt metod 3/4 pozostaje
[METHOD_GRAPH.md](../../loom/src/packet/METHOD_GRAPH.md).

Domyślne zachowanie pozostaje celem obowiązkowego porównania bajtów przez
rzeczywiste publiczne API: 1248 wariantów reasoning i 26 prób typu celu.
Osobne testy sprawdzają dokładne zastąpienie, brak przywracania usuniętych pól,
arbitralne parametry, nieruchomy snapshot i rzeczywiste API warstw 11/12.
Wykonany dowód adaptera: 6/6 i 97 asercji na bazie; 8/8 i 132 asercje
z rzeczywistymi W11/W12; 0 pominięć. Generator→C++→JSON: 13 przypadków
tekstowych zgodnych w pełnej postaci. Historyczna kotwica identyfikacji jest
metadanymi w danych; zmiana rewizji albo wartości builtin presetu zmienia
hash i ślad, obecny preset zachowuje stare ID. To wąski link rzeczywistych utility,
nie zamiennik pełnego CTest: [dowód](chat-selector-2-2026-10-05-evidence/presets/manifest.json).

## Otwarte przekazania z INDEX

Pozostałe 46 grup, domyślny katalog metod, publiczne wiring i trwałe wznowienie
nie należą do deklarowanego wyniku tego przyrostu. R40: warstwy są konsumowane
wyłącznie przez prawdziwe API W12, z walidacją W11; na tej bazie ich nie ma,
więc żądanie warstw zwraca jawne `Unavailable`. Test z przypiętym kodem nie jest
twierdzeniem o integracji tych gałęzi. Nie zmieniono progów ani testów.

R42: przeniesione nazwy modeli/wersji, etykiety effort, budżety, wagi i fallback
są danymi. Pozostają klucze kontraktów, operacje (`first_by_id`, `error`),
pola protokołu dostawcy i diagnostyka. Domyślny hash KB packa nie zmienia się.

Wcześniejszy błąd porównania `std::string == Json` w fixture doctest jest
w pełni odtwarzalny na [osobnej gałęzi archiwalnej](https://github.com/klb-t/chatadhd/tree/2d28075a3b8404430a7239e6fa0148e1d008a35b/docs/reports/chat-selector-2-2026-10-05-negative/helper-compile).
Niezależny replay kompilacji odtworzył identyczny błąd; archiwum zawiera źródła,
piny, wszystkie komendy i pełne logi. Nie włączamy tego negatywu do przyrostu.

0 płatnych wywołań; używane wyłącznie syntetyczne dane i transporty testowe.

## Do wątku N

- **9:** odbiór dopiero po aktualizacji statusu i pełnych dowodach; zachować
  kolejkę 11/12, ponownie wykonać pełne bramki po ich integracji.
- **11:** dwa nowe runtime deskriptory należy włączyć w zwykłą regenerację
  `gen_runtime_profiles.py`; prywatny most używa `from_definition/with_values`,
  nie tworzy konkurencyjnego loadera. Do dalszego uzgodnienia trwałe wskazanie
  efektywnego profilu przez wspólne runtime settings.
- **12/10:** dodać generowany wkład 13 wpisów do istniejącego packa użytkownika
  normalnym forward update, zachowując `pack_id` i CAS. UI/HTTP przekazuje bound
  pack/state/bindings; przy wykluczeniu nie odtwarza wartości z presetów.
- **12:** do sprawdzenia na Waszym source `3c0bc36`: `store.cpp:75–81`,
  `330–335`, `439–442`. Wyłączenie `onboarding.settings`, potem override
  `onboarding.privacy` ze wildcard `store=false`, może zostawić stare privacy
  w `state.profile`: zbiorczy defaults zwraca `Unavailable`, a policy gate
  odczytuje starą kopię profilu mimo nowego effective resolution. To wniosek
  z kodu, nie wykonany wynik. Potrzebny Wasz niezależny regression i naprawa;
  nie edytuję store ani policy gate W12.
- **4/3:** dalsze włączenie domyślnych przepisów do wersji metod i śladów w
  grafie wykonania pozostaje oddzielnym krokiem; wpis defaultu nie jest przebiegiem.
- **3/9:** zachowane otwarte zadania: zwykły legacy transport przez strażnik
  zużycia, publiczne ustawienia/ABI i trwały resume. Nie deklaruję ich ukończenia.
