# IO Matrix / Custom-Keyboard-Pro — audyt filozofii, 2026-10-09

Baza: `main` @ `192f820f2d8c653768237e1bff3a1a6d950d69c0`. Publiczne repo, pełny klon HTTPS. Kod produktu nie został zmieniony.

Wynik: **7 potwierdzonych naruszeń w prześledzonych ścieżkach źródłowych, 2 hipotezy, 2 poprawne mechanizmy i 1 już przyjęty przyrost**. „Potwierdzone” oznacza jednoznaczne sterowanie/wartości w kodzie, nie przeprowadzony test telefonu.

Mianownik: **461 tracked entries**, w tym **233 pliki produktu Kotlin**, **42 pliki danych/zasobów produktu**, **112 testów/fixtures**, **16 tooling/build**, **5 referencyjnych profili**, **5 generated validation evidence**, **4 history**, **44 docs/metadata/other**. Klasyfikacja autora audytu: `file-inventory.json`; klasyfikacja skanera jest odrębna i opisana w `scan/inventory.jsonl`.

Semantycznie przeczytano wskazane zakresy **28/233 plików produktu**, łącznie 31 plików kodu/danych/tooling. To wybór ścieżek, nie certyfikacja całych dotkniętych plików. Zakresy i liczby linii są w `coverage.json`. Skan automatyczny: **252/252 kwalifikowanych plików**, 40530 kandydatów, **nie naruszeń**.

## Tożsamość i historia

`Custom-Keyboard-Pro` to repo aktualnego IO Matrix/iOmatrix; Android IME jest pierwszym hostem. Native Matrix planner przekształceń nie jest tym samym runtime co GoalAgent/GoalSession efektów. Powiązanie z Loom dotyczy opcjonalnej wymiany propozycji, nie automatycznego nadania władzy nad telefonem. ECOSYSTEM określa możliwe połączenia z ChatADHD/AGEDS/WatchDog/LEM/PixelSpace; nie utożsamia repo ani nie narzuca wspólnej bazy.

Każdy z 8 dostępnych branch refs jest przodkiem `main` albo równy mu; `branches.json` zawiera konkretne SHA i ahead/behind. Nie ma nieprzyjętych commitów tych refs. `feat/ai-task-profiles-2026-10-01` ma zero własnych commitów względem main; stare zadanie wiring nie jest nowym backlogiem. B/C w chatadhd sprawdza integrator główny na osobnych SHA; ten raport nie certyfikuje cudzej gałęzi.

## R42: test i sześć wyjątków

Test właściciela: **czy ktoś może chcieć zmienić wartość bez zmiany algorytmu?** Gdy tak, to dane/polityka. Nie usuwamy każdego literału.

| Wyjątek | Rozstrzygnięcie w tym audycie |
|---|---|
| 1. Kontrakt code↔data | Klucze `max_tokens`, `schemaVersion` oraz `format/version` mogą zostać. Endpoint/model/default/label obok nich nie dziedziczą wyjątku. |
| 2. Zewnętrzne standardy | MIME/HTTP i sygnatury `MThd`, PNG/RIFF w Model.sniff są mechanizmem formatów. Regex domenowy albo priorytet formatu nie jest automatycznie standardem. |
| 3. Słownik mechanizmu | GoalSession lifecycle states i opakowanie review/ticket są mechanizmem; ich widoczne komunikaty wymagają danych/tłumaczeń. |
| 4. Serializacja kontraktowa | Wyłącznie format faktycznie wymagany przez kontrakt. Format `%.2f` w widoku confidence jest prezentacją, nie uzasadnia globalnego allowlist. |
| 5. Minimalny bootstrap | Nazwa assetu `providers.json` służy lokalizacji danych. Cała lista LEGACY z adresami i modelami nie jest bootstrapem. |
| 6. Diagnostyka | Wewnętrzne logi mogą pozostać; tekst przekazywany do `notice`/InfoRow użytkownikowi to UI, nie log. |

## Wymagania i przepływy

`requirements.json` mapuje R15/R33/R40/R41/R42 oraz Basic/Expert/wiring; `flow-map.json` przedstawia data → graph → runtime → UI dla 9 rodzin. Rozszerzenie pokrycia grafu nie oznacza nowej centralnej architektury ani ujawnienia sekretów w grafie. Deskryptory mogą odwoływać się do danych lokalnych.

## CKP-A-001 — Wbudowane zadania AI pozostają literałami i wygrywają z personalnym zadaniem o tym samym ID

**naruszenie** · `app/src/main/java/com/example/core/ai/AiTasks.kt:28–177` @ `192f820f2d8c`.

**Dowód i zachowanie:** Dla custom task id="fix" lista zawiera najpierw wbudowany fix; byId wybiera wbudowany prompt. parseCustom łapie każdy błąd i zwraca pustą listę. writeCustom pomija builtIn. Brak stanu disabled/excluded dla wbudowanego zadania w tym modelu.

**Wpływ:** JSON umożliwia dodawanie zadań, lecz nie nadpisanie zachowania wbudowanego ID, trwałe wyłączenie ani wykluczenie przy aktualizacji. Nie jest to brak podłączenia custom tasks w ogóle.

**Źródło wymagania:** OWNER_REQUIREMENTS R40, R41, R42; bieżący prompt: alternatywy/profili i domyślne ≠ ograniczenie; AGENTS.md: choice as instance of shared model.

**Interpretacja:** Interpretacja audytora: ponieważ byId wybiera pierwsze dopasowanie po połączeniu BUILT_IN + custom, edytowalne dane użytkownika nie mogą przesłonić wbudowanego zadania o identycznym ID. To częściowe naruszenie warstwowania R40, a nie twierdzenie, że custom tasks nie działają.

**Alternatywy:** osobne custom ID — implemented; override built-in ID — shadowed by first entry; disable/exclude built-in persistently — not represented in AiTask.

**Dane/graf i konsument:** wersjonowany TaskRecipe + PromptTemplate; warstwy default/user + exclusion tombstone; effective task ID/version/hash → app/src/main/java/com/example/core/ai/AiTasks.kt:all/byId; app/src/main/java/com/example/ime/CustomKeyboardIme.kt:runAiTask; app/src/main/java/com/example/ui/kb/Panels.kt:AiPanel.

**Akceptacja:** Utwórz custom id=fix o odmiennym promptcie i sprawdź request body; ma użyć wybranej warstwy. Wyklucz fix, zaktualizuj pack i potwierdź brak przywrócenia. Błędny custom JSON ma dać jawny błąd, nie pusty sukces.

**Rekomendacja:** Rekomendacja audytora: przenieść wbudowane receptury do danych i podłączyć jeden resolver warstw oraz wykluczeń do byId i UI; zachować dotychczasowe ID i obecny preset.

**Ryzyko migracji:** Zachować istniejące ID i kolejność UI; migracja kolizji wymaga jawnej reguły i zapisania skutecznego źródła.

**R42:** To polityka lub edytowalne dane; zmiana nie wymaga zmiany algorytmu.

## CKP-A-002 — Uszkodzony katalog providerów prowadzi do ręcznie zaszytych endpointów i modeli

**naruszenie** · `app/src/main/java/com/example/core/discovery/ProviderCatalog.kt:246–285` @ `192f820f2d8c`.

**Dowód i zachowanie:** parseList zwraca emptyList po błędzie. all dodaje LEGACY po custom/fetched/bundled. AiConfig dodatkowo pobiera fallback model/base URL z AiProviders. Przy niepoprawnym katalogu rozpoznany legacy ID nadal może dać używalny config, o ile jest klucz/model.

**Wpływ:** Awaria lub wykluczenie danych nie zatrzymuje rozstrzygania endpoint/model; faktyczny wybór może pochodzić z kodu. Nie twierdzę, że sama awaria uruchamia request: potrzebny jest konsument inicjujący wywołanie.

**Źródło wymagania:** R42: brak/uszkodzony pack → jawny błąd lub embedded pack generowany z danych; R40: trwałe wykluczenie; AGENTS.md: endpoints/model defaults as catalogue/profile data.

**Interpretacja:** Interpretacja audytora: compatibility fallback przechowuje zmienne dane providerów w kodzie i może działać po błędzie katalogu. Uzasadnienie kompatybilnością nie spełnia wyjątku minimalnego bootstrapu R42; z samego fallbacku nie wynika samoczynna wysyłka.

**Alternatywy:** custom > fetched > bundled — implemented, hard-coded precedence; legacy compatibility preset as data — not implemented; error vs verified embedded snapshot — no explicit policy in read path.

**Dane/graf i konsument:** ProviderCatalogue version; compatibility preset generated from same data; failure policy + exclusions + resolution provenance → app/src/main/java/com/example/core/discovery/ProviderCatalog.kt:all/byId; app/src/main/java/com/example/core/ai/AiClient.kt:AiConfig.from/effectiveBaseUrl.

**Akceptacja:** Podmień bundled katalog na malformed JSON: bez jawnego fallback preset nie może powstać gotowa konfiguracja do sieci. Zmiana default endpoint/model w danych zmienia request bez modyfikacji Kotlin. Wyklucz provider i sprawdź, że LEGACY go nie wskrzesza.

**Rekomendacja:** Rekomendacja audytora: zapisać compatibility provider preset w danych lub generowanym embedded packu, jawnie określić obsługę błędu katalogu i podłączyć wykluczenia do wszystkich fallbacków; utrzymać wiązanie klucza do providera.

**Ryzyko migracji:** Stare instalacje muszą dostać jawny compatibility preset; nie usuwać wsparcia legacy ani wiązania klucza do providera.

**R42:** To polityka lub edytowalne dane; zmiana nie wymaga zmiany algorytmu.

## CKP-A-003 — Policy Matrix nie dociera w całości do runtime: zaszyte wagi, koszt braku danych i automatyczne plan.best

**naruszenie** · `app/src/main/java/com/example/core/matrix/Planner.kt:4–36` @ `192f820f2d8c`.

**Dowód i zachowanie:** ConvertRunner ustawia tylko allowLeavingDevice, through i avoid. Weights(), maxSteps=5, kara rank*2 i traktowanie kosztu null jako MEDIUM pozostają efektywnymi decyzjami w Kotlinie. Runner wybiera plan.best; inne trasy są komunikatem, w Expert tylko dwa skróty. Użytkownik może wymusić use/avoid, więc alternatywy nie są całkowicie usunięte.

**Wpływ:** Wynik rankingu, budżet szukania i znaczenie nieznanych kosztów są wyborem implementacji bez kontrolki/profilu dla tych pól w ścieżce produkcyjnej.

**Źródło wymagania:** R42: weights/limits as data; R15/R40/R41; AGENTS.md: separate mechanism/policy; unknown cost remains unknown.

**Interpretacja:** Interpretacja audytora: reprezentowalność wag w klasie Policy nie oznacza ich konfigurowalności przez aplikację, skoro badany konsument konstruuje tylko trzy pola. use/avoid zachowują część wyboru tras; stwierdzenie dotyczy wag, braku kosztów i pozostałych parametrów wskazanej ścieżki.

**Alternatywy:** localOnly/use/avoid — wired; weights/maxSteps/unknown-cost policy — engine parameters exist partly; not wired from Settings; explicit route selection — use/avoid constrains; no full route selector in ConvertRunner.

**Dane/graf i konsument:** ConversionPolicy version; cost assessment incl. unknown; route candidates and selected reason; UI presentation policy → app/src/main/java/com/example/core/matrix/Planner.kt:price/search/plan; app/src/main/java/com/example/io/ConvertRunner.kt:convert.

**Akceptacja:** Zmień wyłącznie profil wag i uzyskaj inne uporządkowanie kontrolowanych tras. Przetestuj unknown-cost jako osobne strategie: ask/penalty/interval/deny; zachowaj unknown w śladzie. Pokaż wszystkie trasy w wybranym zakresie; wybór trasy wpływa na execute. Zmień maxSteps w danych i wykryj trasę sześciu kroków.

**Rekomendacja:** Rekomendacja audytora: podłączyć istniejący Policy do ustawień/profili i opisu grafowego, zachować obecny ranking jako preset oraz wystawić uzasadnienie wyboru i stan nieznanego kosztu; nie tworzyć równoległego plannera.

**Ryzyko migracji:** Zmiana wag bez zachowania starego presetu zmieni trasę i możliwy koszt/prywatność; zachować bieżący profil jako kompatybilny.

**R42:** To polityka lub edytowalne dane; zmiana nie wymaga zmiany algorytmu.

## CKP-A-004 — Domyślne ustawienia i metadane UI są generowane z Kotlin, a nie z warstwy danych

**naruszenie** · `app/src/main/java/com/example/core/config/SettingsSchema.kt:223–253` @ `192f820f2d8c`.

**Dowód i zachowanie:** derive bierze istnienie, typ i default z serializacji Settings(). Label/help/range/grupy pochodzą z METADATA Kotlin, Basic keys i scopes z list Kotlin. Generic UI faktycznie zużywa ten opis, lecz poprawienie treści/tłumaczenia/default/range wymaga buildu.

**Wpływ:** Model jest uogólniony na poziomie UI, ale nie spełnia testu R42 dla edytowalnych treści, limitów i polityk. Wyjątek keys/code contract nie rozciąga się na całe wartości METADATA.

**Źródło wymagania:** R42: UI text/defaults/limits are data; R40 default layers; AGENTS.md: expert UI projection of canonical settings.

**Interpretacja:** Interpretacja audytora: generic UI odczytuje metadane, ale źródłem ich wartości pozostaje Kotlin. Istnienie uogólnionego renderera nie spełnia osobnego wymagania R42 dotyczącego zewnętrznych defaultów, etykiet, opisów i zakresów.

**Alternatywy:** generic setting controls — implemented; translated data descriptions/default presets — not authoritative; Basic/Advanced/Expert same persisted values — implemented; Basic selects complete researched workflow — no such bundle in checked settings path.

**Dane/graf i konsument:** SettingDefinition + localized presentation; DefaultLayer + exclusion/provenance; Application/WorkflowPreset → app/src/main/java/com/example/core/config/SettingsSchema.kt:derive; app/src/main/java/com/example/core/config/SettingsStore.kt:read/resetToDefaults; app/src/main/java/com/example/ui/settings/AllSettingsScreen.kt.

**Akceptacja:** Zmień etykietę i default w packu bez kompilacji; generic kontrolka i fresh settings mają tę samą wersję. Przełącz Basic/Expert bez zmiany skutecznych wartości. Wyklucz default, odśwież pack: nie może wrócić.

**Rekomendacja:** Rekomendacja audytora: przenieść zmienne defaulty i metadane prezentacji do wersjonowanych danych; pozostawić w kodzie typy i klucze kontraktu, a obecny SettingsSchema zachować jako konsumenta tego opisu.

**Ryzyko migracji:** Typy mechanizmu i nazwy kontraktu pozostają w kodzie; migracja istniejących wartości nie może zamienić ich na nowe defaulty.

**R42:** Klucze są dozwolone przez R42.1, lecz label/help/default/min/max to zmienne dane. Opis inferKind jako mechanizmu nie legalizuje METADATA.

## CKP-A-005 — Sugestie zawierają niejawne pierwszeństwo źródeł i stratny postprocessing odpowiedzi

**naruszenie** · `app/src/main/java/com/example/core/suggest/SuggestionEngine.kt:131–240` @ `192f820f2d8c`.

**Dowód i zachowanie:** Local scoring używa 2000/1000, bigram×10 oraz rank/1000; first getOrPut ustala reprezentanta duplikatu. AI zawsze trafia na początek niezależnie od score, choć opt-in nie wybierał priorytetu. cleanCompletion usuwa code fence/cudzysłowy/pierwszą linię i tnie do 80 znaków. aiContextChars jest realnym ustawieniem, ale strategia takeLast pozostaje stała.

**Wpływ:** Dwa warianty wyboru/sprzątania tego samego wyniku nie są profilami; brak raportu utraty końcówki i alternatywnej polityki wieloliniowej. To nie zarzut automatycznego wykonania sugestii — ta ścieżka tylko oferuje.

**Źródło wymagania:** R42: weights/thresholds/domain postprocessing as data; R41: analysis recipes/methods; AGENTS.md: explicit information loss/generated and choice as data.

**Interpretacja:** Interpretacja audytora: opt-in do AI uruchamia źródło sugestii, lecz kod dodatkowo ustala jego pierwszeństwo oraz stratne reguły obróbki. Według przywołanego wymagania alternatywy tych reguł powinny być reprezentowalne jako dane; nie przypisuję właścicielowi preferowanej kolejności ani strategii obróbki.

**Alternatywy:** AI off / minChars / debounce / context length — wired; AI priority/dedup representative/postprocess selection — fixed control flow; raw/multiline/quoted continuation — not preserved in suggestion value.

**Dane/graf i konsument:** SuggestionPolicy; ContextSelectionRecipe; PostprocessRecipe + loss record; ranking evidence → app/src/main/java/com/example/core/suggest/SuggestionEngine.kt:collectLocal/mergeAi/cleanCompletion.

**Akceptacja:** Ustaw profile AI-first/local-first/score and verify exact source order for identical candidates. Dla wieloliniowego raw wyniku przechowaj raw plus observed loss i sprawdź profile keep/truncate. Kolizja text z kilku źródeł zachowuje provenance wszystkich, a representative wynika z wybranej polityki.

**Rekomendacja:** Rekomendacja audytora: wydzielić wybór priorytetów, deduplikację, selekcję kontekstu i obróbkę odpowiedzi do wersjonowanych polityk, zachowując obecne zachowanie jako preset oraz raportując zmianę/utratę danych.

**Ryzyko migracji:** Domyślny profil ma zachować aktualne wyniki; zmiana kolejności może psuć pamięć ruchową; testować opóźnione wyniki i brak wysyłki z sensitive.

**R42:** To polityka lub edytowalne dane; zmiana nie wymaga zmiany algorytmu.

## CKP-A-006 — Przepływy ustawień, AI i agenta nie mają wspólnego adresowalnego pokrycia w grafie

**naruszenie** · `app/src/main/java/com/example/core/config/SettingsStore.kt:29–38` @ `192f820f2d8c`.

**Dowód i zachowanie:** W sprawdzonych ścieżkach Settings -> encrypted JSON -> StateFlow i AiConfig -> request nie są wiązane z węzłami wersji/proweniencji; GoalAgent przechowuje goal/proposals/session w pamięci obiektu. Matrix ma własny Provenance/Record dla transformacji, lecz to nie pokrywa tych trzech rodzin.

**Wpływ:** Nie można na podstawie tych ścieżek przejść od wykonania AI do dokładnej wersji promptu, efektywnego warstwowanego presetu, wyboru kontekstu i dowodów jako powiązanych obiektów. Nie wnioskuję, że każde repo musi współdzielić jedną bazę ani semantykę grafu.

**Źródło wymagania:** R15/R40/R41 i bieżący audyt: all used data in graph; ECOSYSTEM.md: różne grafy mogą mieć różną semantykę; kierunki nie są historycznym zleceniem.

**Interpretacja:** Interpretacja audytora: to stwierdzenie zakresowe dla prześledzonych ścieżek Settings/AI/Goal, nie dowód globalnej nieobecności grafu w 233 plikach. Istniejąca proweniencja Matrix nie dostarcza w tych ścieżkach powiązań wymaganych przez R15/R40/R41; nie wynika z tego nakaz wspólnej centralnej bazy.

**Alternatywy:** per-conversion Provenance — implemented; graph descriptors of settings/prompts/requests/sessions — absent in traced paths; secret reference in graph without secret bytes — recommended design boundary.

**Dane/graf i konsument:** adresowalne Setting/Task/Prompt/Context/Execution/Evidence, native IO semantics; bezpieczne references do device-bound sekretów; wersjonowane powiązania użycia i generacji → app/src/main/java/com/example/core/config/SettingsStore.kt; app/src/main/java/com/example/core/ai/AiClient.kt; app/src/main/java/com/example/core/assistant/GoalAgent.kt.

**Akceptacja:** Dla kontrolowanej operacji odtwórz z graph refs skuteczne ustawienia, prompt hash, metodę kontekstu, wynik i provenance; bytes sekretu nie trafiają do przenośnego grafu. UI edycja i aplikacyjna/półautomatyczna edycja mają ten sam identyfikator danych. Zachować native Matrix provenance podczas mapowania do graph descriptors.

**Rekomendacja:** Rekomendacja audytora: dodać do istniejących prześledzonych ścieżek adresowalne deskryptory użytych ustawień, recipe, kontekstu i wykonania z wersją/proweniencją; zachować native semantykę Matrix i lokalne referencje do sekretów.

**Ryzyko migracji:** Wymóg audytu stosowany do obecnego repo; nie ustanawia centralnej bazy. Retencja prywatnych tekstów musi respektować politykę, a descriptors mogą wskazywać dane poza grafem.

**R42:** To polityka lub edytowalne dane; zmiana nie wymaga zmiany algorytmu.

## CKP-A-007 — Stały GoalPrompt i ograniczenia przestrzeni planów są częścią kodu

**naruszenie** · `app/src/main/java/com/example/core/assistant/GoalPrompt.kt:5–44` @ `192f820f2d8c`.

**Dowód i zachowanie:** System prompt powstaje z Kotlin raw string i katalogu akcji; plan wymaga 1–4 alternatives. Brak wyboru wariantu promptu przez AiTaskProfiles: tam są tylko maxTokens/temperature. JSON shape keys są kontraktem; tekst polityki, przykłady i limit 4 nie stają się kontraktem przez umieszczenie obok schema.

**Wpływ:** Można zmienić model/temperaturę/tokeny, lecz nie porównać 3/5 wariantów recipe albo zwiększyć liczby proponowanych tras bez zmiany kodu. Limit bezpieczeństwa jest potrzebny, lecz jego aktualna wartość jest polityką zasobu, nie jedyną możliwą definicją algorytmu.

**Źródło wymagania:** R42: prompts/recipes/limits as data; R33/R41: configurable experiment variants, methods; AGENTS.md: any goal and useful alternative routes.

**Interpretacja:** Interpretacja audytora: wersjonowalny prompt i parametry wielkości planu są odrębne od stałych kluczy schematu oraz lokalnej autoryzacji. Wskazane dane pozostają w kodzie mimo możliwości zmiany recipe lub wartości limitu bez zmiany algorytmu.

**Alternatywy:** task maxTokens/temperature — wired; prompt recipe variants — no setting in GoalPrompt; proposal-count policy — fixed 1..4.

**Dane/graf i konsument:** GoalPlanningRecipe version/hash; validated ResourcePolicy; PromptInput catalogue projection → app/src/main/java/com/example/assistant/GoalAssistantActivity.kt:modelPlan; app/src/main/java/com/example/core/assistant/GoalAgent.kt:checkedPlans; app/src/main/java/com/example/core/assistant/GoalPrompt.kt.

**Akceptacja:** Dwa wersjonowane prompty z tym samym schematem dają dwa udokumentowane requesty bez rekompilacji. Uzgodniony limit alternatyw odczytywany przez prompt, parser i GoalAgent; brak rozbieżnych limitów. Prompt nie może zmienić autoryzacji lokalnego executora.

**Rekomendacja:** Rekomendacja audytora: przenieść prompt recipe i zmienne limity do danych, podłączyć tę samą zatwierdzoną politykę do promptu i walidatorów oraz utrzymać autoryzację executora niezależną od tekstu promptu.

**Ryzyko migracji:** Nie zmieniać niezależnych mechanizmów uprawnień i walidacji przez edycję tekstu promptu; contract keys pozostają w kodzie.

**R42:** To polityka lub edytowalne dane; zmiana nie wymaga zmiany algorytmu.

## CKP-A-008 — Parametry AI są rzeczywiście podłączone; stary przyrost ai-task-profiles jest już zintegrowany

**już naprawione** · `app/src/main/java/com/example/core/ai/AiClient.kt:52–70` @ `192f820f2d8c`.

**Dowód i zachowanie:** UI zapisuje aiTaskProfilesJson, resolve czyta go, AiConfig.from wypełnia maxTokens/temperature, request builder wysyła te pola. Goal host wybiera GOAL_PLAN, IME REWRITE, suggestions INLINE. Błąd profilu oznacza configurationProblem, a complete przerywa przed wysyłką.

**Wpływ:** Nie wolno odtworzyć starego backlogu „parametry tasków bez konsumenta”. Część defaults i dopuszczalne zakresy nadal naruszają R42 (CKP-A-004/007); naprawa wiring nie rozwiązała całej eksternalizacji.

**Źródło wymagania:** bieżący audyt: nie kopiować przyjętych przyrostów; R42 i real consumer requirement.

**Interpretacja:** Interpretacja audytora: prześledzony łańcuch UI → resolver → AiConfig → request potwierdza przyjęcie naprawy wiring parametrów tasków. Nie potwierdza pełnej eksternalizacji danych ani wykonania testów Android w tym audycie.

**Alternatywy:** task override — implemented; provider/global inheritance — implemented with source labels; invalid profile blocks request — implemented.

**Dane/graf i konsument:** istniejące aiTaskProfilesJson -> docelowo TaskParameters node → app/src/main/java/com/example/ui/settings/AiTaskProfileSection.kt; app/src/main/java/com/example/core/ai/AiTaskProfiles.kt; app/src/main/java/com/example/core/ai/AiClient.kt.

**Akceptacja:** Ustawić różne maxTokens dla GOAL_PLAN/INLINE i przechwycić obydwa request bodies. Wstrzyknąć invalid profile i potwierdzić zero transport calls.

**Rekomendacja:** Rekomendacja audytora: oznaczyć dawny przyrost wiring jako przyjęty i zachować jego testy przy dalszej migracji defaultów; nie odtwarzać starego zadania jako nowej usterki.

**Ryzyko migracji:** Zachować działające wiring i precedence; ten audyt niezależnie prześledził źródła, nie uruchomił Android testów.

**R42:** Nazwy max_tokens/temperature w adapterze to klucze code/data contract. Ich wartości nie podlegają temu wyjątkowi.

## CKP-A-009 — Lokalna autoryzacja i stan hipotezy nie są uznawane za wykonanie

**dopuszczalny mechanizm** · `app/src/main/java/com/example/core/assistant/GoalAgent.kt:43–106` @ `192f820f2d8c`.

**Dowód i zachowanie:** Speech/text są źródłami goal; model request ma osobną zgodę. finishPlanning przyjmuje tylko bieżącą rewizję/hosta; wybór planu tworzy GoalSession, begin potrzebuje review. USER_CONFIRMED i VERIFIED pozostają oddzielne. Katalog zaznacza runnable per host, a nie „niemożliwe”.

**Wpływ:** To mechanizm kontraktu i autoryzacji, a nie ukryta decyzja wymagająca usunięcia wszystkich if/else. Brak arbitrary-touch adaptera w assistant host nie dowodzi niewykonalności automatyzacji całej platformy.

**Źródło wymagania:** AGENTS.md: observations/user declarations/hypotheses/outcomes distinct; sharing does not transfer authority; R42 wyjątki 1/3; bieżący audyt: odróżnić mechanizm od naruszenia.

**Interpretacja:** Interpretacja audytora: kontrola rewizji, review/ticket i oddzielne stany wyników realizują lokalny mechanizm autoryzacji oraz epistemicznego rozróżnienia. Nie są dodatkowym wyjątkiem od R42 ani dowodem działania wszystkich adapterów; dopuszczenie dotyczy wskazanych nazw kontraktu i słownika mechanizmu.

**Alternatywy:** text/voice/import proposals — same local lifecycle; unimplemented host adapter — explicit missing capability; verified/user-confirmed outcome — distinct states.

**Dane/graf i konsument:** Capability/Review/ExecutionReceipt descriptors; runtime authority stays local → app/src/main/java/com/example/core/assistant/GoalAgent.kt; app/src/main/java/com/example/core/assistant/GoalSession.kt; app/src/main/java/com/example/assistant/GoalAssistantActivity.kt.

**Akceptacja:** Late response after edit/pause nie może zaakceptować ani uruchomić starego planu. Import proponowanego planu nie przywraca review/ticket. USER_CONFIRMED nie jest serializowany jako VERIFIED.

**Rekomendacja:** Rekomendacja audytora: zachować rozdzielenie propozycji, zgody i wyniku oraz lokalne review/ticket podczas eksternalizacji metadanych; pozwolenia nie mogą wynikać z deklaracji w profilu ani odpowiedzi modelu.

**Ryzyko migracji:** Przenoszenie metadata do danych nie może umożliwić deklaracją nadania realnych uprawnień lub wykonania niepodłączonego adaptera.

**R42:** Nazwy stanów lifecycle i operacje begin/review są własnym słownikiem mechanizmu. UI labels/prompt teksty są osobno naruszeniami i nie są objęte wyjątkiem.

## CKP-A-010 — Kaskada ustawień ma realne konsumenty i proweniencję lokalnych nadpisań

**dopuszczalny mechanizm** · `app/src/main/java/com/example/core/config/ScopedSettings.kt:93–158` @ `192f820f2d8c`.

**Dowód i zachowanie:** Resolver stosuje layout/panel/key, zapamiętuje suppressedSources i zwraca snapshot. IME pobiera scoped snapshot; UI edytuje ten sam override i pokazuje effective/source. Poziom UI nie uczestniczy w cascade. EngineRuntime śledzi SettingsStore.state dla Wires/Streams.

**Wpływ:** JSON profili i ustawień nie jest samą dekoracją. Nadal brak pack default/exclusion i graph descriptors (CKP-A-004/006); nie ogłaszam pełnego audytu wszystkich 233 plików/konsumentów.

**Źródło wymagania:** AGENTS.md: same contextual/expert values and real consumer; R40 częściowa zgodność; brak pełnych warstw/exclusions opisany osobno.

**Interpretacja:** Interpretacja audytora: wskazane odczyty snapshotu i zapis override dowodzą rzeczywistego podłączenia kaskady w zbadanych ścieżkach. Nie dowodzą kompletności wszystkich konsumentów, grafowych defaultów ani wykluczeń. Wyjątki R42 dotyczą nazw kontraktu i mechanizmu, nie całych plików ani zmiennych wartości polityki.

**Alternatywy:** CASCADE/LAYOUT_ONLY/DEFAULTS_ONLY — wired; Basic/Expert visibility — separate from precedence; permanent default exclusion — not supplied by this mechanism.

**Dane/graf i konsument:** istniejący SettingsOverrides/ResolvedSettings/provenance → app/src/main/java/com/example/ime/CustomKeyboardIme.kt:keyboardSettings; app/src/main/java/com/example/ui/settings/LayoutInstanceSettings.kt; app/src/main/java/com/example/engine/EngineRuntime.kt.

**Akceptacja:** Zmiana scoped autoCapitalize wpływa tylko na wybraną instancję, a UI source wskazuje tę instancję. Zmiana poziomu UI nie zmienia snapshot. DEFAULTS_ONLY zachowuje overrides jako suppressed, umożliwiając powrót.

**Rekomendacja:** Rekomendacja audytora: zachować istniejący resolver, jego proweniencję i wspólne dane UI/runtime; uzupełniać brakujące warstwy/defaulty bez budowania drugiego mechanizmu kaskady.

**Ryzyko migracji:** Nie zastępować działających resolverów niesprawdzoną równoległą ścieżką.

**R42:** Identyfikatory scope i operacja resolution to mechanizm. Kolejność warstw to jawna semantyka CASCADE; same listy pól/etykiety Kotlin wymagają oddzielnej migracji danych.

## CKP-A-011 — profiles/*.json są walidowanymi opisami, bez znalezionego podłączenia do app runtime

**niepewne** · `profiles/openrouter_inference.json:1–26` @ `192f820f2d8c`.

**Dowód i zachowanie:** Walidator sprawdza obecność 3 pól i kończy sukcesem. Domyślny Android assets to app/src/main/assets; build nie dodaje root profiles. Nie znaleziono konsumenta root profileId/abstractionId w app/src/main.

**Wpływ:** Plik o nazwie openrouter_inference.json nie jest dowodem konfigurowania AiClient. Nie stwierdzam naruszenia bez ustalenia, czy autor traktuje root profiles jako dokumentację czy operacyjne presety.

**Źródło wymagania:** bieżący audyt: existence of JSON ≠ consumer; AGENTS.md: actual consumers.

**Interpretacja:** Interpretacja audytora: brak znalezionego czytnika root profiles w podanym zakresie pozostawia hipotezę brakującego wiring; pliki mogą być referencją lub dokumentacją. Nie traktuję domniemanej roli operacyjnej jako wypowiedzianego wymagania właściciela.

**Alternatywy:** documentation/reference profiles — consistent with observed usage; authoritative runtime profiles — no wiring evidence in audited build/source.

**Dane/graf i konsument:** wyraźny status reference_only albo operacyjny ProviderProfile; consumer registry → validate_profiles.py; production consumer unresolved.

**Akceptacja:** Oznaczyć role pliku; jeśli operacyjny, zmienić pole w profilu i wykazać różnicę request body; jeśli referencyjny, test nie może reklamować runtime coverage.

**Rekomendacja:** Rekomendacja audytora: jawnie oznaczyć te pliki jako referencyjne albo wskazać ich operacyjnego konsumenta i test wpływu zmiany profilu na request; walidację obecności pól opisywać wyłącznie jako strukturalną.

**Ryzyko migracji:** Nie podłączać automatycznie samej dokumentacji do sieci ani nie przenosić credential authority.

**R42:** To dane poza kodem, ale brakuje dowodu konsumenta; R42 sam nie wystarcza.

## CKP-A-012 — Asynchroniczna akcja rewrite nie wiąże wyniku z pierwotnym celem edycji

**niepewne** · `app/src/main/java/com/example/ime/CustomKeyboardIme.kt:2048–2082` @ `192f820f2d8c`.

**Dowód i zachowanie:** runAiTask sprawdza sensitive przed requestem, ale callback onSuccess czyta aktualny EditorController i robi replaceSelectionOrAll/commitText bez generation/current target check. onFinishInputView czyści suggestions/completions/voice, lecz nie ten job; serviceScope żyje do onDestroy. Wskazuje to na możliwość zastosowania starej odpowiedzi do nowego fokusu/tekstu.

**Wpływ:** Hipoteza do testu urządzenia lub test-double: wynik może nadpisać inne pole albo nowszą edycję. Nie wykonano reprodukcji Android, więc nie klasyfikuję jako potwierdzonego efektu użytkowego.

**Źródło wymagania:** AGENTS.md: preserve user target/focus and private input boundary; bieżący audyt: decyzje w kolejności i async.

**Interpretacja:** Interpretacja audytora: brak powiązania callbacku z rewizją/celowym polem stanowi konkretną przesłankę hipotezy stale-result, ale bez testu Android nie potwierdza jej skutku użytkowego. Kategoria pozostaje niepewne.

**Alternatywy:** immediate replace — current; target/revision-bound apply — not visible in callback; preview/explicit acceptance after stale result — not represented here.

**Dane/graf i konsument:** EditRequest target/revision + result application policy; request receipt → app/src/main/java/com/example/ime/CustomKeyboardIme.kt:runAiTask; app/src/main/java/com/example/ime/EditorController.kt:replaceSelectionOrAll.

**Akceptacja:** Fake delayed AiClient: request w polu A, zmiana fokus do B, późna odpowiedź; B musi pozostać bez zmian. Zmiana treści w A podczas requestu: wynik trafia do podglądu lub jest ponownie zatwierdzany. Sensitive state po wysłaniu musi blokować późniejszą automatyczną aplikację.

**Rekomendacja:** Rekomendacja audytora: najpierw odtworzyć opóźnioną odpowiedź przy zmianie pola lub jego treści; dopiero po reprodukcji wiązać aplikację wyniku z celem/rewizją albo kierować stary wynik do jawnego podglądu.

**Ryzyko migracji:** Wiązanie revision musi działać z selection i whole-field bez czytania zawartości sensitive; zachować explicit task action UX.

**R42:** To hipoteza sterowania wynikiem, nie klasyfikacja literału.

## Walidacja, luki i punkt wznowienia

Uruchomiono skaner na niezmiennym SHA oraz `python3 validate_profiles.py`: 3/3 pliki przechodzą tylko kontrolę trzech wymaganych nazw pól. Próba `bash tools/test-goal-core.sh` kończy się przed testami: brak kotlinc/pinned Gradle bundle. Historyczne 974 testy w RESUME nie są wynikiem tego audytu. Nie wykonano żadnego płatnego modelu ani CI.

Najpierw odtworzyć **CKP-A-012** z opóźnioną atrapą odpowiedzi i zmianą fokusu; pozostaje hipotezą. Następnie **CKP-A-002/001** (legacy fallback i warstwy receptur), potem podłączyć istniejący Policy do danych (003) oraz grafowe deskryptory trzech prześledzonych rodzin (006). `backlog.json` zawiera szczegółowe testy i ryzyka.

Pozostała semantyczna powierzchnia: media/vault/sync/clipboard learning, kompletne capture/accessibility/touch, wszystkie źródła/sinki streamów i wszystkie provider transports. Nie wyciągnięto z samego braku adaptera w GoalCatalogue wniosku o niemożliwości automatycznego dotyku w całym iOmatrix. Automatyczny skan obejmuje kwalifikowane źródła, ale nie rozstrzyga osiągalności ani intencji.

Wznowienie: checkout powyższego SHA; przeczytaj `AGENTS.md`, `CLAUDE.md`, `docs/RESUME.md`; użyj `coverage.json` i `file-inventory.json`, wybierz niesprawdzone moduły, a kandydatów z `scan/candidates.jsonl` traktuj tylko jako wskazówki. Nie odtwarzaj przyjętych gałęzi. Raport/skrypty publikuje sesja A na własnej gałęzi; delegowany audytor nie modyfikował main ani produktu.
