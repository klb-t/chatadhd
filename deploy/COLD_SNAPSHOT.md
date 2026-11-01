# Zimna kopia i odtworzenie danych — narzędzie operatora

`cold_snapshot.py` kopiuje wyłącznie jawnie wskazane drzewo. Nie odkrywa danych,
nie zarządza usługami, uwierzytelnianiem ani pakietami. Przełączanie wersji nadal
należy do `deploy/install.sh`. Nie uruchamiać na produkcji bez okna, uprawnień
operatora i decyzji o RPO. Test syntetyczny opisuje
[raport odbioru](../docs/reports/BACKUP_RESTORE_2026-11-01.md) oraz
[wykonana bramka niepustego WAL](../docs/reports/BACKUP_TOOLS_WAL_2026-11-01.md).

Procedura uszczegóławia kroki 2–4 zachowanego planu operatora
`.integration-production-compat-20261101/ADMIN_BACKUP_ROLLBACK_PLAN.md`.
Ten historyczny dokument integratora pozostaje bez zmian. Jego stary kandydat
8b2c0e68 oraz status bramki loggera są historyczne: bieżący zaakceptowany pakiet
ccb5ef9d ma SHA256 `95bb540ca669dc6410af75aa135434800374957bea7a6cc7a5d2359cfd643481`.
`LOOM_LOG_ENABLED=0` obejmuje logger loom; nie dowodzi wyłączenia direct stdio,
helperów, przeglądarki ani pełnej sanitizacji. Tryb kont został osobno potwierdzony
przez administratora. Żadna z tych informacji nie upoważnia do wdrożenia.

## Warunki przed kopią

Operator ustala rzeczywiste rooty i zamyka wszystkich klientów zapisu oraz
czytelników SQLite: usługę, inne instancje, CLI, importery, synchronizacje,
obserwatory. Zachowuje osobny dowód zatrzymania i wstrzymania nowych zapisów.
Narzędzie wymaga `--writers-stopped`, lecz samo oświadczenie nie jest dowodem.
Nie skanuje hosta i nie zatrzymuje procesów. Blokady i stabilne hashe są dodatkowymi
kontrolami, nie protokołem spójności aplikacji działającej równolegle.

Zakres obejmuje całe drzewo kont i runtime: wszystkie DB oraz odpowiadające WAL/SHM,
bloby, oryginały, konfigurację, profile i pozostałe metadane. Nie wybiera tylko
rozszerzeń `.db`. Nie checkpointuje i nie usuwa WAL. Kopia wielu niezależnych rootów
wymaga jednego wspólnego, nieruchomego okna i osobnego manifestu każdego rootu.
Zewnętrzne magazyny, mounty, klucze szyfrujące i konfigurację poza rootem operator
inwentaryzuje osobno; nie wolno zgadywać ich lokalizacji ani pomijać zależności.
Materiały uwierzytelniające, jeśli wymagane w rzeczywistej kopii, pozostają
niejawnym materiałem operatora, poza repo, raportem i pakietem produktu.

Wymagane: Linux, Python 3 ze standardową biblioteką, `O_NOATIME`/`O_NOFOLLOW`,
POSIX `fcntl` locks oraz filesystem z potrzebnymi xattrs/ACL i `fsync`.
Konto musi móc zachować numeryczne UID/GID i odczytać dane bez zmiany atime.
Pliki SQLite rozpoznawane po nagłówku są otwierane O_RDWR wyłącznie do uzyskania
blokady; kod nie zapisuje przez te deskryptory i nie otwiera źródła API SQLite.
Nierozpoznane/zaszyfrowane bazy nadal kopiowane są bajtowo, lecz ich blokad nie
udowodniono — wymagają odrębnej identyfikacji i bramki operatora.

Przed użyciem administrator sprawdza wolne miejsce i zachowanie właścicieli,
ACL/xattrs/MAC na docelowym filesystemie. Test tej delty obejmuje własny UID/GID,
`user.*` i POSIX ACL z bieżącym UID. Obcy UID w sandboxie odmówiony EINVAL;
pełne uprawnienia produkcyjne nie są odebrane. Błąd zachowania metadanych to STOP,
a nie zgoda na rekursywny chown/chmod albo ignorowanie xattrs.

## Polecenia (tylko operator, jawnie ustawione ścieżki)

Zmienne poniżej nie mają domyślnych wartości produkcyjnych. `SNAPSHOT_WORK`
i jego rodzic muszą być kontrolowane przez operatora; katalog roboczy należy
utworzyć jako nowy, mode 0700. Nie używać `mkdir -p` do maskowania istniejącego celu.
`DATA_ROOT` musi leżeć w `SOURCE_BOUNDARY`, a wszystkie elementy ścieżek muszą
być rzeczywistymi katalogami, nie symlinkami. Użyć zatwierdzonej wersji skryptu.

```sh
# Wartości ustala i sprawdza operator; bez automatycznego uruchamiania usług.
: "${COLD_TOOL:?}" "${DATA_ROOT:?}" "${SOURCE_BOUNDARY:?}" "${SNAPSHOT_WORK:?}"
umask 077
mkdir -- "$SNAPSHOT_WORK"
python3 "$COLD_TOOL" preflight \
  --source "$DATA_ROOT" --allowed-root "$SOURCE_BOUNDARY" \
  --receipt "$SNAPSHOT_WORK/preflight.json" --writers-stopped
```

Exit 0 podaje `receipt_sha256`, liczbę wpisów i rozpoznanych zablokowanych DB.
Operator porównuje liczbę/zakres z własną inwentaryzacją; nie publikuje prywatnego
manifestu. Zapisuje dokładny hash jako `PREFLIGHT_SHA`; brak lub niezgodność
kończy operację. Blokady trzymane są przez cały proces danej operacji; pomiędzy
komendami nadal obowiązuje zewnętrzne zatrzymanie wszystkich writerów.

```sh
: "${PREFLIGHT_SHA:?}"
python3 "$COLD_TOOL" backup \
  --source "$DATA_ROOT" --allowed-root "$SOURCE_BOUNDARY" \
  --destination "$SNAPSHOT_WORK/backup" --destination-root "$SNAPSHOT_WORK" \
  --preflight "$SNAPSHOT_WORK/preflight.json" \
  --preflight-sha256 "$PREFLIGHT_SHA" --writers-stopped
```

Exit 0 zwraca `manifest_sha256`. Zachować go niezależnie jako `SNAPSHOT_SHA`.
`backup/COMPLETE.json` musi odpowiadać manifestowi; obecność `INCOMPLETE.json`
lub jakikolwiek błąd oznacza nieukończony wynik. Częściowy cel pozostaje do
kontroli operatora, bez automatycznej kasacji i retry. Backup nie jest miejscem
uruchamiania aplikacji ani wykonywania zapytań SQLite. Nie odczytywać jego payloadu
narzędziami zmieniającymi atime: zgodność metadanych jest sprawdzana ściśle.
Hash chroni zgodność z wcześniej zapisanym punktem odniesienia, nie jest podpisem
ani dowodem autentyczności samodzielnie podmienionego backupu i manifestu.

```sh
: "${SNAPSHOT_SHA:?}"
python3 "$COLD_TOOL" restore \
  --snapshot "$SNAPSHOT_WORK/backup" --allowed-root "$SNAPSHOT_WORK" \
  --destination "$SNAPSHOT_WORK/restore-check" --destination-root "$SNAPSHOT_WORK" \
  --manifest-sha256 "$SNAPSHOT_SHA" --writers-stopped
```

Dane znajdują się w `restore-check/data`. Wrapper pozostaje mode 0700, a oryginalne
tryby/właściciele drzewa są wewnątrz `data/`. Nie kierować usługi na sam wrapper.
Testować wyłącznie odtworzone drzewo: integrity/FK/schema/count, znane rozmowy,
oryginały z hashami oraz start/health właściwego executable, jawne data-dir,
accounts-root i static-dir, izolowany port, whitelist środowiska, bez providerów.
Właściwe reguły sandboxa obejmują tylko kopię testową; nie sam backup lub źródło.
W razie błędu nie mieszać danych z częściowo odtworzonego i istniejącego rootu.

## Odmowy, metadane i granice

- Istniejący cel (także pusty), nakładające się rooty, `..` i wyjście poza jawny
  root są odrzucane. Każdy komponent ścieżki jest otwierany bez śledzenia symlinków.
- Wszystkie symlinki, specjalne pliki i twarde dowiązania plików są odrzucane.
  Odrzucane są też podkatalogi na innym urządzeniu. Kontrola st_dev nie wykrywa bind mountów
  na tym samym urządzeniu; topologię musi niezależnie sprawdzić operator.
  Narzędzie nie usuwa wykrytych wpisów i nie pomija ich; taki root wymaga osobnej,
  zatwierdzonej metody zachowującej topologię. Nie reklamuje pełnego `rsync -aHAX`.
- Sprawdzane są bytes/SHA256, struktura, UID/GID, mode, atime/mtime nanosekundy
  i wszystkie dostępne xattrs (w tym ACL). Ctime/birthtime, numery inode,
  układ sparse/extents i semantyka obcych filesystemów nie są zachowywanym kontraktem.
- Każdy błąd uprawnień/metadata/ENOSPC/lock lub zmiana źródła przerywa operację.
  Kod nie naprawia uprawnień źródła, nie usuwa WAL ani starych kopii.
- Ochrona jest przeznaczona dla nieruchomego drzewa pod kontrolą operatora,
  nie dla wrogiego procesu tego samego UID zmieniającego namespace podczas kopii.
  Blokady SQLite są advisory; nie gwarantują zatrzymania writerów niestosujących
  SQLite locking ani spójności między bazami przy działającej aplikacji.

## Przywrócenie produkcji pozostaje bramką administratora

Po osobnej zgodzie operator odtwarza pełny zgodny snapshot do nowego rootu,
zachowując stan po nieudanym upgrade jako osobny materiał. Nie nadpisuje czynnych
baz. Powrót do kopii sprzed upgrade może utracić późniejsze zapisy; wymaga decyzji
właściciela o RPO lub odrębnej procedury odzysku. Dopiero po kontroli danych,
uprawnień, konfiguracji i kluczy operator korzysta z istniejącego `install.sh`
i rzeczywistego kontraktu usługi. Nie ma tu drugiego deployera ani automatycznego
rollbacku. Historyczny poprzednik to `/opt/chatadhd/2d39b07c/bin/loom-server`,
SHA256 `a840b35ae6a424380adac2314abe8230be148937460dbd37d840816b1743c14b`;
wybór executable nie odtwarza DB i nie zastępuje kontroli zgodności schematu.
