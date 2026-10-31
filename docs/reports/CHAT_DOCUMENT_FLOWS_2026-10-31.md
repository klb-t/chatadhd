# Przepływy dokumentów w rozmowie — 2026-10-31

PDF był odrzucany przez `ChatEngine::check_attachments`, także w gałęzi poprawek `b3263b05`. Obecność menu „Załącz plik” i zielone testy dotychczasowego zakresu nie oznaczały obsługi dokumentów ani kompletności przepływów wymaganej przez R54 i R68.

Zmiana dodaje lokalny odczyt warstwy tekstowej PDF przez stabilne C API Poppler-GLib ≥0.82, bez dodatkowego modelu i bez zmiany oryginalnych bajtów. Referencja pliku zostaje w wiadomości, tekst jest odbudowywany z pliku dla kolejnych tur zwykłego czatu. Dotyczy to również dotychczasowych tekstów i obrazów: historia zachowuje teraz części multimodalne. Ekstrakcja PDF przechodzi przez istniejące sprawdzanie sekretów. Lista rozszerzeń, adapter, biblioteka i komunikaty są w `runtime/chat_context.pack`, z nakładką użytkownika przez istniejący mechanizm profili.

Czytnik nie jest OCR ani analizą układu strony. Nie odczytuje wykresów i obrazów. Strony bez tekstu są wskazane numerami; całkowity brak tekstu, uszkodzenie, hasło lub brak biblioteki domyślnie odmawiają wysyłki przed zapisem wiadomości i wywołaniem dostawcy. Istniejące ustawienie `chat_attachment_on_unreadable=send_with_warning` nadal obowiązuje. Ostrzeżenia i przycięcia zostają przy odpowiedzi i są widoczne po ponownym otwarciu rozmowy. Ustawienie długości jest wspólne z plikami tekstowymi: preset 15000 znaków, 0 = bez limitu, przycięcie z liczbami.

## Macierz bieżącego zakresu

| Przepływ | Stan i granice dowodu |
| --- | --- |
| PDF z warstwą tekstową → blob → żądanie modelu | Zaimplementowano; test z dwoma skompresowanymi stronami i polskim Unicode. Wyniki poniżej. |
| Kolejne pytanie o wcześniej załączony plik, także po otwarciu rozmowy ponownie | Zaimplementowano odtwarzanie części załączników dla zwykłego przepływu `chat_turns`; test dwóch kolejnych tur i ponownego otwarcia runtime. |
| Brak tekstu / hasło / uszkodzenie / brak czytnika | Jawny wynik, domyślna odmowa, bez wywołania dostawcy; testy mechanizmu. |
| Dokument częściowo tekstowy | Tekst plus numery stron bez tekstu i trwałe ostrzeżenie; nie jest to wynik OCR. |
| Długi dokument | Istniejące ustawienie długości i widoczne przycięcie; brak dowodu pełnego wyszukiwania po dowolnie dużym PDF. |
| Skan, tabele jako obrazy, diagramy, układ strony | Otwarty przepływ wizualny/OCR. Ten przyrost go nie realizuje. |
| DOCX, ODT, EPUB i inne formaty dokumentów | Nadal nieobsługiwane przez ten czytnik. Dawna implementacja Python nie dowodzi obsługi w obecnym jądrze. |
| PDF w natywnym/offline Androidzie lub Windows | Otwarty adapter; obecny Poppler działa w jądrze Linux. Telefon z webem używa czytnika na serwerze po wdrożeniu. |
| Treść plików w przepływie `graph_packet` | Odrębna ścieżka selektora; ten przyrost nie dowodzi odtwarzania historycznych plików w pakiecie grafowym. |
| Import/eksport całej rozmowy z bajtami plików | Wymaga osobnego dowodu; kopia samych wierszy/nazw nie jest kopią dokumentów. |
| Edycja, regeneracja, rozgałęzienie, artefakty, wyszukiwanie, głos i narzędzia | Mają istniejące implementacje i testy cząstkowe. Pełna zgodność z przepływami aplikacji dostawców nie została potwierdzona; wymagają osobnej macierzy akceptacyjnej. |
| Upload i wysyłka na prawdziwym polskim telefonie | Nadal brak próby urządzenia/HTTP/GUI; unit i build tego nie zastępują. |

## Zależność i odwracalność

Na Linux wymagane jest `libpoppler-glib.so.8` z obsługą `poppler_document_new_from_bytes` (Poppler ≥0.82). Instalator Debian/Ubuntu i zadanie CI Loom uwzględniają `libpoppler-glib8`. Tryb `--no-deps` pozostawia zarządzanie zależnością operatorowi. Brak biblioteki daje jawny błąd, bez zmiany danych i bez automatycznego przejścia na płatne API. Brak zmian ABI eksportowanych funkcji. Nie instalowano pakietów systemowych w ramach pracy nad kodem.

Źródła kontraktu C API: [PopplerDocument](https://poppler.freedesktop.org/api/glib/PopplerDocument.html), [PopplerPage](https://poppler.freedesktop.org/api/glib/poppler-Poppler-Page.html), [GLib Bytes](https://docs.gtk.org/glib/ctor.Bytes.new.html). Używany jest publiczny, stabilny interfejs C; prywatny interfejs C++ Popplera nie jest wiązany. Parser nie uruchamia programów ani powłoki.

Stan produkcji odczytany przed pracą: `2d39b07c`. Ten dokument nie potwierdza wdrożenia. Nie przełączano usługi ani danych użytkowników.

## Weryfikacja

- Build C++ jądra, `loom-server` i testów: PASS. Build TypeScript/Vite: PASS (pozostaje ostrzeżenie o wielkości bundla).
- Regresja 30 zestawów CTest obejmujących rozmowy, aktywne zadania, kontekst, profile i C API: pierwotnie 29 PASS / 1 FAIL. Dwie dokładne asercje w `test_chat_engine` oczekiwały starego obiektu przycięcia bez nowego pola `message`; dodano oczekiwany pełny komunikat, zachowując sprawdzenie wszystkich wcześniejszych pól, liczb i wysłanych bajtów. Odrębne powtórzenie całego zestawu `test_chat_engine`: PASS. Pierwotny wynik FAIL zachowany w dowodach pracy. Nie jest to nowy pełny przebieg wszystkich testów repozytorium.
- Nowe scenariusze PDF obejmują: skompresowany tekst i polskie Unicode na dwóch stronach, oryginalne bajty/bloby, kolejne pytania i ponowne otwarcie runtime, sprawdzanie sekretów, dokument bez tekstu, hasło i uszkodzenie, częściowe odczytanie stron, ustawienie długości oraz brak biblioteki z odmową lub świadomą wysyłką z ostrzeżeniem. Osobny test przechodzi przez C API: treść do dostawcy i ostrzeżenie w `done`. Dostawcy są atrapami transportu testowego; nie jest to pomiar odpowiedzi prawdziwego modelu ani OCR.
- Web: 56/56 testów w pięciu zestawach (menu, chipy/ostrzeżenia i trzy projekcje kontekstu), bez pominięć i błędów. Wspólny mechanizm historii jest również sprawdzony dla tekstu i obrazów oraz jawnego wyłączenia historii.
- Generatory danych runtime i onboardingu `--check`, `bash -n deploy/install.sh`, `git diff --check`: PASS.

`attachment_notes[].message` jest nowym opcjonalnym polem komunikatu; stare pola liczbowe i statusy zostają zachowane. Notatki są też zapisywane w metadanych odpowiedzi i przekazywane przez końcowy fragment C API. Brak zmiany schematu tabel bazy i brak migracji źródłowych wiadomości. Czytnik nie zmienia ich tekstu ani referencji do plików.

Brak końcowego testu HTTP/GUI i prawdziwego telefonu; wdrożenia nie wykonano. To ograniczenie dowodu pozostaje mimo zielonych kontroli offline. Kolejny przegląd ma przechodzić po rzeczywistych przepływach i brakach z macierzy, nie zamykać wymagań samą obecnością elementów menu.
