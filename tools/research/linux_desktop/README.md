# Lokalna próba kontraktu Linuksa

Próba badawcza, poza kodem produktu. Plik `.desktop` leży wyłącznie w tym
katalogu worktree użytkownika; nie jest instalowany w katalogu `applications`.
`org.example` i `loom-probe` są nazwami fixture, nie identyfikatorem wydania.
Wskazany `/usr/bin/true` jest atrapą. Test **nie uruchamia** wpisu ani jego `Exec`.

Z korzenia własnego worktree:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/research/linux_desktop/test_contract.py
```

Wymagane istniejące narzędzia: `desktop-file-validate`, Python 3, PyGObject,
GLib (z `GUri`) i GIO Unix (`DesktopAppInfo`). Brak zależności daje błąd,
nie pozorny sukces. Niczego nie instalujemy. Pliki tymczasowe powstają w tym
katalogu i są usuwane po walidacji.

Zakres: walidator specyfikacji akceptuje wpis i odrzuca błędne warianty;
GIO Unix czyta wskazaną fixture; GLib sprawdza absolutne URI i typy argumentów
OpenURI oraz zachowanie oryginalnych bajtów w pakiecie. Test pokazuje też,
że samo przyjęcie wpisu bez `Exec` przez walidator i parser nie dowodzi
gotowości uruchomienia. Nie ma połączenia
D-Bus, wysyłania pakietu, interpretowania wyniku portalu, rejestracji MIME,
otwierania URI/pliku ani testu sesji X11/Wayland. To nie jest adapter produktu.

Odwrócenie: usunięcie trzech plików tego katalogu usuwa całą próbę; nie ma
zmian poza worktree. Kontrakty i dalszy pakiet:
[raport](../../../docs/reports/LINUX_DESKTOP_RESEARCH_2026-10-31.md).
