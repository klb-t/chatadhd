# Wykonana próba restore i narzędzie operatora — 2026-11-01

Wykonano brakującą próbę: rzeczywisty zaakceptowany serwer uruchomił odtworzony
root kont na osobnej ścieżce. Health 200, accounts=true, anonimowy odczyt rozmów
401; łagodne zamknięcie exit 0. Po zamknięciu serwera ta sama pakietowa lib
odczytała dokładnie 3 istniejące rozmowy i 2 oryginały z poprawnymi SHA256.
To odbiór syntetycznego restore, nie restore produkcji ani zgoda na wdrożenie.

## Źródła i brak ponownej integracji

Baza i końcowy HEAD głównego worktree:
`77efca1647fb506d41a606a0fb25e82d10bdb74a`; testowany kod
`ccb5ef9d9a4879bc9634db7296a60ce4d35404ce`, runtime tree
`633ce9cfc9b909426d88b258dbef877782b1e5fe`.
Delta narzędzia/docs/testu powstała wyłącznie we fresh source
`.integration-backup-restore-20261101/source` od tej bazy. Nie scalono jej do
produktowego HEAD w pierwszym etapie. Następny przyrost odebrał tę deltę
i niepusty WAL: [wynik integracji](BACKUP_TOOLS_WAL_2026-11-01.md). Historyczne
patch/manifest/source pozostają bez zmian. Nie budowano ani pakowano.

Użyto istniejącego pakietu `ccb5ef9d-release-log-control-20261101`, SHA256
`95bb540ca669dc6410af75aa135434800374957bea7a6cc7a5d2359cfd643481`.
Server SHA `9f7f35e21838876a94361c1fa916f0b9373bd574687afe8fd8946fa1dd9c5137`;
lib SHA `b923709ed2dacb9eafa0cf553e72f5127e14a29a9fd8fb25c52b732666184358`.
Wybrano nowego kandydata, ponieważ jest bieżącym przyjętym artefaktem, ma
odebraną zgodność schematów i startup loggera; nie powtarzano kompatybilności
poprzednik→kandydat→poprzednik. Stare pakiety i dowody zachowane.

Przejrzano `deploy/install.sh`, backup w `tools/gcp/deploy_guests.sh` i wcześniejszy
`ADMIN_BACKUP_ROLLBACK_PLAN.md`. Istniejący skrypt łączy kopię z usługami/VM i
przełączeniem, nie zapewnia wymaganych odmów/metadanych, więc go nie uruchamiano.
Nowe `deploy/cold_snapshot.py` służy tylko do kopii, bez zarządzania usługami.
[Instrukcja operatora](../../deploy/COLD_SNAPSHOT.md) wiąże go z wcześniejszym planem
oraz istniejącym instalatorem. Kanon §0 rewizja 2: reguły odmów w kodzie,
ścieżki w jawnych argumentach; brak treści użytkowników/poświadczeń w wynikach.

## Przebieg i rzeczywiste wyniki

Źródło: zachowana, zamknięta `synthetic-accounts` z wcześniejszej odebranej bramki,
bez ponownego generatora i uruchamiania oryginału. Inwentaryzacja: 18 plików,
22 podkatalogi i root = 41 wpisów; 5 DB SQLite, 3 WAL po 0 B, 3 SHM po 32768 B.
Całe drzewo obejmuje istniejące bloby, profile/config i metadane. Wszystkie DB
hashy bazowe zgodne z ostatnią zamkniętą fazą poprzedniego testu.

| Kontrola | Wykonany wynik |
| --- | --- |
| Stan przed kopią | Własne procesy bez serwera/writera; 5/5 wyłącznych blokad SQLite; dwukrotna zgodna inwentaryzacja |
| Preflight | 41 wpisów, receipt SHA `b0a37b1b187f8fd69fe7a4fd06381ae08ab8d7572a95e39ede112f9209772702` |
| Cold backup02 → nowy restore/data | Pełne bytes + UID/GID/mode/times/xattrs zgodne; oba manifesty SHA `78f27e8569621b8e210456a1a66c58ad16421b19eb0a29289c1b8f867f678c87` |
| Uruchomiony restored candidate | Końcowy loopback port 41447, health 200, auth/me potwierdza tryb kont bez sesji, conversations 401, SIGTERM exit 0 |
| Accounts przed/po | 2 konta, 8 rekordów sesji, schema SHA `85146e088642e2566c3e56db0daaead31c5e639939056f8a0826e90dd79c3550` |
| Runtime A/B przed/po | 2+1 rozmowy, 0 wiadomości; schema SHA `5526c30128dcac8a14a7b50b46f72146e6867cf5dca9ba40b3a8f9f7b90c4a92` |
| SQLite przed/po | 5/5 integrity=ok, 0 FK violations; DDL/count/_meta identyczne, user_version=0; runtime schema_version=4, loom_schema_version=1, import/annotation=1 |
| Pakietowe native API po stopie serwera | List/get dokładnie 3 rekordy rozmów zgodne z odtworzoną DB; blob_read 2/2 oryginały, po 43 B, eof=true i pełne SHA |
| Źródło i backup02 po próbie | Niezmienione 41/41 każde; canonical tree SHA `502cb91b80b865e9e0aa2420cb4a3d6d69dda6171c0c4038f45bee3a896be369` |

Oryginał A: `8b962f5f8a264244b8244e03f85103c94650174f0210002810c0c2a1a3408f31`.
Oryginał B: `00e0dc5f54643e90424f8a29cca14cc4a3b0ecc32b382615e7df9b026823a9db`.
Nie odczytywano kolumn password/sessionhash ani nie wypisywano treści rozmów,
oryginałów i credentials. Odczyt rozmów/oryginałów przez lib jest lokalną kontrolą
odtworzonych runtime, nie ponownym uwierzytelnionym HTTP ani testem sesji.
Pełny zachowany snapshot wiąże te rekordy z oryginalną fixture.

Rzeczywisty serwer i lib miały Landlock obejmujący wyłącznie odtworzone dane,
wybrany pakiet, skrypt/input i nowy output. Oryginalna fixture i backup były
niedostępne. Whitelist env: PATH/LANG/TMPDIR/LOOM_LOG_ENABLED=0/PYTHONDONTWRITEBYTECODE.
Podczas samej kopii Landlock dopuszczał O_RDWR dla pięciu źródłowych DB wyłącznie
na potrzeby POSIX lock; kod nie wykonuje zapisu ani zapytania SQLite do źródła.
To jawny wyjątek deskryptora blokującego, poparty niezmiennością hashów/metadanych.
Wszystkie własne serwery zakończone; żadnych usług ani danych produkcji.

## Testy narzędzia i zachowane niepowodzenia

`deploy/tests/test_cold_snapshot.py`: **10/10 praktycznych przypadków PASS**:
roundtrip bytes/metadata/xattr/ACL; odmowa istniejącego celu; overlap; brak
deklaracji stop; symlink ucieczki; symlink w rodzicu; nieaktualny preflight;
zły hash snapshotu; wiszący symlink INCOMPLETE; aktywna transakcja SQLite (EAGAIN 11 mimo deklaracji stop).
Test transakcji użył osobnej kopii zamkniętej DB, nie oryginału/generatora kont.

Pierwsza próba metadanych zakończyła się EINVAL 22 dla ACL UID991 w jedno-UID
namespace UID990→0. Zachowana w `tool-tests-01.log`; nie uznano tego za PASS.
Dwie kolejne próby mają 9/9; ACL dotyczy wyłącznie bieżącego UID, obcy właściciel
wymaga administratora. Same-UID ACL i `user.*` sprawdzono na dodatkowym pliku
syntetycznym, ponieważ przyjęta fixture nie miała xattrs.

Pierwszy restore poprawnie odmówił: pomocniczy test skopiował plik backup1 zwykłym
odczytem i zmienił jego atime. Diagnoza potwierdziła jedyną różnicę: atime jednego
pliku; treść niezmieniona. Naprawiono odczyt testu na O_NOATIME i kontrolę stat,
bez osłabienia porównania. Backup1/błędy zachowane. Nowy backup02 z niezmiennego
oryginału miał identyczny manifest; z niego restore i rzeczywista bramka przeszły.

Odpowiadające etapom komendy zapisano w `commands.json`; dokładne końcowe argv/env/Landlock są w `final-tool-commands.json` i `restore-isolation-03.json` obok dowodów. Kolejność:
preflight → backup, praktyczne odmowy → odmówiony restore1 → poprawa harnessu
→ backup02 → 9/9 → restore2 → izolowane `restore_gate.py` → kontrola niezmienności.
Końcowy review usunął `Path.exists()` dla INCOMPLETE: teraz `stat` jest fd-relative
z follow_symlinks=false. Dodano rzeczywistą odmowę wiszącego znacznika. Ta zmiana
uzasadniła końcowy test narzędzia 10/10 oraz restore do nowego `restored-03/data`
i ponowne uruchomienie wyłącznie tej bramki. Wynik końcowy jest związany z finalną
deltą; pierwszy poprawny restore i wcześniejsze błędy pozostają historią.
Skrypt `deploy/tests/verify_restored_accounts.py` jest bajtowo identyczny z użytym
`restore_gate.py`. Dane wejściowe pozostają prywatne; wynik zawiera tylko schemat,
liczniki i hashe. Skrypt odmów wymaga świeżego `--work`; nie uruchamiać ponownie
ukończonego katalogu.

## Granice i next

Nie testowano produkcyjnego snapshotu, jego kluczy, obcego UID/GID/MAC, RPO,
operatora w oknie, awarii zasilania ani pełnego/nonempty WAL replay (WAL fixture
były puste). Rozmowy mają metadane, lecz zero wiadomości. Nie wykonywano nowych
loginów, cross-account matrix, GUI, modeli, buildów ani restore produkcji.
Narzędzie odmawia symlinków/hardlinków/special i mountów na innym urządzeniu.
Nie dowodzi wykrycia bind mountów na tym samym urządzeniu; topologię kontroluje
operator. Nie jest uniwersalnym
backupem wszystkich topologii. Blokady advisory i hashe nie zastępują zatrzymania
writerów przez administratora. Nie zachowuje ctime/birthtime/inode ani sparse layout.

Audyt i integrację narzędzia oraz brakującą próbę WAL zapisano w podlinkowanym
nowszym raporcie. Administrator nadal odpowiada za rzeczywistą kopię, metadane/restore,
zgodę RPO i odrębną decyzję wdrożeniową. Ten przyrost kończy się na syntetycznym
restore; nie uruchamia kolejnego cyklu.
