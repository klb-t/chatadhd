# Runtime bootstrap profiles — dependency for W2

Świeży `fetch` i [autorytatywny kontrakt W2](evidence/runtime-bootstrap/W2_PROFILE_HANDOFF.md) z `910a1d6b3764f82c78e39c5b0adca796ba1168e5` przekazują brakujące dane startowe. Dodano trzy domeny kanoniczne, zatem katalog ma **19 packów**. Ich hashe i dokładne wartości znajdują się w [handoff-receipt.json](evidence/runtime-bootstrap/handoff-receipt.json). Sześciopolowy preset odpowiada też odczytanemu `loom/src/core/config_usage_policy.cpp:20–27` na tej gałęzi W2; taki consumer nie istnieje na bazowym main. To zależność danych: **nie zmieniono `core/config*`, bootstrapu Runtime, nagłówka config ani C API**. DIC-0325–0329 nadal wymagają adaptera/okablowania W2 i przypisania właściciela dla pozostałych miejsc.

| Domena | Historyczne dane | Kontrakt odczytu |
|---|---|---|
| `usage_policy` | sześć pól: schema, `10.0`, 32, 30000, true, pusty obiekt baseline | `RuntimeProfile::load("usage_policy", actual_root)`; po nakładce i po zapisanym override konieczna walidacja native W2 |
| `config` | dokładne 12 wartości i ich kolejność; oddzielne dwa fallbacki Loom | `/legacy_defaults`, `/legacy_key_order`, `/loom_defaults`; fallback polityki zużycia z własnej domeny |
| `runtime_paths` | wejścia środowiska, uporządkowane kandydaty, kolejność wyboru, sentinel z jednym LF, 3 katalogi inicjalizacji, 11 sufiksów DataPaths | `/candidate_rules` wybiera pierwszą zgodną mapę faktów; każdy `/path_segments` renderować inertnie i łączyć operacją filesystem |

`usage_policy` zachowuje **floating JSON `10.0`**, a nie integer `10`: zgodna tożsamość serializacji ma znaczenie dla hashów. Schemat obsługuje nullable union `baseline_window: integer|null`, dodatni int64, oraz ilości zasobów `number|null`. Limity int64/int32 określają reprezentację adaptera, a nie budżet produktu. Nie dodano maksimum wydatków, ilości ani nazw kohort. Nieobecne baseline pozostają niedostępne.

Generic schema nie zastępuje `validate_usage_policy_options`: factor musi być skończony i **ściśle >1**, window dodatni int64 lub null, ilości skończone i nieujemne lub null, nazwy poprawne dla native kontraktu, timeout nieujemny int SQLite. Obecna deklaracja `minimum:1` nie potrafi wyrazić wyłącznej granicy dla factor; consumer musi odrzucić 1. Cały kompletny effective policy należy walidować przed otwarciem ledgeru lub decyzją. Hash profilu opisuje definicję i efektywne wartości, nie jest zgodą na operację.

Zapisany `loom_usage_policy` i inne Config overrides **zastępują całe pola najwyższego poziomu**. Szczególnie `initial_baselines` nie wolno scalać rekurencyjnie, bo przywróciłoby to usunięte kohorty lub wymiary. Nakładka pliku profilu zachowuje oddzielny kontrakt RuntimeProfile: recursive `overrides`, następnie RFC 6902 `patch` do usuwania. Po shallow Config override użyć `with_values()` i ponownej native walidacji. Nie cache'ować bezterminowo zmiennego pliku użytkownika.

Discovery katalogu jest granicą bootstrapu. Przed poznaniem root użyć builtin receptury albo jawnego wejścia startup-profile. Plik `<root>/profiles/runtime_paths.pack` wewnątrz nieznanego katalogu nie może sam wybrać swojej początkowej lokalizacji. Overlay per-root ładuje się dopiero po discovery. Przekazywać faktyczny root także przy zagnieżdżonych nazwach plików; rodzic ścieżki config/ledger nie musi być root. Zmiana sufiksów istniejących danych wymaga jawnego planu zgodności. Nie deklarujemy, że ten pack już zmienił aktywne lokalizacje.

Config recipe zachowuje kolejność 12 kluczy, modele, prompty i wartości bazowego `161cc22`. Seedować tylko `legacy_defaults` według `legacy_key_order`; pozostałe jawnie zadeklarowane nowe klucze dopisywać po istniejących. Native consumer sprawdza brak duplikatów i nieznanych nazw w recepturze kolejności. `loom_defaults` obsługuje tylko brakujące gettery; nie dodawać ich automatycznie do historycznego pliku. Fresh Config nie pisze pliku, istniejące bajty pozostają zachowane. Presence seeded/persisted/default to różne źródła; historyczna obecność lub równość wartości nie dowodzi jawnego wyboru użytkownika.

Dodano **6 przypadków** w `test_runtime_bootstrap_profiles.cpp`: golden usage wraz z `10.0`, nullable/open extensions, dokładne wartości/kolejność config, fresh/no-write i existing-file bytes, kandydaci Android/XDG/HOME (także brak HOME i końcowy slash), sentinel/inicjalizacja i wszystkie DataPaths. Ścisła kontrola składni z ostrzeżeniami projektu i `-Werror` przeszła; [polecenie i wynik](evidence/runtime-bootstrap/strict-syntax.json). Po regeneracji 19-domenowej tabeli wykonano te 6 przypadków razem z 8 przypadkami wspólnego RuntimeProfile: **14/14 przypadków, 168/168 asercji, 0 pominiętych**; [pełny log](evidence/runtime-bootstrap/focused.log), [komendy/proweniencja](evidence/runtime-bootstrap/focused-receipt.json). Jest to świeży obiekt `runtime_profile.cpp` linkowany przed pełnym archiwum bazowym `161cc22`, z niezmienionymi zależnościami Config/FS i bez konstrukcji Runtime. Source-equality sprawdza wszystkie 19 deklaracji. Ten wynik nie zastępuje pełnego aktualnego buildu ani CTest. Własne pełne sondy bazowe pozostają zachowane w `evidence/baseline161` i `evidence/fullcore161`.

## Do wątku 2

Dane trzech domen są gotowe do jednolitego checked adaptera zgodnie z PROFILE_HANDOFF. Używać jednego źródła presetów we fallback/effective/settings/preview/open, native walidacji, shallow overrides i spójnego snapshotu hashów. Zachować etykietę `legacy_code_pending_pack_migration` do rzeczywistego podłączenia wszystkich wejść. Odrębnie zweryfikować zmieniony canonical source po regeneracji/build oraz zmieniony overlay bez przebudowy.

## Do wątku 10

Profile mają `value_schema` i metadane `x-setting`/`x-consumer`/`x-unit`. Pokazać hash i native błąd walidacji; schema-only preview polityki nie jest autoryzacją. Przy path recipes opisać boundary discovery i jawne wejście startup-profile.

## Do wątku 9

Opublikować małą zależność danych po regeneracji istniejącego generatora, bez drugiego loadera/manifestu KB. Następnie W2 rebase na tę zależność i adapter w swoim zakresie. Przypisać właścicieli `config.h`, Runtime bootstrap i publicznego config gettera. Pełny CTest, source-equality i porównanie zmienionego źródła/overlay muszą przejść przed uznaniem DIC-0325–0329 za zmigrowane; obecny raport potwierdza dane, strict syntax i powyższy izolowany test 14 przypadków.
