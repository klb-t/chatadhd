# DOCX derived coordinates — próba odwracalności, 2026-11-01

**Poprzednik nie zachowuje obsługi nowego wystąpienia DOCX in-place.** Odczyt
zwrócił HTTP **409 / conflict / text_source_occurrence_mismatch**. Czat zwrócił
SSE `start,error` z tym samym błędem, bez odpowiedzi i bez wywołania providera.
**Odtworzenie spójnej kopii sprzed zmiany i uruchomienie poprzednika przeszło.**
RPO tej ścieżki to stan z chwili kopii: późniejsze wiadomości, pochodna i wybór
wystąpienia nie zostają zachowane. Nie jest to zgodność in-place ani rollback produkcji.

## Baza i artefakty

Świeży worktree `chatadhd-docx-rollback-5a560710-20261101`, baza
`5a560710f422095510316b4c9fbdb71d313ff6ca`. Odczytano aktualny matching
`docx-ui-integration-admin-receipt.json`, `accepted-base.json` oraz własne wcześniejsze
final-result/binding UI i backendu. Brak wcześniejszego checkpointu/wyników w tym
worktree; śledzone drzewo początkowo czyste. Stary integrator tylko do odczytu.

| Rola | Przyjęty pakiet | SHA256 archiwum |
| --- | --- | --- |
| Poprzednik sprzed derived coordinates | `e77f698e-release-share-20261101` | `d5e7531426acb8243d653956f916ed15fc0efee2bbedd59cfe148f231348f609` |
| Kandydat | `ccada5e5-release-docx-ui-20261101` | `b786b7e654e6660e6382cd7d8ac8434d9ef1517f2e0326ff9003a3abc372ac10` |

Oba archiwa odczytano pod absolutnymi ścieżkami starego integratora. Każde
sprawdzono względem manifestu i istniejącego stagingu: **25/25 plików każdego**,
łącznie ze ścieżkami/symlinkami. Nie rozpakowywano ani nie kopiowano binariów do
nowej próby. Uruchomiono hash-identyczne pliki ze zweryfikowanych stagingów RO.
Server poprzednika: `3f4c670c65efbbf5a73bd7796ecbc63cb1071b19210d4a78fffcbf0b465f7bd2`;
kandydata: `48cd4b760270a420e0109b5176187802b9b850379c9bfb6032c0054b44e556d9`.
Nie używano `/opt`, konfiguracji/usług produkcji, modeli ani cudzych binariów builda.

## Wykonane cztery etapy

| Etap | Wynik |
| --- | --- |
| Poprzednik → nowy syntetyczny root | Jedno konto przez istniejące CLI/password-stdin, login HTTP, jedna rozmowa bez wiadomości i jeden oryginalny DOCX przez upload. Health/auth/odczyt oryginału zgodne. Graceful stop exit0. |
| Zimna kopia → kandydat | `cold_snapshot` preflight/backup/restore do nowej ścieżki. Kandydat użył domyślnego helpera pakietu, zmaterializował DOCX, zapisał selector i dwie wiadomości w istniejącej rozmowie. Jedna rzeczywista odpowiedź lokalnej atrapy z wybranego fragmentu. Stop exit0. |
| Oddzielna kopia stanu kandydata → poprzednik | Konto/sesja i oryginał czytelne, ID selektora zachowane. Explicit read 409; nowa rozmowa testująca kontekst kończy się błędem SSE, **0 wywołań providera**. To jawna niezgodność semantyki, nie PASS kontekstu. Stop exit0. |
| Kopia sprzed zmiany → nowy restore root → poprzednik | To samo konto/sesja, dokładnie ta sama rozmowa i hash oryginału. Trzy DB: integrity=ok, FK=0, DDL/count/meta zgodne przed startem i po stopie. Brak dwóch nowych wiadomości, derived Source i nowego wyboru. Stop exit0. |

Końcowy przebieg: **4/4 etapy wykonane**, in-place compatibility **FAIL**, restore
sprzed zmiany **PASS**. Kod wyjścia harnessu 0 oznacza wykonanie i sprawdzenie
scenariusza; nie oznacza dodatniej zgodności in-place. Na kopii badanej starym
pakietem próba czatu utworzyła nową rozmowę i wiadomość użytkownika, lecz nie
odpowiedź. Nie twierdzimy, że nieudany request cofa wszystkie zapisy. Ten root był
odseparowany od zachowanego stanu kandydata i obu snapshotów.

## Współrzędne, kontekst i schematy

Dane: syntetyczne `loom/tests/fixtures/docx_parts/base.docx`, 1823 B,
SHA256 `c4fb7469f72279f25203b4d297b484415d24d4ab67722063d1f993faf68837dd`.
Pochodny UTF-8: `6ab0adac290b0c372073873affe152c516dda86c1be83c8eb9a30081e56960c0`.
Wybrano wystąpienie akapitu o współrzędnych **derived_utf8_bytes [42,61)**,
19 B, PL, z `original_locator=word/document.xml#body/2`. Provenance wiąże
Source/Unit/observation_refs, oryginalny ZIP, transform/extraction blob i reader
`8a3ed0a585d17d48bc1b42794da7947e7b957eb2f68e632146f6771be55c5f4c`.
Nie są to offsets ZIP/XML ani strony dokumentu.

Atrapa wyprowadzała odpowiedź z faktycznie otrzymanego systemowego kontekstu.
Asercje potwierdziły dokładny wybrany tekst i occurrence ID, brak niewybranego
akapitu/tabeli i brak tekstu źródła w pytaniu. Zapisano hashe żądania/systemu/
odpowiedzi i booleany obecności, bez nagłówków auth lub credentials.
Jedyny POST providera pochodził od kandydata; żadnych GET/realnych API/modeli.
Stary czat użył **nowej rozmowy**, aby historia odpowiedzi kandydata nie mogła
zamaskować braku obsługi selector coordinates.

Nie wystąpiła migracja DDL wspólnych baz. Accounts schema SHA256
`85146e088642e2566c3e56db0daaead31c5e639939056f8a0826e90dd79c3550`, runtime
`5526c30128dcac8a14a7b50b46f72146e6867cf5dca9ba40b3a8f9f7b90c4a92`, FTS
`ecc10fd54f0a95dc8ced9fe6dc6565024806cc1533ecb32c1c065b3539e8d4ff`.
`PRAGMA user_version=0`; runtime `schema_version=4`, `loom_schema_version=1`.
Kandydat po pojedynczym czacie miał dodatkową `usage-policy.sqlite`, otwieraną
również przez poprzednika. Niezgodność dotyczy kontraktu danych wystąpienia,
nie uszkodzenia SQLite. Wszystkie obserwowane DB miały integrity=ok i FK=0.
Odczyty SQL obejmowały DDL, liczniki, wersje i dozwolone metadane — nie wartości
kolumn password/sessionhash.

## Kopia, RPO i niezmienność

Użyto niezmienionego, przyjętego `deploy/cold_snapshot.py`. Każdy writer był własnym
procesem zakończonym i odebranym przez `wait` przed kopią. Preflight → backup →
restore zachował bytes/UID/GID/mode/atime/mtime/xattrs według kontraktu narzędzia.
Manifest kopii sprzed zmiany:
`7667a6cb8526279bf20ef93c6191308da110976ccfe981b9adac1d2b89f5bfcc`.
Manifest kopii kandydata:
`9671f30aaf60c2de938196715256ad2ba79f7ebd23ea670fa3d55ca73ab4e070`.

Kopia sprzed zmiany zakończona **2026-11-01T04:45:37.070102Z** (mtime znacznika
COMPLETE tej syntetycznej kopii). Po restore potwierdzono utratę późniejszych dwóch
wiadomości, pochodnej Source/Unit/Observation, grafu i selektora; wróciły dokładne
liczniki sprzed zmiany, w tym 1 konto, 1 sesja, 1 rozmowa, 0 wiadomości, 1 blob,
1 Source, 0 nodes/links/provenance. Oryginał pozostał dostępny z poprawnym hashem.
To jawny koszt powrotu do snapshotu, bez odzyskiwania zapisów wykonanych później.

Końcowy dodatkowy odczyt `ColdTree` potwierdził niezmienność **4/4** drzew:
oryginalny baseline i jego snapshot (po 27 wpisów), stan kandydata i jego snapshot
(po 47 wpisów). Odczyt był O_NOATIME; brak zmiany danych lub metadanych.
Nie naprawiano ręcznie SQLite i nie stosowano obejścia w runtime.

## Izolacja, limit i zachowana odmowa harnessu

Każdy serwer: `setpriv --no-new-privs --landlock-access fs`, odczyt /usr i jednego
pakietu, RW wyłącznie własnego rootu, /dev/null, jawne data-dir/accounts-root/static-dir,
nowy wolny port 127.0.0.1, whitelist env i `LOOM_LOG_ENABLED=0`. Checkout readera
nie był dostępny. Serwer użył helpera/interpretera z domyślnego manifestu pakietu.
Landlock w tym narzędziu ogranicza system plików, nie jest filtrem sieciowym;
wszystkie adresy próby były loopback, provider lokalny bez klucza, autorefresh
wyłączony, brak rzeczywistego dostawcy. Procesy CLI/server/provider zakończone.

Przed próbą było ponad 2,4 GB wolnego miejsca. Nowe dane, **wraz z zachowaną
pierwszą odmową**, miały najwyżej 5 162 161 B logicznie i 6 070 272 B alokowane
w pomiarach harnessu, znacznie poniżej **128 MiB**. Przed kopiami rezerwowano 16 MiB
w kontroli limitu; pojedynczy plik procesu serwera ograniczał `prlimit --fsize=33554432`.
Nie kasowano starych danych ani nie tworzono nowych kopii dużych pakietów.

Pierwsze wykonanie zatrzymało się po baseline/backup: restore odmówił
`preflight_or_snapshot_changed`. Przyczyną był **mój licznik rozmiaru** używający
`Path.rglob`, który aktualizował atime katalogów kopii. Diagnoza wykazała wyłącznie
te różnice; oryginał baseline bez zmian. Nie osłabiono porównań i nie poprawiano
starych timestampów. Zachowano gate-01/dowody/źródło harnessu; licznik zastąpiono
fd-relative `O_NOATIME`, a zakończony przebieg wykonał się w nowym gate-02.
Nie uruchamiano ponownie historycznych generatorów ani macierzy P0/UI/backend/
G1/accounts/WAL. Credentials pierwszej i końcowej próby były losowe, tylko w pamięci
harnessu i własnych DB; nie zapisano ich w logach, promptach lub raporcie.

## Reprodukcja i granice odbioru

Nowy test: [verify_docx_coordinate_rollback.py](../../deploy/tests/verify_docx_coordinate_rollback.py).
Wymaga świeżego `--work` w tym worktree oraz jawnych `--old-archive/--old-sha256`
i `--candidate-archive/--candidate-sha256`. Dokładne wykonane argv/env i źródłowy hash
są w `.integration-docx-rollback-20261101/command.json`; wynik w `gate-02/result.json`.
Po przerwaniu najpierw checkpoint/wyniki/procesy, nigdy automatyczne ponowienie
tego samego katalogu. Receipt/dane kont i snapshoty pozostają ignorowane i lokalne.

Narzędzie backupu nie zostało zmienione, nie jest nowym deployerem. Nadal obowiązuje
[procedura operatora](../../deploy/COLD_SNAPSHOT.md): niezależne zatrzymanie writerów,
kontrola topologii (st_dev nie wykrywa każdego same-device bind mount), odrębne
uprawnienia/ACL/MAC i uzgodnienie RPO. Advisory locks nie są globalnym freeze.
Nie było produkcyjnej kopii/restore, restartu usług, nowego buildu/pakietowania,
GUI/mobile/modelu/pełnej bramki repo ani wdrożenia. Dotychczasowe artefakty są bez zmian.

**Next dla administratora:** nie przełączać starego executable na danych zawierających
wybrane derived DOCX z oczekiwaniem zachowania kontekstu. Potwierdzona tu ścieżka
powrotu wymaga spójnej kopii sprzed zmiany i jawnej zgody na jej RPO. Zachowanie danych
po zmianie wymagałoby osobnego rozwiązania/odbioru; nie jest wynikiem tej próby.

## Bloker zapisu commita

Jedyna próba `git add -- docs/STATE.md docs/reports/DOCX_COORDINATE_ROLLBACK_2026-11-01.md deploy/tests/verify_docx_coordinate_rollback.py`
zakończyła się exit128: nie można utworzyć
`/var/lib/codex-team/repos/chatadhd.git/worktrees/chatadhd-docx-rollback-5a560710-20261101/index.lock`,
**Read-only file system**. Nie próbowano commita, ponownego add, innego indeksu,
zmiany uprawnień/metadanych ani zapisu do starego worktree. Wynik testu jest gotowy;
delta 3 plików pozostaje niezacommitowana, zachowana jako jawny patch i hashe.
`base_head = result_head = 5a560710f422095510316b4c9fbdb71d313ff6ca`.
Administrator musi umożliwić zwykły staging własnego worktree przed lokalnym
commitem `[skip ci]`; **nie wymaga to ponawiania wykonanej bramki**.
