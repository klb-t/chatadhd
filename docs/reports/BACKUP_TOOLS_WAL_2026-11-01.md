# Odbiór narzędzia cold snapshot i rzeczywistego WAL — 2026-11-01

**Nowa bramka WAL PASS; narzędzie scalone bez zmiany implementacji.** SQLite
zatwierdził zmianę, która pozostała wyłącznie w WAL. Preflight → backup → restore
zachował ją; odczyt odtworzonej DB w trybie read-only zobaczył zatwierdzony rekord.
Kontrola samego pliku głównego go nie zawierała. Produkcyjne backup/restore
pozostają operacją administratora, z osobną bramką topologii i uprawnień.

## Pochodzenie delty

Baza integracji: `77efca1647fb506d41a606a0fb25e82d10bdb74a`.
Task źródłowy: `chatadhd-backup-restore-20261101`, checkpoint
`1793498671388776317`, archive SHA256
`90c78163e2a6fa89ab98b45cb7dad4eb38776c4c7c30563bcff29f8881ce67c6`
(potwierdzenie administratora; prywatnego archiwum nie odczytywano).
Manifest delty `6964c0d2d7e860f5b2b999d41027b8e40e65aee9b0e14e957f6ee989131454f5`,
patch `68200288214959de71bd2cb46ca10b0f3854a12674dc05c893a4de36ec4b6ade`.
48/48 dowodów i 6/6 plików źródłowych zgodne przed i po odbiorze. Jedno
`git apply --check --whitespace=error-all`, następnie jedno apply w głównym
worktree. Bezpośrednio po apply wszystkie 6 hashy zgodne z audytem.

Własne późniejsze zmiany: wykonany test `deploy/tests/test_cold_snapshot_wal.py`,
ten raport, STATE i doprecyzowanie dokumentacji mountów/statusu odbioru.
`deploy/cold_snapshot.py` pozostał bajtowo identyczny:
SHA256 `7d40ab6e67492a8d69095d62530c3a90f7ef653c3c7fa59958e20880908a014e`.
Stary fresh source, patch, manifest i dowody pozostały bez zmian. Nowe artefakty
są w `.integration-backup-tools-integrate-20261101`; nie w produkcie.

## Wykonanie brakującej próby

Wejściem była nowa kopia jednego zamkniętego syntetycznego runtime z wcześniej
zweryfikowanego `backup-02/data`, mainDB SHA256
`37ed2c36de9875fe78aee9ab2934a6421c52249b1ba9752b604a19f74e427696`.
Odczyt wejścia O_NOATIME, hash i stat przed/po zgodne. Nie otwierano go SQLite
ani nie modyfikowano wcześniejszych fixtures. Nie uruchamiano generatorów kont,
loginów, serwerów ani poprzedniej macierzy akceptacyjnej.

Własny krótki proces SQLite 3.46.1 ustawił journal_mode=WAL,
wal_autocheckpoint=0 i synchronous=FULL. W jednej transakcji utworzył syntetyczną
tabelę `cold_restore_wal_canary` i jeden rekord, wykonał COMMIT, sprawdził
zakończenie transakcji i zakończył się przez `os._exit(0)` bez SQLite close.
To kontrolowane zakończenie po zatwierdzeniu, które zachowuje rzeczywisty recovery
WAL; nie zabijano cudzych procesów ani nie dopisywano ręcznie bajtów. Rodzic
zaczekał na exit 0 przed preflight. Nie wykonano checkpointu źródła.

| Asercja | Wynik |
| --- | --- |
| Hash mainDB przed/po COMMIT | Identyczny `37ed2c36…e427696` |
| WAL po zamknięciu procesu | **12 392 B**, SHA `8dc70fdc04c21e5f164456e2eb70df960d6d65db9c38175da7ba0dd4e7d27afe` |
| Odrębna kontrola mainDB bez WAL | mode=ro, integrity=ok, FK=0, tabela canary nie istnieje |
| Preflight zamkniętej nowej fixture | 4 wpisy (root, DB, WAL, SHM), 1 wyłączna blokada SQLite |
| Backup i restore | Ten sam manifest SHA `8217d4f0c6f9ca43df4234a5ed2d2150e768c341a5da6c87af8d3d8abefe9c3e` |
| Restored DB i WAL przed otwarciem | Hashe i rozmiar identyczne z zatwierdzoną fixture |
| Odtworzona DB z WAL | mode=ro + query_only, integrity=ok, FK=0, dokładnie 1 oczekiwany zatwierdzony rekord |
| Źródło po odczycie restore | Pełne 4/4 wpisy zgodne z preflight; przyjęty wejściowy mainDB także niezmieniony |

Preflight SHA `2c9ea73f0e26626ef4a7851dfce65514d0dba91a40af2fcaefeb2fe946635b26`.
Nie użyto immutable=1. Sam mainDB nie wystarczał do odczytu zatwierdzonej zmiany;
różnica nie wynikała z niezatwierdzonej transakcji. Syntetyczna operacja i asercje
są deterministyczne; losowe sole/nagłówki WAL i czasy plików nie muszą dawać tych
samych hashy w kolejnym uruchomieniu. Hashe powyżej wiążą to konkretne wykonanie.

Test wykonano pod Landlock: /usr read/execute, dokładne pliki testu/narzędzia
oraz zaakceptowanego wejścia read-only, zapis tylko do nowego operator-only rootu.
Whitelist PATH/LANG/TMPDIR/PYTHONDONTWRITEBYTECODE; bez dostępu do produkcji,
konfiguracji czy innych rootów danych. Kopie i output mode0700/umask077.
Dokładne argv/env/reguły w `wal-isolation-command.json`; przebieg narzędzia w
`operator/wal-case-01/tool-commands.json`, bez treści użytkowników/credentials.

Polecenie wewnątrz zapisanej izolacji:

```sh
python3 deploy/tests/test_cold_snapshot_wal.py \
  --tool deploy/cold_snapshot.py \
  --closed-sqlite "$CLOSED_SYNTHETIC_DB" \
  --expected-sha256 37ed2c36de9875fe78aee9ab2934a6421c52249b1ba9752b604a19f74e427696 \
  --work "$NEW_PRIVATE_WORK"
```

Rzeczywiste wykonanie użyło audytowanego tool ze starego fresh source i nowego
testu z osobnego fresh source; po skopiowaniu do głównego worktree hashe obu są
identyczne z testowanymi. Nie powtarzano 10 wcześniejszych przypadków ani restore
kont, ponieważ implementacja narzędzia nie zmieniła się. Są dowodem historycznym
odebranym w [pierwszym raporcie](BACKUP_RESTORE_2026-11-01.md).

## Ograniczenia i stan wydania

Kontrola st_dev odrzuca przejście na inne urządzenie; **nie dowodzi wykrycia
bind mounta na tym samym urządzeniu**. Dokumentacja została skorygowana.
Root musi pozostawać pod kontrolą operatora, który weryfikuje topologię
oraz niezależnie zatrzymuje wszystkich writerów. Advisory locks i dwa skany
nie są globalnym freeze. Same-UID ACL nie dowodzi obcego UID/GID/ACL/MAC.
Produkcyjne klucze, filesystem, topologia, RPO i rzeczywisty restore nieodebrane.
Ta próba nie jest symulacją awarii zasilania ani przekrojowym testem wszystkich
wersji SQLite. Nie jest też nową próbą sesji/auth lub GUI.

Kod runtime/log/web niezmieniony, tree `633ce9cfc9b909426d88b258dbef877782b1e5fe`.
Immutable pakiet ccb5ef9d zachowany, SHA256
`95bb540ca669dc6410af75aa135434800374957bea7a6cc7a5d2359cfd643481`.
Bez builda/Release/pakowania/deploy. `install.sh` nadal odpowiada za pakiety;
narzędzie nie uruchamia usług i nie stanowi drugiego deployera.

Next: administrator odbiera lokalny commit i nowy manifest, następnie realizuje
własne bramki operatora przed użyciem produkcyjnym. Brak zgody wdrożeniowej,
nowych procesów serwerowych i kolejnego samodzielnego cyklu. Wynikowy SHA i
manifest zapisano w CHECKPOINT, aby nie tworzyć samoodnoszącego hashu w commicie.
