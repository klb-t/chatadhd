# PASS4 — Watchdog

Baza `58a0c93bd0135e3715dcbc4d92fb80e61bd31215`, main bez zmian. Nowe hipotezy i nieprześledzone wcześniej zakresy, bez ponownego masowego skanu.

11 prób: 3 reprodukcje PASS, 3 akceptacje FAIL, 4 kontrakty PASS, 1 integracja close/reopen PASS. Reprodukcje dowodzą wad, nie poprawności produktu. Zero prób transportu zewnętrznego, guard blokuje fetch/http/net.

- **A4-WD-001:** API schema usuwa wybraną metodę i plan. Wykonawca bez schema odrzuca niezatwierdzoną metodę; po schema wykonuje alternatywny analizator. Nie oznacza to dostępu do cudzych danych: kontrola właściciela source_run_id istnieje w route.
- **A4-WD-002:** ANALYSIS pobiera obserwacje source_run_id, a manifest i eksport pytają current run_id. Giną przyczyny braków, flagi i hash wejścia; source_run_id jest zachowany, Hi nadal ma isMissing.
- **A4-WD-003:** duplikaty tego samego entity/role rozstrzyga niejawne last-wins. Hi zmienia się 25→50 przy odwróceniu tego samego zbioru danych. Test nie traktuje wiadomości/list jako przemiennych.

Pozytywne zakresy: przestawienie kluczy konfiguracji zachowuje hash i wyniki; kolejność jednoznacznych obserwacji nie zmienia wyniku; nieudany fetch kończy FAILED z trwałym zdarzeniem, bez manifestu COMPLETED; prawdziwa baza i manifest przeżywają zamknięcie i ponowne otwarcie.

Fixture jest syntetycznym testem mechaniki, nie pomiarem naukowym. Badano realny schema, analizator, repozytoria SQLite, orchestrator, lokalny store i funkcję eksportu. HTTP route prześledzono semantycznie; nie wykonano pełnego HTTP/browser ani dostawcy. Nie zmieniono produktu ani zablokowanej metody JH16. Zakresy i nieprześledzone wywołania w coverage.json; kompletne pakiety odbioru w handoff.json.
