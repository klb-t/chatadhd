# Custom-Keyboard-Pro / IO Matrix — drugi przebieg A

Repo `klb-t/Custom-Keyboard-Pro`, przypięty `main`:
`192f820f2d8c653768237e1bff3a1a6d950d69c0` (bez przyrostu względem pierwszego audytu).
Pierwszy raport i receipts pozostają nienaruszone. B jest właścicielem poprawek produktu.

**Wykonane:** 25 niezależnych kontroli JVM na rzeczywistym Kotlinie:
10/10 reprodukcji A PASS; akceptacja B 7 PASS / **8 FAIL**. PASS reprodukcji
potwierdza błąd, nie zgodność produktu. Sześć kontroli SQLite przechodzi.
Wszystkie przypadki używają syntetycznych znaczników; realny transport jest blokowany.

- CKP-A-001: wbudowany `fix` przesłania custom ID; niepoprawny JSON daje pusty
  sukces; nieobsługiwane `disabled` znika. Dwie receptury custom o osobnym ID
  rzeczywiście zmieniają prompt przechwycony na granicy `runAiTask → complete`.
- CKP-A-002: niepoprawny bundled katalog nadal daje model/endpoint z Kotlinowego
  LEGACY. Poprawne dwa custom profile zmieniają rzeczywisty resolver. Nie twierdzę,
  że błąd sam uruchamia wysyłkę.
- CKP-A-003: zmiana wag zmienia ranking prawdziwego Plannera; obecny runner nie
  przekazuje tych wag z danych użytkownika. Null cost nadal istnieje w metadanych,
  mimo rankingu jak MEDIUM. Samego równoważnego wyniku liczbowego nie traktuję jako
  niezależnego błędu. Akceptacja pełnego profilu → runner wymaga udostępnienia przez
  B rzeczywistego punktu podłączenia, nie zastąpienia Plannera testową kopią.
- CKP-A-012: dawna hipoteza ma teraz **reprodukcję na hoście**. Niezmienione
  `runAiTask` i `replaceSelectionOrAll` stosują późny wynik po zmianie pola,
  rewizji tekstu albo sensitive state. Trzy reprodukcje PASS, trzy akceptacje FAIL.
  To nie jest test Android lifecycle ani konkretnego OEM.
- **A2-CKP-001 (nowe naruszenie):** `use=nonexistent_audit_transform` przechodzi
  przez prawdziwy Command parser; `names` odrzuca nierozpoznane wymaganie i runner
  wykonuje inną trasę. Reprodukcja oraz negatywna akceptacja są wykonywalne.
- **A2-CKP-002 (mechanizm):** prawdziwy NetListener unieważnia zakolejkowaną akcję
  po `stop`; zły token i wyłączone `allowCommands` blokują wykonanie. Nie przenoszę
  ryzyka rewrite automatycznie na każdy asynchroniczny konsument.
- **A2-CKP-003 (mechanizm):** wykonane SQL z aktualnych migracji zachowuje wiersz
  v1→v5; clear chroni dopięty pin i nowe kopie po review; undo jest związane z batch.
  Source path obejmuje UI potwierdzenia, repozytorium, DAO, migracje. Kontrola SQL
  nie dowodzi zachowania Room, Android URI grant ani sprzątania plików.

## Pokrycie i otwarte końce ścieżek

Mianownik bez zmian: **233 pliki produktu, 461 tracked entries**. W tym etapie
prześledzono wskazane funkcje/zakresy w 32 plikach; 19 z nich nie było w pierwszym
przebiegu. Suma plików z przeglądanymi zakresami rośnie **28 → 47 / 233**.
**186 plików nie ma jeszcze przeglądu semantycznego**; wykaz jest w `coverage.json`.
Nie oznacza to certyfikacji 47 całych plików. Każda pozycja ma zakresy, funkcje
oraz nieprześledzone wywołania. Skanu kandydatów nie powtarzano.

Prześledzono także settings/profile import, kontrolki, wywołania Performer,
szyfrowaną migrację settings, sync preview/apply i zasady eksportu vault. Są to
zakresy źródłowe: crypto/Keystore i trwałość przy zabiciu procesu wymagają własnych
bramek. `SettingsStore.setByKey` bezpośrednio przyjmuje nieznany klucz jako no-op,
ale sprawdzone UI/Performer walidują wejście; nie przedstawiam tego jako dowodu
cichego ignorowania w całej aplikacji. Profile import odrzuca nieznane klucze
wartości i nieobsługiwaną wersję; to inny entrypoint niż surowy codec.

Sync ma oddzielne commity DB, settings i stateFile. Potrzebna pozostaje próba
przerwania/failure injection między nimi; samo przeczytanie kolejności nie jest
reprodukcją utraty danych. Vault import zdejmuje origin binding celowo: przeniesienie
reprezentacji nie przenosi uprawnień do aplikacji na nowym urządzeniu.

## Środowisko i wznowienie

Dawne „brak kompilatora” zostało usunięte. Kotlin 2.1.20 na JRE 17 wykonuje host
próby. Pobranie oficjalnego, przypiętego Android toolchain zakończyło się sukcesem:
Temurin 17.0.20.1+1, Gradle 9.7.1, SDK 36.1, build-tools 36.0.0/36.1.0; skrypt repo
sprawdził przypięte hashe. Bramka istniejących testów Android JVM przeszła 974/974; oddzielny audytowy
JUnit zakończył się 4 PASS / 2 acceptance FAIL. Szczegóły poniżej.

Pakiety dla B: `handoff-B.json`. Uruchamialny indeks: `test-index.json`.
Szczegóły wykonania i atrapy infrastruktury: README narzędzi CKP.
Najpierw B powinien zamknąć target/revision-bound apply CKP-A-012, następnie jawnie
odrzucać nierozpoznane wymagane transformacje A2-CKP-001. Następna niezależna bramka:
opóźniony rewrite na pełnym IME/Android; potem delayed paste oraz przerwanie sync
między DB commit a utrwaleniem settings/stateFile.

## Dodatkowy pakiet rzeczywistych klas Settings

Po `compileDebugKotlin` i `compileDebugJavaWithJavac` uruchomiono niezależny Java
harness przeciw faktycznie zbudowanym klasom aplikacji (żaden Settings/Schema/Profile
nie jest atrapą). `compiled-settings-receipt.json`: **10 kontroli,9PASS/1FAIL**;
2PASS to obserwacje/reprodukcje,7PASS to akceptacja,1FAIL akceptacji. Sprawdzono
zgodność wejść buildu z przypiętym SHA; hashe klas są w receipt.

Profile import odrzuca nieznane klucze i wersję999; wyłączenie `clipboardEnabled`
przechodzi przez codec profilu i trwały codec Settings; Basic/Advanced/Expert
zachowują tę samą wartość. To codec, **nie zaszyfrowany zapis ani restart procesu**.
Surowy codec ignoruje i gubi nieznane pole: zachowanie obserwowane, bez przypisania
mu automatycznie intencji/przyszłej semantyki właściciela.

**A2-CKP-004:** generic `setByKey` dla nieznanego klucza zwraca sukces bez zmiany.
To naruszenie lokalnego kontraktu wymaganego w etapie2. Sprawdzone kontrolki UI i
Performer walidują wcześniej; nie rozszerzam tego wyniku na ich zachowanie.

## Domknięta bramka istniejących testów Android JVM

Instalacja narzędzi wystarczyła do uruchomienia dokładnego buildu repo. Pierwszy
Gradle daemon zniknął po kompilacji Kotlin/Javac, przed testami; ten brak wyniku
zachowano w `android-gate-attempt1.log`. Zmniejszenie heap do1GiB i uruchomienie
po zwolnieniu konkurencyjnego buildu dało `BUILD SUCCESSFUL` w2m43s.
**974/974 istniejących testów PASS,0FAIL,0SKIP**,101 plików XML, bez zmiany bramek
lub źródeł. Dokładny licznik XML i lista testów są w
`android-existing-tests-receipt.json`. To Android JVM/Robolectric, nie telefon,
emulator, ponowny lint ani assembleDebug. Nie przypisuję wyniku pass testom
bezpieczeństwa, których te istniejące przypadki nie obejmują.

## Domknięte niezależne testy Android JVM

Zewnętrzne audytowe JUnit zostały podłączone przez init script do Kotlin test
source set, bez zmian plików produktu. Pierwszy adapter źródeł wskazywał java
source set: Gradle nie znalazł testów; zachowano tę porażkę infrastruktury osobno.
Po korekcie wyłącznie skryptu audytowego wykonano **6 testów:4PASS/2FAIL** w35s.
FAIL to rzeczywiste akceptacje: custom `fix` nadal przegrywa z wbudowanym oraz
generic `setByKey` nadal raportuje sukces dla nieznanego klucza. Nie są skip/xfail.

PASS obejmuje: odrzucenie nieznanej wartości/wersji profilu, zachowanie tego samego
stanu przez poziomy UI, codec z zachowaniem disabled i granicy importu sekretów,
oraz **rzeczywiste save/AtomicFile/reopen SettingsProfiles w Robolectric**.
Ostatni test resetuje referencję Context i ponownie otwiera magazyn w tym samym
procesie; nie nazywam go restartem procesu ani testem Android Keystore.

Wznowienie bramki Android nie jest już blokowane brakiem toolchain. Pozostałe
konkretne luki: fizyczny IME target/focus; realny restart z Keystore; pack update
po wyłączeniu; crash między DB/settings/sync state; trace/history receptury po
edycji. Nie otwierano płatnego CI ani wywołań modeli.
