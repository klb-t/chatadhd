# ChatADHD i rzeczywisty pulpit Linuksa — pierwszy przyrost

Data analizy kodu i weryfikacji źródeł: **2026-10-31**. Baza:
`9533d68e6cf6e133e0fdcd5d5fcf7ee85654da08`. Raport publiczny; przykłady
i próba kontraktu używają wyłącznie fixture. Nie badano danych użytkownika.

**Wniosek projektowy:** integrację można rozpocząć od adapterów istniejących
kontraktów uruchamiania aplikacji, URI, wyboru plików i powiadomień. Ich wspólną
drogą powinien być obecny katalog akcji i mechanizm klienta. Badanie nie wskazuje
potrzeby budowania kompozytora ani wymiany środowiska graficznego. Portale
przewidziano również dla aplikacji poza sandboxem
([Reasons to Use Portals](https://flatpak.github.io/xdg-desktop-portal/docs/reasons-to-use-portals.html),
sekcja główna, odczyt 2026-10-31).

## 1. Dwie różne warstwy i stan rzeczywistego kodu

„Pulpit” R101 to widok grafu i warstwa pod oknami **wewnątrz ChatADHD**.
Launcher systemowy, menedżer plików, obce aplikacje, systemowe powiadomienia,
okna X11/Wayland i usługi sesji/systemu należą do środowiska zewnętrznego.
Wspólny graf może je opisywać, ale przypięcie węzła nie instaluje launchera,
a `window.focus` w obecnym webie nie aktywuje cudzego okna systemowego.
Poniżej obserwacje statyczne z podanej bazy, nie deklaracja wyniku testu GUI.
Lokatory są względne wobec korzenia repozytorium; symbol wskazuje miejsce
odczytu z 2026-10-31.

| Istniejący mechanizm | Plik i symbol / klucz | Co działa w kodzie | Co wymaga adaptera lub rozszerzenia |
|---|---|---|---|
| Jeden rejestr fabryk adapterów | `loom/include/loom/adapter_registry.h`: `AdapterRegistry<Port, Env>::add`, `create_named`, `AdapterDescriptor` | Nazwa z danych wybiera fabrykę; aliasy; `planned` bez fabryki daje `NotImplemented`, nieznana nazwa `Unsupported`. | Użycie tego samego szablonu dla portu pulpitu i fabryki Linuksa; bez drugiej implementacji rejestru. |
| Katalog i reguły akcji | `loom/data/runtime/actions.pack`: `defaults.actions`; `loom/include/loom/action_rules.h`: `actions::Engine`; `loom/src/actions/runner.cpp`: `ActionRunner::runnable`, `install_executors` | Akcje atomowe/złożone, parametry, efekty, reguły, propozycje, wykonawcy; stan wpisu nie wystarcza bez wykonawcy. | Nowe operacje systemowe jako dane katalogu; implementacje operacji protokołu za portem. Nie oznaczać gotowości na podstawie samego systemu Linux. |
| Deklaracja zdolności konkretnego klienta | `loom/include/loom/client_devices.h`: `ClientRegistry`, `client_step_spec`; `loom/src/actions/client_devices.cpp`: `declare`, `client_patches`; `loom/data/runtime/client_devices.pack` | `loom.client_declaration/1`, `granted/prompt/denied`, TTL i aliasy z profilu; deklaracja w pamięci klienta; `loom.client_step/1`. | Klient natywny musi zgłosić zdolności swojej sesji; deklaracja nie jest dowodem uprawnienia systemowego. Brak backendu trzeba odróżnić od odmowy osoby. |
| Wspólne wykonanie i wznowienie | `loom/include/loom/action_runner.h`: `preview`, `run`, `client_result`; `loom/src/actions/runner.cpp`: te metody | Propozycje i paragony, zatrzymanie na kroku klienta, wynik klienta i kontynuacja przez obecny interpreter. | Połączenie kroku klienta z natywnym adapterem; testy pomyłki klienta/paragonu, restartu i niepowielania efektu zewnętrznego. Nie zbadano pełnej autoryzacji tego połączenia. |
| Środowiska w grafie | `loom/include/loom/environments.h`: `sync/list/can_do`; `loom/src/graph/environments.cpp`: `Sync::runtime_env`, `Sync::ports`, `capability_json`, `can_do`; `loom/data/graph/environments.pack` | Węzły środowisk, zdolności i dostępu; katalogi, magazyny, usługi; `can_do` pyta katalog na podstawie grafu. Platformy/urządzenia pochodzą m.in. z pakietu, `built_for()` rozróżnia Android/default. | Bieżące wykrywanie sesji, magistrali, interfejsów i wersji portalu. `available` z pakietu nie dowodzi zgody ani działania portalu. `Sync::ports` obecnie rejestruje ASR/OCR/storage/batch; nowy port trzeba podłączyć. |
| Wybór implementacji jako dane | `loom/include/loom/port_profile.h`: `PortProfile::resolve/explain`; `loom/data/runtime/ports.pack` | Role, adapter, parametry i wyjaśnienie wyboru przez istniejące poziomy ustawień. | Role pulpitu i ich presety/fallbacki; konsumenci muszą jawnie przyjąć wynik `resolve`, profil sam nie zmienia wykonania. |
| Pochodzenie adapterów i wykonań | `loom/include/loom/port_graph.h`: `adapters_of`, `record_adapters`, `record_port_run`, `ActionRunScope` | Rejestr adapterów i wykonania portów jako węzły/relacje, powiązanie z akcją, redakcja pól poświadczeń. | Rejestrowanie wersji i wyniku obserwacji Linuksa; rozróżnienie „żądanie przyjęte” i „wynik widoczny”. Uchwyty aktywacji nie powinny trafiać do trwałych logów. |
| Rejestr dostawców | `loom/include/loom/providers.h`: `ProviderRegistry::can/find`, `constraints_satisfied` | Dobór dostawcy do resource/capability/constraints; dostępność związana m.in. z deklaracją uwierzytelnienia. | To nie sonda sesji GUI. Nie dodawać alternatywnego katalogu akcji pulpitu do rejestru dostawców modeli. |
| Urządzenia webu i most natywny | `loom/web/src/context/{device-host,client-device,browser-host}.ts`: `DeviceHost`, `detectCapabilities`, `stepKind`, `browser-host.ts` → `host.arrangeWindows`, `host.nativeNotify` | Wybór plików/folderów, Notification API lub most powłoki; okna strony zgłaszane jako `arranges_windows`. | Most Linuksa/klient lokalny; przeglądarka z serwerem zdalnym nie uzyskuje przez to sesji desktopowej hosta. |
| Powiadomienie jako istniejąca akcja | `loom/data/runtime/actions.pack`: `device.notify`, `client.capability = shows_notifications` | Gotowy punkt rozszerzenia; bez klienta wpis jest `planned`, klient może uczynić go wykonywalnym. | Adapter `org.freedesktop.Notifications` lub portalu Notification. Obecny `result_text` mówi „wyświetlone”; przy nowym adapterze trzeba zapewnić uczciwe „przyjęte do wysłania”, bo brak dowodu prezentacji. |
| Pulpit i okna aplikacji | `loom/web/src/shell/windows/{model,desktop,registry}.ts`: `desktopOf`, `allWindows`, `desktopCover`, `KINDS`; `Desktop.tsx`: `Desktop`; `shell/data/{desktop,window_kinds}.json` | Jedno tło `background`, deklaracje widoku i rodzaje okien aplikacji, widoczność pod innymi oknami. | Osobne adresy/akcje zasobów systemowych. `window_kinds.json` opisuje widoki aplikacji, nie programy z `/usr/share/applications`. |
| Ikony i przypięcia w grafie | `loom/web/src/shell/windows/desktop-icons.ts`: `pinsFrom`, `iconOf`, `openTargetOf`; `loom/src/actions/ops.cpp`: `window_pin`, `window_unpin`; `loom/tests/test_desktop_pins.cpp` | Relacja `pinned_on`, reprezentanci adresów i otwieranie celu wewnątrz aplikacji. | Opcjonalna projekcja aplikacji/URI systemowych jako węzłów i przejście do wspólnej akcji zewnętrznej. Przypięcie nie może samo zapisać pliku systemowego. |
| Starsza powłoka Kivy | `engine/paths.py`: `_desktop_candidates`; `main.py`: `Window.bind(on_keyboard=...)` | Lokalizacja danych na desktopie i obsługa klawiatury własnego okna. | Nie znaleziono w badanych `engine/`, `gui/`, `main.py` implementacji badanych kontraktów XDG/D-Bus. |

Przeszukanie tekstu `loom/include`, `loom/src`, `loom/web/src`, `loom/server`
nie znalazło `xdg-open`, `org.freedesktop.portal`,
`org.freedesktop.Notifications`, `GDesktopAppInfo`, `g_app_info`,
`XOpenDisplay` ani `wayland-client` w badanych plikach C++/TS.
To ograniczony wynik przeglądu źródeł, nie dowód braku każdej możliwej
integracji. Śledzone pliki nie zawierały gotowego `.desktop`.
`deploy/build-linux.sh` jest skryptem pakowania; nie uruchamiano go.

## 2. Oficjalne kontrakty, zweryfikowane publicznie

Źródła pobrano zwykłym narzędziem WWW z oficjalnych witryn; wszystkie daty
„odczyt” poniżej to data weryfikacji, nie data publikacji. `latest` może się
zmienić, dlatego zapisano sekcje i wersje. Nie odpytywano żywej magistrali.

### Desktop Entry, pliki, MIME i URI

| Kontrakt i lokator | Zweryfikowana treść i konsekwencja dla adaptera |
|---|---|
| [Desktop Entry 1.5, §2 i §2.1](https://specifications.freedesktop.org/desktop-entry/latest/file-naming.html), odczyt 2026-10-31 | Rozszerzenie `.desktop`, zalecana nazwa reverse DNS, identyfikator pliku wynikający z lokalizacji w `applications` i pierwszeństwa katalogów. Fixture poza instalacją nie jest zarejestrowaną aplikacją. Zachować źródłowy plik i jego pochodzenie; nie utożsamiać etykiety `Name` z tożsamością. |
| [Desktop Entry 1.5, §6](https://specifications.freedesktop.org/desktop-entry/latest/recognized-keys.html), odczyt 2026-10-31 | Typy `Application/Link/Directory`, wymagane `Type` i `Name`, lokalizowane etykiety, `NoDisplay/Hidden`, `TryExec`, `Terminal`, `DBusActivatable`, `MimeType`. `NoDisplay` nie oznacza braku aplikacji; `Hidden` ma inne znaczenie. Użyć parsera bibliotecznego i respektować metadane. |
| [Desktop Entry 1.5, §7 Exec](https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html), odczyt 2026-10-31 | `Exec` ma własne zasady argumentów, cytowania i kodów pól: `%u/%U` to URI, `%f/%F` to pliki, `%%` to procent. Co najwyżej jedno z pól pliku/URI; nieznany kod jest błędem. Pole wewnątrz cytowanego argumentu ma wynik niezdefiniowany. Propozycja: parser/launcher GIO, bez ręcznego `split()` i bez przekazania całego `Exec` do `sh -c`. |
| [Desktop Entry 1.5, §8](https://specifications.freedesktop.org/desktop-entry/latest/dbus.html), odczyt 2026-10-31 | Aktywacja D-Bus używa `org.freedesktop.Application` (`Activate`, `Open`, `ActivateAction`) oraz powiązania nazwy usługi/ścieżki z ID aplikacji. Nie deklarować `DBusActivatable=true` bez realizacji tego kontraktu; obsłużyć poprawny mechanizm aktywacji, a nie wyłącznie obecność tekstu `Exec`. |
| [Desktop Entry 1.5, §10](https://specifications.freedesktop.org/desktop-entry/latest/mime-types.html), odczyt 2026-10-31 | `MimeType` deklaruje obsługiwane formaty. Nie ustala pierwszeństwa ani aplikacji domyślnej. |
| [Shared MIME-info, §2.15](https://specifications.freedesktop.org/shared-mime-info/latest/ar01s02.html), odczyt 2026-10-31; lokator tekstowy: „URI scheme handlers” | Wirtualne `x-scheme-handler/<scheme>` opisuje handler schematu URI; nie jest listą wszystkich protokołów, z których aplikacja umie pobierać pliki. W próbie użyto `x-scheme-handler/loom-probe`; to nazwa testowa. |
| [MIME Apps 1.0.1, §2](https://specifications.freedesktop.org/mime-apps/latest/file.html), [§3](https://specifications.freedesktop.org/mime-apps/latest/associations.html), [§4](https://specifications.freedesktop.org/mime-apps/latest/default.html), odczyt 2026-10-31 | `mimeapps.list` ma kolejność wyszukiwania; Added/Removed Associations są odrębne od Default Applications. Wybór domyślny i lista skojarzeń to oddzielne operacje. Ten przyrost żadnej z nich nie zapisuje. |
| [XDG Base Directory 0.8, §2–3](https://specifications.freedesktop.org/basedir/latest/), odczyt 2026-10-31 | `XDG_DATA_HOME` domyślnie `~/.local/share`, `XDG_CONFIG_HOME` domyślnie `~/.config`; ścieżki muszą być absolutne. Przyszła instalacja użytkownika może użyć `XDG_DATA_HOME/applications`; próba pozostaje w worktree, poza tymi katalogami. |
| [xdg-open, Synopsis/Description/Exit Codes](https://portland.freedesktop.org/doc/xdg-open.html), odczyt 2026-10-31 | Otwiera plik/URL preferowaną aplikacją, jest przeznaczony dla sesji desktopowej; ma kody błędu. Możliwy konfigurowalny adapter zastępczy poza sandboxem. Nie uruchamiano go. Wynik procesu nie jest dowodem treści widocznej na ekranie. |

### D-Bus notifications i XDG Desktop Portals

| Kontrakt i lokator | Zweryfikowana treść i konsekwencja dla adaptera |
|---|---|
| [Desktop Notifications 1.3, §2](https://specifications.freedesktop.org/notification/latest/basic-design.html), odczyt 2026-10-31 | Usługa sesji `org.freedesktop.Notifications`, ścieżka `/org/freedesktop/Notifications`, interfejs o tej samej nazwie co usługa. Serwer nie musi istnieć ani uruchamiać się automatycznie. Nie tworzyć drugiego serwera powiadomień. |
| [Desktop Notifications 1.3, §9.1–9.2](https://specifications.freedesktop.org/notification/latest/protocol.html), odczyt 2026-10-31 | `GetCapabilities`, `GetServerInformation`, `Notify(s,u,s,s,s,as,a{sv},i) → u`, `CloseNotification(u)`. Akcje są parami ID/etykieta. Sygnały `NotificationClosed`, `ActionInvoked` i `ActivationToken`; obsługi kliknięć nie wolno zakładać. Wysyłać markup tylko przy odpowiedniej zdolności. Odpowiedź i ID potwierdzają wywołanie, nie przeczytanie powiadomienia. |
| [Portal API Reference](https://flatpak.github.io/xdg-desktop-portal/docs/api-reference.html) i [Writing a New Backend](https://flatpak.github.io/xdg-desktop-portal/docs/writing-a-new-backend.html), odczyt 2026-10-31 | Zwykle nazwa `org.freedesktop.portal.Desktop`, ścieżka `/org/freedesktop/portal/desktop`, **session bus**. API aplikacji jest odrębne od `org.freedesktop.impl.portal.*` przeznaczonego dla backendów. Nie wymaga magistrali systemowej. |
| [Requests](https://flatpak.github.io/xdg-desktop-portal/docs/requests.html) i [Request](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Request.html), odczyt 2026-10-31 | Metoda często zwraca uchwyt, a wynik przychodzi sygnałem `Response(u,a{sv})`: 0 sukces, 1 anulowanie, 2 inne zakończenie. Subskrypcja przed wywołaniem z `handle_token` unika wyścigu; zwrócony uchwyt trzeba sprawdzić. `Close()` kończy żądanie bez późniejszego `Response`. Nie stosować tego schematu do każdego portalu bez sprawdzenia jego kontraktu. |
| [OpenURI, wersja interfejsu 5, OpenURI/OpenFile/OpenDirectory/SchemeSupported](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.OpenURI.html), odczyt 2026-10-31 | `OpenURI(s parent_window,s uri,a{sv} options) → o handle`; **nie przyjmuje `file://`**. Lokalny plik/katalog: `OpenFile(s,h,a{sv})`, z deskryptorem przekazanym przez D-Bus; `OpenDirectory` służy pokazaniu lokalizacji. `ask` od v3, `activation_token` od v4, `SchemeSupported` od v5; `OpenFile` od v2. Dostosować opcje do wykrytej wersji, nie do wersji strony dokumentacji. |
| [FileChooser, wersja 4, OpenFile](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.FileChooser.html), odczyt 2026-10-31 | Wybór plików przez osobę, opcje m.in. `multiple`, `directory` (od v3); wynik `uris` zawiera `file://`. To inne `OpenFile` niż w OpenURI: tutaj wybieramy zasób, tam przekazujemy już dostępny deskryptor aplikacji zewnętrznej. Wybrany plik nie staje się automatycznie danymi do wysłania modelowi. |
| [Notification, wersja 2, AddNotification/RemoveNotification/ActionInvoked](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Notification.html), odczyt 2026-10-31 | `AddNotification(s id,a{sv})` i `RemoveNotification(s id)`; identyfikator tekstowy aplikacji, inny niż liczbowy wynik bezpośredniego `Notify`. Ten portal nie otwiera dialogu zgody i nie daje aplikacji informacji, czy powiadomienie rzeczywiście pokazano. Wspólna akcja `device.notify` może korzystać z obu adapterów, zachowując ich odrębne potwierdzenia. |
| [Security Model](https://flatpak.github.io/xdg-desktop-portal/docs/security-model.html) oraz [portals.conf](https://flatpak.github.io/xdg-desktop-portal/docs/portals.conf.html), odczyt 2026-10-31 | Frontend portalu jest granicą zaufania: waliduje żądania i uprawnienia przed backendem. Backend zależy od konfiguracji środowiska. Dostęp do nazwy magistrali i znajomość API nie dowodzą dostępności operacji ani udzielonej zgody. Nie modyfikowano konfiguracji portali. |

### X11, Wayland i uprawnienia

W X11 EWMH opisuje `_NET_SUPPORTED`, `_NET_CLIENT_LIST` i żądania
`_NET_ACTIVE_WINDOW`; są to kontrakty menedżera okien X, a nie gwarantowane
uprawnienie aplikacji do dowolnej operacji. Ewentualny adapter wymaga dostępu
do właściwego serwera X i sprawdzenia obsługi; tego dostępu nie sprawdzano.
([EWMH 1.5, §3.1–3.2 i §3.8](https://specifications.freedesktop.org/wm/latest/ar01s03.html),
odczyt 2026-10-31).

Wayland reklamuje dostępne obiekty globalne i ich wersje; klient wiąże się
z tym, co oferuje kompozytor
([Wayland Protocol, Versioning/Object Creation](https://wayland.freedesktop.org/docs/book/Protocol.html),
odczyt 2026-10-31). **Wniosek projektowy:** nie przenosić obietnicy listowania
wszystkich okien z X11 na Wayland ani na XWayland. Zdolności dotyczące obcych
okien pozostają nieweryfikowane, dopóki nie zostanie wskazany i sprawdzony
konkretny kontrakt środowiska. Rdzeń protokołu nie jest tutaj traktowany jako
uniwersalny interfejs automatyzacji cudzych aplikacji.

Identyfikator rodzica dialogu portalu to `x11:<XID-hex>` albo
`wayland:<wyeksportowany HANDLE>`; gdy brak uchwytu, można przekazać pusty ciąg.
To rodzic **własnego** dialogu, a nie zgoda na inspekcję obcego okna
([Window Identifiers](https://flatpak.github.io/xdg-desktop-portal/docs/window-identifiers.html),
odczyt 2026-10-31).

Przechwytywanie obrazu i sterowanie wejściem mają osobne kontrakty:
ScreenCast tworzy sesję, wybiera źródła i przy starcie zwykle pokazuje dialog;
RemoteDesktop wybiera urządzenia i zwraca zestaw przyznanych uprawnień po
interakcji z osobą. Nie są warunkiem uruchamiania aplikacji ani powiadomień
i nie były uruchamiane w badaniu
([ScreenCast, Overview](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html),
[RemoteDesktop, Overview](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html),
odczyt obu 2026-10-31).

Należy rozdzielić: regułę użytkownika ChatADHD, wykrytą obecność interfejsu,
uprawnienie do zasobu i wynik konkretnego wywołania. `granted` zadeklarowane
przez klienta nie zastępuje kontroli systemu. Odmowa portalu nie powinna
powodować automatycznej próby obejścia innym adapterem. Fallback i zakres
działania są ustawieniami; brak możliwości jest opisem ograniczenia środowiska.

## 3. Proponowany wspólny interfejs — rozszerzenie obecnego mechanizmu

To propozycja, jeszcze nie implementacja produktu:

```text
rozmowa / ikona / panel
  → actions.pack + Engine + reguły i preview
  → ActionRunner → client_step właściwego klienta
  → lokalny port pulpitu: AdapterRegistry<DesktopPort, DesktopEnv>
  → GIO / portal / D-Bus Notifications
  → client_result + istniejący RunStore i pochodzenie w grafie
```

`DesktopPort` oznacza tu wąski interfejs C++ z operacjami sondy i wykonania;
nie nową bazę akcji. Rejestr i algorytm wyboru pozostają istniejące.
`DesktopEnv` dostarcza połączenie sesji oraz wstrzykiwany transport/atrapę.
Operacja musi trafić na urządzenie wybrane przez osobę: Linux serwera HTTP
nie oznacza Linuksa osoby otwierającej stronę. Na początku połączenie lokalne
w jednym procesie; zdalny most wymaga osobnego kontraktu tożsamości i dostępu.

| Co jest danymi | Miejsce rozszerzenia | Co robi kod |
|---|---|---|
| ID, etykiety, frazy, parametry, efekty i zasady akcji | `runtime/actions.pack`, istniejące reguły, klasy/traits i nakładki | Ten sam interpreter i kontrola argumentów. |
| Role `desktop.open_uri`, wybór `xdg_portal` lub innego adaptera, parametry i warunki fallbacku | `runtime/ports.pack`, `PortProfile`; nazwy robocze | Rejestr fabryk i realizacja konkretnego protokołu, bez ukrytego wyboru polityki. |
| Zdolności `opens_uris`, `lists_applications`, `launches_applications`; istniejące `shows_notifications`, `picks_files` | Deklaracja klienta oraz środowiska/zdolności w tym samym grafie | Sonda realnego interfejsu i wersji; żadnej zamkniętej listy możliwości Linuksa. |
| Obserwacja: adapter, sesja/klient, interfejs, wersja, czas, wynik sondy i powód niedostępności | Metadane istniejących węzłów środowisk/portów, z jawnie zaprojektowanym kontraktem | Aktualizacja/rebuild; oddzielenie obserwacji od deklaracji pakietu. `capability_json` dziś nie eksportuje dowolnych nowych pól — wymaga świadomego rozszerzenia. |
| Tryb URI, zakres źródeł aplikacji, potwierdzanie i zapisywanie wyników | Profile i poziomy ustawień | Respektowanie wyboru osoby; walidacja składni standardu nie jest arbitralnym limitem produktu. |

Pierwszym nowym ID może być `desktop.open_uri` z `client.capability:
opens_uris`, `consumer: client` i parametrem `uri`; to nazwa robocza w danych.
`device.notify` i `device.pick_file` już istnieją — nie dodawać ich kopii
`linux.notify` / `linux.pick_file`. `window.*` oraz `arranges_windows`
zachowują obecne znaczenie okien aplikacji. Sterowanie oknami systemu wymaga
osobnej, precyzyjnie opisanej zdolności i adaptera.

Proponowany wynik zachowuje obecne `state: ok/failed/declined/unavailable`,
a w `data` podaje potwierdzony etap, adapter i wersję interfejsu. Dla
powiadomienia `ok` oznacza przyjęcie przez usługę; widoczność jest nieznana.
Anulowanie Request mapuje się na zakończenie bez ponawiania efektu; awaria
transportu po wysłaniu ma wynik niepewny, a nie automatyczne „wykonaj jeszcze
raz”. Dokładne nazwy pól i reguła wznowienia wymagają kontraktu/testów w pakiecie
implementacji. Nie proponuje się równoległego dziennika wykonań.

Launcher i katalog aplikacji można później zbudować jako projekcję Desktop
Entries do węzłów z lokatorem/oryginałem oraz akcją uruchomienia. Pliki mają
mieć oddzielny wybór i politykę utrwalania. Powiadomienia aplikacji nie
uprawniają do odczytu cudzych powiadomień. Usługi sesji/systemu, harmonogramy,
udostępnianie i dalsze zasoby pozostają otwartą listą możliwych adapterów.
Nie zweryfikowano tutaj kontraktów systemd/polkit, więc zarządzanie usługami
nie otrzymuje statusu dostępnego.

## 4. Wykonana najmniejsza próba odwracalna

Pliki w lokalnym worktree użytkownika:

- `tools/research/linux_desktop/org.example.ChatADHDProbe.desktop`;
- `tools/research/linux_desktop/test_contract.py`;
- `tools/research/linux_desktop/README.md`.

Wpis `Application` wskazuje atrapę `/usr/bin/true %u`, ma `NoDisplay=true`
i testowy handler `loom-probe`. Nie instalowano go w `applications`;
`Exec` nie został uruchomiony. Test czyta wyłącznie wskazaną fixture, a pliki
tymczasowe tworzy pod swoim katalogiem w CWD i usuwa. Odwrócenie polega na
usunięciu tych trzech plików; nie ma skutków w sesji ani ustawieniach systemu.

Użyto istniejących: `desktop-file-validate 0.28`, Python `3.14.4`,
GLib runtime `2.88.0`, PyGObject/GIO Unix. Brak plików pkg-config
`glib-2.0.pc`/`gio-unix-2.0.pc` w bieżącej ścieżce — nie potwierdzono gotowości
środowiska do kompilacji adaptera C++. Nie instalowano zależności.

Faktyczna komenda, z korzenia worktree:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/research/linux_desktop/test_contract.py
```

Wynik końcowy: **8/8 testów PASS, 0 pominiętych**. Walidacja pozytywna i
negatywna rzeczywistym walidatorem; parser GIO Unix czyta wpis; GLib odrzuca
względne URI, sprawdza typy `(ssa{sv})` dla OpenURI i zachowanie oryginalnych
bajtów dwóch syntetycznych URI, także z procentami i znakami podobnymi do
składni powłoki. Pakiet argumentów powstaje wyłącznie w pamięci. Nie utworzono
połączenia/proxy D-Bus ani nie testowano rozwinięcia `%u` przez launcher.
API biblioteczne:
[GIO Unix new_from_filename](https://docs.gtk.org/gio-unix/ctor.DesktopAppInfo.new_from_filename.html),
[GLib Uri.parse](https://docs.gtk.org/glib/type_func.Uri.parse.html),
odczyt obu 2026-10-31.

**Obserwacja negatywna z próby:** pierwszy przebieg 6/7 PASS i kolejny 7/8
PASS ujawniły błędne oczekiwanie testu, że brak `Exec` zostanie odrzucony
przez walidator lub parser. Oba przyjmują ten wariant; GIO zwraca obiekt bez
executable. Test opisuje teraz ten rzeczywisty wynik, a nie zakłada zdolności
uruchomienia. Warianty bez wymaganego `Name`, z `%Z` i z `%u %U` są odrzucane
przez walidator. Przyszły adapter musi sprawdzić wykonalny mechanizm aktywacji
oddzielnie; nie zmieniono żadnej bramki produktu.

**Nie wykonano testu rzeczywistej sesji GUI Linuksa**, ani X11, ani Wayland.
Nie uruchamiano launchera, otwierania pliku/URI, powiadomienia, dialogu portalu,
inspekcji okien, przechwytywania ekranu ani klawiatury. Test lokalnych bibliotek
i typów nie jest dowodem zgodności backendu GNOME/KDE/innego środowiska.
Nie wykonano pełnego buildu, CTest ani testów webu; kod produktu się nie zmienił.

## 5. Następny jeden pakiet implementacji i odbiór

Proponowany pakiet **L1: lokalne otwarcie URI przez istniejącą akcję klienta**.
Jedna funkcja pionowo przez katalog → klient → adapter → wynik; pozostałe
możliwości zostają opisane, ale nie są oznaczane jako wykonane.

| Pliki | Zakres następnego pakietu |
|---|---|
| `loom/data/runtime/{actions,ports,client_devices}.pack`, `loom/data/graph/{classes,environments}.pack` | Dodać dane `desktop.open_uri`/`opens_uris`, efekty, pl/en, frazy i wybór adaptera; nie zmieniać znaczenia `window.*`. Zaktualizować właściwe dane pokrycia rozmowy i wygenerowane pakiety według obecnych generatorów. |
| Nowe `loom/src/desktop/{port.h,registry.cpp,xdg_portal.cpp}` | Wąski port wewnętrzny, istniejący `AdapterRegistry`, wstrzykiwany transport i atrapa; walidacja URI, wersji i parametrów, asynchroniczny Request, anulowanie i jawny wynik niepewny. Zależność D-Bus/GIO wyłącznie opcjonalna dla celu Linux. |
| `loom/CMakeLists.txt` | Opcjonalne wykrycie/linkowanie zależności GIO/D-Bus i warunek budowania adaptera; bez ręcznego dopisywania plików do list źródeł obsługiwanych przez glob. Pozostałe platformy i build bez adaptera zachowują jawną niedostępność tej zdolności. |
| Nowe `loom/src/desktop/local_client.cpp`; `loom/src/actions/runner.cpp` tylko jeśli potrzebny zaczep | Klient lokalny w tej samej sesji/procesie, deklaracja i obsługa `client_step` przez istniejące `loom_invoke("actions", ...)`. Bez nowego endpointu sieciowego. To jawne miejsce wymagające implementacji: obecny web nie jest natywnym hostem Linuksa. |
| `loom/src/graph/environments.cpp`, istniejący `port_graph` | Podłączyć deskryptory portu; obserwację dostępności sesji/wersji oddzielić od pakietu. Brak magistrali/interfejsu nie staje się odmową osoby ani pełną gotowością. Nie utrwalać tokenów aktywacji. |
| Nowe `loom/tests/test_linux_desktop.cpp`; rozszerzenie `loom/tests/test_client_devices.cpp` i `test_environments.cpp` | Atrapa transportu i zegara: sukces, brak usługi/interfejsu, starsza wersja, odmowa, anulowanie, `Close` bez Response, wyścig odpowiedzi, błędny typ, zły paragon/klient, restart z wynikiem niepewnym. Fixture publiczne/syntetyczne, bez sesji właściciela. |
| Ten raport, `docs/STATE.md`, dokumentacja lokalnego klienta | Zapisać faktyczny zakres, tryb uruchomienia/odwrócenia oraz macierz testów wykonanych i niewykonanych. |

Kryteria odbioru L1:

1. Zapytanie o możliwości i wywołanie z rozmowy korzystają z tych samych danych
   i `ActionRunner`; bez klienta zdolność jest uczciwie niedostępna/planned.
2. Test atrapą przechodzi pełne `declare → preview/run → client_step →
   client_result`; reguły i paragony działają, cudzy/stary paragon nie wykonuje
   kroku. Po restarcie klient deklaruje się ponownie; jego deklaracja nie
   jest trwałą zgodą. Efekt nie jest ponawiany przy niepewnym wyniku.
3. URI pozostaje jednym argumentem danych, bez powłoki. `file://` nie trafia
   do `OpenURI.OpenURI`; brak implementacji OpenFile jest jawną niedostępnością
   tej drogi. Inne schematy wynikają z danych i możliwości backendu.
4. Sukces Request i wynik niepewny są rozróżnione; anulowanie nie wywołuje
   fallbacku. Wersja interfejsu i dowód sondy są widoczne w grafie/odpowiedzi,
   bez sekretów i tokenów w trwałym stanie.
5. Wykonać właściwe testy katalogu, reguł, klienta i środowisk w **nowym,
   własnym katalogu buildu w CWD**, generatory `--check` i kontrolę eksportów
   `loom/tests/compat/test_abi_compat.py` przy zmianie publicznego ABI.
   Preferowana droga używa obecnego `loom_invoke`, bez dodawania funkcji C ABI.
6. Osobno, w przyszłym uzgodnionym teście sesji, użyć wyłącznie własnej fixture
   i wskazanej aplikacji testowej. Zapisać X11/Wayland, wersje/backend portalu,
   zgody, faktyczny wynik i cofnięcie. Bez tego odbiór dotyczy mechanizmu z
   atrapą, nie działania na rzeczywistym pulpicie.

Pakiety późniejsze mogą dodać katalog/uruchamianie aplikacji, deskryptory
plików, adapter `device.notify`, integrację launchera użytkownika oraz
specyficzne kontrakty okien/usług. To przykłady dalszej przestrzeni, nie jej
zamknięcie. Nie rozpoczynano ich w tym przyroście.

## 6. Jawne granice i punkt wznowienia

Wymagane kontrakty Desktop Entry, MIME Apps/URI, Notifications i Portals udało
się pobrać. Pierwszy stary adres rozdziału EWMH nie był dostępny; oficjalny
indeks przekierował do działającego `wm/latest/ar01s03.html`, użytego wyżej.
Stary adres klasy GIO miał 404; zweryfikowano aktualną dokumentację GIO Unix.
Nie obchodzono ograniczeń sieci. Kontrakty konkretnego backendu GNOME/KDE,
rozszerzeń do obcych okien, systemd/polkit i dystrybucji pozostają
**nieweryfikowane**; nie przypisano im dostępności na tej maszynie.

**D7–D10 pulpitu R101 oraz W11a-2 pozostają odrębną, otwartą pracą produktu.**
Raport ani test fixture nie zamykają ich. Nie zmieniono aktywnej sesji,
domyślnych aplikacji, VNC, Caddy, autostartu, usług ani środowiska graficznego.
Brak wywołań modeli/API modeli, instalacji, wdrożeń i operacji publikacji.

Punkt wznowienia dla integratora: odebrać pliki z `CHECKPOINT.md`, ponowić
podaną próbę kontraktu, następnie osobno przydzielić L1 i przygotować jego
transport/atrapę. Nie utożsamiać gotowego raportu z gotowym adapterem.
