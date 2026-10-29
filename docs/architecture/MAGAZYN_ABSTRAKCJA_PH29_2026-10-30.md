# Magazyn jako abstrakcja z cechami jako danymi (PH29, dopisek do R103 B8)

Projekt, 2026-10-30, koordynator (Opus). Bez kodu. Stan kodu czytany w worktree `w2b2` na commicie `172bf32e`
(gałąź `w2b2`, zawiera scalone `origin/claude/m1-w1`). Rozwija `BEZ_LOGOWANIA_R103_2026-10-28.md` §6 i §10 (pakiety B7/B8);
tamten dokument pozostaje ważny, ten zastępuje w nim tylko założenie, że „gość” to osobny rodzaj użytkownika.

Słowa robocze (raz): **magazyn** — wszystko, co potrafi przechować dane osoby (pamięć przeglądarki, plik, folder, chmura,
dysk serwera, telefon); **cecha magazynu** — opisana w danych własność magazynu (np. czy przetrwa wyczyszczenie przeglądarki);
**wtyczka** — kod, który umie obsłużyć jeden rodzaj magazynu; **paczka** — przenośna kopia robocza z pakietu B1b
(`loom.guest_bundle/2`, jeden plik JSON z rozmowami, grafem, pamięcią i ustawieniami); **OPFS** — prywatny system plików
witryny w przeglądarce (Origin Private File System); **SQLite-WASM** — oficjalna biblioteka SQLite skompilowana do
uruchamiania w przeglądarce.

## 0. Streszczenie dla właściciela

- Nie ma „gościa” jako osobnego rodzaju użytkownika. Każdy użytkownik ma zero, jeden albo wiele podłączonych magazynów;
  nasze konto to tylko jeden z możliwych magazynów plus dodatki (pula kluczy, synchronizacja).
- Każdy magazyn jest wpisem w danych z opisem swoich cech. Lista magazynów i lista cech są otwarte.
- Propozycja (koordynator): to cechy (a nie to, czy ktoś jest zalogowany) decydują, co aplikacja mówi po pierwszej odpowiedzi i gdzie co zapisuje.
- Przykład zasady wyboru (koordynator, nie słowa właściciela): rozmowy wymagają trwałości, `requires: {durable: true}`; preset do zmiany. Zasady wyboru magazynu też są danymi z presetami.
- Jądro już ma podobny mechanizm dla serwera i komputera (`storage_security.pack`); rozszerzamy go o cechy i przenosimy
  ten sam opis do przeglądarki, zamiast budować drugi, równoległy.
- Pierwsza nowa wtyczka: prawdziwa baza SQLite w przeglądarce (OPFS). Daje zapis tylko tego, co się zmieniło, ten sam układ
  tabel co na komputerze i miejsce na duże dane. Paczka zostaje jako forma do pobrania i przeniesienia.
- Pytań do Ciebie w tym projekcie nie ma (§7).

## 1. Słowa właściciela i mój odczyt

**Słowa właściciela (dosłownie):**

- PH29, 2026-10-13 (`docs/architecture/SLOWA_WLASCICIELA_2026-10-16.md:261`): „serwer z apką webową i kontami Oferujemy i wszystkie bajery tak
  jak dostawcy, przez telefon lub innego rodzaju apki też można się podłączyć do konta, ale że dowolny Storage można podłączyć
  i wszystko dowolnie konfigurować, to można nasze konta olać.”
- 2026-10-29 (spec `s_876d3e3a2863`, poprawka R103): „A ja teraz dopiero załapałem że Wprowadziłem niepotrzebną komplikację bo
  przecież było mówione że zarówno łapka webowa jak i androidowa jak i desktopowa mają mieć możliwość podłączenia dowolnego
  magazynu, lokalnego chmurowego jakiegokolwiek, a logowanie i nasze konto to jest zupełnie opcjonalne”
- 2026-10-29 (tamże): „tak wyniki są tylko takie po prostu żepo świeżym starcie apkałebowanie nie ma żadnej pamięcipoza ROM i
  bieżącą  rozmową. ustawienia przeglądarki pewnie dają do dyspozycji trochę keszu to można wykorzystać Ale zgłaszamy
  użytkownikowi że najlepiej wydzielić katalog na przykład albo skorzystać z konta chmurowego, żeby zapewnić trwałość i
  ciągłość pracy”
- (To nie są słowa właściciela, tylko streszczenie koordynatora; patrz niżej.)

**Streszczenie koordynatora** (zapis dopisku w B8, `BEZ_LOGOWANIA_R103_2026-10-28.md:479-484`, zatwierdzonych słów): „gość” nie
  jest osobnym rodzajem użytkownika — to użytkownik bez naszego konta z dowolnym podłączonym magazynem; bez magazynu aplikacja
  ma tylko ROM (kod, presety, wbudowana wiedza) i bieżącą rozmowę. […] Cechy magazynu (trwałość, limit, widoczność,
  synchronizacja) są danymi i to one decydują o komunikacie po pierwszej odpowiedzi.

**Mój odczyt (interpretacja koordynatora, nie słowa właściciela):**

1. Pojęcie „gość” znika z modelu. Zostaje: użytkownik, zbiór podłączonych magazynów, opcjonalne konto. Słowo „gość” może
   zostać w interfejsie jako opis stanu („nie masz konta”), nigdy jako gałąź logiki.
2. „Dowolny magazyn” to otwarty zbiór wtyczek opisanych danymi; to samo na web, Androidzie i desktopie.
3. Wymienione przez niego magazyny (pamięć przeglądarki, katalog, konto chmurowe) i cechy (trwałość, limit, widoczność,
   synchronizacja) to **przykłady**; lista otwarta (CLAUDE.md, „Lists are examples”).
4. Ocena „tymczasowy / trwały” nie jest etykietą przyklejoną do rodzaju magazynu, tylko wnioskiem z cech (np. pamięć
   przeglądarki po udanym `persist()` jest trwalsza niż bez niego).
5. Zrobione B1–B3 zostają: paczka B1b jest przenośną kopią roboczą, którą każdy magazyn umie przyjąć.

Sprawdzenie z filozofią: nic tu nie ogranicza użytkownika; jedyne listy zamknięte to te wymuszone przez platformę
(§2.6). Brak niezgodności, więc brak pytania.

## 2. Abstrakcja

### 2.1 Magazyn

Magazyn to wpis w danych: `{id, kind, label, traits, config, status}`. `kind` wskazuje wtyczkę (rodzaj obsługi),
`traits` opisuje cechy, `config` mówi, gdzie i jak (ścieżka, adres, odwołanie do sekretu — nigdy sam sekret), `status` mówi,
czy wtyczka istnieje (otwarty tekst; `implemented` / `planned` to presety-przykłady, użyte dziś w `BackendRegistry`). Wtyczka niezaimplementowana jest widoczna
w interfejsie i wyszarzona z powodem (reguła przekazania do testu, BEZ_LOGOWANIA §11).

Operacje wtyczki to wspólne minimum plus opcje ogłaszane jako cechy. Minimum (już istnieje w dwóch kształtach, §4):
zapisz, odczytaj, wypisz, usuń, sprawdź dostępność. Dodatkowe operacje (zapis przyrostowy, zapytania SQL, obserwowanie
zmian, blokada między kartami) wtyczka zgłasza w `traits.ops`, a wywołujący z nich korzysta tylko wtedy, gdy są.

### 2.2 Cechy jako dane (lista przykładów, otwarta)

Cecha to para nazwa → wartość. Wartość jest **otwarta**: logiczna, liczba z jednostką, zakres, tekst albo `unknown` (te rodzaje to format danych; powód: serializacja w JSON, 2026-10-30; otwarcie: nowy rodzaj wartości przez wersję formatu). Cecha
zależna od urządzenia lub chwili (limit miejsca, czy przeglądarka przyznała trwałość) ma źródło i datę — to datowana
obserwacja (rejestr R59), nie stała. Przykłady:

| Cecha (przykład) | Przykładowa wartość | Skąd |
|---|---|---|
| `durable` — przetrwa zamknięcie sesji | `true` / `false` / `unknown` | wtyczka + pomiar |
| `survives_site_data_clear` — przetrwa „wyczyść dane witryny” | `false` dla IndexedDB/OPFS | dokumentacja platformy |
| `persisted` — przeglądarka obiecała nie czyścić sama | wynik `navigator.storage.persist()` | pomiar w chwili startu |
| `quota_bytes`, `free_bytes` | z `navigator.storage.estimate()` | pomiar, z datą |
| `offline` — działa bez sieci | `true` | wtyczka |
| `shared_across_devices` | `true` dla chmury, `false` dla przeglądarki | wtyczka |
| `encrypted_at_rest` | `none` / `ours` (WebCrypto, §6.4) / `provider` | konfiguracja |
| `readers` — kto może przeczytać | „ta przeglądarka”, „Google (konto osoby)”, „serwer właściciela” | wtyczka + warunki dostawcy |
| `latency_ms` | zakres z pomiaru | pomiar |
| `cost` | 0, albo stawka za GB/miesiąc z datą | warunki dostawcy |
| `ops` — operacje dodatkowe | `incremental_write`, `sql`, `random_access`, `watch`, `lock` | wtyczka |
| `max_object_bytes` | liczba albo `unknown` | dokumentacja + pomiar |
| `needs_consent` | „wybór folderu”, „logowanie u dostawcy chmury” | wtyczka |
| `available_on` | web, android, linux, … | wtyczka + wykrycie w chwili startu |

Nazwy cech nie są wyliczeniem w kodzie: nowa cecha to nowy klucz w danych; reguła, która go nie zna, go pomija. Cecha bez
wartości to `unknown`, a nie `false`.

### 2.3 Magazyny jako wpisy (przykłady, lista otwarta)

Przeglądarka: IndexedDB (dziś, paczka B1b), OPFS + SQLite-WASM (§3), Cache API dla plików aplikacji. Plik pobrany i wczytany
(dziś: „Pobierz/Wczytaj archiwum”). Folder wskazany przez osobę (File System Access, `showDirectoryPicker`). Własna chmura
osoby: Google Drive `appDataFolder`, WebDAV (Nextcloud, ownCloud), S3, Dropbox, OneDrive. Kopia robocza na serwerze (to, co
dziś trwa na czas sesji). Kopia na serwerze na życzenie z kodem odzyskania (B8). Katalog danych na komputerze (`~/.chatadhd/`)
i współdzielony katalog Androida — te już są wpisami jądra. Repozytorium GitHub (jest wpis). Pamięć procesu (ulotna; to
„bieżąca rozmowa” ze słów właściciela).

### 2.4 Zasada wyboru (polityka) jako dane

Obserwacja z kodu 2026-10-30: jądro ma `placement`: dla każdej kategorii danych (rozmowy, blob-y, pamięć, ustawienia, …) uporządkowaną listę
magazynów z rolą (przykłady, lista otwarta: `primary`, `replica`, `cache`, `egress`). Rozszerzenie: kategoria może dodatkowo podać **wymagania** i
**preferencje** wyrażone przez cechy, np. (przykład, preset do zmiany):

```
"conversations": { "requires": {"durable": true}, "prefers": ["survives_site_data_clear", "-latency_ms"],
                   "targets": "auto" }
```

`targets: "auto"` = silnik wybiera spośród podłączonych magazynów te, które spełniają `requires`, w kolejności `prefers`;
jawna lista (jak dziś) zawsze wygrywa. Gdy żaden podłączony magazyn nie spełnia wymagań, nic nie jest blokowane: dane idą tam,
gdzie się da (w ostateczności do pamięci procesu), a osoba dostaje **nieblokującą uwagę** z dowodem (która cecha brakuje, w
którym magazynie) i propozycjami magazynów, które ją mają. Zbliżone do komunikatu po pierwszej odpowiedzi z R105 (nie to samo):
jego treść powstaje z cech, nie z „zalogowany/niezalogowany”; treść z cech to propozycja koordynatora. Jak często pytać, aż do „nigdy”, to preset (jak `declined`).

Łączenie magazynów (przykłady, nie wyczerpują): ten sam zbiór danych w dwóch magazynach (szybki lokalny jako `primary`,
chmura jako `replica`); podział kategorii (rozmowy w folderze, cache embeddingów tylko w przeglądarce); magazyn tylko do
odczytu (ROM, wczytane archiwum).

### 2.5 Konto i brak konta — jedna ścieżka

Po świeżym starcie zbiór magazynów jest pusty: aplikacja ma ROM (kod, presety, wbudowana wiedza i pomoc) i pamięć procesu
z bieżącą rozmową. Zalogowanie do naszego konta **dodaje** magazyn „kopia na serwerze konta” i dodatki (pula kluczy,
synchronizacja między urządzeniami); wylogowanie go odłącza, reszta zostaje. Każda inna droga (podłączenie folderu, Drive,
WebDAV) działa tak samo z kontem i bez. W kodzie znikają rozgałęzienia „jeśli gość” w miejscach, które dotyczą danych; zostają
tylko tam, gdzie chodzi o rzeczy naprawdę zależne od konta (pula, limity publicznego punktu — B2, B6).

### 2.6 Listy zamknięte (z powodem i datą)

- Operacje minimum wtyczki (zapisz/odczytaj/wypisz/usuń/sprawdź) — zamknięte, bo to kontrakt interfejsu; nowe operacje idą
  przez `traits.ops`. Powód: jeden interfejs dla wywołujących; konieczne dla wywołujących. Data: 2026-10-30. Otwarcie: wersja kontraktu wtyczki.
- Wartości `persist()` i `estimate()` — narzuca je platforma (standard Storage). Data: 2026-10-30.
Nic więcej nie jest zamknięte.

### 2.7 Jeden opis, dwa wykonawcy

Opis magazynów i polityki to jeden zestaw danych (`storage_security.pack` + nakładka w `<data>/profiles`). Wykonawców jest
dwóch: jądro C++ (`StorageService`, serwer, desktop, Android) i warstwa web (TypeScript, przeglądarka). Rejestr wtyczek web ma
ten sam kształt co `BackendRegistry` (`{kind, status, factory}`), a web dostaje opis przez istniejące API profilu (zakłada się, że tak jest; niesprawdzone — luka 1; do sprawdzenia w pakiecie P2, §8). Gdy kiedyś jądro pobiegnie w przeglądarce (P6), wykonawca web może zniknąć bez zmiany danych.

## 3. Pierwsza wtyczka: OPFS + SQLite-WASM w przeglądarce

### 3.1 Co daje ponad paczkę B1b w IndexedDB

Dziś (obserwacja 2026-10-30) przeglądarka trzyma jedną paczkę jako tekst pod jednym kluczem i po każdej zmianie zapisuje ją całą (INDEX.md, luka 5
raportu B1b). Między innymi (przykłady) OPFS + SQLite daje:

1. **Zapis przyrostowy** — zmienione wiersze, w jednej transakcji, zamiast całego pliku.
2. **Ten sam układ tabel co jądro** — DDL z `loom/src/db/schema_sql.inc` dla tabel z listy dozwolonych paczki
   (`working_copy.cpp:170-175`, lista = preset w danych); paczka i baza są dwiema postaciami tych samych wierszy, bez nowego formatu.
3. **Duże dane** — OPFS ma limit całego originu (zwykle procent wolnego dysku; obserwacja do pomiaru), nie limit wartości
   IndexedDB w pamięci; zapytania bez wczytywania całości.
4. **Podstawę pod pracę offline** i pod jądro w przeglądarce (P6): to już jest prawdziwa baza danych.

### 3.2 Związek z paczką

Paczka zostaje formą przenośną: „Pobierz archiwum” buduje paczkę z bazy OPFS (eksport), „Wczytaj archiwum” wlewa paczkę do
bazy (import, `merge`/`replace` jak dziś). Odtworzenie nowej sesji na serwerze: z bazy OPFS powstaje paczka i idzie
istniejącym `POST /api/working-copy/bundle?mode=replace` (`app.cpp:1428`). Suma kontrolna: paczka zbudowana w przeglądarce musi
dać ten sam `content_sha256` co serwer (`working_copy.h:45-46`); jeśli kanoniczny zapis JSON (liczby zmiennoprzecinkowe) się nie
zgodzi, wtyczka zachowuje też ostatni oryginalny tekst paczki jako zapas — decyzja D4, test w P1 to rozstrzyga.

### 3.3 Skąd zmiany (zapis przyrostowy)

P1: przeglądarka nadal pobiera całą paczkę z serwera (`GET /api/working-copy/bundle`), ale do bazy zapisuje tylko wiersze,
których skrót się zmienił, i usuwa te, których już nie ma — oszczędza zapis na dysku, nie transfer. P3: serwer podaje tylko
zmiany od ostatniego kursora (`GET /api/working-copy/changes?since=…`), z dziennikiem zmian prowadzonym wyzwalaczami
tymczasowymi (TEMP TRIGGER, tylko w połączeniu, bez zmiany schematu na dysku); po restarcie serwera kursor traci ważność i
przeglądarka robi pełne porównanie (D5).

### 3.4 Wiele kart

*Warunkowe: cały paragraf zależy od sprawdzenia w P1 (zachowanie `opfs-sahpool`, luka 2); „jedno połączenie na raz” to założenie, a nie ustalenie — liczba połączeń i wybór lidera są ustawieniem (preset: jeden lider).*

Dziś: ostatni zapis wygrywa (luka 6 B1b). Plan: tryb VFS `opfs-sahpool` (nie wymaga nagłówków COOP/COEP; obserwacja 2026-10-30 (grep):
serwer dziś ich nie wysyła; wybór VFS zmienia się, gdy nagłówki się pojawią, D7; jedno połączenie na raz — założenie o
bibliotece, niesprawdzone, do potwierdzenia w dokumentacji sqlite.org przy P1). Baza działa w dedykowanym Workerze jednej karty-lidera wybranej przez `navigator.locks`; pozostałe karty
wysyłają zapisy przez `BroadcastChannel`. Gdy lider zniknie, blokadę przejmuje następna karta. Przeglądarka bez OPFS lub bez
Web Locks: wtyczka wyszarzona z powodem, zostaje IndexedDB.

### 3.5 `persist()`, limit, czyszczenie

- `persist()` i `estimate()` już są (`guest-store.ts:97-106`); ich wyniki stają się cechami `persisted`, `quota_bytes`,
  `free_bytes` tej wtyczki (z datą pomiaru), więc trafiają do komunikatu z §2.4 zamiast do osobnego `noticePeriod`.
- Brak miejsca (`QuotaExceededError`): zapis nie znika po cichu — uwaga z liczbami i propozycją magazynu z większym limitem.
- Czyszczenie: „Wyczyść wszystko z tej przeglądarki” już usuwa cały katalog OPFS (`guest-store.ts:158-169`); wtyczka musi
  wcześniej zamknąć bazę w Workerze (przypuszczenie: inaczej uchwyty blokują usunięcie) — test w P1.

### 3.6 Zależność

Nowa biblioteka web: `@sqlite.org/sqlite-wasm` (oficjalna, sqlite.org), wersja przypięta dokładnie. To kod, nie nowy dostawca
— żadne dane nie wychodzą z przeglądarki. FTS5 w tej kompilacji: nie wymagany w P1 (zakres pakietu); dostępność do sprawdzenia (jądro używa FTS5,
`CMakeLists.txt:109`); nie wyłączany z konfiguracji.

## 4. Co już jest (obserwacja datowana 2026-10-30, `w2b2@172bf32e`)

- `loom/include/loom/storage_backend.h:46-58` — interfejs `StorageBackend` (put/get/stat/list/remove/probe);
  `:82-87` rejestr rodzajów (`local`, `s3`, `webdav`, `gdrive` — `storage_backend.cpp:41-47`, wszystkie `implemented`);
  `:90-139` `StorageService`: `placement` per kategoria z rolami, kworum zapisu, odczyt z kolejnością preferencji, `transfer`.
- `loom/data/runtime/storage_security.pack:206` (`local_dir`) … `:358-385` (`browser`) — 11 wpisów (dane z 2026-10-30) magazynów z polami
  `class`, `access`, `encryption`, `sync`, `capabilities` (lista operacji: read/write/list/watch), `available_on`,
  `jurisdiction`; `:558` `placement`, `:680` `allowed_storages`; kategorie danych (18, m.in. `conversations`, `blobs`,
  `browser_state`). Brak: cech trwałości, limitu, kosztu; brak `requires`/`prefers`; wpis `browser` mówi
  „localStorage / sessionStorage”, nie zna IndexedDB/OPFS.
- `loom/include/loom/working_copy.h:30-46` — paczka `loom.guest_bundle/2`: eksport/import, lista dozwolonych tabel,
  `content_sha256`; `working_copy.cpp:170-175` lista tabel (domyślny zestaw w danych, preset).
- `loom/server/src/app.cpp:1417` `GET` i `:1428` `POST /api/working-copy/bundle`.
- `loom/web/src/context/guest-store.ts:24-30` interfejs `GuestStore` (save/load/list/wipe), `:52-63` wtyczka IndexedDB,
  `:97-106` `persist()`/`estimate()`, `:109` `noticePeriod` (komunikat z dwóch stanów), `:158-169` czyszczenie (IndexedDB,
  OPFS, localStorage, sessionStorage, wylogowanie). `GuestLayer.tsx:80-90` start: trwałość, odczyt paczki, odtworzenie.
- Dwa interfejsy magazynu (C++ i TS; obserwacja 2026-10-30) i dwa słowniki („storage”, „schowek”/„guest store”) — do zbliżenia w P2.

## 5. Pakiety (każdy ≈ 50–80 wywołań narzędzi, commit + karta wznowienia)

**P1 — wtyczka OPFS + SQLite-WASM (pierwsza).** Worker z SQLite-WASM (`opfs-sahpool`), DDL generowany z `schema_sql.inc` dla
tabel paczki (narzędzie w `tools/`, wynik jako dane), zapis paczki do bazy różnicowo (skróty wierszy), eksport bazy do paczki,
lider kart przez Web Locks, cechy `persisted/quota/free` z pomiaru, zamknięcie przed czyszczeniem. Wpis `browser_opfs_sqlite`
w `storage_security.pack` z polem `traits` (pierwsze użycie). IndexedDB zostaje jako zapas tam, gdzie OPFS nie ma.
Akceptacja: Playwright (Chromium): rozmowa → przeładowanie → wraca z OPFS; druga karta pisze, pierwsza widzi, baza bez
uszkodzeń (`PRAGMA integrity_check`); paczka z OPFS przyjęta przez serwer z poprawną sumą (albo udokumentowany zapas D4);
czyszczenie zostawia pusty OPFS; pomiar bajtów zapisu przy jednej nowej wiadomości (IndexedDB vs OPFS) na danych testowych
o rozmiarze ≥ 50 MB — dane syntetyczne tylko w teście, raport to mówi. Właściciel dotyka: wybór magazynu pokazuje
„Przeglądarka — baza SQLite” z jej cechami; nic poza tym się nie zmienia.

**P2 — cechy i polityka jako dane.** Pole `traits` we wszystkich wpisach magazynów (wartości z datą i źródłem), `requires` /
`prefers` / `targets: "auto"` w `placement`, wybór w `StorageService` i w rejestrze web (ten sam kształt), uwaga nieblokująca
z dowodem; komunikat po pierwszej odpowiedzi liczony z cech (zastępuje `noticePeriod`). Akceptacja: testy jednostkowe
polityki na tabeli przypadków + test własności (losowe zbiory magazynów i wymagań: wybór zawsze spełnia `requires` albo
zgłasza uwagę z brakującą cechą); e2e: ta sama sesja z IndexedDB i z OPFS daje różne komunikaty zgodne z cechami.

**P3 — zmiany przyrostowe z serwera.** Dziennik zmian w połączeniu, `GET /api/working-copy/changes?since=`, kursor z epoką.
Akceptacja: jedna nowa wiadomość = transfer rzędu jej wierszy; restart serwera → pełne porównanie bez utraty danych.

**P4 — folder osoby (File System Access).** Ta sama baza SQLite zapisywana do wskazanego folderu jako migawka plus paczka
(preset: migawka, bo SQLite w zwykłym folderze bez blokad między procesami bywa niebezpieczny, D6; tryb żywej bazy dostępny w ustawieniu, z niewiążącą podpowiedzią); odnawianie uprawnienia po restarcie. Akceptacja:
e2e w Chromium z folderem testowym; plik bazy otwiera się w `loom` CLI.

**P5 — konto = magazyn.** Kopia na serwerze konta jako wpis magazynu; logowanie dodaje magazyn, wylogowanie odłącza; usunięcie
rozgałęzień „gość” dotyczących danych (spis miejsc w karcie P5). Akceptacja: ta sama ścieżka e2e z kontem i bez daje te same
zapisy w magazynach lokalnych.

Dalej (istniejący B8, bez zmian zakresu): Drive `appDataFolder`, WebDAV, szyfrowanie WebCrypto zgodne z `crypto_suite.h`,
kopia serwerowa z kodem odzyskania — już jako wtyczki z cechami. **P6 (najpierw osobny projekt):** jądro Loom w WASM
działające wprost na bazie OPFS — praca bez serwera.

Kolejność P1 → P2 → P3 → P4 → P5 to preset koordynatora; odwrócenie: zmiana w `docs/reports/INDEX.md`.

## 6. Decyzje (odwracalne)

- **D1** Brak rodzaju użytkownika „gość”; „gość” to stan (brak konta), nie typ. Odwrócenie: przywrócenie typu w `auth`.
- **D2** Rozszerzamy `storage_security.pack` i `StorageService` zamiast nowego mechanizmu web-only. Odwrócenie: osobny pack web.
- **D3** Nowe pole `traits` obok istniejącego `capabilities` (to drugie zostaje listą operacji, bez zmiany znaczenia), żeby nie
  łamać czytelników. Odwrócenie: scalenie pól migracją packa.
- **D4** Suma paczki z przeglądarki liczona tak jak na serwerze; gdy się nie da bajt w bajt, zapas: ostatni oryginalny tekst
  paczki w OPFS. Odwrócenie: opcja importu sprawdzająca skróty wierszy zamiast sumy całości.
- **D5** Zmiany przyrostowe przez wyzwalacze tymczasowe (bez zmiany schematu na dysku, niezmiennik zgodności z Pythonem
  nietknięty). Odwrócenie: rozszerzenie sesji SQLite (`SQLITE_ENABLE_SESSION`) albo stała tabela dziennika z migracją.
- **D6** Preset: SQLite w folderze osoby jako migawka (podobna uwaga w `storage_backend.h:10-12`: WAL na dysku sieciowym lub
  synchronizowanym bywa niebezpieczny). Tryb żywej bazy dostępny w ustawieniu, z niewiążącą podpowiedzią; nic nie jest zablokowane.
- **D7** VFS `opfs-sahpool` + lider przez Web Locks (bez COOP/COEP; obserwacja 2026-10-30: serwer nie wysyła nagłówków — wybór zmienia się, gdy się pojawią). Odwrócenie: VFS `opfs` z nagłówkami COOP/COEP na serwerze.
- **D8** Pierwsza wtyczka to OPFS, nie folder ani chmura (zgodnie z kolejką `QUEUE_S1.md:10`). Odwrócenie: kolejność pakietów.

## 7. Pytania do właściciela

Brak. Nowa biblioteka (SQLite-WASM) to kod uruchamiany w przeglądarce, nie dostawca otrzymujący dane; koszt zerowy; brak
sprzeczności z wymaganiami. Magazyny chmurowe osoby (Drive, WebDAV) to jej własne konta i jej decyzja przy podłączeniu.

## 8. Luki

1. Czy web może dziś odczytać `storage_security.pack` przez istniejące API profilu — niesprawdzone (P2 to sprawdza).
2. Zachowanie `opfs-sahpool` (jedno połączenie, wymagane wersje przeglądarek) i obecność FTS5 w oficjalnym SQLite-WASM —
   obserwacje z pamięci koordynatora, niesprawdzone w dokumentacji; P1 zaczyna od ich potwierdzenia.
3. Safari/Firefox: dostępność OPFS w Workerze i Web Locks — do pomiaru w Playwright (WebKit, Firefox), nie tylko Chromium.
4. Załączniki i blob-y (`attachments/`, `blobs/`) dalej poza paczką (luka 2 B1b); OPFS to naturalne miejsce, ale wymaga
   rozszerzenia paczki — do B7.
5. Android i desktop: abstrakcja obejmuje je przez jądro, ale wybór magazynu w ich interfejsie nie jest zaprojektowany tutaj — luka, nie „poza zakresem”.
6. Konflikty przy dwóch magazynach `primary` na różnych urządzeniach (synchronizacja dwukierunkowa) — luka, nie „poza zakresem”;
   obserwacja 2026-10-30: pierwszeństwo ma kolejność `placement`.
7. Pokrycie przestrzeni możliwości: test własności polityki (P2) obejmuje kombinacje cech; kombinacje wtyczek × przeglądarek
   są pokryte tylko tym, co Playwright uruchamia — reszta to luka zgłaszana w raportach.

## Kontrola przed użyciem

Dopisek z 2026-10-29/30, po tanim przeglądzie czterech antywzorców z CLAUDE.md („Check before use”). Wyniki przeglądu to były
twierdzenia do weryfikacji; zastosowano je jako poprawki tekstu (bez zmiany projektu).

Blokujące (7):

1. §0: zdanie „rozmowy muszą przetrwać wyczyszczenie przeglądarki” nie było cytatem właściciela — oznaczone jako przykład
   koordynatora (`requires: {durable: true}`, preset).
2. §2.4: „dokładnie komunikat z R105” zmienione na „zbliżone (nie to samo)”; treść z cech to propozycja koordynatora.
3. P4 i D6: tryb żywej bazy w folderze nie jest już tylko odwróceniem — preset to migawka, tryb żywej bazy dostępny w ustawieniu.
4. §3.4: oznaczony jako warunkowy wobec sprawdzenia w P1; „jedno połączenie na raz” to założenie i ustawienie.
5. §2.7: „web czyta pack przez API profilu” zapisane jako założenie niesprawdzone (luka 1).
6. §2.1: `status` to otwarty tekst; `implemented`/`planned` to presety-przykłady.
7. §3.4 i D7: brak nagłówków COOP/COEP to obserwacja z 2026-10-30; wybór VFS zmienia się, gdy się pojawią.

Brzmieniowe: „Propozycja (koordynator)” w §0; streszczenie koordynatora pod własnym nagłówkiem w §1; pełna ścieżka do
`SLOWA_WLASCICIELA`; FTS5 (nie wymagany w P1, dostępność do sprawdzenia, nie wyłączany); luki 5 i 6 pozostają lukami; lista tabel
paczki jako domyślny zestaw w danych; notatki o stanie obecnym oznaczone jako obserwacje datowane; przypuszczenie o uchwytach w
§3.5; role w `placement` jako przykłady (lista otwarta); rodzaje wartości cech z powodem i datą; „konieczne dla wywołujących”
w §2.6; „Między innymi (przykłady)” w §3.1.
