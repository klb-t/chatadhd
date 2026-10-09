# Audyt Watchdog-JH16 — 2026-10-09

Baza: `main` `58a0c93bd0135e3715dcbc4d92fb80e61bd31215`, repo publiczne. Kod produktu niezmieniony.

Drugi checkpoint: 11 ustaleń: 9 naruszeń, 1 dopuszczalny mechanizm, 1 obszar już podłączony. 6 naruszeń potwierdzonych wykonanymi sondami na rzeczywistych funkcjach: nieskuteczne `ratio.exclude`, odrzucanie parametrów generacji `templateId` bez wyboru szablonu, prefix kontekstu 24000, jedyna strategia misfire coalesce i przepuszczenie zmiany znaku liczby przez guard. Przeszło typecheck i 53 istniejące testy punktowe. Sondy odtwarzają błędy; ich zielony wynik nie oznacza naprawy produktu.

Mianownik: 603 śledzonych plików, 266 plików kodu produktu. Semantycznie prześledzono 25 wskazanych plików i 5 przepływów; wykaz w `coverage.json`. Repo ma więcej ścieżek niż objęto analizą semantyczną.

- **WD-001 — naruszenie**: Identyfikator szablonu narracji nie wybiera szablonu; prompt, język i top-3 są w kodzie. `backend/watchdog_api/services/narrative.ts:85–122`.
- **WD-002 — naruszenie**: ratio przyjmuje missingPolicy=exclude, lecz zachowuje brakujący wiersz jak propagate. `backend/watchdog_api/analysis/primitives.ts:137–180`.
- **WD-003 — naruszenie**: Parametry żądania LLM są odrzucane w adapterze dostawcy. `backend/watchdog_api/llm/profile_generator.ts:11–22`.
- **WD-004 — naruszenie**: Preferencje workspace żyją wyłącznie w localStorage i cicho wracają do default. `src/lib/workspace_preferences.tsx:7–18`.
- **WD-005 — naruszenie**: Routing wybiera politykę suite/kwantyla/Wilsona zaszytą w kodzie. `backend/watchdog_api/llm/routing.ts:32–67`.
- **WD-006 — naruszenie**: Ekran Setup ma teksty użytkowe w TSX mimo danych PL/EN nawigacji. `src/pages/Setup.tsx:119–147`.
- **WD-007 — dopuszczalny mechanizm**: Stałe operacji i strażnicy naukowi są uzasadnionym mechanizmem. `backend/watchdog_api/analysis/executor.ts:45–95`.
- **WD-008 — już naprawione**: Dostawcy i budżet asystenta są rzeczywiście konfigurowalne, domyślnie opt-in. `backend/watchdog_api/services/assistant.ts:80–118`.
- **WD-009 — naruszenie**: Kontekst metodologii to stały prefix 24000 znaków; instrukcja ekstrakcji jest literalna. `backend/watchdog_api/services/paper_intake.ts:43–67`.
- **WD-010 — naruszenie**: Scheduler ma tylko politykę pomijania zaległych slotów przez coalesce. `shared/automation.ts:17–43`.
- **WD-011 — naruszenie**: Strażnik nowych wartości liczbowych pomija znak liczby. `backend/watchdog_api/services/narrative.ts:143–175`.

Pełne dowody, alternatywy, interpretacje, źródła wymagań, obiekty docelowe, konsumenci, testy i ryzyka znajdują się w `findings.jsonl`. Nie przypisano niewypowiedzianych intencji właścicielowi. Sześć wyjątków R42 rozpatrzono zgodnie z testem „czy zmiana wymaga zmiany algorytmu”; stałe procentu, nazwy operacji i lifecycle nie zostały potraktowane jako domenowe dane.

Relacje: `ECOSYSTEM.md` sam opisuje mapę brainstormu, nie gotową integrację. WatchDog jest odrębnym produktem; ChatADHD jako interfejs i Loom/LEM/AGEDS jako możliwe źródła zdolności są kierunkami. Nie stwierdzono tożsamości repo z którymkolwiek z nich.

Pozostało: zwiększyć semantyczne pokrycie clinical/field/search/import oraz zweryfikować konkretne SHA B/C przez integratora. Porównanie 18 heads i szeroki skan są zapisane. Nie wznawiać dawnych E4.7/E7.7/E3.17: handoff opisuje ich przyjęcie, choć zakresy audytu filozofii nadal mają luki.

Skan automatyczny objął 282 wpisy kodu (26906 linii), spośród 284 kwalifikujących się; 2 kwalifikujące się pliki wyłączono ochroną ścieżek. Całe drzewo ma 603 wpisy. 31119 sygnałów to kandydaci do oceny, nie liczba naruszeń. Skaner analizuje TypeScript leksykalnie; semantyczne ustalenia są odrębnym etapem. `scan/summary.json` podaje hashe skanera/reguł i pełne ograniczenia.

Gałęzie nieprzyjęte odczytano na konkretnych SHA w `branches.json`: desktop `ecae0fd0212befe63dfade7581a117f9bdb39979`, `789806807c357db298607fc49eb11d8ee8ab0b91`, DevBox `d0f2f4a921d4e560994d277f8eeb3a40247d4047`, Claude `41e525b1d4d62a0c07598c0c3b2246453db37bc3`. Nowe comparison families zachowują mianownik i próby, ale nie naprawiają wykrytych problemów narracji/parametrów/missingness. Strażnik P04 jawnie wymaga przeglądu ręcznego. Zmiana paper_intake wyodrębnia anchorQuote; nadal zostawia prefix 24000 i literalny prompt. Nie ogłoszono odbioru runtime gałęzi desktop. Nie zaobserwowano gałęzi B/C w Watchdog; ich brak nie blokował audytu.

Dokładny punkt wznowienia: `main@58a0c93bd0135e3715dcbc4d92fb80e61bd31215`, `backlog.json` WD-B002/WD-B003/WD-B001/WD-B009/WD-B011 (P1); najpierw potwierdzić, czy nowsze SHA zmieniły lokalizatory ustaleń. Dalsza analiza może zaczynać od źródeł klinicznych i wyszukiwania, których nie oznaczono tutaj jako semantycznie pokrytych.
