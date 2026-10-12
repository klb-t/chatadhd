# B-WD-03 — Watchdog: preferencje workspace jako ustawienia, katalog komunikatów Setup, polityka schedulera

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/Watchdog-JH16` @ `58a0c93b`; potwierdzić na bieżącym `main` |
| Fala / priorytet | W3 / P3 |
| Ustalenia | WD-004, WD-006, WD-010 |
| Niezmienniki EITG | G5, G6, G10 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | B-WD-01 (nic kodowo); konfiguracja strażnika G5 z B-ECO-02, jeśli gotowy |
| Worktree | `t9-wd-03` |

## Zakres
- Preferencje workspace są tym samym obiektem co ustawienia konta (`WorkspacePreference` =
  `EffectiveSetting`) z warstwą źródła; `localStorage` to jawny cache z regułą konfliktu i
  informacją o fallbacku; błąd cache nie kasuje ustawienia (WD-004; wzorzec C).
- Teksty ekranu Setup (nagłówki, opisy, statusy, etykiety a11y) w katalogu komunikatów PL/EN,
  który już istnieje dla nawigacji (WD-006).
- Scheduler: `misfirePolicy` (`coalesce` / `bounded catch-up` / `skip`), `catchUpMax`, limit
  budżetu i wolumenu, ślad pominiętych/scalonych slotów jako dane; domyślnie `coalesce` (WD-010).

## Pliki
- `src/lib/workspace_preferences.tsx:7–18` (`WorkspacePreferencesProvider`);
  `shared/workspace_preferences.ts` (`resolveWorkspace`); `db/repositories/settings.ts`
  (`get/save`); `src/components/ConfigurationWizard.tsx`; dane: `config/workspace-preferences.json`.
- `src/pages/Setup.tsx:119–147`.
- `shared/automation.ts:17–43` (`nextOccurrence`); `db/repositories/automation.ts` (`dispatchDue`);
  `services/automation.ts` (`tick/state`); `src/pages/Automation.tsx`.

## Mechanizm docelowy (istniejący)
`settings` repozytorium po stronie serwera (jedno źródło), `config/workspace-preferences.json`,
istniejące dane PL/EN nawigacji jako katalog do rozszerzenia; polityka harmonogramu jako wpis
w `config/`.

## Kroki
**Rozważenie.** Potwierdzić lokatory; ustalić, czy `settings.get/save` może nieść `source_layer`;
rekord decyzji: `localStorage` = cache z regułą „ustawienie konta wygrywa, cache odświeżany”.

**Implementacja.** (a) test: zmiana presetu/języka widoczna w trwałym obiekcie z `source-layer`
i w UI; błąd cache nie kasuje; `reason` przy decyzji o default; wykluczony preset nie wraca po
aktualizacji paczki; (b) Setup z katalogu: test PL/EN zmienia nagłówki/opisy/statusy/a11y; brak
klucza → jawny błąd walidacji; stabilne `test-id`; (c) scheduler: zamrożony zegar + trzy sloty
zaległości: `coalesce` ≤ 1 zadanie, `bounded catch-up` respektuje limit i budżet, `skip` zapisuje
przyczyny.

**Optymalizacja.** Jeden katalog komunikatów dla nawigacji i Setup; snapshoty E2E odświeżone
raz, z zachowaniem ról semantycznych.

**Iteracja.** Pozostałe strony z tekstem w TSX (agent liczy) tym samym katalogiem; wpis
`localStorage` do `storages.pack` repo (B-ECO-02).

## Kryterium zamknięcia (WD-004, WD-006, WD-010)
- Trzy testy z „Implementacja” PASS; lint/typecheck/testy zielone; snapshoty zaktualizowane
  świadomie (diff w rekordzie decyzji).

## Czego nie robić
- Nie gubić wyborów użytkownika przy rozstrzyganiu konfliktu cache ↔ konto (ryzyko audytu).
- Catch-up tylko po opt-in i z limitem; `coalesce` pozostaje domyślne.

## Co zapisać
Rekord decyzji; liczby komunikatów per język; wynik testu schedulera.
