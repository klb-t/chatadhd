# Wątek 8 — zamknięcie sesji, 2026-10-05

**Status: NOTREADY — do odbioru brakuje pełnego ASan CTest.**
Commit implementacji do review: **`57ad9bb20b5e21b4445d03abb7bcd2e616f63f2d`**.
Gałąź: `gpt/repo-hygiene-2026-10-04`; potomny commit dokumentacyjny zawiera
końcowe dowody i ten raport, bez kolejnych zmian runtime.
Baza i ostatni fetch main: `e4109df7e4af22b461def5f7d62e268d9b9a8825`.
Szesnaście wcześniejszych commitów W8 przeszło czysty rebase; stary tip
`758aba6` zachowano pod `archive/repo-hygiene-before-main-rebase-2026-10-05`.
Nie pushowano main, nie zmieniono STATE ani żadnego istniejącego README poza
własnym `loom/tools/seeding/README.md`. Płatnych/provider calls: **0**.

## Zrobione i liczby

| Miara | Przed | Po |
| --- | ---: | ---: |
| Martwe workflow pilotów OpenRouter | 3 | 0 aktywnych; pełne oryginały w archiwum |
| Workflow w repo | 5 | 2 |
| Seeding unittest | 36 na main / 50 w poprzednim W8 | 69/69, 10,568 s |
| Weryfikacja narzędzi CI | 38 testów | 38/38, 0,897 s |
| Pełne CTest na aktualnym przyroście | pierwsze GCC 109/110 | GCC 110/110, Clang 110/110 |
| Zestawy z dawnym błędnym dowodem 0 przypadków | context/knowledge: 0 | context_engine 18/1364 asercji; knowledge 18/150 |
| Historyczne seeding/results | 26 plików / 13 800 361 B | te same pliki i bajty; testy ich używają |
| Pełne rankingi / metryki przechwyconego V4 | 2135 / 648 | identyczne; 2783 adresowalne wyniki z provenance |

Guard przypadków pozostaje niezmieniony: bez luzowania progów, usuwania testów
lub nowych wyjątków skip. Presety/role/seeding dimensions/projection są danymi
z nakładkami; szczegóły w [raporcie bridge](repo-hygiene-seeding-method-bridge-2026-10-05.md).
Nowy adapter konsumuje istniejący `loom.method_graph/1` i `loom.method_run_trace/1`.
Nie jest rejestrem wykonującym rankery. Verifier ma wspólny preflight ×10,
zapis decyzji, aktualny UTC i jawną symulację wewnątrz Packet przy fixture clock.
66/66 było wynikiem przed tymi poprawkami; **finalna** cała suite to 69/69.

Szkic prezentacyjnego README: [repo-hygiene-readme-draft-2026-10-04.md](repo-hygiene-readme-draft-2026-10-04.md).
W9 dostosuje go do przyjętych przyrostów; dawne liczby w szkicu są historyczne.

## Końcowe bramki

| Konfiguracja / źródło wykonania | Build | Pełny CTest | Guard / skips / stabilność |
| --- | --- | --- | --- |
| GCC13.3/dev, `57ad9bb`, Debug/WERROR/server/shared ON | PASS | **110/110, 160,20 s** | PASS / 0 / źródło i 6 binariów identyczne przed i po |
| Clang18/vendored, `20ddd8d`, Debug/WERROR/server/shared ON | PASS | **110/110, 188,21 s** | PASS / 0 / źródło i 6 binariów identyczne przed i po |
| GCC13.3/ASan+UBSan, Debug `-g1`/WERROR/server ON/shared OFF | pełny build PASS przed parkingiem | **NIE WYKONANO** | nie przypisujemy coverage ani gotowości |

Obie wykonane pełne bramki: **663 native cases, 24 489 asercji, 1336 Python**.
Packet: **4 native / 24 asercje i 16 compat / 0 skip**. Research.structure
rzeczywiście wykonało 859 przypadków; seeding 69. Wyłącznie istniejący opt-in
`unit.test_catalog_scale` pozostaje jawnie 0/0. Kontrola guard/input hashes
oraz niezależny audyt potwierdziły oba wyniki.

[Podsumowanie macierzy](../verification/repo-hygiene-matrix-2026-10-05/final-matrix-summary.json)
łączy manifesty, pełne XML/LastTest.log, receipts, komendy i before/after.
`gcc-final/LastTestsFailed.log` to odziedziczona historia pierwszego timeoutu;
świeże XML, exit0 i guard potwierdzają zero bieżących failures.

Buildy natywne powstały przed przyrostem Python/docs: GCC/ASan na `fedaf6b`,
Clang na `cbf374b`. Osobny ledger przypina pierwotne logi, cache i binaria;
**365 natywnych wejść bez zmian** w obu późniejszych snapshotach wykonania.
To jawny reuse poprawnych binariów, nie twierdzenie o nowej kompilacji po
zmianie dokumentacji. Wstępny web build przeszedł (85 modułów, 6,04 s), jego
zapis jest wcześniejszy; W8 nie zmienia web. Hosted CI nie uruchomiono ponownie;
nie deklarujemy zielonego bieżącego CI na main.

Clang snapshot: `archive/repo-hygiene-clang-fix-validation-2026-10-05`,
**`20ddd8dfaebe00ee6deb9896bad184fb615924dc`**, tree
`015f07134f74eb03e06432d1b78ab3bfcfc08b55`.
Względem własnego `57ad9bb` różni się **tylko app.cpp, 2+/2−**: dwa oryginalne
capture hunki z W10 `23c6e74c50b87c2157ffc317d45fca79aade27fa`.
Trasa `/api/packet` zachowana. Nie importowano reszty starego W10 ani nie
edytowano jego implementacji na gałęzi W8. Clang PASS jest warunkowy od
odbioru tej poprawki W10 przez W9; własny niepoprawiony main wciąż ma blocker.

## Negatywy i ASan — punkt kontynuacji

Pełne negatywne/interrupted logi są w `docs/archive/repo-hygiene-matrix-2026-10-05/`:
OOM przy pełnym `-g`, linker memory kill, ENOSPC podczas ar/link, pierwsze pełne
GCC 109/110, diagnostyka historii, pre-freeze NameError naprawiony bez utraty
starego coverage, oraz zatrzymana próba podczas synchronizacji commit metadata.
Żadnego z tych zapisów nie zastąpiono późniejszym PASS.

Pierwsze GCC przekroczyło existing 60 s w research.structure. Fetch archiwalnej
historii dostarczył oryginalny `b118c80` z wymaganym tree `b122b30`; brakującego
aliasu e8bbae nie odzyskano. Izolowany CTest 1/1 w 33,28 s był diagnostyką;
końcowe pełne GCC i Clang zamykają bramkę oraz dowód liczby przypadków.

Wspólne zasoby: 8 GiB pamięci i dysk 32 GiB. Lokalne Clang/ASan użyły cienkich
archiwów `ar qcT` (wyłącznie magazynowanie obiektów), ASan `-g1` zamiast `-g`;
wszystkie flagi/opcje sanitizera, asercje i WERROR zachowane. Po zakończeniu
buildów wyzerowano własne generated `.o/.a`, aby zwolnić dysk. **Następny build
w tych katalogach wymaga clean/regenerate**, także GCC/Clang.

Podczas odtwarzania spakowanego ASan CLI kontrola odrzuciła kopię:
**87 088 412 B zamiast 89 467 630 B**, EOF gzip. Przyczyna nieustalona;
wyczerpanie dysku poprzedzało parking. Nie użyto częściowego CLI. Receipt:
[asan-restoration-failure.json](../verification/repo-hygiene-matrix-2026-10-05/asan-restoration-failure.json).
Dwa pozostałe zaparkowane ASan exe odtworzono dokładnie; komplet sześciu GCC
binarnych hashy przywrócono. ASan CLI jest celowo pusty; nie wolno uznać
tego za gotowy build do testów. Nie zaczęto nowego kosztownego rebuildu po
poleceniu właściciela kończącym sesję.

Następna osoba zaczyna od:

1. Fetch/rebase na bieżący main, zachowując poprawkę W10 i wpisy innych wątków.
2. W stabilnym środowisku clean/regenerate ASan; przepis/flags/cache/logi są
   przypięte w dowodach. `cmake --build --preset asan --target clean`, potem
   `cmake --preset asan --fresh -DLOOM_BUILD_SERVER=ON -DLOOM_SHARED=OFF
   -DCMAKE_C_FLAGS_DEBUG=-g1 -DCMAKE_CXX_FLAGS_DEBUG=-g1` i pełny build.
   Ustawić faktyczne compiler/SQLite/Python środowiska jak w receipts.
3. Z tego samego źródła zbudować zwykły shared FFI companion według
   `.github/workflows/loom.yml`, albo ponownie dowieść zgodności istniejącego
   dev/libloom.so i cache. FFI pozostaje bez sanitizera; natywne exe są instrumentowane.
4. Uruchomić bez filtrów `run_case.py --source <repo> --preset asan --jobs <n>
   --ffi-companion <libloom.so> --companion-cache <CMakeCache.txt>
   --output <NOWY-PUSTY-KATALOG>`. Wymagane full110/110, guard PASS, 0 Python skip,
   niezmienione source/helper/binary hashes. Nie powielać starych dowodów jako nowych.
5. Dopiero wtedy zgłosić gotowość W9. Jeżeli rebase zmieni runtime/native inputs,
   powtórzyć stosowne pełne bramki i web zgodnie z zasadami integracji.

Natywny full-V4 accept/restart nie został rozpoczęty po końcowym steering.
Istniejące small-native/frozen-pure dowody są jawnie historyczne, unguarded
fixed-clock development fixtures: nie dowodzą finalnego guarda/czasu obserwacji
ani prawdy kandydatów. Patrz prior-proof-annotation.json. Nie ponawiać tiny
×37 bez potwierdzenia właściciela; dozwolony domyślny full V4 ma zapisany ×2,665.

## Do wątku 4

Cztery native packet cases wykonano GCC/Clang; ASan jest nadal pending.
Obecne loom_packet CAPI przypadki są Python/ctypes. Potrzebny osobny natywny
harness C API (init/free, poprawne i błędne wejścia, polityka). Ordinary FFI
companion wykonuje istniejące ctypes suite, ale nie daje sanitizer coverage CAPI.

## Do wątku 10

Odbiór oryginalnych capture fixów przez W9 pozostaje zależnością Clanga;
wąski snapshot zachowuje Packet route i nie kopiuje starego całego app.cpp.

## Do wątku 9

Kod do review: `57ad9bb`; odbiór **WSTRZYMANY do pełnego ASan**. GCC i Clang
po aktualnym rebase są green z rzeczywistymi przypadkami i niezależnym audytem.
Nie traktować dawnych 108/108 ani samego build PASS jako zakończonej macierzy.
Przyjąć oryginalną poprawkę Clanga W10 przed W8, z własnym fetch/rebase,
pełnym CTest i web. Seeding/results zachować — nadal używane. Szkic README
wstawić dopiero po uzgodnieniu finalnego stanu interfejsu; STATE/README/INDEX
pozostają wyłącznie w zakresie W9.
