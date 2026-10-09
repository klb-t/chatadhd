# Walidacja publicznych artefaktów audytu — 2026-10-09

Status: **PASS**. 65 findings, 65 unikalnych ID; 212/212 poprawnych lokatorów na wskazanych SHA.

Błędy: **0**; ostrzeżenia: **0**.

| Repo | SHA | Findings | Naruszenia | Mechanizmy | Naprawione | Niepewne | Lokatory | Hash skanera/reguł |
|---|---|---:|---:|---:|---:|---:|---:|---|
| klb-t/chatadhd | `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` | 15 | 11 | 1 | 2 | 1 | 53/53 | PASS |
| klb-t/Watchdog-JH16 | `58a0c93bd0135e3715dcbc4d92fb80e61bd31215` | 11 | 9 | 1 | 1 | 0 | 11/11 | PASS |
| klb-t/Custom-Keyboard-Pro | `192f820f2d8c653768237e1bff3a1a6d950d69c0` | 12 | 7 | 2 | 1 | 2 | 53/53 | PASS |
| klb-t/AGEDS | `9c1d513bc19d177bd324d7506a21fbab98c2e268` | 12 | 9 | 2 | 1 | 0 | 47/47 | PASS |
| klb-t/LEM-Workbench | `1755e1dfaa69fcaac6fbdd12aa95ea1a593ec456` | 8 | 7 | 1 | 0 | 0 | 31/31 | PASS |
| klb-t/loom | `0b23fa64c1de955c947349feef7bb67c05763f97` | 7 | 5 | 1 | 0 | 1 | 17/17 | PASS |

Walidacja obejmuje istnienie plików i zakresów linii, unikalność ID, zgodność SHA z summary, obecność pól wymaganych przez właściciela (z obsługą aliasów schematów), statusy alternatyw i uzasadnienie przywołanych wyjątków R42. Puste dependencies są poprawną deklaracją braku zależności.

Nie uruchamiano ani nie importowano produktu. Nie czytano prywatnych repo ani korpusów blind/holdout. To kontrola integralności artefaktów; nie stanowi ponownego dowodu wszystkich twierdzeń semantycznych.

## Konkretne braki i ostrzeżenia

Brak stwierdzonych błędów lub ostrzeżeń w zdefiniowanym zakresie.

## Granice

Lokatory występujące wyłącznie w swobodnym tekście nie są automatycznie parsowane. Obecność pól nie dowodzi poprawności zachowania. Puste interpretation/recommendation są zgłaszane osobno jako braki jawnego rozdzielenia warstw, bez dopisywania intencji autora. Źródła i wejściowe findings mają hashe w JSON; późniejsza zmiana raportu wymaga ponowienia tej walidacji.
