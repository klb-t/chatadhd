# Przegląd gałęzi GPT `implicit-structures-2026-10-14` (f628e50b) — 2026-10-16

Zakres: `git diff origin/main...gpt/...  -- loom ':!loom/docs'` (kod ~1160 linii; docs pominięte). Przegląd adwersarialny.

## Znaleziska (naprawione w integracji)
1. `loom/src/generalize/relation_pattern.cpp` (ok. l. 81, 131): wyczerpanie `max_steps`/`max_matches` zwracało `Errc::Unavailable`,
   a `anchor_op` (`match.cpp:304/311`) propaguje błąd — jeden gęsty podmiot (>64 dopasowań; limit 64 w `facets/feedback_execution.json:79`)
   przerywał cały etap `generalize` całego przebiegu. Poprawka: wynik `complete:false` + `exhausted:"max_steps|max_matches"` w `reasons`
   (jawne, nie fałszywie puste; przebieg trwa). Limity zostają danymi pakietu (presety, nie reguły).
2. `relation_pattern.cpp` (`same_qualifiers`): porównanie `next.qualifiers[name] != value` na `ordered_json` zrównuje 1 i 1.0 — dokładnie
   to, przed czym ostrzega komentarz przy filtrze `claim_qualifiers`. Poprawka: `json::canonical` po obu stronach.
3. Przenośność libc++: `std::tie` w `infer.cpp`/`match.cpp` bez `<tuple>`; `std::uint64_t`/`std::string`/`std::vector` w
   `relation_pattern.cpp` bez własnych include'ów. Dopisane.

## Sprawdzone, bez zastrzeżeń
- Wzorzec `json::find(x.value(...))` (wiszący wskaźnik): w nowym kodzie nie występuje. `claim->qualifiers.to_json()` trzymane w lokalnym `Json`.
- Ciche catch'e: brak; budżety i błędy walidacji są jawne (`v_relation_pattern` odrzuca nieznane pola, rozłączne wzorce).
- `auto& state = stack.back()` po `push_back` nie jest używane (zawsze `continue`) — poprawne.

## Duplikacja z mechanizmami S1/S2 — decyzje
- GPT nie ma własnego rejestru: wzorce to dane pakietu (`anchors[].relation_pattern`), pokwitowanie to `Instance.recognition`
  (węzeł w KnowledgeStore). Brakowało **widoczności przez widok efektywny P24** — dodany adapter
  (`generalize::recognition_overlay`, `src/generalize/recognition_overlay.cpp`): kandydat → `add` rodzaju krawędzi
  `structure:<paradigm>` + `disable` (hipoteza `structure_recognition`, czytelnicy pomijają), overlay `views/recognition.json` przepisywany
  przy każdym przebiegu. Awans = `disable {restore:true}` w overlayu użytkownika (późniejszy w kolejności) albo parametr etapu
  `recognition_overlay=auto`; preset `propose`, `off` wyłącza. Nie ma auto-awansu.
- P23 (adnotacje `structure_role:<rola>`) i `kinds.structure_recognition` w `guardian.pack` (plan fuzji pkt 1–2, 5): **NIE zrobione** —
  wymaga funkcji zapisu adnotacji per źródło i wpisów w pakiecie strażnika; patrz „Otwarte”.
- P29 (placement): brak nakładania się.

## Problemy projektowe (nie przepisywane)
- `transfer()`/`analogies()` iterują teraz po *wszystkich* instancjach (nie najlepszej na podmiot): koszt O(P²) po instancjach; przy dużych
  korpusach rozważyć indeks po paradygmacie. Brak twardego limitu (zgodnie z zasadą właściciela), ale bez pomiaru.
- Kandydat bez wersjonowanej tożsamości między przebiegami: `Instance.id` zależy od (paradigm, subject), więc zmiana metody nadpisuje
  pokwitowanie; historię trzyma tylko `pipeline_version` w fingerprincie przebiegu.
- `kPipelineVersion` 7→10 i `kGeneralizerVersion` 1→4 unieważniają istniejące przebiegi (zamierzone, ale dla użytkownika to pełne przeliczenie).

## Otwarte
- Adnotacje `structure_role:*` (P23) + rodzaj `structure_recognition` w profilu `guardian` z cyklem awansu/propozycją w rozmowie.
- Ustawienie użytkownika (settings) dla `recognition_overlay`; dziś tylko parametr etapu `stage_params.generalize.recognition_overlay`.
