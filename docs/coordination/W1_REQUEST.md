# W1 — Kompilator aktywnej instrukcji

Start: `README.md` w tym katalogu i aktualne `../STATE.md`. Sprawdź bieżącą
gałąź integracyjną, nie powtarzaj już wdrożonego wpięcia ContextEngine do czata.

Cel: aktualna instrukcja po doprecyzowaniu przez użytkownika trafia do
rzeczywistego zapytania, zachowując źródła, wersje, wyjątki i odrzucone warianty.

Własność: `loom/src/chat/`, nowe testy `test_chat_*`, własne receipts.
Zmiany `chat_engine.h`, runtime/C ABI uzgodnij z ROOT; nie edytuj kontekstu lub web.

1. Odczytaj R26/R34, kontrakty T1, evaluator T2, bieżące ślady requestu i tests.
2. Zaimplementuj najmniejszą użyteczną ścieżkę przyjęcia wersjonowanego
   ActiveTaskSpec. Nie uznawaj dowolnego streszczenia za aktualną wolę użytkownika.
3. Zachowaj wybieralny sposób składania historii oraz mapę fragmentów instrukcji
   do źródłowych wiadomości. Import historii nie dowodzi wysłania requestu.
4. Sprawdź przechwycony payload fake transportu: ostatnia instrukcja, wyjątki,
   brak ponownego włączenia odrzuconej propozycji, bieżąca wiadomość dokładnie raz.
5. Osobny agent pisze kontrprzykłady i przegląda semantykę poprawek.

Nie dodawaj nowego modelu do odgadywania ActiveTaskSpec bez oceny fidelity.
Najpierw deterministyczny kontrakt i rzeczywisty konsument; modele są wymienne.
Wynik: commit, testy, znane ograniczenia i receipt według `README.md`.
