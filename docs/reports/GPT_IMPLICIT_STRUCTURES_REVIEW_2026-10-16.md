# Przegląd gałęzi GPT `implicit-structures-2026-10-14` (f628e50b) — 2026-10-16

Zakres: `git diff origin/main...gpt/...  -- loom ':!loom/docs'` (kod ~1160 linii; docs pominięte). Przegląd adwersarialny.

## Znaleziska (naprawione w integracji)
1. `relation_pattern.cpp` (`same_qualifiers`): porównanie `next.qualifiers[name] != value` na `ordered_json` zrównuje 1 i 1.0 — dokładnie
   to, przed czym ostrzega komentarz przy filtrze `claim_qualifiers`. Poprawka: `json::canonical` po obu stronach.
2. Przenośność libc++: `std::tie` w `infer.cpp`/`match.cpp` bez `<tuple>`; `std::uint64_t`/`std::string`/`std::vector` w
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
- Wyczerpanie budżetu (`max_steps` 100000, `max_matches` 64 w `facets/feedback_execution.json:78-79`) zwraca `Errc::Unavailable` z
  `relation_pattern.cpp`, a `anchor_op` (`match.cpp:304/311`) propaguje go: jeden gęsty podmiot przerywa cały etap `generalize`.
  Testy GPT utrwalają ten kontrakt (`test_implicit_structures.cpp:135,230`), więc NIE zmieniono. Limit 64 to wymyślone ograniczenie
  (zasada właściciela); proponowana zmiana: wynik `complete:false` + `exhausted` w `reasons` zamiast błędu i domyślnie wyższy/zerowy limit.
- `transfer()`/`analogies()` iterują teraz po *wszystkich* instancjach (nie najlepszej na podmiot): koszt O(P²) po instancjach; przy dużych
  korpusach rozważyć indeks po paradygmacie. Brak twardego limitu (zgodnie z zasadą właściciela), ale bez pomiaru.
- Kandydat bez wersjonowanej tożsamości między przebiegami: `Instance.id` zależy od (paradigm, subject), więc zmiana metody nadpisuje
  pokwitowanie; historię trzyma tylko `pipeline_version` w fingerprincie przebiegu.
- `kPipelineVersion` 7→10 i `kGeneralizerVersion` 1→4 unieważniają istniejące przebiegi (zamierzone, ale dla użytkownika to pełne przeliczenie).

## Otwarte
- Adnotacje `structure_role:*` (P23) + rodzaj `structure_recognition` w profilu `guardian` z cyklem awansu/propozycją w rozmowie.
- Ustawienie użytkownika (settings) dla `recognition_overlay`; dziś tylko parametr etapu `stage_params.generalize.recognition_overlay`.

## Dla GPT: co zintegrowano (nie robić drugi raz)
Zintegrowano gałąź w stanie 04d58c6a (merge main P28-P30; brak nowego kodu GPT). GPT zapowiada własną „recognition integration" — to już jest:
- `generalize::recognition_overlay` / `write_recognition_overlay` (`include/loom/generalize.h`, `src/generalize/recognition_overlay.cpp`), wywoływane z
  `run_stage` (`stage.cpp`, po `put_areas`), parametr etapu `recognition_overlay` = propose (preset) | auto | off.
- Wynik: overlay `<data>/views/recognition.json` na widoku `edge_kinds` (wpis `structure:<paradigm>`, `disable`d = hipoteza
  `structure_recognition`, provenance origin=generalize/via=recognition). Test: `tests/test_recognition_overlay.cpp`.
- Poprawki: kanoniczne porównanie `same_qualifiers`, include'y `<tuple>`/`<cstdint>`/... (libc++).
- NIE zrobione (do GPT): adnotacje P23 `structure_role:<rola>`, `kinds.structure_recognition` i producent w `guardian.pack`, ustawienie w settings.
