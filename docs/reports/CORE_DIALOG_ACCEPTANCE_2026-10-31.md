# Odbiór rdzenia dialogu HTTP — 2026-10-31

Baza i kod produktu: `1d53bd0fcdd902cbac1809ab7cdfa96fd0c1fe19`.
Przyrost P0 korzysta z przyjętego BYOK bez ponownej implementacji. Wszystkie dane
są syntetyczne, model jest atrapą HTTP na 127.0.0.1, bez płatnych API.

## Zaimplementowano

`loom/server/tests/test_server_dialog_flow.py` używa wspólnego harnessu HTTP
PDF/credential/security, bez dublowania scenariuszy PDF. Tworzy nową rozmowę,
odbiera odpowiedź, wysyła kolejną turę z rzeczywistą wcześniejszą wiadomością
i odpowiedzią, odczytuje historię, restartuje własny serwer na tym samym świeżym
katalogu i kontynuuje. Dostawca zapisuje faktycznie otrzymane requesty, a test
porównuje pełną sekwencję ról i dokładne polskie treści.

Następnie dostawca zwraca kontrolowany HTTP 400. Kontrakt błędu: końcowe `error`,
bez `done` i fałszywej odpowiedzi; wcześniejsze ukończone tury pozostają bez zmian,
a wysłana wiadomość użytkownika zostaje zachowana. Po kolejnym restarcie test
sprawdza tę samą historię, przywraca dostawcę i kontynuuje z pełną historią.
To rozróżnia błąd dostawcy po zapisie wysłanej wiadomości od odmowy nieczytelnego
PDF przed zapisem jakiejkolwiek części tury.

## Weryfikacja

Bazowy odbiór: zwykły dialog 1/1 PASS (13,01 s), PDF 5/5, HTTP poświadczeń
9/9 i C API 2/2, łącznie 4/4 CTest PASS (121,70 s, zero skipów).

Po zielonym rdzeniu dodano test API `context.receipt` → `graph.navigate`,
ze źródłami i powodami pominięcia oraz porównaniem SHA-256 receipt z rzeczywistym
requestem dostawcy. Kontrolowany profil ma okno historii 2 wiadomości;
„Szerzej” ma wysłać 4, następna tura znów 2, dwa świadome kroki 8 wiadomości.
Test sprawdza rzeczywiste treści requestów i reset, nie same deklarowane liczby.

Bazowy dodatkowy test wykrył błąd: pojedynczy krok był zapisany jako
`['wider', 'wider']` (zestaw dialogu 1/2 PASS, 1 FAIL, 22,91 s). Przyczyna to
ponowne dopisanie identycznej sekwencji przy obliczaniu węzłów i okna historii.
Najmniejsza korekta `apply_steps` w `loom/src/chat/chat_engine.cpp` odbudowuje
listę dla pokrętła. Zachowuje świadome powtórzenia kroków użytkownika.
Bez zmian ABI, schematu danych ani istniejących asercji.

Po poprawce: PDF **5/5 PASS**, dialog i ślad/graf/Szerzej **2/2 PASS**, C API poświadczeń **2/2 PASS**; ponowiony zakres HTTP poświadczeń **2/2 PASS**. Zero skipów. Implemented/tested zakresowo przez HTTP; GUI i urządzenie niewykonane.
Odpowiedź atrapy nie dowodzi jakości prawdziwego modelu.

Istnieje browser harness `loom/web/e2e/m1-context-sent-native.mjs` z wąskim
widokiem 390×844, ale w tym worktree brakuje `vite` i własnego `web/dist`.
Preflight `node --input-type=module -e 'await import("vite"); await import("playwright"); console.log("vite/playwright dostępne")'`
uruchomiony z `loom/web` zakończył się exit 1, `ERR_MODULE_NOT_FOUND: vite`.
Zależności nie instalowano. GUI, kliknięcia „Co wysłaliśmy”/„Pokaż w grafie”,
widok telefonu i odbiór urządzenia są niewykonane; HTTP ich nie zastępuje.

## Przekazanie

Bramki 09:00 PL / 12:00 PL pozostają odbiorem integratora; ten zakresowy test nie
jest deklaracją ich spełnienia ani odbioru GUI/urządzenia. Bez publikacji/push,
CI lub wdrożenia. Odwrócenie: cofnięcie korekty śladu, usunięcie nowych testów/harnessu i ich rejestracji
CTest, bez migracji danych. Locale i `App.tsx` nietknięte; CMake scala integrator.


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
