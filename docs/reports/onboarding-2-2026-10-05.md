# W12 — drugi przyrost: dane prezentacji i jedno źródło presetów

Gałąź: `gpt/onboarding-2-2026-10-05`. Baza nowej gałęzi:
`66da570d3b5379492e128d940ad474467082c59f` integratora (main e4109df + przyjęty W5).
Poprzedni W12 `3c0bc36552ef9851f1174946cfb108549aae3228` pozostaje bez zmian i w kolejce.
Ten checkpoint opisuje zakres, nie gotowość implementacji.

Inwentarz W11 powstał przed W12; brak `docs/reports/data-in-code/thread-12.md`.
W12 przygotowuje osobny suplement swoich nowych plików, bez dopisywania grup
ani wyników do pierwotnych 695/213. Przeczytano thread-10/thread-11 z W11
`2eb65d475cb6671dbe6388c53bb63eec74ea92a8` i bieżący INDEX.
R42 odczytano z wymagań właściciela na gałęzi Claude, commit
`3cd5848e7484d50d00f19f0855bc2cd228ad7f3b`; main nie zawiera jeszcze tej sekcji.

Zakres następnego przyrostu: teksty i presety prezentacji własnego UI, sześć
wyjaśnień warstw, usunięcie drugiej ręcznej kopii settings/privacy z deklaracji
RuntimeProfile. Domyślne angielskie teksty, wartości, akcje i zachowanie mają
pozostać identyczne. Mechanizmy lifecycle, CAS, prywatności i wykluczeń nie są
przedmiotem zmiany. Testy offline, zapisane syntetyczne odpowiedzi, zero modeli.

## Do wątku 9

Pierwszy W12 nadal wymaga przyjęcia. Drugi przyrost przygotowywany jest w
odrębnym, odłączonym worktree do porównań; nie zmienia oryginalnej gałęzi.
Nie kopiujemy do nowej gałęzi foundation czekającego w kolejce. Produkcyjny
przyrost zostanie oparty na stanie integratora zawierającym przyjęty foundation.
Dotychczasowe kolejka i progi obowiązują.

## Do wątku 10

Prezentacja onboarding będzie danymi i wartością istniejącej warstwy domyślnych.
Kontroler zachowuje akcje i filtrowanie/model token/CAS. App/transport/nawigacja
pozostają po Waszej stronie; nie edytujemy innych plików web ani serwera.

## Do wątku 3

Policy gate i ślad metod pierwszego W12 pozostają bez zmiany. Ten przyrost
nie jest produkcyjnym podłączeniem selektora/writera.

## Do wątku 11

Nie powstaje drugi loader/schema interpreter. W12 usuwa własną drugą kopię
presetów runtime przez wyprowadzenie z istniejących wpisów i bindings.
Wasz bloker duplikacji usage W2 nadal występuje na nowszym refie 2eb65d4;
sam nowszy commit nie oznacza jego zamknięcia.
