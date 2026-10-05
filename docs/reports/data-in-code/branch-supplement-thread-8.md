# W8 — uzupełnienie inwentarza gałęzi

Świeży `git fetch origin` ujawnił raport i walidator z gałęzi `origin/gpt/repo-hygiene-2026-10-04` na `2544cf3a75c1477797213328f7279b3e8576579e`. To osobny przegląd późniejszego kodu: **nie powiększa 695 grup inwentarza sprzed edycji** na `161cc22`. Pełne linie, hashe źródeł i propozycje są w [branch-supplement-thread-8.json](branch-supplement-thread-8.json).

Pięć grup obejmuje rejestr presetów CI, dwie polityki dostępności, wybór parsera według nazw wpisów oraz formaty dowodu. Cztery pierwsze mogą korzystać z deklaratywnego packa weryfikacji, który zachowa dokładne obecne wartości. Regexy podsumowań doctest/unittest, znacznik schema i składnia `skipped=N` pozostają formatami dowodu; nie są promptami ani metodami analizy semantycznej.

Polityka ASan jawnie odlicza dwa całkowicie niedostępne wpisy FFI oraz dokładnie dwa przypadki ABI, wymaga diagnostyki braku biblioteki i dla ABI pozostawia dodatnią liczbę wykonanych przypadków. Opt-in `catalog_scale` z 0 przypadków i 0 asercji jest wykazany jako niewykonany. Każdy inny nieoczekiwany skip, brak lub wieloznaczność podsumowania pozostaje błędem. Tych reguł nie zmieniono.

Nazwy `unit.*`, `compat.*`, `research.*`, `server.chat_active_task` i `eval.harness` sterują wyborem parsera. Nowe wpisy CTest wymagają przypisania do rejestru: obecny fallback skryptowy potwierdza zewnętrzny status, ale nie udowadnia wykonania przypadków wewnętrznych. Nie uruchamiano ponownie zdalnego CI i nie przedstawiamy zgłoszonych w raporcie W8 wyników jako nowego pomiaru W11.

## Do wątku 8

Przyszłe przeniesienie polityki walidatora należy do W8. Zachować klasyfikacje, dokładne powody/liczniki niedostępności, odliczanie niewykonanych przypadków, kompletność logów oraz regresje odrzucające nieoczekiwane pominięcia. DIC-0692 pozostaje zadaniem pierwotnego inwentarza; ten suplement nie przypisuje mu migracji.

## Do wątku 9

Przy integracji kontrolować przypisanie nowych wpisów CTest do parsera; zachować hash efektywnej wersji polityki w dowodzie. Raport i kod W8 są tylko odczytane, bez zmian w `.github`, progach lub źródłach produkcyjnych.
