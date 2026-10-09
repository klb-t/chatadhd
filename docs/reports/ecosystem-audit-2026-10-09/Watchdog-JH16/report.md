# Audyt Watchdog-JH16 — 2026-10-09

Baza: `main` `58a0c93bd0135e3715dcbc4d92fb80e61bd31215`, repo publiczne. Kod produktu niezmieniony.

Pierwszy checkpoint: 8 ustaleń: 6 naruszeń, 1 dopuszczalny mechanizm, 1 obszar już podłączony. 3 naruszenia potwierdzone wykonanymi sondami na rzeczywistych funkcjach: nieskuteczne `ratio.exclude`, odrzucanie parametrów generacji oraz `templateId` bez wyboru szablonu. Przeszło typecheck i 53 istniejące testy punktowe. Sondy odtwarzają błędy; ich zielony wynik nie oznacza naprawy produktu.

Mianownik: 603 śledzonych plików, 266 plików kodu produktu. Semantycznie prześledzono 19 wskazanych plików i 5 przepływów; wykaz w `coverage.json`. Repo ma więcej ścieżek niż objęto analizą semantyczną.

- **WD-001 — naruszenie**: Identyfikator szablonu narracji nie wybiera szablonu; prompt, język i top-3 są w kodzie. `backend/watchdog_api/services/narrative.ts:85–122`.
- **WD-002 — naruszenie**: ratio przyjmuje missingPolicy=exclude, lecz zachowuje brakujący wiersz jak propagate. `backend/watchdog_api/analysis/primitives.ts:137–180`.
- **WD-003 — naruszenie**: Parametry żądania LLM są odrzucane w adapterze dostawcy. `backend/watchdog_api/llm/profile_generator.ts:11–22`.
- **WD-004 — naruszenie**: Preferencje workspace żyją wyłącznie w localStorage i cicho wracają do default. `src/lib/workspace_preferences.tsx:7–18`.
- **WD-005 — naruszenie**: Routing wybiera politykę suite/kwantyla/Wilsona zaszytą w kodzie. `backend/watchdog_api/llm/routing.ts:32–67`.
- **WD-006 — naruszenie**: Ekran Setup ma teksty użytkowe w TSX mimo danych PL/EN nawigacji. `src/pages/Setup.tsx:119–147`.
- **WD-007 — dopuszczalny mechanizm**: Stałe operacji i strażnicy naukowi są uzasadnionym mechanizmem. `backend/watchdog_api/analysis/executor.ts:45–95`.
- **WD-008 — już naprawione**: Dostawcy i budżet asystenta są rzeczywiście konfigurowalne, domyślnie opt-in. `backend/watchdog_api/services/assistant.ts:80–118`.

Pełne dowody, alternatywy, interpretacje, źródła wymagań, obiekty docelowe, konsumenci, testy i ryzyka znajdują się w `findings.jsonl`. Nie przypisano niewypowiedzianych intencji właścicielowi. Sześć wyjątków R42 rozpatrzono zgodnie z testem „czy zmiana wymaga zmiany algorytmu”; stałe procentu, nazwy operacji i lifecycle nie zostały potraktowane jako domenowe dane.

Relacje: `ECOSYSTEM.md` sam opisuje mapę brainstormu, nie gotową integrację. WatchDog jest odrębnym produktem; ChatADHD jako interfejs i Loom/LEM/AGEDS jako możliwe źródła zdolności są kierunkami. Nie stwierdzono tożsamości repo z którymkolwiek z nich.

Pozostało: zapisać porównanie aktualnych nieprzyjętych gałęzi desktop z 7.10, dołączyć szeroki skan, zwiększyć pokrycie clinical/field/search/import oraz zweryfikować konkretne SHA B/C przez integratora. Nie wznawiać dawnych E4.7/E7.7/E3.17: handoff opisuje ich przyjęcie, choć zakresy audytu filozofii nadal mają luki.
