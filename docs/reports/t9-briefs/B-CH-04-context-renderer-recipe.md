# B-CH-04 — Renderer kontekstu jako wersjonowany przepis: wagi historii, znaczniki, parametry selekcji

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/chatadhd` @ `9e20f99a`; inwentarz wątku 3 @ `161cc22d` |
| Fala / priorytet | W3 / P2 |
| Ustalenia | CH-007; grupy wątku 3 (51 w inwentarzu, m.in. DIC-0332 `.5`, DIC-0333 `.8^count`, DIC-0334 budżet 4000/576/178) ze statusem `hardcoded`/`open` na bieżącym SHA; DIC-0022/0026/0028 (`partial` wątku 11, selektor) |
| Niezmienniki EITG | G2, G6, G10 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | **T2** (preset kolejności kontekstu `legacy` / `stable_first` musi istnieć pierwszy; ten brief nie zmienia kolejności) |
| Worktree | `t9-ch-04` |

## Zakres
Wagi historii, znaczniki instrukcji, progi relewancji, dyskonto różnorodności i budżety selekcji
stają się przepisem `ContextRenderer`/`HistoryWeightRecipe` z wersją i hashem w przepisie
kontekstu (`context recipe hash` w śladzie), bez zmiany historii źródłowej.

## Pliki
- `loom/src/chat/chat_engine.cpp:801–815` (wagi), `:717–721` (znaczniki).
- `loom/src/context/context_engine.cpp:73`, `:195`, `:221` i pozostałe lokatory wątku 3.
- `loom/src/graph/graph_memory.cpp:48`, `:175`, `:208` (DIC-0022/0026/0028).
- Dane: `loom/data/context/` (1 plik), `loom/data/policy/relevance.json`, `selection_rules.json`,
  `thresholds.json`, `loom/data/runtime/graph_memory.pack`, `loom/data/runtime/chat_reasoning.pack`,
  `context_goal_cues.pack`; generator `loom/src/context/gen_runtime_presets.py --check`.
- Konsument: `ChatEngine::build_messages → provider messages`; `ContextEngine`.

## Mechanizm docelowy (istniejący)
Presety runtime (`loom.runtime_profile/1`) z `x-setting`/`x-consumer` jak w `usage_policy.pack`;
ślad przebiegu z `produced_by` i hashem przepisu; kolejność bloków z T2.

## Kroki
**Rozważenie.** Po T2 policzyć, które grupy wątku 3 zostały; potwierdzić, że ślad kontekstu
(R26: „co faktycznie wysłano”) istnieje i może nieść hash przepisu; rekord decyzji: obecne wagi
= preset `legacy` bajt w bajt.

**Implementacja.** (a) fixture: ten sam stan rozmowy → przechwycony pakiet wiadomości;
(b) wagi/znaczniki/progi do packa; (c) renderer czyta efektywny przepis; hash w śladzie;
(d) test A/B: dwa presety progów/renderera zmieniają tylko właściwe wiadomości i hash przepisu;
(e) test: brak przepisu → jawny błąd albo pack osadzony.

**Optymalizacja.** Zmierzyć stabilność prefiksu (bajty identyczne między turami dla bloków S0–S2)
przy `stable_first` — wejście do eksperymentu z `GRAPH_ENTRY` §7 (bez płatnych wywołań tu).

**Iteracja.** Pozostałe parametry selektora (DIC-0026/0028) przez rejestr metod wątku 3.

## Kryterium zamknięcia (CH-007)
- Dwa presety progów/renderera zmieniają tylko właściwe wiadomości i hash przepisu kontekstu.
- Obecne zachowanie pozostaje reprodukowalnym presetem; pochodzenie aktywnego zadania zachowane.
- Historia źródłowa niezmieniona bajt w bajt.
- Brama pełna; `gen_runtime_presets.py --check` zielony.

## Czego nie robić
- Nie zmieniać kolejności bloków (T2) ani historii źródłowej.
- Nie wprowadzać sufitów; budżet jest ograniczeniem z ustawienia, nie definicją (R28).

## Co zapisać
Rekord decyzji; hash przepisu `legacy`; lista grup wątku 3 zamkniętych / pozostałych.
