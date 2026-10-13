# Wdrożenie ChatADHD (Loom) na VM

Serwer + web, konta użytkowników, HTTPS przez Caddy.

## 5 kroków: od czystej VM do logowania z telefonu

1. VM z Ubuntu/Debian, porty 80 i 443 otwarte. Domena (rekord A na IP VM) jest zalecana.
2. `git clone <repo> && cd chatadhd`
3. `sudo deploy/install.sh --domain chat.twoja.domena --admin-login anna`
   (bez domeny: pomiń `--domain`, będzie certyfikat wewnętrzny i ostrzeżenie w przeglądarce)
4. Podaj hasło (min. 8 znaków) gdy zapyta. Hasło nie trafia do logów ani argumentów.
5. Na telefonie otwórz `https://chat.twoja.domena` i zaloguj się.

Budowanie trwa kilka minut. Skrypt można uruchamiać wielokrotnie.

## Gotowa paczka z GitHub Actions (bez kompilatora na VM)

Workflow „Package (Android APK + server)” (`.github/workflows/package.yml`) uruchamia się **tylko ręcznie**
(Actions → Package → Run workflow), więc zużywa minuty tylko, gdy o to prosisz. Daje dwa artefakty:

- `chatadhd-server-linux-x64`: `chatadhd-server-linux-x64-<wersja>.tar.gz` + `.sha256` (serwer + web),
- `chatadhd-android-apk`: `app-debug.apk` (patrz „Telefon” niżej).

Na VM (Ubuntu 24.04+ / Debian 13+, bo archiwum jest budowane na glibc 2.39):

```
sudo deploy/install.sh --from-tarball chatadhd-server-linux-x64-<wersja>.tar.gz --domain chat.twoja.domena --admin-login anna
```
Skrypt sprawdza sumę `.sha256` (jeśli leży obok), rozpakowuje do `/opt/chatadhd/<wersja>`, sprawdza biblioteki
(`ldd`) i dalej robi to samo co instalacja ze źródeł (systemd, Caddy, `current`, rollback). Nie instaluje
kompilatora ani node. Starszy system: budowa ze źródeł (`deploy/install.sh` bez `--from-tarball`).
Archiwum można też zbudować samemu: `deploy/install.sh --package KATALOG --no-deps`, a sprawdzić
`deploy/verify-package.sh PLIK.tar.gz` (uruchamia serwer z archiwum na `127.0.0.1` i go odpytuje).

## Aktualizacja i powrót

- Aktualizacja: `git pull && sudo deploy/install.sh --domain ...`
  Nowa wersja ląduje w `/opt/chatadhd/<wersja>`, `current` przełącza się na nią.
- Powrót: `sudo deploy/install.sh --rollback <poprzednia-wersja>`
  (lista: `ls /opt/chatadhd`; poprzednią wersję skrypt wypisuje po instalacji).
- Dane w `/var/lib/chatadhd` nie są ruszane.

## Gdzie są dane

`/var/lib/chatadhd` (konta, rozmowy, ustawienia). Kod jest osobno w `/opt/chatadhd`.
Kopia zapasowa = skopiowanie tego katalogu (najlepiej przy zatrzymanej usłudze:
`sudo systemctl stop chatadhd`, kopia, `sudo systemctl start chatadhd`).

## Rejestracja: otwarta / zaproszenia / zamknięta

- `sudo deploy/install.sh --no-deps --no-build --no-caddy --version <obecna> --registration open|invite|closed`
  (lub edycja `/var/lib/chatadhd/profiles/server_accounts.pack`, pole `registration`, potem `sudo systemctl restart chatadhd`).
- Domyślnie: `closed` (konta tworzysz sam).
- Użytkownicy: `sudo /opt/chatadhd/chatadhd-user add|passwd|disable|enable LOGIN`, `invite` (kod zaproszenia), `list`.

## Przydatne

- Status/logi: `systemctl status chatadhd`, `journalctl -u chatadhd -f`
- Opcje skryptu: `deploy/install.sh --help` (port, katalogi, wersja, `--no-systemd`, `--no-caddy`, `--prefix`).
  Wszystko da się nadpisać; nic nie jest ukryte.
- Serwer słucha tylko na `127.0.0.1`; z internetu wchodzi się przez Caddy.

## Alternatywa: Docker

```
cd deploy
DOMAIN=chat.twoja.domena docker compose up -d --build
docker compose exec -it chatadhd loom-server user add anna
```
Dane są w wolumenie `chatadhd_data` (lub `CHATADHD_DATA_DIR=/sciezka`). Rejestracja:
`CHATADHD_REGISTRATION=invite docker compose up -d`.
Uwaga: ta ścieżka nie została jeszcze zbudowana w testach (patrz raport P2).

## Telefon (Android): połączenie z serwerem

1. Zainstaluj aplikację ChatADHD (patrz „Instalacja APK” niżej). Przy pierwszym uruchomieniu wybierz
   „Konto na serwerze” (później: ikona ⚙ w prawym dolnym rogu).
2. Wpisz adres, np. `chat.twoja.domena`. Aplikacja sprawdza `/api/auth/me` i pokazuje powód błędu.
3. Zaloguj się kontem z serwera. Sesja zostaje zapamiętana.
4. Wymagany jest HTTPS. Zwykły HTTP tylko po zaznaczeniu opcji i tylko dla adresu w sieci lokalnej
   (hasło idzie wtedy bez szyfrowania).
5. Powrót do trybu „Lokalnie” nie kasuje żadnych danych; dane lokalne i serwerowe są osobne.

### Instalacja APK z GitHub Actions

1. Actions → „Package (Android APK + server)” → Run workflow (domyślnie tylko `arm64-v8a`, czyli telefony;
   `x86_64` dodaj dla emulatora). Pierwsza budowa trwa długo (kompiluje cały rdzeń C++), kolejne są szybsze (ccache).
2. Z zakończonego przebiegu pobierz artefakt `chatadhd-android-apk`, rozpakuj na telefonie i otwórz `app-debug.apk`
   (Android zapyta o zgodę na instalację z tego źródła).
3. APK jest podpisany kluczem *debug*. Workflow przypina ten klucz (sekret `ANDROID_DEBUG_KEYSTORE_B64`, a bez
   niego pamięć podręczna Actions), żeby kolejne wersje instalowały się **na** poprzednią. Gdy klucz się zmieni
   (adnotacja „New APK signing key” w przebiegu), Android odmówi aktualizacji, a odinstalowanie usuwa prywatne
   dane aplikacji. Wtedy najpierw przenieś dane (niżej). Podpis wydania (release) czeka na Twój keystore jako sekret.

### Gdzie aplikacja trzyma dane (ustawienie, nic bez Twojej zgody)

⚙ → „Lokalizacja danych…”. Dostępne: prywatny katalog aplikacji, katalog aplikacji na woluminie współdzielonym,
współdzielony `Documents/ChatADHD` i dowolna ścieżka.

- **Nowa instalacja**: prywatny katalog aplikacji. Inne aplikacje go nie odczytają, nie trzeba „dostępu do
  wszystkich plików”. Odinstalowanie aplikacji kasuje te dane.
- **Starsza instalacja z danymi w `Documents/ChatADHD`**: dane zostają tam, gdzie były. Nad ekranem jest
  czerwony pasek (każda aplikacja z „dostępem do wszystkich plików” może je czytać), a w ekranie lokalizacji
  przycisk **„Przenieś do prywatnego katalogu”**: kopiuje wszystko, sprawdza każdy plik (SHA-256), dopiero wtedy
  przełącza. **Stara kopia zostaje**, dopóki sam jej nie usuniesz („Usuń starą kopię”); do tego czasu nadal
  jest czytelna dla innych aplikacji (z kluczami API, jeśli leżały w jawnym pliku).
- Współdzielony katalog nadal możesz wybrać świadomie (przeżywa odinstalowanie, można go synchronizować).
  Ostrzeżenie zostaje, zakazu nie ma.
- **Klucze API** są zapieczętowane kluczem z Android Keystore (`secrets.json.keystore`); jawny `secrets.json`
  istnieje tylko chwilowo. Klucz Keystore nie przeżywa reinstalacji ani przeniesienia na inny telefon, więc po
  nich wpisz klucze API ponownie. Wyłączysz to w tym samym ekranie. Rozmowy i pamięć zostają jawne; ich
  ochroną jest wybór lokalizacji.
