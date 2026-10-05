# Wątek 11 — knowledge, net i model

Data: 2026-10-04. Inwentarz wykonano przed edycjami; identyfikatory DIC odnoszą się do jego pierwotnych kotwic. Ten raport opisuje migracje w zakresie wątku 11 oraz izolowane dowody. Nie zastępuje pełnego buildu i `ctest` gałęzi.

## Co wykonano

| Zakres inwentarza | Dane i mechanizm |
|---|---|
| DIC-0168–0170, DIC-0172; dodatkowo polling | `loom/data/runtime/knowledge.pack`: domyślne flagi konfiguracji, limity zapytań o zadania, liczba prób według trybu i etapu, polling, reguły nazw plików w fingerprintach źródeł |
| DIC-0195–0197 | `loom/data/runtime/net.pack`: timeout, przekierowania, keep-alive, kolejność zmiennych środowiska CA i mapy zmiennych proxy według schematu URL |
| DIC-0053, DIC-0054, DIC-0060 | `loom/data/runtime/model.pack`: rangi pochodzenia, klasy statusów dla historii oraz inertny szablon uzasadnienia zakotwiczenia |
| Tożsamość operacji knowledge/materialize | Efektywne hashe receptur przed lookupem cache; porównanie tożsamości kolejki z wykonaniem oraz porównanie faktycznie wyrenderowanego produktu przed zapisem |

Presety odtwarzają dotychczasowe wartości. Nie dodano ograniczenia kolejności rang: użytkownik może ustawić dowolne rangi reprezentowalne jako `int`, także ujemne, równe lub nadające modelowi rangę większą od użytkownika. Rangi nie zmieniają zapisanych `Origin` i `EvidenceClass`; reguły uczciwego pochodzenia i dopuszczania przesłanek pozostają osobnymi mechanizmami.

`knowledge` domyślnie wybiera `priors=true`, `llm=off`, jedną próbę run, dwie próby etapu, jedną dla `extract` w trybie `auto`, polling 20 ms, zapytania latest=1/stages=100. Pomijanie nazw zaczynających się od `.` i nazwy `__MACOSX` dotyczy plików odkrytych w katalogu, zgodnie z wcześniejszym zachowaniem. Jawnie podane pliki nadal uczestniczą w fingerprintach. Chronione katalogi współdzielą dotychczasowy mechanizm katalogu; nie edytowano jego zakresu.

`net` domyślnie wybiera 30000 ms, przekierowania włączone, keep-alive wyłączone, CA z pierwszej niepustej wartości `SSL_CERT_FILE`, następnie `REQUESTS_CA_BUNDLE`, oraz proxy HTTPS z `HTTPS_PROXY`. Preset nie przypisuje proxy do HTTP. Mapę schematów URL i listy zmiennych można zmienić lub usunąć przez dokładne zastąpienie wartości/JSON Patch. Wartość niepozytywna timeoutu przekazana w dotychczasowym żądaniu zachowuje historyczne rozstrzygnięcie do timeoutu presetu; ustawienie presetu na 0 przekazuje natywne 0 do adaptera. Nie opisuje się tej wartości jako gwarantowanego trybu bez limitu. Weryfikacja certyfikatu TLS pozostaje włączona w adapterze.

`model` zachowuje rangi `archive=4`, `repo=4`, `user=5`, `external_authority=3`, `model_knowledge=1`, `system=2`, nieznane enum=0. Klasa obecności to `implemented/partial/restored`, klasa utraty to `lost`. Tablice mogą być puste; przy nakładaniu klas gałąź utraty ma pierwszeństwo, zgodnie z dotychczasową maszyną historii. Rzeczywiste wartości zapisanych statusów nie są przepisywane. Szablon zakotwiczenia domyślnie daje dokładny wcześniejszy tekst; identyfikator `m.anchor.<paradigm>.<kind>`, jawne związanie roli i confidence=1 zachowują swoje znaczenie.

## API i nakładki

Wspólna ścieżka to `RuntimeProfile::load(domain, data_root)`, nakładka `<data_root>/profiles/<domain>.pack`, walidacja według `value_schema`, `inspection()` i hash efektywnych wartości. Szczegóły scalania obiektów, zastępowania tablic, JSON Patch, usuwania kluczy oraz inertnych szablonów opisuje [RUNTIME_PROFILES.md](RUNTIME_PROFILES.md). Konsumenci sprawdzają domenę i walidują dokładne `profile.values()` przez `builtin.with_values(...)`; luźniejszy schemat dostarczony przez klienta nie omija schematu obsługiwanego przez konsumenta.

| API | Zachowanie |
|---|---|
| `KnowledgeConfig::from_json_with_profile(json, profile)` | Brakujące `priors` i `llm` bierze z `config_defaults`. Jawne pola DTO wygrywają. Odrzuca obcą domenę i niezgodne wartości także wtedy, gdy jawne pola zastępowałyby wadliwe ustawienia. |
| `KnowledgeConfig::from_json(json)`; zwykły `KnowledgeConfig{}` | Korzysta z niezmiennego presetu builtin. Typ pozostaje agregatem. Zapisany pełny DTO odczytuje swoje jawne flagi; późniejsza nakładka nie przepisuje historycznej konfiguracji. |
| `HttpRequest::from_json_with_profile(json, profile)` | Rozróżnia pominięty timeout i jawnie podaną wartość, sprawdza natywną reprezentację liczby. Stary parser i agregat korzystają z presetu builtin; kolejność pól agregatu zachowana. |
| `make_default_transport_checked(data_root, overrides)` | Tworzy niezmienny snapshot transportu, jawnie zwraca błąd istniejącej wadliwej nakładki. |
| `make_default_transport_with_profile(profile)` | Dokładna walidacja przed utworzeniem adaptera. Stare `make_default_transport()` zachowuje preset builtin. |
| `HttpTransportPolicy::for_request(request, profile, lookup)` | Deterministyczny podgląd timeoutu, flag, CA, proxy i hasha bez wysłania żądania. Wstrzyknięty lookup umożliwia test bez odczytu środowiska procesu. |
| `authority_rank(origin, profile)` | `Result<int>` z walidacją. Dotychczasowy `authority_rank(origin) noexcept` nadal istnieje i odczytuje tablicę presetu builtin według rzeczywistych enumów. |
| `order_status_history(records, profile)` | `Result<vector<StatusRecord>>`; klasyfikacja z danych, sortowanie i automat historii zachowane. Stary wrapper korzysta z builtin. |
| `anchoring_morphisms(pack, profile)` | Walidowany szablon uzasadnienia ze zmiennymi `paradigm`, `kind`, `role`; pozostałe pola morfizmu identyczne. Stary wariant korzysta z builtin. |

Przykład nakładki knowledge:

```json
{
  "schema": "loom.runtime_profile_overlay/1",
  "domain": "knowledge",
  "overrides": {
    "config_defaults": {"priors": false, "llm": "auto"},
    "query": {"stage_tasks": 250},
    "source_filters": {"excluded_prefixes": [], "excluded_names": []}
  }
}
```

Schematy nie ustanawiają politycznych granic kosztu. Ograniczenia liczb opisują potrzebną reprezentację `int`, `int64_t` oraz operację: próba wykonania ma liczbę co najmniej 1, liczności i czas oczekiwania są nieujemne. Enum `llm` opisuje dwa faktycznie zaimplementowane tryby, a enum statusu — rzeczywisty kontrakt modelu.

## Cache i zmiana receptury podczas wykonania

Nie-builtin profile `knowledge` i `materialize` trafiają do fingerprintu run oraz etapu przed deduplikacją. Identyfikatory i bajty builtin pozostają bez zmian. Wybrane hashe są dodatkowo zapisane w najwyższym poziomie parametrów zadania jako `runtime_profiles`; nie trafiają do `KnowledgeConfig.stage_params` ani do starego fingerprintu produktu. Jeśli wszystkie efektywne wartości są builtin, metadane nie są dodawane.

Brak hasha domeny w zapisanym zadaniu oznacza oczekiwanie receptury builtin. Kolejka nie przyjmuje późniejszej nakładki pod wcześniejszym input hash. Orkiestrator sprawdza oczekiwanie przy wejściu i lookupie wyników; handler etapu sprawdza je przed wywołaniem i przed zapisaniem sukcesu. `StageContext.expected_runtime_profiles` to osobne, wewnętrzne pole końcowe: pusty obiekt przypina builtin, `nullopt` pozwala bezpośredniemu wywołaniu etapu wybrać recepturę na wejściu.

Materializacja ponownie sprawdza oczekiwanie na wejściu, a przed każdym zapisem porównuje zewnętrzną recepturę nazw/MIME z `Rendered.data.runtime_profile_hash` faktycznego renderera. Brak znacznika w produkcie oznacza builtin. Rozbieżność daje jawny `Conflict: runtime profile recipe changed...` przed zapisem następnego bloba, artefaktu i eksportu. Już zapisany wcześniejszy artefakt może pozostać jako prawdziwy ślad częściowego wykonania; etap nie zapisuje wyniku sukcesu ani fałszywego wpisu cache.

Pięć regresji w `test_knowledge.cpp` obejmuje zmianę nie-builtin profilu przed kolejką run/etapu dla obu domen, pojawienie się nakładki przy braku metadanych, zmianę podczas etapu, granicę handler→materialize oraz zmianę pomiędzy rendererami. Ten ostatni przypadek używa synchronicznego hooka SQLite po pierwszym INSERT artefaktu: wcześniejszy produkt zostaje, kolejny blob/eksport nie powstaje, wynik etapu pozostaje failed. Osobny test porównuje historyczne bajty builtin parametrów run i etapu oraz ich input hash.

## Wyniki przed i po

Źródło sondy: `loom/tests/compat/profile_net_model_parity.cc`. Wywołuje wyłącznie stare publiczne API, nie konstruuje `Runtime`, nie wysyła prawdziwych żądań. HTTP korzysta z `ScriptedTransport`. Pełne syntetyczne wyjścia: [before.json](evidence/knowledge-net-model/before.json), [after.json](evidence/knowledge-net-model/after.json).

| Pomiar | Przed | Po |
|---|---:|---:|
| Rangi enumów oraz nieznana wartość | 7 | 7 |
| Historie statusów / rekordy | 345 / 1043 | 345 / 1043 |
| Pełne morfizmy zakotwiczenia | 122 | 122 |
| Fixtures serializacji żądań HTTP | 7 | 7 |
| Rozmiar pełnego wyjścia | 264606 B | 264606 B |
| SHA-256 | `6eb6f00d3c398870a15f350a918d7558eb20d68e31b64af82ca745d250b0b7a4` | ten sam |

Porównanie jest bajtowo identyczne. Obejmuje wszystkie trójki z siedmiu statusów oraz dwa dłuższe cykle, dokładne uzasadnienia i wszystkie pola morfizmów, domyślny agregat HTTP, jawny timeout, 0, ujemny, ułamkowy i historyczny fallback dla pola innego typu, a także binarne body.

[focused-tests.log](evidence/knowledge-net-model/focused-tests.log): **12/12 przypadków, 850/850 asercji, 0 pominiętych** — sześć przypadków `net.profile` i sześć `model.profile`. Sprawdzają m.in. zmienne środowiska i ich kolejność, nowe mapy schematów URL, usunięcie mapowania, profilową wartość pominiętą i jawną, wadliwy plik oraz luźny schemat, natywny overflow, dowolne rangi bez przepisywania dowodów, puste/zmienione klasy statusów i edycję tylko uzasadnienia morfizmu. Wszystko wykonano offline, bez wywołań płatnych.

To wtórny izolowany dowód zakresu. Przed: zamrożone nagłówki `chatadhd-baseline` i archiwum rdzenia `9d15d2d` z `/workspace/scratch/98e6ad903811/baseline/loom/build/dev`. Po: bieżące obiekty `http_common.cpp`, `http_default.cpp`, `sse.cpp`, `model_core.cpp`, `model_action.cpp`, `model_pack.cpp`, `runtime_profile.cpp` przed tym samym archiwum zależności. Do runnera użyto istniejącego `framework_runner.cpp`. TLS jest skompilowany z `CPPHTTPLIB_OPENSSL_SUPPORT=1` i `LOOM_HAVE_OPENSSL=1`; żadnego połączenia TLS nie wykonano. Dodatkowy izolowany przebieg z lokalnym świeżym `baseline-build` także przeszedł 12/850; raportowany dowód powyżej używa żądanego archiwum 9d15.

Ścieżki i SHA-256 zależności zapisano w [baseline-manifest.json](evidence/knowledge-net-model/baseline-manifest.json), polecenia ścisłej kompilacji obiektów w [compile-manifest.json](evidence/knowledge-net-model/compile-manifest.json), liczności w [parity-summary.json](evidence/knowledge-net-model/parity-summary.json). Te obiekty skompilowano z pełnymi ostrzeżeniami projektu i `-Werror`, C++20, bez poluzowania testów. Net/model, knowledge engine, materialize i testy knowledge przeszły dodatkowo kontrolę składni z oryginalnymi flagami projektu.

[knowledge-config-tests.log](evidence/knowledge-net-model/knowledge-config-tests.log): **3/3 przypadki, 24/24 asercje, 0 pominiętych**. `knowledge.profile` sprawdza stary pełny DTO, wczytanie flag z rzeczywistej nakładki, pierwszeństwo pól jawnych, niezmienny odczyt zapisanego DTO i odrzucenie obcej/permisywnej domeny. Użyto świeżej tabeli wszystkich 16 profili po dodaniu `config_defaults`, bieżących obiektów knowledge/materialize/framework oraz tych samych zależności 9d15. Ten runner także nie konstruuje Runtime i nie wykonuje pipeline. Polecenia zapisano w [knowledge-config-compile-manifest.json](evidence/knowledge-net-model/knowledge-config-compile-manifest.json). Pięć regresji Runtime knowledge oczekuje na zintegrowany build; ich końcowy wynik dopisze integrator po uruchomieniu.

Odtworzenie sondy wymaga kompilacji tego samego `profile_net_model_parity.cc` z nagłówkami baseline do programu „przed” oraz z bieżącymi nagłówkami i siedmioma wymienionymi obiektami do programu „po”, linkowanych kolejno z `libloom_core.a`, `libloom_sqlite3_amalgamation.a`, `libloom_miniz.a`, `-pthread -ldl -lm -lssl -lcrypto`. Zapisać stdout obu programów, wykonać `cmp before.json after.json` i SHA-256. Runner doctest z tymi samymi obiektami oraz `test_net_profile.cpp`/`test_model_profile.cpp` uruchomić filtrem `--test-suite=net.profile,model.profile`.

## Czego nie wykonano i dlaczego

Nie wykonano pełnego `ctest`, buildu web, rebase, commita ani push z tego podzadania — prowadzi je główny agent wątku 11/integrator. Nie rozszerzano sześciostopniowego rzeczywistego wiring pipeline, gramatyki URI/SSE, portów standardowych, walidacji TLS, identyfikatora zakotwiczenia ani historycznych decoderów pól brakujących w zapisanych rekordach. Wartości decoderów DIC-0055–0059 pozostają jawnie otwarte dla przyszłego API tworzenia obiektów; nie wolno interpretować starych zapisów według zmiennego presetu bez migracji formatu. Nie migrowano konsumentów poza zakresem wątku 11.

## Do wątku 1

Jeżeli ekstrakcja/resolve mają korzystać z wybranej polityki rang lub klas statusów, wstrzyknij efektywny `model` do nowych checked overloadów. Stare wrappery świadomie zachowują builtin. Zmiana rangi nigdy nie uzasadnia zmiany `Origin`/`EvidenceClass` i nie zastępuje sprawdzania przesłanek. Podgląd pełnego zapytania LLM i jednorazowa edycja promptu pozostają w twoim zakresie; inspection tych profili nie udaje takiego API.

## Do wątku 2

Podłącz własny interfejs zużycia przed operacją do efektywnych parametrów prób, polling i transportu; walidacja profilu nie jest strażnikiem ×10. Liczba prób w danych nie przydziela nowego budżetu wywołaniom modelowym. Rezerwacje kosztu muszą przetrwać retry/checkpoint i zachować budżet wątku 7. Granice reprezentacji w schemacie nie są progami polityki zasobów.

## Do wątku 9

Zintegruj dane, generator, konsumentów i testy razem; zregeneruj osadzone profile po dodaniu `knowledge.config_defaults`. CLI ma użyć `KnowledgeConfig::from_json_with_profile`; C API/serwer oraz runtime transportu wymagają podłączenia checked parsera/fabryki w swoim zakresie. Obecne `Runtime::open`/reset transportu nadal wybierają dawną no-arg fabrykę, więc sam plik `profiles/net.pack` nie wybiera transportu bez tego adaptera. Przebuduj konsumentów zmienionych nagłówków. Uruchom pełne `ctest`, nowe testy knowledge i build web; nie uznawaj wtórnej sondy bez Runtime za pełny dowód integracji. Zachowaj wszystkie pełne receipts i negatywne wyniki na gałęzi/archiwum.

## Do wątku 10

Buduj formularz z `inspection().value_schema`: pokaż efektywne flagi `config_defaults`, timeout i źródła CA/proxy, rangi, klasy statusów oraz szablon zakotwiczenia. Pokaż hash receptury, błędy walidacji i konieczność ponownego utworzenia snapshotu transportu. Edycja map musi umieć usunąć klucz przez dokładne wartości/JSON Patch; merge nie usuwa kluczy. Rangi nie mają arbitralnego porządku; maksima `int`/`int64_t` opisują reprezentację. Błąd zmiany receptury kolejki wymaga nowego jawnego uruchomienia z aktualnymi danymi i nie powinien wyglądać jak udana odpowiedź z cache.
