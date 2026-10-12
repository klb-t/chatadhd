# B-WD-02 — Watchdog: parametry generacji, routing i konstrukcja kontekstu jako wersjonowane profile

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/Watchdog-JH16` @ `58a0c93b`; potwierdzić na bieżącym `main` |
| Fala / priorytet | W2 / P2 |
| Ustalenia | WD-005, WD-009, WD-003 (jeśli nadal wadliwe po B-WD-01) |
| Niezmienniki EITG | G2, G6, G10 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | B-WD-01 (status WD-003) |
| Worktree | `t9-wd-02` |

## Zakres
- Routing (suite / kwantyl / Wilson) jako `RoutingStrategy` + `SuiteSelectionPolicy` +
  `ConfidenceParameters` w danych; obecny wybór jako preset z hashem; udokumentować obecne
  `q²` albo nową jawną funkcję bez cichej zmiany (WD-005).
- Kontekst metodologii: stały prefiks 24 000 znaków i literalna instrukcja ekstrakcji jako
  `ContextConstructionPolicy` + `PaperAssessmentPrompt` z wersją, hashem, zakresem źródeł, śladem
  pominięć i zgodą na wysyłkę wybranego materiału (WD-009).
- `EffectiveGenerationParameters` z kolejnością warstw, pochodzeniem i hashem; nadpisania per
  żądanie dozwolone przez schemat zdolności dostawcy (WD-003).

## Pliki
- `backend/watchdog_api/llm/routing.ts:32–67` (`routeModel`); `services/assistant.ts`
  (`preview/generateRequest`); `db/repositories/settings.ts` (`routingEvidence`);
  `config/assistant.json` (`routingEvidence` — istniejący plik danych).
- `backend/watchdog_api/services/paper_intake.ts:43–67` (`assess`); `db/repositories/research.ts`
  (`finishAssessment`); `src/pages/Research.tsx`.
- `backend/watchdog_api/llm/profile_generator.ts:11–22` (`generate`); `llm/openai_compatible.ts`.
- Dane: istniejący katalog `config/` (`assistant.json`, `workspace-preferences.json`) — profil
  w tym samym miejscu, nie nowy katalog.

## Mechanizm docelowy (istniejący)
Katalog `config/*.json` repo (WD-008 pokazał, że dostawcy i budżet są stamtąd konsumowane);
rejestr dowodów routingu `routingEvidence`; zapis runu bez sekretów.

## Kroki
**Rozważenie.** Potwierdzić lokatory; sprawdzić, czym jest `routingEvidence` i czy może nieść hash
profilu; rekord decyzji: obecny routing i obecny prefiks 24 000 = presety `legacy`.

**Implementacja.** (a) atrapa transportu przechwytująca żądanie i wybór modelu; (b) profil
routingu w `config/` z presetem `legacy`; test: dwa profile przy identycznym katalogu → różne
przewidywane wybory i hashe; test `q=0.5` dokumentuje obecne `q²`; (c) profil kontekstu
i prompt oceny z wersją/hashem; test: dwa profile wybierają różne znane fragmenty długiego
dokumentu z hashem i offsetami źródłowymi; limit nie zmienia się niejawnie po zmianie modelu;
(d) parametry generacji: `temperature`/`seed` z profilu docierają do żądania; próba nadpisania
`model`/`messages`/budżetu odrzucona; zapis runu odzwierciedla ciało bez sekretów.

**Optymalizacja.** Prompt oceny jako stabilny prefiks (R49); jedna ścieżka rozwiązywania
parametrów dla asystenta i oceny.

**Iteracja.** Pozostałe prompty w kodzie (agent liczy po `grep` na literalne instrukcje w
`services/`) jako kolejne wpisy profilu.

## Kryterium zamknięcia (WD-003, WD-005, WD-009)
- Dwa profile routingu → różne, przewidywane wybory i hashe; zmiana poziomu ufności przez dane ze śladem.
- Dwa profile kontekstu → różne fragmenty z offsetami; pełne źródło niezmienne; brak profilu → jawny błąd.
- Efektywne `temperature`/`seed` w przechwyconym żądaniu; nadpisanie modelu odrzucone.
- Lint/typecheck/testy zielone; stare próby oceny nieprzepisane (ryzyko audytu: „bez przepisywania starych prób”).

## Czego nie robić
- Nie przekraczać tego samego budżetu (wybór droższego modelu wyłącznie z profilu, z pokwitowaniem).
- Nie zmieniać znaczenia dawnych wyników oceny; nowa wersja profilu, nowe próby.

## Co zapisać
Rekord decyzji; hashe presetów `legacy`; liczba promptów literalnych przed/po.
