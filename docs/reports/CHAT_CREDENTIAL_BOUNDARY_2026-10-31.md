# Granica poświadczeń gościa przy zmianie źródła — 2026-10-31

Baza: `9533d68e6cf6e133e0fdcd5d5fcf7ee85654da08`. Przyrost lokalny, bez commita i wdrożenia. Wszystkie dane utworzone na potrzeby testu; dwa dostawcy HTTP na `127.0.0.1`, różne porty przydzielone przez OS. Wyłącznie fikcyjne znaczniki kluczy, bez prawdziwych modeli i API.

## Odtworzenie na bazowym runtime

Potwierdzono błąd, nie tylko podobieństwo kodu: po `PATCH /api/config` zmieniającym `base_url` z A na B, bez zmiany `loom_guest_source`, B dostał nagłówek `Authorization: Bearer fake-operator-A-credential-boundary`. Dotyczyło to `POST /api/chat` oraz odświeżenia katalogu przez `POST /api/models/refresh` (żądanie dostawcy `GET /models`). Po świadomym zapisie BYOK C API również nadal używało overlay operatora.

Bazowy runtime: SHA-256 serwera `4575e7ec7906372313072a38b3b4a3dc4e5a7f1246bdf4d1aebda6817d86b9ca`; biblioteki `5f7788a8fbae0a6dd756c540f211dae6d3d20a84f57a6231829de03d4de442e1`.

Pięć scenariuszy HTTP i dwa C API zakończyły się oczekiwanymi negatywami na konkretnych wartościach. Wcześniejsza próba HTTP miała błąd w nowym harnessie: oczekiwała 200 zamiast kontraktowego 201 przy tworzeniu rozmowy; nie jest dowodem granicy poświadczeń. Po poprawieniu wyłącznie tej oczekiwanej wartości wszystkie pięć scenariuszy dotarło do asercji poświadczeń i wykazało błąd. Logi lokalne: `.flow-verification/baseline-http.log`, `baseline-http-contract-corrected.log`, `baseline-capi.log`.

## Zakres poprawki

- `Secrets::set` usuwa tylko odpowiadający zapisywanemu kluczowi wpis overlay. Obejmuje istniejące C API oraz zapis sekretu przez akcję ustawień. Dotyczy też pustej wartości; pozostałe wpisy overlay zachowują pierwszeństwo i nie trafiają do pliku sekretów.
- Serwer odłącza overlay, kiedy gość zmienił źródło/adres albo zapisał własny klucz. Kontrola odbywa się przy uzyskaniu kontekstu dla każdego żądania gościa, przy jego odtworzeniu oraz przed automatycznym odświeżeniem/przełączeniem puli. Obejmuje katalog modeli poza listą `model_routes`.
- Wybrane źródło ma dodatkowe pole `auth_secret` z nazwą slotu poświadczenia (bez wartości), aby kontrola korzystała z nazwy zadeklarowanej przez dostawcę. Brak nowych funkcji C ABI i migracji danych.

Testy sprawdzają dokładny nagłówek po stronie dostawcy: operator A działa na A, bez BYOK B nie otrzymuje żądania, świadomy BYOK B działa także zapisany przed zmianą adresu i po restarcie serwera. BYOK dla pierwotnego endpointu również przetrwa restart. Osobne C API sprawdza zapis BYOK, pustą wartość, zachowanie niezależnego wpisu overlay oraz dokładną zawartość nowo utworzonego pliku sekretów.

## Wynik poprawionego runtime

Build C++ serwera i biblioteki z `LOOM_WERROR=ON`: PASS. Serwer SHA-256 `36bc0c4b3784844512624f6fb0d6707a018337e912071ea9624a3efcf6d96705`; biblioteka `3f342ca6a9bd5dc9d74294adda9a0c8c7e1cb8f5f07b5f451e0334d010a334a7`.

- HTTP: 5/5 scenariuszy PASS (jeden wpis CTest), 23,15 s; `.flow-verification/fixed-http.log`.
- C API z rzeczywistym lokalnym HTTP: 2/2 scenariuszy PASS (jeden wpis CTest); `.flow-verification/fixed-capi.log` i `fixed-capi-verbose.log`. Powtórzenie z `-V` zachowuje również PID/porty i liczbę przypadków w logu.
- Natywne istniejące źródła `test_config.cpp` i `test_capi.cpp` z nową regresją: 23/23 przypadki, 6567/6567 asercji, zero pominięć. Osobny mały runner z tych dwóch plików, połączony z rzeczywistą, poprawioną `libloom_core.a`; `.flow-verification/native-tests.log`.
- Kontrakt ABI, eksportowane symbole i FFI smoke: 3/3 PASS; `.flow-verification/abi.log`. Brak nowych eksportów C ABI.
- `git diff --check`: PASS. Istniejące asercje bez osłabienia.

Dokładne komendy i przekazanie: `CHECKPOINT.md`. Nie deklarujemy pełnej bramki repozytorium, audytu wszystkich możliwych nadpisań tras dostawców ani dowodu przepływu PDF/GUI/telefonu. Następny przyrost pozostaje osobną próbą rzeczywistego uploadu PDF, pytania o drugą stronę i trwałych ostrzeżeń po ponownym otwarciu rozmowy. OCR, DOCX, graph_packet i eksport bajtów pozostają otwarte.

Odwrócenie: cofnięcie wyłącznie delty tego przyrostu; źródłowe bajty i schematy baz pozostają bez migracji.
