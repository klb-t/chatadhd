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

1. Zainstaluj aplikację ChatADHD. Przy pierwszym uruchomieniu wybierz „Konto na serwerze”
   (później: ikona ⚙ w prawym dolnym rogu).
2. Wpisz adres, np. `chat.twoja.domena`. Aplikacja sprawdza `/api/auth/me` i pokazuje powód błędu.
3. Zaloguj się kontem z serwera. Sesja zostaje zapamiętana.
4. Wymagany jest HTTPS. Zwykły HTTP tylko po zaznaczeniu opcji i tylko dla adresu w sieci lokalnej
   (hasło idzie wtedy bez szyfrowania).
5. Powrót do trybu „Lokalnie” nie kasuje żadnych danych; dane lokalne i serwerowe są osobne.
