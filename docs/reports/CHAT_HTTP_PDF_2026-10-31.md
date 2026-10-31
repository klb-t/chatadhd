# Rzeczywisty HTTP PDF — 2026-10-31

Baza i kod produktu: `1d53bd0fcdd902cbac1809ab7cdfa96fd0c1fe19`.
Zakres P0: testy prawdziwego `loom-server` z multipart upload, trwałą historią
oraz dostawcą HTTP na 127.0.0.1. Wyłącznie fixture mechanizmu z repo i świeże
syntetyczne dane. Przyjęta granica poświadczeń/BYOK pozostaje bez zmian.

## Zaimplementowano

Pięć osobnych scenariuszy w `loom/server/tests/test_server_pdf_flow.py`:

1. `unicode-compressed.pdf`: upload, dokładny tekst dwóch stron w requestcie
   dostawcy, kolejna tura bez ponownego załączenia, odczyt historii przez HTTP,
   restart własnego procesu na tym samym nowym katalogu i kolejna tura.
   Sprawdzane są role, kolejność, powiązanie pliku z pierwszą wiadomością oraz
   oryginalne bajty przez pobranie HTTP i SHA-256, również po restarcie.
2. `mixed-text-blank.pdf`: dokładny numer pustej strony `[3]`, liczba stron 3,
   polskie ostrzeżenie w wysłanym kontekście, metadanych odpowiedzi i `done`;
   zachowanie notatki po reopen/restart i załącznika w kolejnej turze.
3. Kontrolowany limit 10 znaków: dokładnie 78 oryginalnych znaków, zachowane 10
   znaków Unicode (`Zażółć gęś`), widoczne przycięcie i trwałość liczb/notatki.
4. PDF bez tekstu: domyślna odmowa, zero requestów dostawcy, zero wiadomości
   także po restarcie, oryginalny upload pozostaje nienaruszony.
5. PDF z hasłem: ten sam kontrakt odmowy i trwałości.

Treść fixture sprawdzono niezależnym lokalnym odczytem Poppler-GLib przed
ustaleniem oczekiwań: strona 1 `Zażółć gęślą jaźń — PDF PAPUGA. Strona 1.`,
strona 2 `Druga strona: Łódź, żółw, ąćęłńóśźż.`. Między stronami zachowany
jest separator form feed; całość ma 78 znaków, wariant z pustą stroną 79.

Wspólny `server_flow_harness.py` wykorzystuje start/cleanup serwera z istniejącego
harnessu credential i parser SSE z harnessu security. Dostawca zapisuje rzeczywiste
surowe requesty UTF-8 i zdekodowany JSON do JSONL; opcjonalny
`LOOM_FLOW_EVIDENCE_DIR` zachowuje zapis pod CWD po sprzątnięciu danych testowych.
Asercje dotyczą requestów dostawcy, a nie treści wymyślonej przez atrapę.

## Weryfikacja

Baza przed poprawką: CTest 4/4 PASS, w tym PDF 5/5, zwykły dialog 1/1,
HTTP poświadczeń 9/9 i C API poświadczeń 2/2 (121,70 s, bez skipów).
Składnia Python 3/3 PASS jest osobną kontrolą statyczną.

Warunkowy test istniejącego śladu kontekstu ujawnił błąd produktu: przy profilu
z oknem historii pojedyncze „Szerzej” zapisuje w receipt dwa kroki, choć żądanie
jest rozszerzone raz. Wynik bazowy: dialog rozszerzony 1/2 PASS, 1 FAIL
(22,91 s); dokładna asercja `['wider', 'wider'] != ['wider']`. Przyczyna:
`apply_steps` dopisuje ten sam krok dla każdego pokrętła. Minimalna korekta
w `loom/src/chat/chat_engine.cpp` odbudowuje listę zastosowanych kroków przy
obliczeniu danego pokrętła. Świadome powtórzenia pozostają osobnymi krokami.
Ekstraktor PDF, ABI i schematy danych pozostają bez zmian.

Po poprawce: PDF **5/5 PASS**, dialog i ślad/graf/Szerzej **2/2 PASS**, C API poświadczeń **2/2 PASS**; ponowiony zakres HTTP poświadczeń **2/2 PASS**. Zero skipów. Implemented/tested zakresowo przez HTTP; GUI i urządzenie niewykonane.

## Granice i odwracalność

Odpowiedź atrapy nie dowodzi jakości ani rozumowania prawdziwego modelu. Bez OCR,
DOCX, `graph_packet`, eksportu całych rozmów z bajtami i nowego UI. Odbiór GUI
oraz prawdziwego telefonu niewykonany. Nie wdrażano i nie publikowano.
Odwrócenie: cofnięcie dwóch linii korekty śladu, usunięcie nowych testów/harnessu i rejestracji CTest; bez migracji
lub zmiany źródłowych danych. CMake scala integrator.


## Dokładne komendy i środowisko

Wszystko z głównego CWD własnego worktree; Debug `-O0 -g0`, WERROR, systemowy
SQLite i istniejący Poppler-GLib. Każdy build `-j2`, CTest `-j1`, TMPDIR pod CWD.

```sh
export TMPDIR="$PWD/.pdf-core-verification/tmp"
export CCACHE_DIR="$PWD/.pdf-core-verification/ccache"
cmake -S loom -B .pdf-core-verification/build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g0' -DLOOM_BUILD_SERVER=ON -DLOOM_BUILD_CLI=OFF -DLOOM_BUILD_TESTS=ON -DLOOM_SHARED=ON -DLOOM_WERROR=ON
timeout 1500 cmake --build .pdf-core-verification/build --target loom-server loom -j2
timeout 240 cmake --build .pdf-core-verification/build --target loom-server loom -j2
cmake -S loom -B .pdf-core-verification/build
export PYTHONDONTWRITEBYTECODE=1
export LOOM_FLOW_EVIDENCE_DIR="$PWD/.pdf-core-verification/build/http-evidence-initial"
timeout 480 ctest --test-dir .pdf-core-verification/build -j1 -V -R '^(server\.(pdf_flow|dialog_flow|guest_credentials)|capi\.credential_override)$'
LOOM_FLOW_EVIDENCE_DIR="$PWD/.pdf-core-verification/build/http-evidence-context" timeout 120 ctest --test-dir .pdf-core-verification/build -j1 -V -R '^server.dialog_flow$'
timeout 120 cmake --build .pdf-core-verification/build --target loom-server loom -j2
LOOM_FLOW_EVIDENCE_DIR="$PWD/.pdf-core-verification/build/http-evidence-fixed" timeout 100 ctest --test-dir .pdf-core-verification/build -j1 -V -R '^(server\.(pdf_flow|dialog_flow)|capi\.credential_override)$'
LOOM_SERVER_BIN="$PWD/.pdf-core-verification/build/server/loom-server" timeout 35 python3 loom/server/tests/test_server_guest_credentials.py CredentialBoundary.test_endpoint_change_without_byok_never_sends_operator_a_to_b CredentialBoundary.test_byok_before_endpoint_change_and_after_restart
git diff --check
```

Konfiguracja exit 0. Pierwsze wywołanie buildu: exit 124 po 285/292 zadaniach
(timeout bez błędu kompilatora); kontynuacja tego samego drzewa w tej samej
rezerwacji: exit 0. Kompilacja przyrostowa minimalnej korekty: exit 0.
Bazowy CTest: exit 0, 4/4. Dodatkowy bazowy dialog: exit 8, 1/2 przypadki PASS,
błąd podwójnego kroku receipt. Końcowy CTest: exit 0, 3/3 zestawy i 9/9 przypadków
(PDF 5, dialog 2, C API 2); zakresowy credential HTTP po korekcie: exit 0, 2/2.
Żadnego pominięcia w wykonanych zestawach. Nie wykonano pełnej bramki/native unit,
ABI/FFI, web build ani GUI/urządzenia; brak nowych eksportów ABI.

Serwer po korekcie SHA-256: `bf3441b99f357229ce69430fc170f9bfd2f84baec4b75c1af39104d17e696e06`.
Biblioteka po korekcie SHA-256: `465b04bff1789e170d6a1b5c8e6b49f8d05b4f0cd2780156de006736d36de265`.

Bramki **09:00 PL: niewykonane; 12:00 PL: niewykonane** — odbiór integratora,
bez deklarowania odbioru GUI, telefonu, publikacji lub wdrożenia.
