# Wątek 3 — drugi przyrost, 2026-10-05

**Gotowy do odbioru: `4115267d92f057763dfaf9d17c1cf8a9114227b0`**, gałąź
[`gpt/chat-selector-2-2026-10-05`](https://github.com/klb-t/chatadhd/tree/gpt/chat-selector-2-2026-10-05).
Następny commit zmienia wyłącznie ten raport. Baza po końcowym fetch/rebase: aktualny `main`
`6ae0e28b8405b43aa9d8995ad8273a0bcb0b34c4`. Źródła produktu są dokładnie
identyczne z przebadanym `ec34a67` (baza kompilacji `66da570d` plus ten przyrost);
różnica dotyczy wyłącznie obcych dokumentów. Pełny dowód równoważności jest w manifestach.
Poprzedni przyrost i gałąź `gpt/chat-selector-2026-10-04` pozostają bez zmian.

## Zmiana i liczby przed/po

| Zakres z inwentarza 11 | Przed | Po |
|---|---|---|
| DIC-0336/0337: arytmetyka cue, confidence i fallback celu | wartości w C++ | 6 ustawień w `context_goal_cues.pack` |
| DIC-0368/0369/0370: markery modeli/wersji, effort i budżety | tabele i dispatch presetów w C++ | 7 pól w `chat_reasoning.pack` |
| Wybrane grupy / pozostałe z pierwotnych 51 | 5 / 46 | 5 przeniesionych / 46 nieobjętych deklaracją |
| Presety i wpisy warstw | brak tego wkładu | 2 deskriptory `loom.runtime_profile/1`, 13 stabilnych wpisów wyprowadzanych z tych samych danych |
| Domyślne wyniki rzeczywistego API | 1248 chat + 26 cel | 1274 zgodne bajt w bajt, 0 zmian |

Kanoniczne dane: `loom/data/runtime/{context_goal_cues,chat_reasoning}.pack`.
`loom/data/context/runtime_preset_layers.pack` zawiera identyfikatory/bindings i
historyczną kotwicę identyfikacji, bez drugiej tabeli wartości. Osadzenie generuje
`python3 loom/src/context/gen_runtime_presets.py`; `--check` jest zielone.
R42: w kodzie pozostają operacje, klucze kontraktów i diagnostyka. Zachowano nawet
dotychczasowe przypadkowe dopasowania nazw modeli; poprawa klasyfikacji to osobny krok.

Kontrakt wejść i integracji: [RUNTIME_PRESETS.md](../../loom/src/context/RUNTIME_PRESETS.md).
Config `context_goal_cues`, scope `goal_typing.goal_cues` oraz Config `chat_reasoning`
przyjmują pełne `effective_values` lub bound `layer_snapshot`. Brak merge/fallback
usuniętych pól. Snapshot reasoning jest zamrożony przed transportem, inspekcja
zostaje w metadanych także przed błędem transportu. Zmiana wartości/rewizji celu
zmienia hash i Goal/Context ID; bieżący builtin zachowuje historyczne JSON/ID.

R40: wspólne warstwy/wykluczenia obsługuje rzeczywiste API 11/12. Na tej bazie
nie są zintegrowane, więc żądanie warstw zwraca jawne `Unavailable`. Nie dodano
drugiego loadera, resolvera, walidatora schematów ani pisarza stanu. Bez W11
natywne sprawdzenie dotyczy faktycznie konsumowanych pól; `builtin_comparison=canonical_json`
ignoruje kolejność kluczy, lecz rozróżnia reprezentacje liczb. Z W11 inspekcja
i dokładne porównanie wartości pochodzą z jego fabryki.

## Bramki na końcowym kodzie

- Pełny build GCC Debug `-O0 -g0`, WERROR, assertions, SQLite vendored: **PASS**.
- Pełny CTest: **115/115 PASS (213,55 s)**; niezmieniony guard W8 `fedaf6b`: **PASS, 114 wykonanych zestawów**.
  **710** natywnych przypadków / **26 399** asercji,
  **1321** przypadków Python / **0** skipów. Istniejący opt-in
  `unit.test_catalog_scale` ma jawnie 0/0; nie przedstawiamy go jako wykonanego.
- Domyślna zgodność: **1248/1248** payloadów i **26/26** wyników celów,
  pełne pliki przed/po zgodne. To rzeczywiste API i zamrożony rdzeń, nie kopia algorytmu.
- Konfiguracja celów: checkpoint **51 wykonań / 132 asercje**, source `c0e6d0ba`
  przed końcową zmianą metadanych adaptera; konfiguracja/scope, błędy, wagi, IDs
  oraz rzeczywiste TU zmieniające builtin wartość i rewizję. Nie deklarujemy
  ponowienia tych 51 na ostatnim adapterze. Końcowe 26 domyślnych wyników oraz
  7/109 testów adaptera potwierdzono osobno; źródło konsumenta i wartości nie zmieniły się.
- Reasoning: **42/42 PASS**, **2** zmierzone fake HTTP, 0 zewnętrznych/paid calls.
- Adapter: native **7/109**, stare rzeczywiste W11/W12 **9/162**, najnowsze
  W11 `22873e0` + W12 `5115477` **9/168**; generator→C++→JSON **13/13** tekstów.
  Pełna inspekcja fabryki W11 sprawdzona, także przy zmianie kolejności kluczy
  i liczbach >2^53. To izolowany link rzeczywistych zależności, nie integracja gałęzi.

Pełne wybrane logi, wejścia, źródła fixture, piny, komendy i wyniki:
[manifest](chat-selector-2-2026-10-05-evidence/final/manifest.json) oraz
[verification.tar.gz](chat-selector-2-2026-10-05-evidence/final/verification.tar.gz).
Raw JUnit i guard są również dostępne osobno. Prywatne fixture nie są wliczane
ponownie do liczników CTest. Build web/Clang/ASan i executable servera nie są
deklarowane; mixed gates po scaleniu należą do 9/8. Tylko dane publiczne/syntetyczne.

## Negatywy i ograniczenia środowiska

Pełne wcześniejsze compile negatives zachowane na archive; późniejsze **41/42**
(błędna kolejność oczekiwanego JSON w fixture), błędny layout checkera oraz kolizja
starego/new Ninja wraz z dokładnym ledgerem i niezależnym parser replay:
[archiwum dbc4dd8](https://github.com/klb-t/chatadhd/tree/dbc4dd8deaf8a434aa660c98ea9bef57c636578f/docs/reports/chat-selector-2-2026-10-05-negative).
Nie poprawiano produkcji dla błędnego oczekiwania ani nie luzowano checkera.
Stary proces nie został zatrzymany przez próbę kill w innej przestrzeni PID;
skończył naturalnie. Końcowy build miał jednego pisarza. Z powodu pełnego dysku
przeniesiono wyłącznie własne artefakty do tmpfs i usunięto już włączone do
archiwum obiekty po zapisaniu hashów. Kompletnego pierwszego logu OOM nie zachowano;
nie deklarujemy jego odtwarzalności. Źródła i testy nie zostały usunięte.
Dodatkowy build na wyprzedzającej bazie integratora `4887eb0` zatrzymano
świadomie Ctrl-C (exit 130), gdy zamknięto przyrost na aktualnym main o źródłach
identycznych z zielonym dowodem. Pełny częściowy log i dokładny source:
[da415cc](https://github.com/klb-t/chatadhd/tree/da415cc7a802cd6e1af9a6382c2d8f708f40db9d/docs/reports/chat-selector-2-2026-10-05-negative/cancelled-integrator-build).
Nie jest to ukończona ani zielona bramka.

Pełne duże mapy linkera zachowane lossless poza wybranym przyrostem:
[positive-artifacts](https://github.com/klb-t/chatadhd/tree/archive/gpt/chat-selector-2-positive-artifacts-2026-10-05/docs/reports/chat-selector-2-2026-10-05-artifacts).

## Niedokończone i punkt wznowienia

Nie zaczynać od powtórnej migracji tych pięciu grup. Po fetch sprawdzić nowy
`INDEX.md`, odebrać ten commit i uruchomić jego replay na mixed source. Następny
przyrost zacząć od pack/profile defaults `context/method_registry.cpp` i
pozostałych DIC-0332–0335/0338–0367; rozdzielić wiring publiczne od owned silnika.
Oddzielnie pozostały legacy transport przez usage guard, trwały resume oraz
pełne włączenie przepisów do wersji metod i grafowych śladów. Nie deklarujemy
ukończenia wszystkich 51 grup ani pełnego UI/R39–R41. Wspólny kontrakt 3/4
[METHOD_GRAPH.md](../../loom/src/packet/METHOD_GRAPH.md) pozostaje bez zmian.

## Do wątku N

- **9:** przyjąć wyłącznie wskazany przyrost; świeży fetch/rebase i mixed gates,
  zachować kolejkę 11/12 i wykonać testy adaptera po ich integracji. Nie przenosić
  negatywów/dużych map do main. Dopisać gotowy commit i pozostałe zadania w INDEX.
- **11:** włączyć dwa kanoniczne deskriptory do zwykłej regeneracji
  `gen_runtime_profiles.py` / publicznego katalogu; bridge korzysta z
  `from_definition/with_values`, nie tworzy własnego subsystemu profili.
- **12/10:** import wygenerowanego wkładu 13 wpisów do istniejącego packa,
  z zachowaniem `pack_id`, stabilnych IDs i CAS. UI/HTTP przekazuje bound
  pack/state/bindings lub pełne efektywne wartości; preview przez `read`, nie `open`.
  Nie odtwarzać wykluczonej wartości z presetu.
- **12:** niezależny regression do source `3c0bc36` (`store.cpp:75–81,330–335,439–442`):
  disable `onboarding.settings`, potem privacy override wildcard `store=false`.
  Zbiorczy defaults może zwrócić Unavailable, zostawiając stare privacy w
  `state.profile`, z którego czyta policy gate. To wniosek z kodu, nie wykonany
  wynik; naprawa poza zakresem 3.
- **3/4/11:** settings default entry nie jest przebiegiem metody. Dalsze receptury
  jako wersje metod i krawędzie śladu według istniejącego METHOD_GRAPH.
- **3:** runner zależności powinien jednorazowo rozwiązać ruchome refs do SHA
  przed probing/extract. Zapisane próby używały już niezmiennych SHA; tej dodatkowej
  poprawki nie zaczęto przy zamknięciu.
- **3/9/8:** dalsze legacy guard/resume i publiczne ABI; prywatne replay fixture
  uruchamiać według manifestu, ewentualną rejestrację w CMake/CI robi ich właściciel.
