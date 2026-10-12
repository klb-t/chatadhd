# B-CH-05 — Stan interfejsu web jako węzły: perspektywy, sesje workflow, filtr widoczności, katalog komunikatów

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/chatadhd` @ `9e20f99a`; E @ `3eaac295`; inwentarz wątku 10 @ `161cc22d` |
| Fala / priorytet | W3 / P2 |
| Ustalenia | CH-008, CH-009, A4-CH-VIEW001, A3-WEB-001, A4-E-001; grupy wątku 10 (81 `open`: DIC-0516…) |
| Niezmienniki EITG | G1, G5, G7, G10 |
| Rozmiar / model | L / Sonnet 5.5 high (TypeScript + C++ server) |
| Zależności | **T3** (panel web widoku wejściowego, `graph_perspective.pack`), B-ECO-02 (deklaracja `localStorage` jako cache) |
| Worktree | `t9-ch-05` |

## Zakres
1. Perspektywa, sesja workflow, checkpoint i receipt są węzłami grafu z ID; `localStorage` jest
   **zadeklarowanym cache** odtwarzalnym po ID (CH-009).
2. Etykiety, wartości domyślne i początkowy układ workspace w danych z wersjami językowymi
   (CH-008) — wzór: `loom/data/onboarding/ui.pack#/locales` (W12).
3. Filtr widoczności węzłów i krawędzi w `GraphView` jest polityką z danych, przełączalną
   (A4-CH-VIEW001); widoczne / wybrane do analizy / dostępne wg uprawnień to trzy różne zakresy
   (`pass4/TASK.md`).
4. Nieznane pole profilu zewnętrznego nie blokuje odczytu znanych (A3-WEB-001; G7).
5. Deklaracja nieużywanego adaptera nie potwierdza zdolności aktywnego planu (A4-E-001).
6. Grupy wątku 10 (serwer HTTP: limity ciała 64 MiB, keep-alive 100, domyślne rozmiary stron,
   mapa błędów → HTTP, MIME eksportów) jako presety transportu; kontrakt DTO bez zmian.

## Pliki
- `loom/web/src/workspace/state.ts:4–40`; `loom/web/src/profiles/workflow-checkpoint.ts:10–47`,
  `workflow-snapshot.ts:7–31`, `profiles/graph.ts:45–92`, `profiles/runtime.ts`;
  `loom/web/src/components/KnowledgeWorkbench.tsx:64–82`, `GraphView.tsx:29–79`;
  `loom/web/src/graph-perspectives/select.ts:36–47`, `plan.ts`, `adapters.ts:234–280`.
- `loom/server/src/app.h:19`, `app.cpp:44`, `:157`, `:198`, `:307`, `:666` (wątek 10).
- Dane: `loom/data/profiles/graph_perspective.pack`, `loom/data/graph_perspectives/catalog.json`,
  `external-profile.json`, `loom/data/onboarding/ui.pack` (wzór katalogu), nowy katalog komunikatów
  web i preset transportu w `loom/data/runtime/` (np. `server.pack`, rejestracja w
  `runtime_sources.pack`).
- Konsumenci: `newPane/initialWorkspace → KnowledgeWorkbench`, `workflow-checkpoint → localStorage`,
  `WorkflowSnapshot → config key`, `ApplicationProfile → GraphPacket`.
- Testy: `loom/web` fixture + Playwright E2E (`npm run e2e`; Chromium działa — pass4).

## Mechanizm docelowy (istniejący)
`graph_perspective.pack` i katalog perspektyw; GraphPacket jako forma wymiany; `ui.pack` jako wzór
katalogu komunikatów z `locales`; R40 warstwy dla domyślnych układów; presety runtime dla serwera.

## Kroki
**Rozważenie.** Po T3 sprawdzić, co panel widoku wejściowego już daje; policzyć grupy wątku 10
na bieżącym SHA; rekord decyzji: perspektywa i sesja workflow zapisywane przez istniejący native
store (pass4: „realny zapis/reopen/replay przez ID grafowy działa”), `localStorage` tylko cache.

**Implementacja.** (a) fixtures headless (bez renderera, jak E4 w pass4); (b) perspektywa/sesja
jako węzły z ID; odczyt po utracie `localStorage`; (c) katalog komunikatów web + locale, układ
początkowy z danych; (d) filtr widoczności z `graph_perspective.pack` z przełącznikiem;
(e) `unknown field` → ostrzeżenie, znane pola czytane; (f) zdolność planu liczona z użytych
adapterów; (g) presety serwera (wątek 10) z obecnymi wartościami bajt w bajt; (h) allowlista
strażnika literałów dla kontraktów DTO (kategoria 1) i statusów HTTP (kategoria 2).

**Optymalizacja.** Jedno źródło katalogu komunikatów (onboarding + workspace); pomiar rozmiaru
bundle; usunięcie drugich domyślnych (Advanced edytuje identyczne dane użyte przez Basic — CH-008).

**Iteracja.** Zasilenie B-ECO-02 wpisami `localStorage` z rolą `cache` i przepisem.

## Kryterium zamknięcia (CH-008, CH-009, A4-CH-VIEW001, A3-WEB-001, A4-E-001)
- Po utracie `localStorage` perspektywa i sesja odzyskiwane po ID z grafu bez ponownego dispatch;
  snapshot grafowy nie udaje dowodu wykonania (osobny status powiązania z receipt).
- Zmiana locale i układu w danych zmienia UI bez kompilacji; Basic i Advanced na tych samych danych.
- Filtr widoczności przełączalny z danych; trzy zakresy rozróżnione w teście.
- Nieznane pole nie blokuje znanych; nieużywany adapter nie potwierdza zdolności.
- Dwa presety serwera → różne limity w działaniu, DTO bez zmian; build web i E2E zielone.

## Czego nie robić
- Nie tworzyć drugiego store ani resolvera w web (pass4 sprawdzało to wprost).
- Nie zmieniać DTO/wire ani kodów HTTP (kontrakt).
- Nie migrować zapisanych workspace v1/v2 stratnie (ryzyko audytu).

## Co zapisać
Rekord decyzji; liczby: komunikaty w katalogu per język, grupy wątku 10 zamknięte/pozostałe.
