# W6 — Niezależne dowody i import

Start: `README.md`, `../STATE.md`, źródła wymagań i istniejące wyniki.
Rola niezależnego recenzenta; nie stroisz modelu na materiale, który oceniasz.

Własność: nowe niezależnie napisane fixtures/scorers/receipts, dokumentacja
formatów i zagrożeń. Błędy produkcyjne odsyłaj z reprodukcją do właściciela pliku.

Cel: ocenić działanie całego przepływu źródło → kontekst → request → wynik,
bez mylenia poprawnego protokołu z poprawnym zrozumieniem rzeczywistych danych.

1. Napisz przypadki z wyjątkami, odrzuconymi wariantami, zmianą zdania, cytatem
   innego autora, sprzecznością i brakiem przesłanki. Ustal oczekiwania przed wynikiem.
2. Sprawdź payload przechwycony przez niezależny transport i trwałość śladu po
   ponownym otwarciu/reindeksacji. Oddziel request przygotowany od faktycznie wysłanego.
3. Import: byte/leaf recount, gałęzie, załączniki, nieznane pola, locatory;
   nie nazywaj syntetycznego testu walidacją całego prawdziwego eksportu.
4. Dane właściciela odczytuj wyłącznie w zakresie przydzielonego zadania.
   Surowe archiwa, rozmowy i klucze nie trafiają do repo ani dostawcy modeli.
5. Dokończ mapę zagrożeń T12 z rzeczywistych przepływów: lokalne dane, provider,
   narzędzia, logi, eksport, pamięć; nie zastępuj jej automatycznymi zakazami.

Kryteria: dokładne SHA/komendy/mianowniki, zachowane pierwsze negatywne wyniki,
niezależność autora oceny, granice wnioskowania. Nie czytaj chronionych holdoutów;
ich otwarcie wymaga osobnego zamrożonego protokołu i decyzji integratora.
