# Wątek 11: worker, media, GitHub — przekazanie do raportu głównego

Zakres edycji: trzy moduły, odpowiadające nagłówki, trzy istniejące pliki testów, trzy packi runtime oraz źródło porównawcze `loom/tests/compat/profile_worker_media_github_parity.cc`.

- `media.pack`: 61 liści presetów; deskryptory dostawców, ich nazwy, klucze referencyjne sekretów, kolejność i wyłączanie adapterów, mapy formatów/języków, endpointy, nagłówki, timeouty, model/parametry ASR/OCR i historyczne zastępcze wartości confidence. Dostawcę można dodać w danych dla istniejącego adaptera. Nieznany adapter daje jawny `Unsupported`; implementacja nowego protokołu wymaga kodu adaptera.
- `worker.pack`: 21 ustawień; kolejka, liczebność batcha i warunek jego użycia, opóźnienia, tempo, timeouty, endpoint, nagłówki, referencja sekretu, ilość wejścia/wyjścia, usunięcie prefiksu dostawcy, stany końcowe, estymator kosztu i skracanie diagnostyki. Jawne stare `WorkerOptions` mają pierwszeństwo i są wpisywane do efektywnego profilu, więc podgląd oraz polling odpowiadają realnym ustawieniom. `llm_rate_limit=0` wyłącza czekanie; nie powstaje dzielenie przez zero.
- `github.pack`: 13 liści presetów; domyślna gałąź, kierunek i wzorce plików, URL/nagłówki/timeouts, referencja sekretu, szablony autoryzacji i wiadomości commita. Dane sekretów nie trafiają do konfiguracji ani raportu.
- Konsumenci walidują pełne wstrzyknięte wartości `with_values()` wobec własnego kanonicznego schematu. Testy potwierdzają odrzucenie podrobionego luźnego schematu i zachowanie usuniętych kluczy map/nagłówków przez JSON Patch. INT64_MAX przy pięciu liczbach/duracjach workera uniemożliwia zawinięcie uint64.
- Menedżery czytają nakładki z `<data>/profiles/media.pack`, `worker.pack`, `github.pack`, wyprowadzając katalog z istniejących ścieżek Secrets/Config. Samodzielne adaptery mają addytywną iniekcję zwalidowanego profilu. `runtime_profile()` daje wartości, schemat oraz hash osobno, bez zmiany historycznych JSON wyników/statusów.
- Packi korzystają z `loom.runtime_profile/1`, z typami i metadanymi dla edytora eksperckiego. Maksimum dla pól API typu `int` oznacza wyłącznie reprezentowalność C++, nie limit polityki; historyczne 5/50/500/10000 są presetami i można je przekroczyć.

## Weryfikacja

- Wszystkie trzy źródła i trzy pliki testów przeszły kompilację składni; schematy domyślnych danych przeszły także `jsonschema.validate`.
- Przed: media 13, GitHub 19, worker 9 przypadków testowych. Po: 16/21/13, razem 50 (9 nowych).
- Stary kod własnego zakresu i nagłówki są identyczne w `git diff 9d15d2d..161cc22`. Pomocnicze stare biblioteki 9d15 można wykorzystać do porównania bez konstrukcji starego Runtime; pełny nowy build/ctest nadal obowiązuje po stronie rodzica.
- Źródło porównawcze używa tylko API sprzed zmiany i syntetycznych danych. Obejmuje pełne żądania/odpowiedzi Groq/Google/OCR, dwa żądania GitHub i SyncConfig, wszystkie WorkerOptions oraz pełne żądanie batch z 501 wiadomościami. Normalizuje wyłącznie losowe granice multipart i identyfikatory wiadomości. Atrapy nie wysyłają nic do sieci.
- Stan końcowy: 50/50 przypadków, 333/333 asercji, 0 pominiętych; pełny log `worker-media-github-tests.log`. Po świeżym osadzeniu wszystkich 15 profili 6/6 wyników porównawczych jest identycznych bajtowo: 604681 bajtów przed i po, SHA-256 `d947a102fbdd04aa5d6343c8020755e1a63d22ff9ca80d80c5c05c5e50f1b7d2` obu plików. Nazwy: `profile-worker-media-github-before.json` i `profile-worker-media-github-after.json`. W izolowanym linkowaniu przebudowano wszystkie zmienione konsumowane obiekty (worker/media/github/framework, semantyka/graf/pamięć/selektor oraz nowe fs/http), bez starego Runtime.

Dokładny baseline `161cc22dfb84fe863389d6b90323bd44516a68dc` wykonano później z ukończoną biblioteką tego samego checkout i odpowiadającymi nagłówkami. `evidence/baseline161/worker_media_github.json` ma te same **604681 bajtów i SHA d947a102fbdd04aa5d6343c8020755e1a63d22ff9ca80d80c5c05c5e50f1b7d2** co starszy helper baseline. Potwierdza to stronę bazową bez mieszania ABI. Aktualne pełne porównanie/ctest nadal jest oddzielnym dowodem rodzica. Receipt zachowuje polecenia, kompilator i hashe bibliotek/źródeł.

## Czego nie zrobiono

Podczas pierwszego pomocniczego uruchomienia użyto starego osadzonego snapshotu bez nowego profilu util. `TempDir` zwracał pustą ścieżkę, więc 23 przypadki dały 11 przejść, 12 porażek i SIGSEGV po nieudanym `unwrap`; 27 przypadków nie wykonano. Pełny negatywny log zachowano jako `worker-media-github-tests-stale-profile.log`. Znane syntetyczne pliki usunięto. Kolejne porównanie sprawdza TempDir i wszystkie programy działają z odrębnego katalogu scratch; końcowy wynik opisany wyżej jest po świeżej tabeli wszystkich 15 domen. To błąd pomocniczego linkowania migrujących obiektów, nie wynik oceny modeli.

Nie zmieniono prompta analizy semantycznej w workerze: należy do wątku 1. Nie zmieniono semantyki confidence: 0.9/0.85 nadal są historycznymi placeholderami, nie pomiarem dostawcy. Nie uruchomiono płatnych ani rzeczywistych wywołań. Nie dodano nowych tras UI/C ABI ani automatycznego przeładowania worker/GitHub po zapisie nakładki; trzeba odtworzyć konsumenta, media ma `refresh()`.

## Do wątku N

- Do wątku 1: pozostaje `kAnalysisPrompt` użyty w batchu worker; przenieść go wraz z własnym API wersji/podglądu i zachować domyślne bajty żądania.
- Do wątku 2: podłączyć strażnik ×10 do zmiennych batch/ASR/OCR tam, gdzie wspólne API zużycia będzie dostępne; ustawienia są odsłonięte w RuntimeProfile. Limit reprezentacji typu `int` nie zastępuje polityki zużycia.
- Do wątku 5: `Database::get_unanalysed_msgs` nadal ma `length(text)>=20` i `int limit`; te dane/API są poza zakresem edycji 11. Fixtury batch mają >=20 znaków, żeby istniejący filtr ich nie pomijał.
- Do wątku 10: korzystać ze schematów i `runtime_profile()`; wartości reference `secret_key` są nazwami pól, nie samymi kluczami. Nakładki `<data>/profiles/<domain>.pack`, obiekty scalane, listy/scalary zastępowane; do usuwania kluczy dostępny JSON Patch API profilu (przyjęcie patch w nakładce zależy od rootowego loadera). Nowy adapter pozostaje zdolnością implementacji; nowy deskryptor istniejącego adaptera jest danymi.
- Do wątku 9: wszystkie pełne kompilacje i ctest muszą potwierdzić isolated capture; nie ma edycji STATE/README ani commita/pusha podagenta.
