# Odbiór P0 locale / PDF / dialog — 2026-10-31

Przyjęty kod P0: **`9f672641e5eb223cf7bdc5561faca08dc679a753`**.
Implementacja przyjęta po własnym buildzie i zakresowych testach backendu;
**GUI/mobile i wydanie niezaakceptowane, deployed: nie**. Ten SHA jest bazą
do osobnego worktree G1. Nie trzeba czekać na macierz dostawców ani OCR/URL-share.

## Uzupełnienie własnej bramki web/GUI — 2026-10-31 21:12 UTC

Na `cab9ca88d725c9d78cd8bceb8993443c2969624f` wykonano własny
`npm --prefix loom/web run build` (**PASS**, Vite26,64s; ostrzeżenie o dużych
chunkach), `lang-page-native` **9/9 PASS** i `polish-person-native` **8/8 PASS**.
Zapisane wyniki zakończono przed przerwą; po wznowieniu zweryfikowano logi,
receipts, czyste źródła, hashe serwera/biblioteki (identyczne z opisanymi poniżej)
i zapisano manifest hashy bundla, dowodów i syntetycznych screenshotów.
Manifest lokalny SHA256: `6a7a46318926c019240d4e271198c3b56f7ecee4fff96b5a8be1731ff8933523`.
Nie powtórzono ukończonych prób po przerwie ani 12HTTP+2 regresji backendu.

**Korekta poprzedniego rozpoznania:** preflight zależności był fałszywie negatywny.
Pakiety należy rozwiązywać przez `createRequire` względem `loom/web/package.json`;
własne React/ReactDOM/Vite/Playwright i cache Chromium są dostępne. Brak native TS
w Node nie blokuje istniejącego toolchaina. Poniższy zapis o braku wszystkich
zależności jest historycznym błędem diagnostyki, nie bieżącym blokerem środowiska.

GUI sprawdziło jawne EN/PL, zapis na dysku, reload/reopen, profil językowy,
PDF dwóch polskich stron w faktycznym żądaniu HTTP do atrapy, pytanie bez
ponownego załączania, historię, trwałe polskie ostrzeżenie o pustej stronie,
nawigację istniejącego grafu i widoczny błąd dostawcy. Testy używały własnego
serwera, syntetycznych profili/PDF i dostawcy loopback; wąski viewport Chromium
nie jest fizycznym telefonem ani dowodem jakości rzeczywistego modelu.

**GUI przyjęte w powyższym zakresie; pełne wydanie nadal niezaakceptowane.**
Zachowane luki produktu: widoczny surowy komunikat HTTP503, etykiety `user`/
`assistant`/`v1`, komunikat o kluczu przy syntetycznym bezkluczowym źródle oraz
wcześniejsze braki pełnej polonizacji. PASS scenariusza błędu oznacza widoczność
błędu i zachowanie rozmowy, nie poprawną lokalizację i przyjazne sformułowanie.
Bez fizycznego urządzenia, rzeczywistych modeli, pełnego repo gate i wdrożenia.

## Podstawa i pochodzenie

Integrator wznowił `08c6d1d15e54e0be7f7d605fbc90d066e02ebccd`.
Zachowano wcześniejszy BYOK i odbiór macierzy dostawców jako przodków.
Obaj wykonawcy pracowali od `1d53bd0fcdd902cbac1809ab7cdfa96fd0c1fe19`;
nie resetowano HEAD do tej bazy. Publiczna baza fali pozostaje
`9533d68e6cf6e133e0fdcd5d5fcf7ee85654da08`.

| Pakiet | Task / checkpoint | SHA256 śledzonego patcha |
| --- | --- | --- |
| Locale | `chatadhd-p0-locale-20261101` / `1793475525216770347` | `c3d3761cbfa48edfd05e507dcaaa7c1c4111c2e49a9d2b76063563d534767d2d` |
| PDF/dialog | `chatadhd-p0-pdf-core-20261101` / `1793477633662228719` | `2c8f54c81891905eda36817c7a084aff2f1c743bf687d5ab61439b0b0b78c0c8` |

Stan `completed` posłużył do ustalenia zakończenia pracy, nie do akceptacji.
Przed odczytem i po nałożeniu porównano aktualne receipts, checkpointy,
HEAD, status, patch, listę nowych plików oraz sumy wszystkich wybranych plików.
Starszy admission opisywał PDF jako pracujący; nowsze zgodne audyty końcowe
potwierdziły stabilność. Archiwów root-only nie odczytywano.
Z PDF wybrano 7 plików produktu; 857 wpisów prywatnych artefaktów pozostało
poza odbiorem. Locale: 9 plików produktu. Checkpointy nie są częścią produktu.

Przejrzano pełną treść 8 nowych plików i diff 8 śledzonych. Oba jawne patche
przeszły `git apply --check`, następnie nałożenie i kontrolę sum 16/16.
Brak konfliktu własności plików.
Locale zapisano jako `85bda3dd2f8b6ce6d35005c58b3911662f53534a`,
PDF/dialog z testami integratora jako `9f672641e5eb223cf7bdc5561faca08dc679a753`;
oba lokalne `[skip ci]`. Sumy manifestów wszystkich plików produktu (w tym nowych):
locale `1dbfb69d9424195effdabe03b442aaa5fa3a140857697ff1b176301d21d9c732`,
PDF `cb08048e6b620f7e391199e642a1fc4d618b694fcb49e88ad0b4c7e3b9b09b79`.
Lokatory checkpointów i hashe są również w opisach commitów. Integrator dodał osobno dwa testy HTTP
(locale i tekst) oraz rejestrację ich w CTest; nie rozszerzył implementacji formatów.

## Zakres zmiany

Locale odczytuje zapisany profil przy otwarciu aplikacji, uwzględnia
`preference.language` po jawnym `ui_language` oraz odświeża konfigurację powłoki
po potwierdzonym zapisie karty ustawień. Etykiety/opisy nadal są w danych.
Duży diff osadzonego profilu wynika z granic chunków: po zdekodowaniu 108 bloków
zmieniły się tylko dane `settings_fields` i ich suma źródłowa. Generator `--check` PASS.

PDF dodaje testy rzeczywistego HTTP, historii i restartu. Poprawka w
`apply_steps` czyści listę `applied` przed obliczeniem kolejnego parametru:
jedna dyspozycja `wider` ma jeden wpis w receipt, dwie mają dwa. Nie dodano
nowego czytnika PDF, nie zmieniono dawnej poprawki BYOK ani ABI/tabel bazy.

## Własne wykonanie

Własny build **PASS**; CTest **4/4 zestawy, 12/12 przypadków**, bez skip.
Końcowy przebieg na zapisanym SHA wskazanym wyżej: **4/4, 12/12 PASS**,
120,85 s, 0 skip. Log i syntetyczne transkrypty zachowano oddzielnie.
Pierwszy przebieg na identycznych bajtach trwał 118,29 s; nie sumujemy
powtórzeń jako nowego pokrycia. Porównanie 18/18 blobów commita z manifestem
przejrzanych/budowanych/testowanych plików PASS.

| Własna kontrola | Wynik | Faktycznie wykazany zakres |
| --- | --- | --- |
| `server.pdf_flow` | 5/5 PASS | 2 strony PL (78 znaków), multipart HTTP, treść w żądaniu, następne tury, reopen/restart, SHA bajtów; strona bez tekstu i przycięcie z trwałymi komunikatami; odmowa PDF bez tekstu/z hasłem przed dostawcą i częściową historią |
| `server.dialog_flow` | 2/2 PASS | Historia/kolejność, restart, kontrolowany HTTP400 dostawcy i dalsza rozmowa; receipt odpowiada żądaniu, źródła/pominięcia oraz `wider` raz/dwa razy/tylko jedna tura |
| `server.text_flow` (integrator) | 3/3 PASS | Dokładna treść UTF-8/TXT, Markdown i `.py` jako tekstu w żądaniu, dalsza tura bez ponownego attach, reopen/restart i oryginalne bajty |
| `server.locale_flow` (integrator) | 2/2 PASS | `settings.set`: EN/PL/auto zapisane do rzeczywistego nowego config.json, odczyt po restarcie bez zmiany modelu; preferencja EN warstwy user i jawne PL współistnieją po reopen/restart |
| Wybrane regresje `CredentialBoundary` | 2/2 PASS | Zmiana endpointu bez wycieku operatorowego klucza do B; własny klucz przed zmianą endpointu i po restarcie. Wymyślone klucze, tylko loopback |
| Generator runtime / składnia | PASS | `gen_runtime_profiles.py --check`; Python AST 5 plików; `node --check` 3 skryptów. To nie testy GUI |
| Zakres/staging | PASS | `git diff --check`, jawny staging 9+9 plików; przegląd źródeł i skan znanych wzorców sekretów 18/18, nie ogólny dowód braku sekretów |

Dwie regresje poświadczeń trwały 14,02 s na identycznym scalonym drzewie.
Nie ponawiano dawnego odbioru BYOK, jego całych 9 testów, C API ani native/ABI.
Brak nowych eksportów ABI; historycznych wyników nie przypisano do tego SHA.

Własny nowy katalog `.integration-p0-20261031/build`, GCC 15.2,
Debug `-O0 -g0`, WERROR, system SQLite/OpenSSL; cele `loom-server` i `loom`.
SHA256 serwera: `22c533d1df75775e1af3c8bd0905f67193a916d1343b7216042e5b25d5130fa7`;
`libloom.so.0.10.0`: `4c19e976e4a79b2a4e4237cd421b5f4401902d99922645980dad602a733b835e`.
Po odświeżeniu rejestracji CTest: `ninja: no work to do`.
Wyłączna rezerwacja, build `-j2`, testy CTest `-j1`. Bez cudzego binarium,
starych katalogów build/dev i bez instalacji.

Dane są wyłącznie syntetyczne: repozytoryjne małe PDF z polskim Unicode,
blank page i hasłem; generowane w testach TXT/Markdown/plik Python jako tekst;
nowe konta gości, rozmowy, profile i wymyślone klucze. Dostawcy działają tylko
na loopback; proces produktu nie dziedziczy kluczy ani proxy. Porównujemy
rzeczywiste żądanie, treść i SHA256 oryginalnych bajtów. Odpowiedź atrapy nie
jest dowodem rozumowania modelu. Prywatne transkrypty testowe nie są commitowane.

## Bramki niewykonane i ograniczenia

Brak własnego buildu web i GUI: w tym worktree nie ma `node_modules`, `dist`,
React/Vite/Playwright ani przeglądarki. Systemowy Node 22.22.1 zwraca
`ERR_NO_TYPESCRIPT` przy próbie natywnego uruchomienia TS. Dwie próby uruchomienia
resolvera zakończyły się błędem środowiska przed asercjami; nie są PASS ani
negatywnym dowodem działania produktu. Sprawdzenie składni 3 nowych/zmienionych
skryptów `.mjs` nie zastępuje testu React ani kompilacji TypeScript.

Autor locale raportuje GUI 9/9 i scenariusz osoby 8/8; są to wyniki autora
na jego drzewie, nie własna bramka scalonego SHA. Zachowane luki: język warstw
gościa/logowania, niektóre etykiety/ostrzeżenia przy auto i prezentacja błędów.
Brak pełnej polonizacji, własnej akceptacji GUI/mobile i próby fizycznego telefonu.

Brak pełnego CTest/repo gate, ASan/TSan, testu równoległości, release build,
testów prawdziwych dostawców/jakości, OCR, DOCX/XLSX/PPTX i pakietów grafowych
z historycznymi załącznikami. Tekst pliku `.py` nie oznacza wykonania kodu.
G1 źródło–części–graf oraz macierz dostawców są odrębnymi przyrostami.

## Filozofia, gotowość i odwracalność

Punkt odniesienia: kanon §0, rewizja 2 z 2026-10-31. Dane ustawień pozostają
oddzielone od mechanizmu; reset licznika receipt jest inwariantem kodu.
Istnieją poziomy Debug/Info/Warning/Error i nazwy modułów (`loom/log.h`),
próg globalny oraz oddzielne sinks/ring/stderr (`src/util/log.cpp`).
`set_stderr(false)` nie wyłącza ring i pozostałych sinks. Nie wykazano jednego
wyłącznika całego logowania deweloperskiego ani egzekwowanej hierarchii modułów;
nie wykonywano niezwiązanego refaktoru. Istniejące ostrzeżenia w `chat_engine.cpp`
logują także nazwę/ścieżkę załącznika; ich zgodność z zakazem prywatnych treści
nie jest tu potwierdzona. Nowa delta nie dodaje takich logów. Brak rzeczywistych
sekretów w testowych danych źródłowych;
pełny audyt logów produkcyjnych pozostaje poza zakresem i nie został wykonany.

Ocena 2026-11-01 09:00 PL nie może zostać uznana za pełną akceptację wydania
na podstawie samych testów backendu. Do pakietu/testu akceptacyjnego na 12:00 PL
potrzebny jest własny build web i próba GUI PL→EN→PL, reopen, PDF/ostrzeżenia,
dialog/błędy i mobile, wraz z oceną zachowanych luk. Ten raport nie jest zgodą
na wdrożenie. Niczego nie opublikowano ani nie wdrożono.

Zmiany lokalne są odwracalne przez osobne rewerty commitów tego przyrostu
w przyszłym autoryzowanym zadaniu; nie wymagają migracji bazy. Stare commity,
delty i własne buildy pozostają. Nie wykonano resetu ani rewertu. Administrator
może utworzyć nowe worktree G1 od podanego przyjętego SHA; integrator nie
uruchamia następnego cyklu.

## Pakiet do dalszej akceptacji

Odtwarzalny zakres backendu: własny `loom-server`/`libloom.so` z tego drzewa,
CTest `-j1 -R '^server\.(pdf|dialog|text|locale)_flow$'`, a następnie dwa jawnie
wskazane przypadki `CredentialBoundary` (zmiana endpointu i BYOK po restarcie).
Zestawy HTTP zapisują surowe syntetyczne żądania w `LOOM_FLOW_EVIDENCE_DIR`,
który musi być pod katalogiem roboczym testu. Te logi nie są materiałem produktu.

Pozostający test akceptacyjny GUI w osobno przygotowanym środowisku:

1. Polski język urządzenia, świeża sesja; jawny EN → PL przez kartę ustawień,
   zapis, reload i nowe otwarcie. Wybrany model i język odpowiedzi nie zmieniają się.
2. Zapisana preferencja profilu wraca bez otwierania panelu profilu; jawne ustawienie
   UI ma pierwszeństwo. Sprawdzić również warstwy gościa i logowania.
3. Upload syntetycznego PDF z dwoma polskimi stronami; kolejne pytanie bez
   ponownego załączania, reopen i restart. Ostrzeżenie dla pustej strony i przycięcia
   musi być widoczne również po tych operacjach, nie tylko w JSON serwera.
4. Zwykły dialog i kontrolowany błąd lokalnego dostawcy: brak fałszywej odpowiedzi,
   czytelny komunikat i możliwość dalszej rozmowy; sprawdzić wąski ekran/telefon.
5. TXT/Markdown/kod jako tekst: potwierdzić rzeczywistą treść w kontekście i dostęp
   do oryginalnego pliku po reopen. Nie dopisywać obsługi formatów biurowych.

To lista brakujących kroków wydania, nie PASS ani polecenie uruchomienia nowego
workera. Decyzja o publikacji/wdrożeniu pozostaje osobna.
