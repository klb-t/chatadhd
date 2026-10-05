# CLI profiles — ograniczony przyrost 2026-10-05

`profile list`, `inspect`, `validate` i `save` nie otwierają już `Runtime`.
Odczyt katalogu stosuje dotychczasową kolejność discovery, bez inicjalizacji.
`save` tworzy wyłącznie katalog potrzebny do nakładki i zapisuje sprawdzony
plik `<root>/profiles/<domain>.pack` istniejącą operacją `atomic_write`.
Nie otwiera bazy, configu, ledgeru ani odzyskiwania zadań. Pozostałe polecenia
aplikacji nadal otwierają Runtime. Aliasy komend i podkomend są rozwiązywane
przed dispatch; uszkodzona nakładka CLI nadal blokuje bootstrap jawnie.

Zakres zmian: `loom/cli/main.cpp` i istniejący
`loom/cli/tests/test_cli_smoke.py`; bez edycji core/config, bootstrapu, nowego
resolvera R40, danych packów ani progów dotychczasowych testów. Baza robocza
po rebase: `2eb65d475cb6671dbe6388c53bb63eec74ea92a8`; porównanie przed zmianą
jest zamrożone w `chatadhd-before`. Niniejszy raport jest dodatkiem do
[poprzedniego raportu CLI](implementation-cli.md), którego pełne testy dotyczą
poprzednich źródeł.

| Właściwość | Przed | Po |
|---|---|---|
| Operacje profili otwierające Runtime | 4/4 | 0/4 |
| Kontrola native usage w edytorze `validate/save` | brak | `validate_usage_policy_options` na pełnych efektywnych wartościach |
| Jawne nadpisanie równe presetowi | ginęło przy `Json::diff` | zachowane w `overrides` lub jawnym `replace` |
| Usunięcie w słowniku/tablicy przy kolejnym częściowym zapisie | zachowywane przez diff | zachowywane wraz z jawnymi wyborami |
| Domyślna pomoc | 4770 B | 4770 B, identyczna |
| Domyślne flagi | 17 | te same 17 |

## Zapis i jawny wybór

`load_profile_document` (`main.cpp:1095`) czyta dokument nakładki raz i deleguje
walidację do rzeczywistego `RuntimeProfile::with_overlay`. Ta sama kopia daje
efektywne wartości i zapisane wybory; nie rekonstruujemy intencji z równości
wartości z presetem. Brak pliku oznacza builtin; istniejący błędny, nieczytelny
albo nie-UTF-8 dokument zwraca błąd. Znany `ENOTDIR` oznacza brak możliwego
pliku nakładki dla odczytu, a próba zapisu nadal zwraca błąd filesystemu.

`saved_profile_document` (`main.cpp:1148`) scala wcześniejsze jawne `overrides`
z nowym wejściem i zachowuje wybrane pola także wtedy, gdy są równe presetowi.
Po operacjach patch wybrane pola są przenoszone do końcowych sprawdzonych
wartości; usunięte pola nie wracają. Korekta RFC6902 opisuje różnicę względem
builtin już z tymi jawnymi wyborami. Jawne docelowe `add/replace/copy/move`
z patcha są zachowane jako końcowe `replace`, również przy wartości równej
defaultowi. Dla tablic `replace` nie dodaje kolejnego elementu. Poprzedni
patch usuwający indeks nie jest odtwarzany na nowej, krótszej tablicy.
Przed zapisem ponownie sprawdzamy, że serializowany dokument odtwarza dokładny
hash zwalidowanego profilu.

Koperta jest rozpoznawana po dokładnym `schema:loom.runtime_profile_overlay/1`,
a nie nazwie pola `overrides`. Otwarte dane domeny usage mogą zatem posiadać
zwykłe rozszerzenie o tej nazwie. Aktualny publiczny kontrakt loadera wymaga
`overrides` także w kopercie zawierającej sam patch; edytor zachowuje ten błąd
i nie naprawia istniejącego niepoprawnego dokumentu samodzielnie.

`inspection.is_builtin` nadal oznacza równość efektywnych wartości z builtin,
nie brak jawnego wyboru. Wybór równy defaultowi pozostaje w zapisanej nakładce.
W dawnych plikach, w których zapis diff już usunął taki wybór, jego intencji
nie da się odtworzyć. Zapis atomowy nie dostarcza CAS dla równoległych edytorów.
RFC6902 remove nie zastępuje trwałego identity exclusion W12/R40.

## Usage policy i aktywacja

`cmd_profile` (`main.cpp:1174`, native kontrola `:1191`) sprawdza proponowane
pełne efektywne `usage_policy` poprzez validator W2. Odrzuca m.in. factor 1
i puste nazwy kohort/zasobów, które dopuszczał ogólny schemat; dopuszcza native
nullable window/zasoby oraz otwarte rozszerzenia. Błąd występuje przed zapisem.
Ta walidacja dotyczy **edytora profili**: nie jest decyzją admission, zgodą na
wywołanie ani aktywacją pliku w polityce zużycia. Przyjęte W2 nadal pobiera
aktywny override z `config.json/loom_usage_policy`, z własną semantyką shallow
replacement. Nie zmieniono tego mechanizmu ani nie otwarto ledgeru.

## Weryfikacja bieżącego źródła

Ścisła kontrola składni C++ PASS:

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -fsyntax-only -Iloom/include -Iloom/src -isystem loom/third_party/nlohmann \
  loom/cli/main.cpp
```

AST testu Pythona PASS; scoped `git diff --check` PASS. `cli.pack` nie zmieniono:
SHA256 przed i po `56998afe641e43c8950c4520aef423bf8916e51aa2974f1a9b46a91ed5015a2b`.
Pomoc przed i po: SHA256
`1efe7af10542ef871d475fae9b0e648372fe97dbc864f84e4ca07ac127ce8fbe`.
Jest to porównanie dokładnych danych presetu, nie wykonana sonda całego CLI.

Nowe asercje w istniejącym smoke są gotowe do wykonania na prawdziwym programie:
odczyt/validate bez tworzenia root; nieznana domena/błędny JSON/native usage
bez zapisu; jawne defaulty scalar/array i equal-value patch; aliasy bez Runtime;
patch-only błędny input i istniejący plik bez zmiany; usunięcie elementu tablicy,
częściowy save i późniejsza krótsza tablica; uszkodzone DB/config/sentinel
niezmienione bajtowo; root tylko do odczytu; native usage nullable/open i pełny
tree tylko z jedną nową nakładką; nieudany atomic write zachowujący całe drzewo;
pierwszeństwo explicit/env/sentinel bez inicjalizacji. Wszystkie stare asercje
pozostają, w tym usunięcie `.cpp` ze słownika i późniejszy częściowy zapis.

Root wykonał rzeczywisty pełny CLI smoke: **exit 0**. Sonda nowego CLI z
zamrożonym kompletnym rdzeniem i headerami przed zmianą zachowała dokładne
wartości, schematy i hashe **19/19** profili oraz bajty pomocy/wersji. Stary CLI
utworzył sentinel i dwie bazy; nowy nie utworzył żadnego pliku.
[Receipt zakresu](evidence/continuation-2026-10-05/cli-scoped-proof/receipt.json)
podaje faktyczne źródła, polecenia i hashe. To dowód zakresu CLI, nie finalnego
nowego rdzenia. **Pełny CTest nowego przyrostu pozostaje do wykonania po rebase.**
Nie uruchamiano providerów ani płatnych wywołań. Stan
213 grup i historycznych dowodów nie został przepisany na podstawie samej
kontroli składni.

## Do wątku N

- **Do wątku 2/9:** publiczny read-only root discovery i aktywacja config/path
  oraz metadane seeded/persisted/explicit nadal wymagają właściwego właściciela.
  CLI zachowuje duplikat czystej historycznej kolejności discovery do czasu API;
  nie uważa zapisanej nakładki za aktywny Config override.
- **Do wątku 10:** jawny wybór równy builtin jest obecny w nakładce mimo
  `is_builtin:true`. Edytor ma rozróżniać równość wartości, wybór i aktywację;
  profile-only CLI nie inicjalizuje teraz danych aplikacji.
- **Do wątku 12:** trwałe wykluczenia i wyjaśnienia warstw należą do wspólnego
  `DefaultLayers`; bez drugiego resolvera w CLI.
- **Do wątku 9:** wykonać rzeczywisty CLI smoke/default parity i pełny build/CTest
  na aktualnym źródle, dopisać source-bound receipts przed odbiorem.
