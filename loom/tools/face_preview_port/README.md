# Lokalny Face preview port

Stan: samodzielny helper lokalny, bez podłączenia hosta/klienta. Baza
`2c789e5847d1057ce284a35ae6e07350761588b3`. Nie jest to usługa ani gotowa
funkcja produktu. Nie wykonuje embeddingu, inferencji, oceny emocji ani avatara 3D.
Jedyny obraz pochodzi z jawnych dziewięciu współrzędnych rysunku proceduralnego.

Wymaga istniejącego Pythona z NumPy. Pillow nie jest potrzebne: przypięty renderer
sam zapisuje PNG przez stdlib. Nie instaluje bibliotek, nie otwiera sieci,
nie uruchamia podprocesów. `vendor/` zawiera kopie identyczne bajtowo z przekazanym
`face-reference/`; SHA obu modułów są w kodzie i sprawdzane przed wykonaniem.
Kontroler dostarcza wyłącznie istniejącą walidację `vector`; nie powstaje wątek,
evaluator, druga implementacja kontrolera ani funkcja celu.

```bash
python3 -I -B loom/tools/face_preview_port/port.py \
  --output-root /tmp/face-preview-owned-example \
  < loom/tools/face_preview_port/fixtures/render.jsonl
```

Root musi być absolutny, bez dowiązań, należeć do procesu i mieć tryb `0700`.
Helper utworzy końcowy katalog, jeśli go nie ma; rodzic musi istnieć. Host powinien
wybrać nowy prywatny root dla zadania. Nazwy artefaktów generuje helper,
nigdy request. Pliki mają `0600`; otwarcia używają dirfd i `O_NOFOLLOW`.
Root i zaufana instalacja nie mogą być równolegle modyfikowane przez ten sam UID.
Helper nie deklaruje izolacji jądra przed siecią ani przed złośliwym kodem tego UID.

## Protokół /1

Wejście: UTF-8 JSONL, jeden obiekt na linię, EOF kończy proces. Wyjście: dokładnie
jeden obiekt JSON na obsłużoną linię, bez logów/tracebacków na stdout. To skończony
proces batch, nie daemon. Wspólny zestaw wymaganych pól:
`schema=loom.face_preview.request/1`, `op`, `request_id`, `version`, `context_id`.
Identyfikatory są niepustymi, ograniczonymi rozmiarem napisami bez NUL;
nie są ścieżkami. `version` to dodatni integer dokładnie reprezentowalny w JS.

`render` wymaga dodatkowo:

- `params`: dokładnie 9 liczb JSON, bez bool/string/NaN/Infinity; domena jest
  sprawdzana istniejącą funkcją `async_controller.vector`.
- `parameter_source`: `user_vector` lub `library`; określa deklarowane źródło
  parametrów, nie poświadcza pochodzenia biblioteki ani znaczenia emocjonalnego.
- `size`: dokładnie `256`. Rozmiar i supersampling są ograniczeniami tej wersji
  istniejącego renderera, a nie nową polityką zasobów.

Kolejność współrzędnych i bazowy config są w `data/method.json`; kolejność musi
odpowiadać kontraktowi przypiętej referencji. Limity transportu są presetem
`data/limits.json`; zaufany operator może wskazać `--limits-file`. Ich zmiana
nie rozszerza dziedziny renderera. Opisy PL są w `data/labels.pl.json`.
Nieznane/brakujące i zduplikowane pola, zła wersja schematu, niedozwolony rozmiar
i niefinitywne liczby powodują odmowę. Nadmierna linia kończy proces kodem 2,
bez interpretowania jej końcówki. Odmowy pojedynczych poprawnie ograniczonych
linii nie kończą batcha (kod procesu 0); trzeba sprawdzić `status` każdej odpowiedzi.

Każdy render faktycznie wykonuje renderer. Nie ma cache ani dziedziczenia starych
wyników. Wersje renderów muszą ściśle rosnąć w obrębie strumienia. Receipt/odpowiedź
powtarza `request_id`, `version`, `context_id`; host musi porównać całą trójkę
z aktualnym zadaniem przed publikacją. Ten helper nie zna nowszych żądań czekających
w hoście ani epok poprzedniego procesu.

Odpowiedź `status=rendered` wskazuje względny `png_path` pod rootem. Osobny katalog
`fp-<32 hex>` zawiera `input.jsonl` (dokładne bajty z separatorem, jeśli był),
`config.json`, `preview.png`, `receipt.json`. Receipt wiąże ich hashe, pin kodu
renderera/kontrolera/adaptera, hash danych metody oraz wersje Python/NumPy.
`receipt_sha256` z odpowiedzi poświadcza dokładne bajty osobnego receipt.
PNG nie zawiera nazw, tekstu rozmowy ani zdjęć. To RGB 256×256.
Metadata jawnie podaje `derived/system`, `confidence=null`, `model=null`,
`embedding=NOT_RUN`, `inference=NOT_RUN`, `real_expression=NOT_RUN`,
`avatar_3d=unavailable` i `procedural_2d_preview`.

`provenance` podaje `sha256:` źródła oraz locatory w istniejącym formacie
`model::Locator`: cały request jako Unit i `/params` jako Observation rodzaju
`field`. Helper nie produkuje alternatywnego magazynu Source/Unit/Observation,
nie wymyśla ID obiektów modelu i niczego nie importuje. Przyszły host ma użyć
istniejących `model::Unit::make_id`, `Observation::make_id`, BlobStore i normalnej
materializacji; integracja hosta pozostaje osobnym krokiem. Obserwowalne są podane parametry;
PNG jest ich pochodną, a nie obserwacją prawdziwej mimiki.

`cleanup` wymaga tylko wspólnych pól oraz `artifact_id` z odpowiedzi. Należy podać
wersję i kontekst oryginalnego renderu; `request_id` może identyfikować nowe
żądanie cleanup. Odrzuca traversal, dowiązania, obce pliki, hardlinki i niezgodne
wiązanie receipt. Usuwa wyłącznie cztery znane pliki i katalog artefaktu. Nie jest
idempotentny: brak katalogu daje odmowę. Nie usuwa roota. Usuwanie po częściowym
błędzie I/O może wymagać cleanup prywatnego workspace przez hosta; brak gwarancji
transakcji/durability na poziomie filesystem. EOF zachowuje ukończone artefakty.
Po awarii/timeout host usuwa cały własny workspace, bez polegania na destructorze
helpera. Root produkcyjnych danych nie jest dopuszczalnym workspace.

Przykład cleanup (token trzeba przepisać z bieżącej odpowiedzi):

```json
{"schema":"loom.face_preview.request/1","op":"cleanup","request_id":"cleanup-1","version":1,"context_id":"synthetic-context","artifact_id":"fp-00000000000000000000000000000000"}
```

## Weryfikacja tego przyrostu

`python3 -I -B loom/tools/face_preview_port/sanity.py` wykonał jeden render i
18 istotnych odmów, walidację CRC/struktury/dekompresji RGB PNG, hashy, dokładnych
bajtów źródła, wiązania kontekstu/wersji i cleanup. Sprawdzono też kształt argv
istniejącego runnera oraz terminalną odmowę nadmiernej linii. Kod 0; wynik
`fixtures/actual/sanity.json`. `fixtures/actual/` to nowe syntetyczne dowody z tego
wykonania, nie E1/ASYNC ani badanie statystyczne. Skrypt przy ponownym jawnym
uruchomieniu nadpisuje wyłącznie te własne fixture/dowody.

NOT RUN: C++/host/client integration, build/regresja całego repo, modele/API,
real embedding, GPU, produkcja, deploy. NumPy 2.3.5, Python 3.14.4 były dostępne;
Pillow brak i nie jest wymagane. Żadnych instalacji. Inne wersje NumPy/zlib/Python
nie dziedziczą gwarancji identycznych bajtów — receipt zapisuje faktyczny hash.

Propozycja zmiany wspólnego hosta pozostaje w handoff autora i nie jest częścią
tego odebranego helpera. Wymaga osobnego przeglądu zaufanej instalacji, ograniczeń
procesu, pinów zależności i cleanup; capability pozostaje `unavailable`.
