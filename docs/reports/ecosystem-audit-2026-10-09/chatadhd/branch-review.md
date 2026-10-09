# Przegląd odrębnych gałęzi badawczych — 2026-10-09

Baza `main@9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` i aktualny `docs/reports/INDEX.md` z 2026-10-05. Cztery badane gałęzie **nie zawierają nowych zmian runtime produktu**: ich unikalne przyrosty są pod `docs/` i `loom/tools/`. Dają narzędzia przygotowania, oceny i publikacji danych. Nie są wynikiem nowych płatnych eksperymentów i nie dowodzą konsumpcji presetów przez aplikację.

Porównanie ancestry z `main` samo zawyża kolejkę: wspólne badania były przyjmowane po rebase. Użyto `git cherry -v` do wyłączenia commitów równoważnych patchowo oraz sprawdzono ścieżki zmieniane przez pozostałe commity.

| Gałąź | Tip | Commity równoważne na main | Commity bez odpowiednika patchowego | Pliki dotknięte tymi commitami |
|---|---|---:|---:|---:|
| `gpt/research-jev-validation-2026-10-05` | `0f52ac5f262bf615560c7adf12d31af85eade6e5` | 47 | 8 | 441 |
| `gpt/research-prompt-preferences-2026-10-05` | `8e9d31ad00d2f2c3e1efbabf48054203bdc9b2e5` | 46 | 4 | 155 |
| `gpt/real-prompt-preparation-2026-10-05` | `ffe6443e82d71adde07ee899c650925e7afc37b3` | 47 | 4 | 21 |
| `gpt/real-source-intake-2026-10-05` | `62d1b054657481e2a616144d4474cc9cc6a6dde9` | 47 | 1 | 9 |

## gpt/research-jev-validation-2026-10-05

Narzędzia badawcze offline i projekcja publicznych agregatów, nie zmiana runtime aplikacji. Ostatni commit 0f52ac5 dodaje allowlistowy eksporter z private validation commitments; nie stanowi wykonania nowej walidacji Jev.

**Co rzeczywiście wnosi:** Eksporter buduje nowe publiczne pola z allowlist, wiąże private validation SHA i metody, opisuje stratę projekcji; to materialna ochrona na ścieżce eksportu badawczego, nie pełny publiczny replay.

**Czego nie zamyka:** Brak nowych odpowiedzi/cost evidence; brak 7C dedicated real-source freeze. Brak podłączenia tej projekcji do native graph store/UI/preset selection; export grafu to artifact, nie dowód runtime consumption.

**Wobec aktualnego INDEX:** INDEX poprawnie nie zalicza tego do nowych wyników modeli. Report na tip nadal wycofuje syntetyczny dispatch, wynik nowych rodzin null, 0 własnych paid calls. Najnowszy public projector rozszerza narzędzia względem wcześniejszego 7C reportu.

**Wznowienie:** Odbierać selektywnie narzędzia z unikalnych commitów po nowej bramce; nigdy nie uruchamiać wycofanego dispatchu ani nie uznawać eksportera za wyniki.

Źródła przy tym SHA: `docs/reports/research-jev-validation-2026-10-05.md:3–21`; `loom/tools/structure/jev_public_projection_v1.py:165–256`; `loom/tools/structure/jev_public_projection_v1.py:259–314`; `loom/tools/structure/jev_dispatch_binding_v1.py:1–8`.

## gpt/research-prompt-preferences-2026-10-05

Materializer/scorer i zachowane niewykonane przygotowanie. Nie zmienia kodu produktu i nie wykonuje real-data badania.

**Co rzeczywiście wnosi:** Strict first-response scoring, zachowanie mianowników i missing values oraz portable ModelProfile metric projection.

**Czego nie zamyka:** Metryki profili są projekcją, native_graph_write=False. Stary syntetyczny panel wycofany, parametry jakości niezmierzone. Nie naprawia doboru presetów w produkcyjnej aplikacji.

**Wobec aktualnego INDEX:** Zgodne z INDEX: poza main pozostają przygotowanie i narzędzia, 0 nowych odpowiedzi. Końcowy handoff czekał na źródła; późniejszy intake/real preparation w innych branchach częściowo zastępuje ten punkt wznowienia.

**Wznowienie:** Zachować scorer i jego testy; nie promować starego prepared manifest do aktywnej kolejki. Real adapter ffe6443 używa późniejszego materializera tej gałęzi.

Źródła przy tym SHA: `docs/reports/research-prompt-preferences-2026-10-05.md:1–20`; `docs/research/model_research_2026-10-04/express20261005/prompt-parameters-v1/prepare.py:238–450`; `docs/research/model_research_2026-10-04/express20261005/prompt-parameters-v1/prepare.py:453–510`.

## gpt/real-prompt-preparation-2026-10-05

Rzeczywiste przygotowanie offline przyjmuje prywatne źródła i produkuje zamrożony panel; bez transportu dostawcy, bez eksperymentalnych odpowiedzi, bez native write.

**Co rzeczywiście wnosi:** Odrębny prepare_real adapter konsumuje ancestry-preserving private graph view i provisional reference; freeze/operation hashes oddziela od historycznych ośmiu operacji.

**Czego nie zamyka:** Referencja nie jest niezależnie adjudykowanym gold; adapter odrzuca branching/ambiguous clocks. Brak dedicated 7C real-data j_active/j_directed preparation, płatnego preflight/collection i nowych rankingów.

**Wobec aktualnego INDEX:** Późniejszy od starego statusu źródłowego 7B; report deklaruje 3 rozmowy/44 wiadomości/128 request bodies przygotowane prywatnie. Nie odtworzono tych danych w tym audycie. Końcowy akapit o zaległej integracji W11/W6/attempt-identity jest przestarzały względem aktualnego INDEX 2026-10-05.

**Wznowienie:** Punkt wznowienia: wykorzystać jawny adapter i prywatny checkpoint z tego SHA po weryfikacji źródeł oraz decyzji o provisional reference; nie wznawiać starej kolejki W11/W6 i nie robić drugiego intake równolegle.

Źródła przy tym SHA: `docs/reports/recovery-real-prompts-2026-10-05.md:9–27`; `docs/reports/recovery-real-prompts-2026-10-05.md:39–64`; `docs/research/model_research_2026-10-04/express20261005/real-prompt-recovery-20261005/prepare_real.py:103–204`; `docs/research/model_research_2026-10-04/express20261005/real-prompt-recovery-20261005/prepare_real.py:221–300`.

## gpt/real-source-intake-2026-10-05

Dodaje instrument weryfikacji prywatnej kapsuły i osobny graph view; to naprawa wejścia badawczego, nie zmiana importera ChatADHD/native GraphPacket store.

**Co rzeczywiście wnosi:** intake.py sprawdza hash payloadów, parent coverage, cykle i deklarowane children; buduje topologiczny widok bez twierdzenia, że kolejność rodzeństwa jest chronologią. Public receipt konstruowany allowlistą.

**Czego nie zamyka:** Nie podmienia dawnych requestów; samo istnienie graph view nie naprawia ich historycznej projekcji ani nie dowodzi skutku dla jakości modeli. Brak konsumującego tę projekcję kodu produktu w przyroście.

**Wobec aktualnego INDEX:** Źródłowy blok przygotowania jest nowszy niż stare statusy czekania na źródła; ten commit jest bezpośrednim przodkiem ffe6443, więc nie jest osobnym nieprzyjętym pakietem obok całej real-prompt-preparation.

**Wznowienie:** Traktować jako część real-prompt-preparation; adaptera badawczego nie ogłaszać naprawą produkcyjnego importera. Ewentualne przeniesienie mechanizmu wymaga testu w real consumerze.

Źródła przy tym SHA: `docs/research/model_research_2026-10-04/express20261005/real-source-recovery-20261005/README.md:18–36`; `docs/research/model_research_2026-10-04/express20261005/real-source-recovery-20261005/intake.py:100–221`; `docs/research/model_research_2026-10-04/express20261005/real-source-recovery-20261005/intake.py:224–293`.

## Granice interpretacji

`62d1b05` jest przodkiem `ffe6443`: nie liczyć intake i jego odziedziczenia w real-prompt-preparation jako dwóch niezależnych wdrożeń. Ostatnia uwaga recovery reportu o nieprzyjętych W11/W6 i błędzie attempt identity nie jest aktualnym backlogiem: INDEX na bieżącym main wyraźnie opisuje ich przyjęcie/naprawę.

Źródła prywatne, request bodies, ZIP-y i blind/holdout nie były odczytane; nie powtarzano deklarowanych 37/43/22 testów gałęzi ani modelowych eksperymentów. Badanie obejmuje zakres wszystkich unikalnych zmienionych ścieżek i wybrane bezpieczne publiczne dokumenty/entrypointy, nie pełną ocenę każdego pliku danych. B/C z 2026-10-09 pozostają do osobnej końcowej weryfikacji sesji głównej.
