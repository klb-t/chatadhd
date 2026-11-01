# Logger — scalona delta i nowy lokalny Release, 2026-11-01

**Integrated / built / tested zakresowo; deployed NIE.** Kod:
`ccb5ef9d9a4879bc9634db7296a60ce4d35404ce` (lokalny commit `[skip ci]`).
Baza `6cfd9a40052859c4efd3333655def3a41a0aebc5`, poprzednio testowany kod
`8b2c0e689b1ddf43c74519e68ed626e79d5a4b16`; wszystkie wcześniejsze commity zachowane.
Kanon §0 rewizja 2. Nie wykonywano wdrożenia ani zmian konfiguracji produkcji.

## Odbiór i build

Audytowana delta `chatadhd-log-control-20261101`:
SHA256 `092c3d6f8ecbffc4e6d1a70f9ca34b9c6b88e77a19f88513d513799330622c0f`,
manifest `28f7536760d9d2512f1e5a9a9c85a4ded15eab3df24ae2457d63f19e0409fcbb`.
Własna stabilność 25/25 artefaktów przed/po, zgodność HEAD/status źródłowego
worktree; apply-check, **jedno apply**, 8/8 hashy przed aktualizacją samej
dokumentacji. 6/6 plików kodu/testów loggera identycznych z odebranymi.
18/18 wcześniejszych plików G1GUI (poza aktualizowanym STATE) zgodnych.
Focused 6/6 procesów / 453 asercje i symbole 7/7 z pierwszego przyrostu zachowane
jako wcześniejszy dowód — nie uruchamiano ponownie.

Przed kompilacją zachowano pełny poprzedni Release build przez
`cp -a --reflink=never`:410 wpisów porównanych hashami/linkami/katalogami,
rozmiar logiczny 395522001 B; wolne 12244049920 B. Kopia w
`.integration-log-release-20261101/previous-release-build` jest snapshotem,
nie relokowanym cache do budowania. Oryginalny cache pozostał we właściwej
ścieżce `.integration-release-20261031/build` ze źródłami bieżącego `loom/`.
Debug i wszystkie stare pakiety/dowody/source worktree zachowane.

Przyrostowy build **PASS exit0**: Release `-O3 -DNDEBUG`, server+lib, `-j2`,
CLI/tests OFF, shared/runtime DOCX ON, system SQLite/OpenSSL.
Sześć ostrzeżeń kompilatora (nlohmann/std_function, null dereference oraz
maybe-uninitialized) zachowano w `build.log`/`build-result.json`; dotyczyły
niezmienionych obszarów, nie wyłączano ostrzeżeń ani nie zmieniano bramki.
To nie deklaracja rozstrzygnięcia tych ostrzeżeń lub pełnej bramki repo.

## Nowy pakiet

`.integration-log-release-20261101/chatadhd-server-linux-x64-ccb5ef9d-release-log-control-20261101.tar.gz`

| Artefakt | SHA256 |
|---|---|
| Archiwum | `95bb540ca669dc6410af75aa135434800374957bea7a6cc7a5d2359cfd643481` |
| MANIFEST.json | `3f6c7c5a47713a5c2c0f9e6740f0d84a86cabeec13bca1dfb3c79179f2587e6f` |
| bin/loom-server | `9f7f35e21838876a94361c1fa916f0b9373bd574687afe8fd8946fa1dd9c5137` |
| lib/libloom.so (cel symlinku) | `b923709ed2dacb9eafa0cf553e72f5127e14a29a9fd8fb25c52b732666184358` |

24/24 payloady sprawdzone w stagingu, archiwum i po testach. Zmieniły się
binaria server/lib (również hashe dwóch symlinków lib) oraz VERSION/BUILD_INFO.
Pozostałe 18 payloadów: 16 web + reader.py/runtime.json DOCX, identyczne z
poprzednim pakietem. Także osadzone OCR/URL/helpery i zasoby mają niezmienione
hashe. Web nadal pochodzi z własnego przyjętego buildu G1GUI kodu8b2c0e68;
źródła web do ccb5ef9d nie zmieniły się. Nie wykonywano npm/webbuild.
Manifest zawiera source SHA/tree, opcje, wersje i hashe zależności systemowych,
pochodzenie web/helperów oraz kontrakt loggera.

Stary pakiet 8b2c0e68 zachowany, końcowy SHA ponownie zgodny:
`e65ed96eab67d119b20852e05d65b0bcc4fb8a9ce24b5978a92c7d1fa43ba1db`.
Nie relabelowano go. Nowe binaria mają inne hashe i pochodzą z wykonanego buildu.
Archiwum utworzone standardowym Python tarfile z numerycznym właścicielem 0
oraz zachowanymi symlinkami. Nie tworzono drugiego deployera ani instalacji hosta.

## Wykonana brakująca bramka artefaktu

**CTest 3/3 PASS, sekwencyjnie `-j1`, 5.51 s**:

1. Rzeczywista nowa lib: 7 wymaganych symboli C++ obecnych, 126 eksportów C
   identycznych ze starą lib. C++ jest lokalne zgodnie z dotychczasowym
   kontraktem sharedlib; nie deklarujemy nowego dynamicznego C++ ABI.
2. Dwa odrębne procesy z rzeczywistą lib przez istniejące C API:
   `LOOM_LOG_ENABLED=1` — syntetyczny marker nazwy katalogu runtime w stderr,
   ringu i callbacku; po operacjach 9 rekordów w ringu i 9 callbacków.
   `LOOM_LOG_ENABLED=0` — stderr pusty, ring 0, callback 0, mimo setterów
   poziomu/stderr oraz `log_level:debug` w `loom_init_ex`.
   Błędny katalog nadal daje null context i błąd JSON w obu trybach; wyłączony
   logger nie usuwa kanału błędu. Runtime otwarty bez workers/FTS, providerów brak.
   Następnie własny server z loggerem 0, nowym rootem kont, guest OFF:
   health 200, statyczny index 200 z identycznymi bajtami, auth/me 200 z
   accounts=true/authenticated=false; conversations 401, logs 401,
   cross-site POST 403. Komunikaty operacyjne listening/stopped zachowane,
   SIGTERM/exit 0. Ring serwera pozostaje chroniony istniejącym API;
   zweryfikowano go przez lib, nie dodano endpointu ani nie osłabiono auth.
3. Istniejący `deploy/verify-package.sh` na NOWYM archiwum: checksum, biblioteki,
   startup, health, statyczny web i auth/me PASS. Własny wrapper BusyBox tar
   omija wcześniej udokumentowane GNUtar/openat2 ENOSYS; bez zmiany hosta.

Lib/server/verify uruchomione z whitelistą środowiska i Landlock: tylko `/usr`,
`/dev/null`, nowy pakiet oraz własne pliki/katalogi smoke. Jawne data-dir,
accounts-root i static-dir; nowe porty 127.0.0.1. Bez produkcji, sekretów,
rzeczywistych modeli/API. Dane i canary wyłącznie syntetyczne. Wszystkie procesy
zakończone, końcowy ps własnego UID bez serwerów/kompilatorów testowych.

## Niepowodzenia narzędziowe zachowane w dowodach

- Pierwszy skrypt pakowania integratora nadpisał nazwę pakietu nazwą payloadu
  w pętli porównawczej. Tar dostał błędną ścieżkę; jest to błąd mojego skryptu,
  nie produktu. Zachowano `assemble.attempt1.py` i `assemble.log`.
  Istniejący staging zweryfikowano i dokończono archiwum przez
  `finalize_archive.py`, bez ponowienia install/build lub mutacji stagingu.
  Poprawiono również skrypt odtwarzania pakietu.
- CTest uruchomiono zbyt wcześnie, przed zakończeniem finalizacji archiwum.
  Pierwsze 0/3 zatrzymały się na braku release-package.json, **przed uruchomieniem
  jakiegokolwiek binarium**. Zachowano `gates-01.log`, wynik i puste katalogi
  `verify-preflight-01`. Po potwierdzeniu zakończenia pakowania uruchomiono
  rzeczywiste bramki; `gates-02.log` zawiera 3/3 PASS. Asercji nie osłabiano.

## Komendy i granice odbioru

```sh
cmake --build .integration-release-20261031/build --target loom-server loom -j2
python3 .integration-log-release-20261101/assemble.py \
  --build .integration-release-20261031/build --kind Release
# Po opisanym błędzie kontynuacja istniejącego stagingu:
python3 .integration-log-release-20261101/finalize_archive.py
ctest --test-dir .integration-log-release-20261101/ctest -j1 -V --output-on-failure
```

Skrypty, diffy, source-binding, snapshot builda, manifesty i logi zachowano
w `.integration-log-release-20261101`; CHECKPOINT wskazuje końcowy commit.
Nie powtarzano accounts/single-runtime/G1GUI/P0, focused loggera ani rollbacków.
Ich wyniki pozostają historyczne, nie nowymi bramkami tego SHA. Brak GUI/mobile,
TSAN/full repo gate i rzeczywistego modelu dla nowego artefaktu. Systemowe
zależności nie są bundlowane — nie jest to deklaracja przenośnego pakietu Ubuntu.

Kontrakt: unset/dokładne 1 pozwala, 0/każda inna podana wartość blokuje;
profil/poziom/stderr nie obchodzą startowej bramki. Wyłączenie runtime nie jest
drain barrier, stary ring pozostaje. Directstdio/PhaseTimer/helpers/browser,
pełna redakcja sekretów i hierarchiczna instrumentacja ekosystemu poza zakresem.
Manifest opisuje `LOOM_LOG_ENABLED=0`; ustawiono je tylko w procesach testowych.
Nie zmieniono produkcyjnego unit/env ani konfiguracji użytkownika.

Next: administrator odbiera nowy pakiet i dowody; kopia/restore produkcji oraz
odrębna decyzja o wdrożeniu nadal należą do administratora/właściciela.
Brak blokera dla ukończonego zakresowego odbioru. Bez automatycznego kolejnego
cyklu, push/CI/deploy. Nie odnawiano deadline 2026-11-01T17:21:19.260820Z.
