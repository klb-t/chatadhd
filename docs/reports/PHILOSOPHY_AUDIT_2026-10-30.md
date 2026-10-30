# Audyt zgodności kodu z filozofią właściciela — 2026-10-30

Podstawa: gałąź main, commit 2d39b07c (worktree `/home/kielbowicz_marcin/wt/philo`).
Audyt czytał WYŁĄCZNIE kod (grep i odczyt fragmentów). Niczego nie uruchamiano, nie budowano, nie zmieniano.
Pominięto 86 ścieżek z `/home/kielbowicz_marcin/wt/philo_skip.txt` (pliki zmieniane przez działające pakiety).
Ważne: pominięty `loom/server/src/app.cpp` i `loom/data/runtime/server_accounts.pack` to dokładnie miejsce, gdzie żyje `guests.model_sources` — ten przykład z zadania nie był tu weryfikowany ponownie.

Słowniczek (raz): "preset" = wartość domyślna zapisana jako dana, którą użytkownik może zmienić; "advisory" = nieblokująca uwaga z pytaniem "czy to zamierzone"; "pakiet danych" = pliki `.pack` w `loom/data`.

## Podsumowanie (liczby)

| Wzorzec | Liczba |
|---|---|
| A. Tekst widoczny dla człowieka w kodzie (pliki) | 18 plików .tsx (ok. 450 miejsc) + 3 miejsca w C++ |
| B. Ciche przejście na droższą ścieżkę | 0 potwierdzonych (zob. uwaga) |
| C. Ustawienie zapisane/wystawione, a nikt go nie czyta | 2 (1 potwierdzone, 1 niepotwierdzone) + 1 stan bez wyświetlenia |
| D. Wybór zaszyty w kodzie, powinien być presetem w danych | 4 |
| E. Zamknięta lista bez zapisanego powodu | 3 |
| F. Stan bez wyjścia | 0 potwierdzonych |
| G. Ciche `catch` ukrywające błąd | 5 potwierdzonych (+ ok. 40 w web opisanych komentarzem, uznane za dopuszczalne) |
| H. Test sprawdzający "coś jest", nie "to jest właściwe" | 2 |

Uwaga do B: w obszarze czytanym kolejność źródeł gości (`server/src/provider_runtime.h:12`) jest jawna ("gość dostaje następne źródło i jest o tym poinformowany"), a progi partii (`batch_threshold`) idą z danych. Nie znalazłem cichego przejścia na płatne źródło; sam logiki w `app.cpp` (pominięty) nie czytałem.
Uwaga do F: pętle odświeżania (`LogPanel.tsx:21`, `SemanticStatus.tsx:63`, `SemanticDot.tsx:19`) mają `clearInterval` w sprzątaniu — nie znalazłem trybu bez wyjścia. `GuestLayer.tsx` i `useCoupling.tsx` (przywracanie) są na liście pominiętych.

## A. Tekst w kodzie zamiast w danych

Frontend (.tsx) — jedna linia na plik, liczba przybliżona (heurystyka: napisy między znacznikami oraz atrybuty title/label/placeholder/aria-label). Osobny pakiet językowy ma je przenieść. Nie ma tam żadnego mechanizmu tłumaczeń używanego przez te pliki (wyjątek: część plików powłoki używa `t("...")` i katalogu danych — te są poprawne). Pewność: zweryfikowane grepem + odczytem próbek.

- `loom/web/src/components/KnowledgeWorkbench.tsx` ok. 106 — np. l.42 `title="Confidence reported by the engine; not a guarantee"`, l.105 `Refresh`.
- `loom/web/src/components/MethodsPanel.tsx` ok. 49 — l.155 `<h2>Graph methods</h2>`, l.165 `Library receipt IDs (JSON)`.
- `loom/web/src/components/UsagePolicyPanel.tsx` ok. 42 — l.29 `No resource diagnostics returned.`
- `loom/web/src/components/AnalysisPanel.tsx` ok. 39 — l.194 `aria-label="Analysis workbench"`, l.202 `Prompt contract JSON: ...`.
- `loom/web/src/components/GraphReplyWorkbench.tsx` ok. 32 — l.144 `aria-label="Graph reply workbench"`, l.154 `Original response`.
- `loom/web/src/components/OperationsPanel.tsx` ok. 28 — l.98 `aria-label="Artifact browser"`, l.107 `Listing reads metadata. Content is loaded only when you choose Read content.`
- `loom/web/src/components/ApplicationProfiles.tsx` ok. 27 — l.226 `aria-label="Application profile views"`.
- `loom/web/src/components/ContextPlanEditor.tsx` ok. 21 — l.32 `Thesis anchors, reach and budget`.
- `loom/web/src/components/SettingsPanel.tsx` ok. 19 — l.142 `Configuration could not be loaded: ...`, l.225 `Expert configuration`.
- `loom/web/src/graph/GraphChatSettings.tsx` ok. 18; `ImportedMessageContent.tsx` ok. 12; `WorkflowRecoveryPanel.tsx` ok. 11 (l.62 `Session restored without dispatching or retrying an operation.`); `UserProfilePanel.tsx` 8; `ConversationBranches.tsx` 6; `ResourcePresetEditor.tsx`, `MemoryPanel.tsx`, `ContextSlider.tsx`, `ChatView.tsx` po 5.
  Także `web/src/components/ImportPanel.tsx:10` (`const WORDS = {...}` — słowa w kodzie) i `web/src/context/semantic-queue.ts` (część zrobiona poprawnie przez katalog, ale `SemanticStatus.tsx:148` ma awaryjne `"retry"`).
  Koszt: duży łącznie (pakiet językowy), mały na plik.

C++ (komunikaty użytkownika w kodzie):
- `loom/src/core/config.cpp:~203` — `system_prompt` "You are a helpful assistant with access to the user's hierarchical memory." jest tekstem wysyłanym do modelu, zaszytym w kodzie. Łamie: tekst/preset w danych. Zweryfikowane. Kierunek: przenieść do pakietu danych jako preset (uwaga: niezmiennik zgodności z Pythonem w CLAUDE.md). Koszt: mały.
- `loom/src/providers/providers.cpp:67-135` — `display_name` ("OpenRouter", "Local model (this device)", ...) i cały manifest dostawców jako stały napis JSON w kodzie (komentarz sam nazywa to "policy data"). Zweryfikowane. Kierunek: plik `.pack` w `loom/data`. Koszt: średni.
- `loom/src/chat/chat_engine.cpp:1279` — znaczniki `--- FILE: ... ---` / `--- END FILE ---` wkładane do prompta zaszyte w kodzie (to nie UI, ale treść wysyłana modelowi bez możliwości zmiany). Zweryfikowane. Koszt: mały.

## C. Ustawienie nic nie robi / stan niewidoczny

1. `loom/src/core/config.cpp` (`loom_config_defaults`) + `loom/data/runtime/settings_fields.pack:4816` — klucz `perspective_composition` (sectors / side_by_side) ma preset i opis w ustawieniach, ale w `loom/src`, `loom/server`, `loom/cli`, `loom/web/src` nie ma żadnego odczytu klucza (`grep` zwraca tylko plik ustawień). Człowiek może go przestawić i nic się nie zmieni. Łamie: "nic nie jest ukryte / ustawienie ma działać". Pewność: zweryfikowane grepem (odczyt mógłby być w pominiętych plikach, np. `projection.cpp`, `graph-projection.ts` — NIE sprawdzone, więc: weryfikowane z zastrzeżeniem). Kierunek: podłączyć do widoku grafu albo ukryć/oznaczyć jako niedostępne. Koszt: średni.
2. `topbar_model_line` (`settings_fields.pack:5027`, `config.cpp`) — w `loom/web/src` występuje tylko w komentarzach (`ChatView.tsx:318`, `shell/model-chip.ts:24`), nie znalazłem odczytu wartości chip/inne. Pewność: niepotwierdzone (odczyt mógł być pod inną nazwą zmiennej). Kierunek: sprawdzić i podłączyć. Koszt: mały.
3. `loom/src/semantic/semantic_llm.cpp:83-87` + `loom/include/loom/semantic_llm.h:55,79` — po 5 kolejnych błędach analiza semantyczna LLM sama się wyłącza, a jedyny ślad to wpis w logu (`log::warn`); metoda `disabled_by_errors()` nie ma żadnego użytkownika poza własnym nagłówkiem (grep po `src`, `server`, `web/src`). Człowiek nie dowie się, że analiza stoi. Dodatkowo próg 5 jest stałą w kodzie (`kMaxConsecutiveFailures`). Łamie: "nic nie dzieje się po cichu", preset w danych. Zweryfikowane. Kierunek: pokazać stan w statusie semantycznym i uczynić próg ustawieniem. Koszt: mały–średni.

## D. Wybór zaszyty w kodzie

- `loom/src/chat/chat_engine.cpp:169,1278` — `kMaxFileChars = 15000`: załączony plik tekstowy jest po cichu obcinany do 15 000 znaków przed wysłaniem do modelu, bez znacznika "obcięto" i bez ustawienia. Wymyślone ograniczenie + ukryte. Zweryfikowane. Kierunek: ustawienie (0 = bez limitu) + jawny znacznik w prompcie i w paragonie kontekstu. Koszt: mały. (Ważne — wpływa na wynik i koszt.)
- `loom/src/chat/chat_engine.cpp:162-168` — lista rozszerzeń plików tekstowych (`text_exts()`) zaszyta; plik o innym rozszerzeniu jest odrzucany (l.1239). Bez powodu zewnętrznego (to nie protokół). Zweryfikowane. Kierunek: lista w danych albo wykrywanie po treści. Koszt: mały.
- `loom/src/catalog/profile.cpp:97-98` — `kMaxFiles = 4000` i lista rozszerzeń repozytorium; po cichu ucina skan repozytorium (komentarz: "capped so a huge repository cannot blow up"). Zweryfikowane. Kierunek: ustawienie + advisory przy przycięciu. Koszt: mały.
- `loom/src/core/config.cpp:~198-210` — domyślny dostawca/model (`https://openrouter.ai/api/v1`, `anthropic/claude-sonnet-4-20250514`, `temperature`, `max_tokens`) jako stałe w kodzie. To presety (OK co do wartości), ale nie są danymi i domyślnie kierują ruch do płatnego źródła. Zweryfikowane. Kierunek: preset w pakiecie danych z widocznym "skąd to wzięło się". Koszt: średni (zgodność z Pythonem).
- Stałe czasowe w web (`LogPanel.tsx:4` POLL_MS=3000, `SemanticStatus.tsx:11` POLL_MS=4000, `SemanticDot.tsx:19` 4000 zaszyte w wywołaniu, `GuestLayer.tsx:22`, `client-session.ts:42`) — część nazwana "PRESET", ale nie ustawialna. Niepewne czy to naruszenie (to szczegóły techniczne) — drobne. Koszt: mały.

## E. Zamknięta lista bez zapisanego powodu

- `loom/web/src/context/semantic-queue.ts:8` — `RETRY_MODES = ["explicit_requeue", "automatic"]` ("values the worker accepts") i filtr w l.22: nieznana wartość z jądra jest zamieniana na `null` i znika z widoku. Komentarz podaje tylko "co robi", nie powód zamknięcia (nie jest to protokół/format/prawo). Zweryfikowane. Kierunek: lista z jądra (`semantic_worker.cpp:536` zna wartości). Koszt: mały.
- `loom/include/loom/model.h` — ok. 30 `enum class` (m.in. `Medium { Text, Code, Audio, Video, Image, Structured }`, `ObservationKind`, `RefKind`, `Verdict`). Nagłówek (l.8-15) deklaruje zasadę "zbiory zamknięte w enumie, otwarte w danych", ale powodu przy konkretnych enumach nie ma — np. nowy rodzaj nośnika (3D, dane pomiarowe) wymaga zmiany modelu. Zweryfikowane co do braku uzasadnienia przy enumach; czy każdy powinien być otwarty — do decyzji. Kierunek: przy każdym zamkniętym zbiorze zapis powodu i daty, a `Medium`/`ObservationKind` rozważyć jako dane. Koszt: duży (dotyka modelu) — najpierw sam zapis powodów, mały.
- `loom/web/src/shell/menu.ts:16` (`SHEETS`), `loom/web/src/shell/panels.ts:3` (`SHELL_PANELS`; plik na liście pominiętych), `web/src/components/ViewEnginePanel.tsx:24` (`INPUT_DEVICES = ["keyboard","touch"]`), `web/src/shell/windows/desktop-icons.ts:14` — zamknięte listy w kodzie bez zapisanego powodu. Niezweryfikowane co do wagi (mogą być wymuszone przez wiązanie z kodem). Koszt: mały.

## G. Ciche przechwycenie błędu

- `loom/web/src/components/LogPanel.tsx:15` — `.catch(() => {})`: gdy pobranie logów zawodzi, panel logów (narzędzie do diagnozy) po prostu stoi na starych liniach, bez informacji. Zweryfikowane. Kierunek: pokazać błąd w panelu. Koszt: mały.
- `loom/web/src/shell/RequestInspector.tsx:24` — `.catch(() => undefined)` przy odczycie konfiguracji: ustawienie `request_inspector_show_setting_values` po cichu wraca do domyślnego, jeśli odczyt zawiedzie. Zweryfikowane. Koszt: mały.
- `loom/web/src/components/SettingsHuman.tsx:57` — `getSettingsFields().catch(() => null)`: przy błędzie zamiast etykiet widać identyfikatory (komentarz to przyznaje), bez żadnej uwagi dla człowieka. Zweryfikowane. Koszt: mały.
- `loom/web/src/context/guest-store.ts:158-167` (`wipeEverything`, "Clear everything from this browser") — każdy z 5 kroków (usunięcie magazynu, OPFS, localStorage, sessionStorage, wylogowanie) ma pusty `catch`; funkcja nie zwraca informacji, co się NIE udało usunąć. Człowiek klika "wyczyść wszystko" i dostaje sukces nawet gdy dane zostały. Najpoważniejsze z tej grupy (prywatność). Zweryfikowane. Kierunek: zwrócić listę kroków, które się nie udały, i pokazać ją. Koszt: mały.
- `loom/src/chat/chat_engine.cpp:1272-1275` — nieudany odczyt załączonego pliku tekstowego: `log::error(...); continue;` — wiadomość idzie do modelu BEZ pliku, a człowiek nie dostaje żadnej wiadomości (to płatne wywołanie z brakującą treścią). Zweryfikowane. Kierunek: zwrócić błąd lub jawne ostrzeżenie w odpowiedzi. Koszt: mały.
- Drobne, dopuszczalne: ok. 40 `catch { /* powód */ }` w web (np. `history.ts`, `client-session.ts`, `start-view.ts`) mają komentarz z powodem i dotyczą niedostępnego magazynu przeglądarki — nie zgłaszam. W C++ pary `catch (...) {}` w `chat_engine.cpp:210`, `use_model.cpp:467` (sprzątanie w destruktorze) oraz `src/catalog/query.cpp:427` (pusty `catch (const Json::exception&) {}`, nie zbadany — niezweryfikowane).

## H. Testy "coś jest" zamiast "to właściwe"

- `loom/web/tests/conversation-view/send-queue.test.mjs:75`, `client-device.test.mjs:62,372` — `assert.ok(a && b && c)` sprawdza tylko, że klucze tłumaczeń są niepuste; nie sprawdza, że teksty są właściwe/sensowne (częściowo ratuje `deepEqual` kluczy w l.60). Zweryfikowane. Koszt: mały.
- `loom/tests/test_capi.cpp:280-281` — `CHECK(logs.is_array()); CHECK(!logs.empty());` dla `loom_get_logs`: nie sprawdza, że w logu jest ten wpis, który został wywołany ("Deleted conversation"). Zweryfikowane. Koszt: mały.
- (Przeszukano tylko wzorce `assert.ok` i `CHECK(...is_object/is_array)`; w C++ ok. 68 asercji typu "niepuste/rozmiar > 0" — nie przeglądano ich pojedynczo.)

## Czego nie pokryto (luki, nie "poza zakresem")

- 86 ścieżek z listy pominiętych (m.in. `server/src/app.cpp`, `guest_source.h`, `actions/ops.cpp`, `graph/projection.*`, wszystkie `loom/data/runtime/*.pack` z listy, `App.tsx`, `GuestLayer.tsx`, `Desktop.tsx`, `useCoupling.tsx`, `loom-api.ts`) — do audytu po scaleniu. W szczególności: ciche przejście na płatne źródło dla gości i odczyt `guests.model_sources` powinny być sprawdzone właśnie tam.
- `loom/android` (Kotlin) — przeczytane tylko komentarze o `android_back_exit_confirm`; nie sprawdzano tekstów i cichych błędów w `MainActivity.kt`.
- `loom/cli/main.cpp` — policzone grepem, że komunikaty są w kodzie (ok. 16 `Error(Errc...` i teksty użycia), nie przeglądane; to zwykle tekst CLI do przeniesienia razem z pakietem językowym.
- Wszystkie 39 `enum class` poza `model.h`/`analyzer_binding.h` oraz ok. 46 `catch (...)` w C++ — przejrzano tylko kilkanaście miejsc (embedding.cpp, context_candidates.cpp, retrieval.cpp, method_execution.cpp, chat_engine.cpp:2083): te zwracają jawny status błędu, nie zgłaszam.
- `loom/web/e2e` — niemal wszystkie skrypty nieczytane (testy pominiętych funkcji są na liście). `loom/tools`, `loom/deploy` — niczytane.
- Stany bez wyjścia (pętle ponowień, przywracanie): przeczytano tylko wątki odświeżania; logika przywracania gości i okien jest w pominiętych plikach.
- Tekst w plikach `.ts` (nie `.tsx`) i w serwerze (`server/src/*`) — nie liczono.
- Audyt nie sprawdzał prawdziwości komentarzy typu "preset" ani zgodności wartości presetów z `settings_fields.pack`.

Audyt czytał wyłącznie kod (bez uruchamiania czegokolwiek), data: 2026-10-30, baza: 2d39b07c.
