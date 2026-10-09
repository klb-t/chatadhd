# Audyt chatadhd — 2026-10-09

Baza: `main` `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`. Kod produktu, schematy i STATE/INDEX niezmienione.

15 ustaleń: {'naruszenie': 11, 'dopuszczalny mechanizm': 1, 'niepewne': 1, 'już naprawione': 2}. Semantycznie prześledzono wybrane zakresy 35 plików; szczegóły i mianownik repo w `coverage.json`, szeroki skan w `scan/`.

Wykonano 10/10 sond rzeczywistych jednostek Python (DB i transport zastąpione stubami; zero sieci), 9/9 native helper tests (168 asercji) oraz 1/1 generator roundtrip z 13 stringami. Zielone sondy błędnego zachowania potwierdzają problem, nie naprawę. Brak pełnego CTest/E2E nie jest ukrywany.

Najważniejszy wzorzec: są już poprawne packi, grafowe profile i resolver warstw. Luki pozostają na styku z konkretnymi konsumentami. Sześć wyjątków R42 opisano w CH-014; nie nakazujemy usuwania każdego literału.

Bieżący INDEX 2026-10-05 jest punktem odniesienia integracji. Stary reset modelu oraz wybrane presety są **już naprawione**. Nie przepisywano 695 historycznych grup jako nowego backlogu. R15/R33/R40/R41 pozostają istniejącymi wymaganiami; nie proponujemy konkurencyjnego grafu/silnika.

- **CH-001 — naruszenie**: Domyślne ustawienia startowe pozostają ręcznym obiektem w Pythonie i C++. `loom/src/core/config.cpp:197–224`.

- **CH-002 — naruszenie**: Legacy SemanticLLM nadal składa literalny prompt i obcina wejście do 3000 znaków. `loom/src/semantic/semantic_llm.cpp:23–43`.

- **CH-003 — naruszenie**: Nakładka semantic_analyzer dociera do real-time grafu, ale nie do wspólnego analizatora workera. `loom/src/runtime.cpp:181–202`.

- **CH-004 — naruszenie**: Warstwy i prywatność onboardingu nie są automatycznie źródłem ordinary chat. `loom/src/onboarding/store.cpp:428–460`.

- **CH-005 — naruszenie**: Usage policy nie obejmuje zwykłego transportu czatu ani starego workera. `loom/src/chat/chat_engine.cpp:1148–1171`.

- **CH-006 — naruszenie**: Legacy graph ingestion automatycznie materializuje relacje modelu. `loom/src/graph/graph_engine.cpp:21–88`.

- **CH-007 — naruszenie**: Wagi historii i znaczniki instrukcji są nadal polityką w kodzie czatu. `loom/src/chat/chat_engine.cpp:801–815`.

- **CH-008 — naruszenie**: Workspace UI ma ręczne etykiety i wartości domyślne. `loom/web/src/workspace/state.ts:4–40`.

- **CH-009 — naruszenie**: Perspektywy i sesje workflow nie mają kompletnego pokrycia grafowego. `loom/web/src/profiles/workflow-checkpoint.ts:10–47`.

- **CH-010 — naruszenie**: Deduplikacja nazw odrzuca alternatywny typ encji i redukuje wagę regexu bez strategii danych. `engine/semantic_llm.py:223–241`.

- **CH-011 — naruszenie**: Błąd analizy workera staje się stanem analysed bez jawnej polityki ponowień. `engine/semantic_worker.py:163–169`.

- **CH-012 — już naprawione**: Model użytkownika i reset awarii semantycznych są już naprawione. `engine/config.py:102–117`.

- **CH-013 — już naprawione**: Presety rozumowania i celu są danymi z działającą walidacją warstw. `loom/src/context/runtime_preset.h:47–107`.

- **CH-014 — dopuszczalny mechanizm**: Granice R42: kontrakt, standard, mechanizm, format, bootstrap i diagnostyka są dozwolone. `loom/src/model/runtime_profile.cpp:22–42`.

- **CH-015 — niepewne**: Nie potwierdzono kompletnego Basic presetu i opt-in eksperymentów od checkpointu. `loom/include/loom/tasks.h:1–17`.

Pełne dowody, alternatywy, odbiór migracji i źródła wymagań: `findings.jsonl`. Wymaganie, obserwacja, interpretacja i rekomendacja są osobnymi polami. Brak integracji programu eksperymentów CH-015 pozostaje hipotezą o zakresie zbadanym, nie twierdzeniem o całym drzewie.

Punkt wznowienia: uruchomić end-to-end mock transport dla CH-003/004/005 na niezmiennym SHA, następnie rozliczyć pozostałe sygnały skanera per moduł. Nie czytać eval/real-holdout-key ani blind; nie wykonywać płatnych modeli.
