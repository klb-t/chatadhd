# W4 — Transformacje grafu i trwałe wykonanie

Start: `README.md`, `../STATE.md`, istniejące GraphPacket, AnalysisPlan,
`../../loom/tools/coordination` i najnowsze receipts. Nie twórz drugiego
kanonicznego grafu ani kolejnego równoważnego schedulera.

Własność: `loom/tools/coordination`, przydzielone przez ROOT adaptery wykonania
GraphPacket/AnalysisPlan i odpowiadające testy. Zmiany schematu native idą przez ROOT.

Cel: istniejące transformacje grafu działają przez wspólną ścieżkę wykonania,
z rejestrem przydziału, kosztem, wynikiem i odzyskaniem po przerwaniu.

1. Sprawdź bieżącą koordynację: expiry przed dispatch, unknown po dispatch,
   fencing, jawne uzgodnienie wyniku, zasoby i oryginalne pierwsze odpowiedzi.
2. Podłącz jeden realny konsument (wybrany task/CLI) do dostępnej koordynacji;
   zachowaj oryginalne checks autoryzacji/budżetu/akceptacji.
3. Sprawdź integralność grafu wejście → patch → preview/apply → wynik/receipt;
   akceptacja nie zmienia inferencji w obserwację.
4. Zmierz i opisz rzeczywisty narzut cache/koordynacji; nie pomijaj przygotowania.
5. Osobny agent atakuje przerwania, dwa procesy, nieaktualny fence, zmianę
   konfiguracji i spóźniony wynik. Nie przypisuj dokładnie-jednorazowych efektów
   zewnętrznych mechanizmowi, który ich nie potrafi zagwarantować.

Kryteria: działająca ścieżka dla użytkownika/operatora, restart, trwałe receipts,
brak cichego ponowienia. SQLite jest współdzielony lokalnie, nie przez kopie Git.
Duże symboliczne plany nie alokują z góry wszystkich wykonawców.
