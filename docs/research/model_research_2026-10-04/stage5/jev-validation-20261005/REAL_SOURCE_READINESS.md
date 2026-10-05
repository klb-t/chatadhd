# Wznowienie 7C na rzeczywistych eksportach

Wymaganie właściciela z 2026-10-05 18:16 Amsterdam zastępuje preparację nowego
eksperymentu na syntetykach. Nie wysyłać wcześniej przygotowanych 192 body.
Pierwszy wspólny wycinek, jego wersję i status udostępnia 7A w aktualnym pliku
`express20261005/COORDINATION.md` / `.json` na gałęzi badania. Nie zakładamy,
że starsza deklaracja `express_resume_authorized` dotyczy nowych danych.

Przed collection potrzebne są trzy odrębne zamrożenia:

1. Źródło rzeczywiste: hash eksportu/wycinka, sposób wyboru rozmów, dokładne
   locatory jednostek, role, gałęzie oraz rzeczywiste timestampy. Nie tworzyć
   fikcyjnych tur ani dopisywać twierdzeń do oryginalnych wypowiedzi. Ślad
   transformacji reprezentacji źródła jest danymi, nie zmianą oryginału.
2. Gold: konkretna relacja, uporządkowane argumenty, mówca i as_of, etykieta
   supported/refuted/unknown, cytat i locator ze źródła. Niejasność jest jawna;
   pytanie rozmówcy samo nie oznacza jego afirmacji. Gold i review zamrożone
   przed collection; bez korekt po wynikach. Oddzielny nowy hash, bez podmiany
   historycznego `2f6e0410...`.
3. Nowe żądania: j_active/j_directed z ich dokładnymi wersjami pytań/przepisów,
   parametrami, pełnym układem operacji i hashami body. Cel pozostaje porównaniem
   96/arm na 24 rodzinach ×4 pytania, z 12 PL/12 EN, jeżeli wspólny rzeczywisty
   wycinek dostarcza ten zadany układ. Brakujące języki/rodziny ujawnić przed
   freeze; tłumaczenie nie tworzy nowej rzeczywistej rodziny. Nie korygować
   populacji po obejrzeniu odpowiedzi. Wykonawca pozostaje wyłącznie u 7A.

Zamrożony helper `score_first_only.py` jest deterministycznym instrumentem
powiązanym z konfiguracją; można rozważyć nową konfigurację do prawdziwych źródeł
po sprawdzeniu reprezentacji. Stary config i jego hashe pozostają historyczne.
Nie zmieniać progu >0,5, nie naprawiać JSON, nie wybierać późniejszej odpowiedzi,
nie usuwać braków z mianownika. Dotychczasowe osiem kontroli i mechanika
porównania/eksportu pozostają zachowane; nie dowodzą jakości na nowych źródłach.

Kontrakt publikacji musi być jawny przed pierwszym publicznym zapisem.
`programme_results_v1.normalize()` zachowuje body żądań, a dotychczasowy
`jev_validation_results_v1.export()` zapisuje pełne NORMALIZED/REQUESTS i
FROZEN_*_INPUT. Te ścieżki są poprawne dla fikcyjnego publicznego korpusu, ale
**nie używać ich jako publicznego eksportera rzeczywistych prywatnych rozmów**.
Prywatny pełny replay należy trzymać w przeznaczonym dla niego miejscu.
Publiczne datowane oceny mogą referować hashe prywatnych dowodów i zatwierdzoną
bezpieczną projekcję; ta projekcja musi jawnie określać usunięte pola i stratę
odtwarzalności publicznej. Nie twierdzić, że publiczny agregat jest pełnym
prywatnym replay. Publikowane metryki/parametry też wymagają tego kontraktu.

Przy dostawie od 7A przygotować osobny replay, datowane twierdzenia o metodach,
PL/EN, wszystkie pary, protokół i dostępność osobno, rzeczywisty koszt wyłącznie
z rachunków generacji oraz pełne negatywy w dozwolonym prywatnym/archiwalnym
przechowaniu. Przypinać wersje i produced_by; żadnej automatycznej adopcji.
Historycznych 80/96 i 89/96 nie łączyć z nowym wynikiem.
