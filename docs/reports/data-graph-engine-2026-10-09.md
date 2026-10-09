# Zadanie B — DATA / GRAPH / ENGINE — 2026-10-09

## Baseline i checkpoint

Świeży `git fetch --prune origin` zakończył się poprawnie dla wszystkich siedmiu repo. Checkouty były czyste; HEAD = origin/main. Gałąź produktu: `gpt/data-graph-engine-2026-10-09`. Integracja main pozostaje u Claude’a.

| Repo | Bazowy SHA |
|---|---|
| klb-t/chatadhd | `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` |
| klb-t/Watchdog-JH16 | `58a0c93bd0135e3715dcbc4d92fb80e61bd31215` |
| klb-t/Custom-Keyboard-Pro | `192f820f2d8c653768237e1bff3a1a6d950d69c0` |
| klb-t/AGEDS | `9c1d513bc19d177bd324d7506a21fbab98c2e268` |
| klb-t/LEM-Workbench | `1755e1dfaa69fcaac6fbdd12aa95ea1a593ec456` |
| klb-t/loom | `0b23fa64c1de955c947349feef7bb67c05763f97` |
| klb-t/ar-drafty | `d8af2dd022dacb2f068074158d83e566813592d6` |

Próba odkrycia dodatkowych repo: `gh repo list klb-t --limit 100 --json name,defaultBranchRef` → GraphQL Forbidden. Dostępne repo nie są blokowane. Gałęzie A/C nie pojawiły się podczas fetchu; nie czekamy na ich publikację.

Przeczytane instrukcje AGENTS/CLAUDE, STATE, INDEX i kontrakty R39–R42. Integracja 2026-10-05 zawiera już rejestr metod, profile runtime i prezentację W12. Historycznych bramek nie zaliczamy jako nowych. Nie edytujemy loom/tools/structure. Nie wywołujemy modeli, płatnego CI ani nowych usług.

## Kolejny konkretny krok

Sprawdzić ochronę danych ograniczonych max_detail/max_sensitivity: brak metadanych nie może omijać limitu z grafowej polityki. Użyć istniejącego resolvera i OnboardingStore; zachować nieograniczone defaults. Baseline testów onboardingu uruchomiony; wynik przed zmianą do dopisania.

## Gotowe przyrosty

Brak — checkpoint nie jest gotowym przyrostem.

## B-POLICY-001 — klasyfikacja danych przy limitach prywatności

Decyzja implementacyjna (odwracalna): przy wskazaniu pola jego zapisane detail/sensitivity stanowią dolną granicę klasyfikacji sprawdzanej przy istniejącym limicie. Jawne wyższe wartości żądania też są sprawdzane. Alternatywa: ufać tylko klasyfikacji caller’a — odrzucona, bo pominięcie lub obniżenie wartości omijało limit. Bez pola i bez metadanych ograniczona kategoria daje InvalidArgument; nieograniczone defaults zachowują wynik. Nie zmieniono wartości polityki ani budżetu.

Pion: onboarding.privacy w packu/override → istniejąca walidacja → R40 resolution → privacy_decision / policy_decision → filtrowany model_request → SQLite reopen. Nowa regresja korzysta z rzeczywistej bazy i istniejącej projekcji grafu, bez transportu/modeli.

Wykonane: baseline 7/7 onboardingu (47,58 s), build GCC13 Debug WERROR shared CLI/server vendored SQLite, po zmianie 7/7 onboardingu (43,32 s). Logi: /workspace/.onboarding/logs/data-graph-{baseline,build,caps-tests}.log. Pełna bramka i web dopiero uruchamiane; nie deklarujemy ukończenia.

Punkt wznowienia: kod i regresja gotowe roboczo; przed commitem wykonać pełny CTest + guard i build web. ASan uruchomić osobno. Odkrycie repo także REST users/klb-t/repos → Forbidden.

Wykonano dodatkowo build web PASS oraz niezależny Chromium E2E 16/16 PASS (system Chromium 151, oryginalny npm run e2e; screenshoty zachowane poza Git). B-POLICY-001 nie zamyka CH-004: ordinary chat nadal wymaga osobnego podłączenia zgód. Audyt A pobrano jawnie z baa9e30c; fetch domyślny obejmował wyłącznie main. C: 6988085. Żadne źródła A/C nie zostały scalone.

Pełny CTest trwa. Pierwszy przebieg ma timeout research.contracts przy równoległej kompilacji ASan; build ASan przerwano, limit 60 s zachowano. Konfiguracja ASan najpierw nie znalazła dynamicznych bibliotek sanitizerów w lokalnym sysroot; podłączono istniejące systemowe ABI libasan.so.8 i libubsan.so.1 i ponowna konfiguracja PASS. Pełny build/test ASan nadal nieukończony. Historii/negatywów nie nadpisujemy.
