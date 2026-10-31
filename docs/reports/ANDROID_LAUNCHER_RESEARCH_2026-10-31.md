# ChatADHD jako launcher Androida — pierwszy przyrost badawczy

Data odczytu kodu i dokumentacji: **2026-10-31 UTC**. Baza i badany HEAD:
`9533d68e6cf6e133e0fdcd5d5fcf7ee85654da08`, gałąź `team/chatadhd-android`.
Początkowy `git status --short` był pusty. To raport z inwentaryzacji i projekt
próby; w tym przyroście nie zmieniono aplikacji ani manifestu.

**Wniosek:** aktualny ChatADHD jest aplikacją uruchamianą z innego launchera.
Ma rozwijany pulpit grafu wewnątrz WebView, ale nie deklaruje wejścia HOME.
Można dołożyć natywną warstwę HOME, zachowując obecny graf, okna i czat.
Kwalifikacja w manifeście, wybór domyślnej aplikacji i kompletność interfejsu
launchera to trzy osobne rzeczy. Poniższy prototyp bada dwie pierwsze;
nie stanowi deklaracji gotowego zamiennika pulpitu.

## 1. Dowody z aktualnego kodu

Ścieżki są względem katalogu repozytorium; numery linii dotyczą badanego HEAD.
„Brak” oznacza wynik określonego odczytu/przeszukania, nie dowód z urządzenia.

| Istniejąca zdolność | Plik i symbol / lokalizacja | Luka | Dowód i granica dowodu |
|---|---|---|---|
| Ikona i wejście do aplikacji | `loom/android/app/src/main/AndroidManifest.xml`, `.MainActivity`, filtr `intent-filter` | Brak `HOME` i `DEFAULT`, brak obsługi wyboru HOME | Parser XML: 3 aktywności, 1 filtr, dokładnie `MAIN` + `LAUNCHER`; `exported=true`, `singleTask`. Ikona nie dowodzi funkcji HOME. |
| Określona baza Androida | `loom/android/app/build.gradle.kts:34`, blok `android/defaultConfig` | Nowsze API trzeba rozdzielać według wersji | `applicationId=com.chatadhd.android`, `minSdk=26`, `compileSdk=34`, `targetSdk=34`; nie jest to wynik kompilacji. |
| Powłoka lokalna i serwerowa | `loom/android/app/src/main/java/com/chatadhd/android/MainActivity.kt:247`, `startForMode()` | HOME nie powinien zależeć od logowania ani dostępności serwera | Tryb serwerowy ładuje URL bez mostka; lokalny dodaje `LoomBridge` i inicjuje rdzeń. |
| Granica zaufania WebView | ten sam plik:140, `shouldOverrideUrlLoading()`; `ConnectionPolicy.java:77`, `shouldInjectBridge()` | Nowe akcje systemowe potrzebują równie ścisłej granicy | Zewnętrzne URL idą do `ACTION_VIEW`; mostek dodawany tylko w lokalnej gałęzi. To otwieranie URL, nie katalog/uruchamianie aplikacji według profilu. |
| Pulpit grafu, przypięcia, tło grafu i deklaracje widoku | `loom/web/src/shell/windows/Desktop.tsx:98`, `Desktop()`; `desktop-load.ts:19`, `loadPins()` i `:43`, `loadAround()` | Źródło ikon aplikacji Androida jeszcze nie istnieje | Przypięcia przez `pinned_on`, początkowe przypięcia przez akcję `window.pin`, otoczenie przez `graphNavigate`; tło rysowane na canvas. |
| Różne źródła ikon, ustawienia jako dane | `loom/web/src/shell/windows/desktop-icons.ts:15`, `ICON_SOURCES`; `desktop.ts:24`, `DESKTOP`; `loom/web/src/shell/data/desktop.json` | Brak adaptera tożsamości `profil + component` | Obsługiwane źródła: rodzaje okien, zminimalizowane okna, układy, ostatnie rozmowy. Nazwa „ikona” nie oznacza komponentu Androida. |
| Otwieranie elementów pulpitu | `loom/web/src/App.tsx:738`, `deskIcon()`; `:546`, obsługa `desktop.show` | Brak otwierania zewnętrznego komponentu | Ikona ustawia fokus nawigatora/rozmowę i otwiera wewnętrzny widok; `desktop.show` minimalizuje okna w jednym wpisie historii. |
| Okna i odtwarzanie układu | `loom/web/src/shell/windows/WindowHost.tsx:150`, `WindowHost()`; `loom/web/src/App.tsx:713`, `writeSaved(localStorage, ...)` | To nie systemowy menedżer zadań ani osadzanie obcych aplikacji | Renderowanie okien React/DockTree, stan widoku zapisany w WebView; brak dowodu zastępowania Android Recents. |
| Wejście do grafu przez JNI | `loom/web/src/components/GraphEntryPanel.tsx:29`, `GraphEntryPanel()`; `loom/web/src/api/loom-jni.ts:358`, `getGraphEntry()`; `LoomBridge.kt:289`, `graphEntry()`; `loom/android/app/src/main/cpp/loom_jni.cpp:543`, wpis `graph_entry` | Brak kontraktu operacji launchera | Istniejący łańcuch web → mostek → C ABI dla grafu. Ogólne `call()`/`invoke()` nie zapewniają dostępu do Android `LauncherApps`. |
| Obsługa cyklu życia | `MainActivity.kt:295`, `loadApp()`; `:353`, `onDestroy()`; `LoomBridge.kt:135`, `shutdown()` | Trzeba sprawdzić zimny start, ponowne HOME i odtwarzanie procesu | Init przed załadowaniem webu; shutdown wywołuje `nativeShutdown()`. Brak `onNewIntent()` w tej klasie. Dodanie drugiej aktywności inicjującej JNI wymagałoby rozwiązania własności kontekstu. |
| Istniejące zgody urządzenia i magazynu | manifest; `MainActivity.kt`, `requestDevices`, `requestNotifications`, `ensureStoragePermissionThenLoad()` | Żadna z nich nie ustanawia HOME | 7 deklaracji: Internet, 3 magazynu, mikrofon, kamera, powiadomienia; brak `<queries>`, `QUERY_ALL_PACKAGES`, hosta widgetów i metadanych shortcuts. |
| Zaplecze weryfikacji | `loom/android/verify/CMakeLists.txt`, `jni_host_smoke_test`; `loom/android/smoke/run.sh`, `drive.mjs` | Brak scenariusza wyboru i wycofania HOME | Hostowy JNI wymaga JDK i biblioteki; CMake może zakończyć konfigurację z **zerem testów**. Skrypt emulatora uruchamia obecną aplikację/czat; nie potwierdza roli HOME. Nie uruchomiono go w tej sesji. |

Sprawdzenie tekstu objęło **41 plików** `.kt/.java/.cpp/.ts` z
`loom/android/app/src/main/java`, `loom/android/app/src/main/cpp` i
`loom/web/src/api`. Dla słownika `RoleManager`, `CATEGORY_HOME`,
`ACTION_HOME_SETTINGS`, `LauncherApps`, `LauncherActivityInfo`, `ShortcutManager`,
`AppWidgetHost`, `QUERY_ALL_PACKAGES`, `queryIntentActivities`,
`getInstalledApplications`, `getLaunchIntentForPackage`, `startMainActivity`,
`UserHandle` uzyskano **0 dopasowań**. Osobno przeczytano powyższe pliki pulpitu.
To ograniczony audyt źródeł, bez analizy scalonego manifestu APK lub zależności.

## 2. Oficjalny kontrakt Androida

Odczyt wykonano zwykłym dostępnym narzędziem web, bez omijania blokad,
pobierania SDK lub obrazów. Poniższe twierdzenia pochodzą z treści dokumentów,
nie z pamięci o API. Propozycje implementacyjne są wydzielone w §4–5.

### HOME i świadomy wybór

`CATEGORY_LAUNCHER` oznacza pokazanie aktywności w launcherze;
`CATEGORY_HOME` oznacza aktywność ekranu głównego.
Źródło: [Intent — HOME / LAUNCHER](https://developer.android.com/reference/android/content/Intent#CATEGORY_HOME), odczyt 2026-10-31.
Oficjalny przykład kwalifikacji HOME zawiera **osobny filtr `MAIN` + `DEFAULT` + `HOME`**.
Źródło: [RoleManagerCompat.ROLE_HOME](https://developer.android.com/reference/androidx/core/role/RoleManagerCompat#ROLE_HOME), odczyt 2026-10-31.

Od API 29 `RoleManager.ROLE_HOME` pozwala poprosić o rolę. Należy sprawdzić
`isRoleAvailable()`, a `isRoleHeld()` opisuje bieżące posiadanie roli.
`createRequestRoleIntent()` otwiera systemową prośbę do osoby; wynik może być
`RESULT_OK` albo `RESULT_CANCELED`. Kwalifikacja w manifeście nie zastępuje tej
decyzji. Źródło: [RoleManager](https://developer.android.com/reference/android/app/role/RoleManager), odczyt 2026-10-31.
Dla API 26–28 dostępnym w kontrakcie wejściem jest `Settings.ACTION_HOME_SETTINGS`
(od API 21), ekran wyboru preferowanej aktywności HOME. Źródło:
[Settings.ACTION_HOME_SETTINGS](https://developer.android.com/reference/android/provider/Settings#ACTION_HOME_SETTINGS), odczyt 2026-10-31.

AOSP opisuje HOME jako ekran uruchamiany przyciskiem Home, z ikonami aplikacji,
widgetami i nawigacją przyciskami/gestami. Sam manifest nie dowodzi spełnienia
tych oczekiwań interfejsu. Źródło:
[Android roles — HOME](https://source.android.com/docs/core/permissions/android-roles#home), odczyt 2026-10-31.

### Aplikacje, widoczność i profile

`LauncherApps` (od API 21) udostępnia aktywności dla użytkownika i dostępnych
profili. `getActivityList(packageName, user)` zwraca wpisy launchera;
`startMainActivity(component, user, ...)` uruchamia wskazany komponent w profilu.
`getProfiles()` jest od API 26; aplikacja działająca w profilu służbowym nie
otrzymuje przez niego dostępu do profilu głównego. `registerCallback()` służy
zmianom pakietów. Lista może zawierać syntetyczny wpis prowadzący do ustawień
aplikacji — nie każdy wynik oznacza jej zwykły ekran startowy. Źródło:
[LauncherApps](https://developer.android.com/reference/android/content/pm/LauncherApps), odczyt 2026-10-31.

Dla aplikacji z `targetSdk >= 30` Android 11+ filtruje widoczność pakietów,
m.in. wyniki zapytań `PackageManager`. Dokumentacja zaleca deklarowanie
potrzeb przez `<queries>`; szerokie `QUERY_ALL_PACKAGES` ma ograniczone
zastosowania i przy publikacji w Google Play podlega zatwierdzeniu.
Nie ma podstaw do twierdzenia „HOME widzi wszystko”. Źródło:
[Package visibility](https://developer.android.com/training/package-visibility), odczyt 2026-10-31.
Można zadeklarować zapytanie według sygnatury intencji, np. dla potrzeb
katalogu `MAIN` + `LAUNCHER`; to proponowany zakres, a jego kompletność dla
wybranej ścieżki `LauncherApps` trzeba sprawdzić na systemie. Źródło mechanizmu:
[Declare package visibility needs](https://developer.android.com/training/package-visibility/declaring#intent-signature), odczyt 2026-10-31.

Profil służbowy izoluje dane i aplikacje; administrator kontroluje dozwolone
przejścia między profilami. Obecność pakietu w drugim profilu nie daje dostępu
do jego prywatnych plików. Źródła:
[Work profiles](https://developer.android.com/work/managed-profiles),
[Enterprise developer guide](https://developer.android.com/work/guide#work-profiles), odczyt obu 2026-10-31.
W projekcie katalogu tożsamość powinna więc obejmować instalację urządzenia,
profil i komponent, a nie samą nazwę pakietu. Jest to **wniosek projektowy**;
format trwałego identyfikatora profilu wymaga osobnego kontraktu i próby.

### Shortcuts: hostowanie a publikowanie

Shortcuts statyczne, dynamiczne i przypięte są odrębne od zwykłych ikon
aplikacji. Dokumentacja wskazuje `LauncherApps` jako API odczytu dla launchera;
dane shortcuts są w magazynie credential-encrypted, dostępnym po odblokowaniu.
Źródło: [Manage shortcuts — visibility](https://developer.android.com/develop/ui/compose/system/shortcuts/managing-shortcuts#shortcut-visibility), odczyt 2026-10-31.
Host powinien sprawdzać `hasShortcutHostPermission()`; odczyt przez
`getShortcuts()` i uruchamianie przez `startShortcut()` są w API 25+.
Źródło: [LauncherApps — shortcuts](https://developer.android.com/reference/android/content/pm/LauncherApps#hasShortcutHostPermission()), odczyt 2026-10-31.
`ShortcutQuery` rozróżnia flagi wyboru, np. dynamiczne od API 25 i cached od API 30.
Źródło: [LauncherApps.ShortcutQuery](https://developer.android.com/reference/android/content/pm/LauncherApps.ShortcutQuery), odczyt 2026-10-31.

W API 26+ `PinItemRequest` reprezentuje żądanie przypięcia. Launcher wywołuje
`accept()` po akceptacji osoby, sprawdza ważność i wynik; `false` nie oznacza
udanego przypięcia. Żądanie widgetu używa opcji z przydzielonym ID widgetu.
Źródło: [LauncherApps.PinItemRequest](https://developer.android.com/reference/android/content/pm/LauncherApps.PinItemRequest), odczyt 2026-10-31.
Publikacja własnego skrótu ChatADHD nie jest dowodem hostowania skrótów innych
aplikacji. Integrację obu kierunków trzeba zaprojektować oddzielnie.

### Widgety

Osadzanie cudzych widgetów wymaga `AppWidgetHost` i `AppWidgetHostView`,
przydzielania ID instancji oraz zachowania powiązanego stanu położenia i rozmiaru.
To natywne widoki; proponowaną integracją z grafem jest natywny kontener
z odwołaniem z węzła, a nie zastąpienie widgetu obrazkiem w canvas.
Źródło mechanizmu: [Build a widget host](https://developer.android.com/develop/ui/views/appwidgets/host), odczyt 2026-10-31; wybór kontenera jest propozycją.

`bindAppWidgetIdIfAllowed()` może odmówić; wtedy służy systemowy
`ACTION_APPWIDGET_BIND`. Sam wybór HOME nie potwierdza zgody na powiązanie widgetu.
Źródło: [AppWidgetManager — bind](https://developer.android.com/reference/android/appwidget/AppWidgetManager#bindAppWidgetIdIfAllowed(int,%20android.content.ComponentName)), odczyt 2026-10-31.
Ważny szczegół: przewodnik hosta pokazuje deklarację `BIND_APPWIDGET`, ale
referencja uprawnienia oznacza je jako niedostępne dla zwykłych aplikacji
zewnętrznych. Nie projektować tego jako zgody `requestPermissions()`;
oprzeć próbę na wyniku bind i systemowym ekranie wyboru. Źródła:
[BIND_APPWIDGET](https://developer.android.com/reference/android/Manifest.permission#BIND_APPWIDGET),
[przewodnik hosta](https://developer.android.com/develop/ui/views/appwidgets/host#bind-app-widgets), odczyt obu 2026-10-31.

### Granice wersji i pobrania

| Zakres | Potwierdzony kontrakt / konsekwencja |
|---|---|
| API 26–28, wspierane przez obecne `minSdk` | Nie używać `RoleManager` bez bramki wersji; ekran HOME przez Settings. Źródła RoleManager / Settings powyżej, odczyt 2026-10-31. |
| API 29+ | Możliwa systemowa prośba o rolę, pod warunkiem dostępności roli; osobno sprawdzać rzeczywiste wpisy katalogu. Źródła RoleManager / LauncherApps powyżej, odczyt 2026-10-31. |
| API 30+, przy obecnym target 34 | Uwzględnić filtrowanie pakietów; pusty wynik nie dowodzi braku instalacji. Źródło Package visibility powyżej, odczyt 2026-10-31. |
| API 35+ — Private Space | Android 15 wymaga roli HOME i normalnego `ACCESS_HIDDEN_PROFILES`, aby launcher widział tę przestrzeń. Deklaracja niesie obowiązki: osobny kontener, ukrywanie/pokazywanie, blokowanie/odblokowywanie; po zablokowaniu aplikacje nie mogą ujawniać się także w wyszukiwaniu. Źródło: [Android 15 — Launcher apps](https://developer.android.com/about/versions/15/behavior-changes-all#private-space), odczyt 2026-10-31. Obecne compileSdk 34 nie daje podstaw do deklarowania tej integracji. |

Wszystkie wykorzystane wyżej strony udało się odczytać; nie odnotowano blokady
sieci. Dwa stare adresy przewodników przekierowały do podanych adresów
`/develop/ui/...`. Nie pobierano SDK, obrazów, kodu Launcher3 ani bibliotek.
Nie wykonano pełnego audytu Androida 16/17, polityk OEM, dystrybucji sklepowej
ani zachowania profili na urządzeniu. Bieżące referencje mogą opisywać API
nowsze od compileSdk projektu; nie są dowodem ich dostępności w tym APK.

## 3. Zgody i prywatność — proponowany model

Oddzielić następujące decyzje w interfejsie i stanie aplikacji:

1. **Wybór HOME**: systemowa decyzja osoby; podgląd nie uruchamia prośby
   automatycznie, anulowanie pozostawia dotychczasowy pulpit.
2. **Katalog urządzenia**: metadane aplikacji/profili mogą ujawniać zainteresowania
   lub pracę. Prototyp odczytuje je lokalnie na potrzeby widoku. Trwały zapis
   w grafie, historia uruchomień i synchronizacja to osobno wybierane funkcje,
   nie skutki nadania HOME. Proponowany preset próby: katalog nietrwały.
3. **Shortcuts i widgety**: stosować ich rzeczywiste kontrakty dostępności
   i akceptacji opisane w §2. Nie zastępować odmowy ogólnym „brak aplikacji”.
4. **Wyjście danych poza urządzenie**: prawo do odczytu lokalnego nie jest
   zgodą na wysłanie nazw, etykiet, ikon czy profili do serwera/modelu.
   Nowy katalog w prototypie nie przechodzi przez JNI ani transport sieciowy;
   późniejsze reguły zachowania i udostępniania są konfigurowalne.

To propozycje produktu, nie dodatkowe wymagania API. Istniejące zgody kamery,
mikrofonu, powiadomień i magazynu pozostają osobnymi przepływami; brak potrzeby
rozszerzania ich w celu samej próby HOME. Nie proponuje się `QUERY_ALL_PACKAGES`,
AccessibilityService, dostępu do statystyk użycia ani powiadomień innych aplikacji
w pierwszym pakiecie. Dalsze funkcje mogą mieć odrębne, uzasadnione kontrakty.

## 4. Najmniejszy odwracalny prototyp

**Propozycja P1: natywna `HomeActivity` bez własnego WebView i bez inicjowania
Loom/JNI.** Zachować obecne `MainActivity` z ikoną `LAUNCHER`; nowej aktywności
dać osobny filtr `MAIN` + `HOME` + `DEFAULT`. Istniejąca strona „Połączenie”
otwiera jawny podgląd nowej aktywności. Samo otwarcie podglądu lub instalacja
nie wywołuje prośby o zmianę domyślnej aplikacji.

Podgląd zawiera lokalną listę komponentów z bieżącego profilu, jawne określenie
zakresu, stan „podgląd / wybrany HOME / brak dostępnej roli”, otwarcie ChatADHD
oraz osobny przycisk świadomego wyboru HOME. Po nim, na API 29+, sprawdzenie
dostępności/posiadania i systemowa prośba; na 26–28 ekran ustawień HOME.
Obsłużyć anulowanie, brak aktywności ustawień i zmianę roli poza aplikacją.
Nie deklarować sukcesu na podstawie samego kliknięcia przycisku.

Kliknięcie aplikacji uruchamia wybrany komponent przez natywny adapter
`LauncherApps`; lista jest odświeżana, a zniknięcie/wyłączenie komponentu daje
czytelny stan. Wyniki syntetyczne prowadzące do ustawień nie są opisywane jako
zwykłe uruchomienie. Powrót do ChatADHD używa istniejącej `MainActivity`,
z zachowaniem wybranego trybu i zapisanego widoku. Do pulpitu grafu prowadzi
dzisiejsza akcja `desktop.show`; P1 nie wymusza nowego układu ani nie obiecuje
automatycznego przejścia do grafu po każdym Home.

Natywna lista stanowi awaryjne, lokalne wejście także przy niedostępnym serwerze
lub rdzeniu. Oddzielenie jej od JNI wynika z odczytanego cyklu init/shutdown;
zachowanie zadań, Back i ponownych intencji pozostaje do sprawdzenia na Androidzie.

Wycofanie: przycisk otwiera systemowy wybór HOME; osoba wybiera poprzedni
launcher. Nie wymaga to usuwania ChatADHD ani danych. Usunięcie nowej aktywności
i jej filtra wycofuje zmianę kodu bez migracji magazynu grafu.
Widżety, shortcuts i pełna obsługa profili pozostają następnymi próbami:
P1 ma być oznaczony jako eksperymentalny, częściowy HOME.

Równoległy kierunek pulpitu grafu, po sprawdzeniu P1: nowy adapter źródła ikon
aplikacji, wspólne przypięcia/położenia i odwołania do komponentów z węzłów;
klikalny cel Androida wymaga walidacji w natywnym adapterze. Nie zamieniać
`WindowHost` w pozorny menedżer obcych aplikacji. Shortcuts mogą być celami
akcji, widgety natywnymi powierzchniami powiązanymi z węzłami. Dalej otwarte są
m.in. wyszukiwanie, dostępność, klawiatura, gesty, tablet, różne profile,
przywracanie układu i automatyzacje — to rozwijana lista kierunków, nie zamknięty
katalog funkcji. Zainstalowany pakiet jest obserwacją o środowisku, nie
obserwacją preferencji ani faktycznego używania aplikacji.

## 5. Konkretny następny pakiet P1

Wszystkie poniższe zmiany są **planowane**, nie zapisane w tej sesji.

| Zakres plików | Zmiana i kryterium odbioru |
|---|---|
| `loom/android/app/src/main/AndroidManifest.xml` | Dodać `HomeActivity` i osobny filtr HOME; zachować istniejący filtr MainActivity. Ewentualne `<queries>` wyłącznie pod uzasadnione zapytanie `MAIN/LAUNCHER`; bez szerokiego dostępu. Sprawdzić scalony manifest zbudowanej paczki. |
| nowe `loom/android/app/src/main/java/com/chatadhd/android/HomeActivity.kt`, `AndroidLauncherGateway.kt` | Lokalny podgląd, stan roli, wybór systemowy, lista i uruchamianie komponentu, powrót do MainActivity. Brak inicjalizacji JNI, automatycznej prośby o HOME i zależności katalogu od sieci. |
| nowy `loom/android/app/src/main/java/com/chatadhd/android/LauncherPolicy.java` | Tylko czysta logika decyzji podgląd/prośba/ustawienia i walidacji wyboru; platformowe działania w adapterze. Testować znaczące granice: brak roli, już posiadana rola, anulowanie, nieaktualny cel, zakres profilu. |
| `loom/android/app/src/main/java/com/chatadhd/android/ConnectionActivity.kt`, `res/values/strings.xml`, `res/values-pl/strings.xml` pod `app/src/main` | Jawne wejście w podgląd, wyjaśnienie skutku i powrotu do poprzedniego HOME. Napisy w zasobach; bez zmiany trybu local/server. |
| nowe `loom/android/verify/java/loomverify/LauncherPolicyTest.java`; `loom/android/verify/CMakeLists.txt` | Uruchamiać rzeczywistą czystą klasę, nie kopię. Jawnie raportować brak JDK / zero wykonanych testów. Bez nowego publicznego C ABI. |
| nowy `loom/android/smoke/launcher.md` lub osobny scenariusz w tym katalogu; `loom/android/README.md` | Spisać i wykonać macierz niżej z konkretnym APK/rewizją/API. Nie zmieniać istniejącego smoke tak, by sam wybierał domyślny HOME. |

**Odbiór hostowy — oddzielnie:** sprawdzenie XML i scalonego manifestu;
test czystej polityki z atrapą rezultatów; build APK w przygotowanym środowisku
z JDK/SDK bez pobierania zależności w obecnym przyroście. Istniejące testy
webowego pulpitu (`windows-desktop*.test.mjs`) są odpowiednie po zmianie
webu w kolejnym pakiecie; nie zastępują testów Androida. Konfiguracja CMake
z zerem testów nie spełnia odbioru hostowego testu JNI/polityki.

**Odbiór emulatora/urządzenia — do wykonania:** użyć izolowanego środowiska
z już przygotowanymi narzędziami, atrapami i fikcyjnymi aplikacjami/danymi.
Zapisać hash APK, rewizję, API, ABI, model urządzenia, stan profili i log wyniku.

| Scenariusz | Kryterium i dowód |
|---|---|
| API 26 lub 28 oraz API 29 | Na starszym systemie działa ścieżka Settings; na 29 dostępna rola używa systemowej prośby. Brak awarii od niedostępnego API. Zdjęcie/odczyt ekranu oraz aktywna aktywność. |
| API 30 i 34; instalacja, ikona i podgląd | Przed i po otwarciu podglądu domyślny HOME w ustawieniach pozostaje ten sam. Anulowanie prośby niczego nie zmienia. Nie używać grantów/komend ustawiających HOME jako substytutu zgody. |
| Świadome wybranie ChatADHD | Osoba wybiera ChatADHD w UI systemu; przycisk Home/gest wraca do nowego ekranu. Ponowne Home i Back nie tworzą pętli ani stosu kopii aktywności. Zachować log i odczyt bieżącego HOME. |
| Uruchamianie oraz zniknięcie celu | Dwa komponenty/aplikacje testowe uruchamiają właściwe cele; odinstalowany lub wyłączony cel daje stan niedostępności i odświeżenie listy. Rozróżnić ewentualny wpis ustawień. |
| Offline, zimny start i odtworzenie | Katalog HOME działa bez sieci i uruchomionego rdzenia. Obrót/odtworzenie aktywności i powrót do MainActivity zachowują jej dane; otwarcie i `desktop.show` nadal udostępniają pulpit grafu i jego przypięcia. |
| Wycofanie | Osoba wybiera poprzedni launcher w ustawieniach; Home wraca do niego, a ikona ChatADHD, rozmowa i układ pozostają dostępne. |
| Profil służbowy i API 35+ | Osobna próba z przygotowanym profilem: ta sama paczka w dwóch profilach nie jest jedną tożsamością. W P1 lista deklaruje zakres bieżącego profilu; brak obsługi pozostałych oznaczyć jako lukę. Private Space bez pełnej implementacji z §2 nie otrzymuje statusu „obsługiwane”. |

Testy shortcuts (odczyt/uruchomienie, utrata uprawnienia hosta, pin accept/cancel)
i widgetów (bind odmowa/zgoda, konfiguracja, rozmiar, odtworzenie, usunięcie ID)
należą do kolejnych pakietów. Dopiero ich wyniki uzasadnią szerszą deklarację
funkcji launchera. Do pełnego odbioru P1 potrzebny jest też test na fizycznym
telefonie; wynik emulatora opisywać oddzielnie.

## 6. Weryfikacja tego przyrostu i przekazanie

- **Statyczny odczyt manifestu:** wykonany parserem Python `xml.etree.ElementTree`;
  potwierdzono opis 3 aktywności / 1 filtra oraz brak HOME, DEFAULT, queries,
  aliasów i receiverów. Nie jest to test scalonego manifestu ani urządzenia.
- **Sprawdzenie tekstu:** 41 plików / 0 dopasowań określonego słownika API;
  ręczny odczyt symboli pulpitu, mostka, trybów połączenia i cyklu życia.
- **Możliwości lokalne:** `python3`, `node`, `cmake`, `curl` dostępne;
  `rg`, `java`, `javac`, `adb`, `gradle`, `sdkmanager`, `emulator` nieobecne
  w PATH. Jest repozytoryjny wrapper Gradle, lecz bez Javy nie uruchamiano go.
  Sprawdzenie nie jest pełnym przeszukaniem dysku w poszukiwaniu SDK.
- **Test hostowy JNI / kompilacja Kotlin/APK / emulator / fizyczne urządzenie:**
  niewykonane. Nie instalowano narzędzi, nie uruchamiano CI ani modeli/API.
- Brak zmian kodu, schematu, `docs/STATE.md`, kolejki integratora i metadanych
  Git. Raport i `CHECKPOINT.md` pozostają nowymi plikami nieśledzonymi;
  `git diff --binary --no-ext-diff HEAD` dla śledzonych plików jest pusty.
  Listę plików przekazuje `git ls-files --others --exclude-standard`.

Następny wykonawca zaczyna od pakietu P1 i jego macierzy odbioru; niniejszy
przyrost kończy się na raporcie, bez samoczynnego rozpoczęcia implementacji.
