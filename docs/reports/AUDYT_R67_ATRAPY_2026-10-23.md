# Audyt R67 — atrapy, placeholdery, panele deweloperskie w interfejsie (2026-10-23, tylko odczyt, Haiku, 34 wywołania)

Słowa właściciela: `spec:R67` („bez mokapów, placeholderów, fakeów, tymczasowych mostków... tylko pełne funkcjonalności”; w kontekście „wybrać boxy”).
Drzewo: `claude/m1-w1` na 693a8d4f (= main 94872898 + W2b-1). Ścieżki pod `loom/web/src/`. Werdykty czytnika; „niesprawdzone” = tylko grep.

## Które „boxy” mógł zobaczyć
- Zwykły czat (układ „czat”) **nie ma** natywnych pól wyboru w obszarze kontekstu. Blok z checkboxami to Warsztat (`ChatView.tsx:1399-1450`, `!chat`): „Active memory / Legacy graph memory / Conversation history / Knowledge selection / Follow recorded counter-evidence links / Lexical shadow diagnostic” + `ContextPlanEditor.tsx` („Explicit retrieval plan”, Plan ID, Thesis ID, Entity anchors, Source reference (optional JSON)). Ostatni układ jest przywracany przy starcie (`start-view.ts:49`) — jeśli telefon ostatnio był w Warsztacie, to to widział.
- W czacie: jedyne natywne checkboxy to blok importowanej wiadomości (`ImportedMessageContent.tsx:136`: Show blocks / Show artifacts / Show references / Wrap code) i przyciski `role=checkbox` perspektyw w nawigatorze (`GraphNavigator.tsx:252,254`, prawdziwe, z jądra). Przełączniki menu „+” to wiersze `menuitemcheckbox` z tekstem Wł./Wył. (bez rysowanego boxu).

## Liczby
30 `type="checkbox"` w `web/src`: 16 tylko Warsztat; 4 w panelach niedostępnych z menu czatu (LogPanel, AnalysisPanel, PerspectivesPanel, ContextSlider); 8 w warsztacie wiedzy (KnowledgeWorkbench — ścieżki serwera wpisywane ręcznie, np. `/data/exports/archive.zip` — deweloperskie); 1 OnboardingPanel (z danych); 1 blok importu (czat).

## Ustawienia ze statusem „planned” widoczne na ekranie Ustawień czatu (`settings_fields.pack`)
| pole | co mówi pack | co robi kod | werdykt |
|---|---|---|---|
| `theme` (:804) | czytnik tylko Kivy | zapis bez skutku w przeglądarce/telefonie | placeholder (uczciwa etykieta) |
| `perspective_composition` (:3881) | sektory nawigatora nie zbudowane | nic nie czyta | placeholder (uczciwa etykieta) |
| `topbar_model_line` (:4092) | „chip modelu nie istnieje” | **FAŁSZ**: `ChatView.tsx:1531-1535` renderuje chip modelu w czacie; ustawienie nic nie robi | placeholder z nieprawdziwym statusem |
| `android_back_exit_confirm` (:4222) | czytnik w MainActivity nie zbudowany | Wstecz zamyka aplikację od razu | placeholder (uczciwa etykieta) |
| `secrets_in_chat` (:4287) | filtr kluczy/haseł w chat.send nie zbudowany | klucz wpisany w czacie idzie do dostawcy bez zmian | placeholder (uczciwa etykieta), **prywatność** |
| `layout = windows` (:873) | planowane, on_set ostrzega | otwiera Warsztat z powodem | uczciwie planowane |

## Kontrolki kontekstu według rodzaju
- Wnioskowane/automatyczne: ContextStrip; podgląd zapytania; „Automatic preview” w ContextSlider; nagrywanie „auto”.
- Modyfikatory względne (czat): Kontekst › szerzej/głębiej/węziej/płycej; kroki w paragonie.
- Bezwzględne (tylko Warsztat/panel kontekstu): budżet tokenów, hops, głębokość w ContextSlider; ContextPlanEditor hops/waga.
- Ręczny wybór elementów (tylko Warsztat): zapytanie selekcji, ID projektu, ID encji, ID biegu, ID tez, „Candidate channels JSON”.
- W czacie są tylko wnioskowane + względne + flagi źródeł. Bezwzględne i ręczne tylko w Warsztacie.

## Angielskie teksty wpisane w kod (górne oszacowania z grep)
ChatView 52, KnowledgeWorkbench 82, MethodsPanel 41, UsagePolicyPanel 45, AnalysisPanel 40, GraphReplyWorkbench 34, OperationsPanel 30, ApplicationProfiles 28, ContextPlanEditor 21, SettingsPanel 18, WorkflowRecoveryPanel 13, ImportedMessageContent 11, UserProfilePanel 9, ConversationBranches 7, MemoryPanel 7, ContextSlider 6. Zero: ContextReceipt, ContextStrip, ChatShell, GraphNavigator, ModelProvenance, InterviewCards, ActionProposalBlock, RoleCard. W czacie zostały m.in.: „A request continues in another conversation. Stop remains available.” (`ChatView.tsx:1391`), „Save (new version)” / „Cancel” przy edycji (`:1213-1218`), `route_trace` (debug, `:1285`).

## Trzy najgorsze
1. `topbar_model_line`: ustawienie nic nie robi, a status mówi nieprawdę (chip istnieje).
2. `secrets_in_chat`: kontrolka prywatności, która niczego nie chroni, na ekranie, na którym się pisze.
3. Blok kontekstu Warsztatu (`ChatView.tsx:1399-1450` + `ContextPlanEditor.tsx`): angielskie formularze, JSON, surowe ID — najpewniej „te boxy”.

## Co z tego wynika (decyzje koordynatora, do odwrócenia)
- Pakiet R67-fix (web + dane): (a) ustawienia „planned” bez czytnika znikają z ekranu Ustawień do czasu, aż czytnik istnieje, ALBO dostają czytnik (dla `secrets_in_chat` — zrobić naprawdę: filtr przed wysyłką z paragonem; dla `topbar_model_line` — podpiąć do istniejącego chipu i poprawić status; `theme` — podpiąć do webu); (b) blok kontekstu Warsztatu: bezwzględne parametry i ręczny wybór zostają jako PEŁNE funkcje (bezwzględna korekta z pochodzeniem w paragonie), ale po polsku, z danych i bez JSON-ów do ręcznego wpisywania; to, co jest diagnostyką deweloperską (Lexical shadow, Candidate channels JSON, route_trace), przechodzi do panelu „Diagnostyka” oznaczonego jako narzędzie, nie do ekranu osoby; (c) angielskie resztki w czacie → słowa z paczki.
- Wnioskowanie głębokości/szerokości z kontekstu (R67 pkt 2, R69) to osobny pakiet jądra (K3) — czeka na dokończenie wypowiedzi właściciela.
