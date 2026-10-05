# W8 — minimalny adapter seeding → method graph (2026-10-05)

Zastosowano adapter do rzeczywistego kontraktu W3/W4
`loom/src/packet/METHOD_GRAPH.md`. Istniejące `loom_packet` i
`loom_graph_packet_store` wystarczają; brak zmian C++, ABI, CMake i UI.
Nowy manifest eksperymentu wskazuje dostępny eksport opt-in zamiast oczekiwania
na uzgodnienie kontraktu. Sam eksperyment nie uruchamia eksportu.

`method_graph.py` projektuje **cały zapisany eksperyment** jako jedną metodę,
jej wersję i konkretny run. Nie jest to rejestr ani dispatch czterech rankerów.
Domyślnie wszystkie nowe Sources i Entities mają czas pierwszego zaobserwowania/exportu
`projected_at`; nie cofamy wiedzy o score do daty manifestu predykcji. Oryginalny
czas pozostaje w raw capture, `trace.prepared_at` i `attrs.original_prepared_at`.
Oryginalne policy, profile, uporządkowane overlaye i moduły są zachowane jako
exact-byte Sources oraz adresowalne wersje definicji. Osobny ProjectionVersion
wiąże efektywny profil eksportu i dokładne bajty załadowanego adaptera oraz
wspólnych codeców; run ma rzeczywiste `uses_projection`. Instrument nie zmienia
wersji historycznego producenta.

Vocabulary, role wejść/wyjść, selektory manifestów i czasu, wyłączenia recipe,
wzorce traversalu, długość cytatu wsparcia i presety eksportu są danymi w
`profiles/method_graph.json`. Nie ma nazw algorytmów w adapterze. Nieobsługiwane
operacje/formaty i błędne pointery zwracają jawny błąd.

Preset `records` daje osobny węzeł każdemu pełnemu rankingowi oraz słownikowi
metryk, literalny Claim z Source/JSON Pointer/canonical hash i dwa rzeczywiste
Claim edges `produced_in_run` / `produced_by_method_version`. Dane
`evaluation=true` dodają jeszcze 648 Claims **o metodzie**:
MethodVersion → `has_evaluation_record` → konkretny Metric Result. To
odniesienie do istniejącego przechwyconego score, bez awansu do prawdy o świecie.
Original gzip
bytes występują raz na plik; nie są powielane w każdym węźle. `recover_results`
sprawdza literal Claim, pointer i hash każdego rekordu. Kandydaci zachowują
oryginalne `extrapolated`, `candidate`, przesłanki i niezweryfikowane właściwości
po odkodowaniu. Natywne twierdzenia mówią o strukturze przechwyconego outputu,
nie o prawdziwości rekomendacji. Preset `bulk` ma tylko dwa snapshot outputs;
nie przypisujemy mu provenance poszczególnych rankingów/metryk.

| Miara — inspected synthetic V4 | Przed | Po |
| --- | ---: | ---: |
| Pełne rankingi | 2 135 | 2 135 dokładnie |
| Pełne słowniki metryk | 648 | 648 dokładnie (72 + 576 repeticji) |
| Result entities z dwoma provenance edges | 0 | 2 783 |
| Oryginalne pliki historycznych 5 runów | 26 / 13 800 361 B | 26 / te same bajty i SHA256 |
| Mandatory seeding unittest | 50 | 69/69, 10,568 s po finalnych poprawkach |
| Dodatkowy artefakt gzip V4 z fixture i instrumentami | 0 B | 5 497 581 B |
| Wszystkie output bytes: oryginalne gzip + artefakt | 3 302 089 B | 8 799 670 B (×2,665) |

`full-v4-pure.json` zachowuje before/after matrix SHA256
`f7e4e2236a6168e55198a985671a06c526466146d336f3b54c5ce9e1d4768381`.
Odtworzono faktycznie archiwalny V4 program z jego oryginalnym policy/protocol,
zweryfikowanym synthetic fixture i stałym czasem. Oba pełne gzip outputy,
program, policy i protocol odtworzono byte-for-byte. To nowy offline replay,
nie dowód wykonania dawnego producenta w chwili pierwszego zapisu.

Pierwotne `full-v4-pure.json` oraz `native-small.json` powstały ze stałym
`projected_at=2026-10-05T00:00:00+00:00` i bez preflight w verifierze.
Są to **historyczne dowody development fixture**, nie dowody rzeczywistego
zaobserwowania Sources/Entities o północy. Mały native proof miał wzrost output
×37,277; nie uzyskano na niego nowego potwierdzenia i nie powtarzamy go.
Rzeczywiste wcześniejsze operacje native oraz offline replay pozostają dowodami
działania kontraktu. Ich oryginalnych receipts i SHA256 nie przepisano;
zakres koryguje osobne `prior-proof-annotation.json`.

CPU jednego zbudowania/serializacji eksportu: 5,283 s; pełny bieżący frozen
producer replay: 5,467 s. Dodanie jednego eksportu daje tu około ×1,97 pracy CPU;
CLI liczące najpierw dokładny dry-run i następnie eksport wykonuje tę projekcję
dwukrotnie (szacunek łącznie około ×2,93). Historyczny CPU/koszt pozostają null.
Nie wykazano wzrostu ×10 na rzeczywistym pełnym V4. Default eksperymentu nie
eksportuje nic. CLI przed zapisem podaje szacunek z realnych bajtów i rekordów,
a duże cytaty wsparcia wykrywa lower bound pracy **przed budową pakietu**.
Przy oczekiwanym ×10 wymaga jawnego `--confirm-large-export`. Presety i zakres
projekcji są konfigurowalne, bez nowych limitów rozmiaru.

Verifier korzysta teraz z tego samego `method_graph.estimate` przed właściwą
budową eksportu, frozen replay i native write. Zapisuje estimate oraz decyzję;
bez wymaganego potwierdzenia kończy się z błędem i małym rejected receipt.
Domyślny zegar to rzeczywisty bieżący UTC. Opcjonalny `--fixture-clock` wymaga
strefy czasowej i jawnie oznacza symulację w summary, `producer_evidence` oraz
wewnątrz Packet: `task.verification_clock`, atrybuty wszystkich Sources i opis
semantyki `known_at`. Wspólny `codec.make_packet` przelicza source/provenance
hashes i packet identity, więc zapis native zachowa oznaczenie symulacji.

Walidacja:

- Dotychczasowe 50 testów zachowane bez zmian i luzowania; 19 nowych testów
  obejmuje dokładne raw bytes/gzip headers/BOM/overlaye/moduły, dowolne nazwy
  i kolizję `all`, zmianę wszystkich ról/pointerów wyłącznie przez dane,
  rekordy puste i pełne, tampering, duplikaty, nieznane operacje, wersje
  producenta vs projekcji, dry-run/overwrite, brak retrodating, krawędzie ewaluacji metody i preflight ×10 na prawdziwym
  syntetycznym payloadzie >100 KB. Ostatnie trzy testy potwierdzają odrzucenie
  pełnych i ogromnych cytatów przed build, zapis odmowy przed native import,
  bieżący UTC oraz simulation disclosure i rehash wewnątrz Packet. Oryginalny
  test CLI zachowuje confirmation/overwrite coverage na rzeczywistym
  highentropy baseline z fizycznym output ratio <×10; wymusza tylko ostrzejszy
  caller preset. Świeża cała suite: 69/69, 10,568 s, exit 0; before/after SHA
  obu finalnych runtime plików są identyczne. Dowód:
  `unittest-guard-clock-final.json` i rzeczywisty `.log`. Wcześniejsze 66/66
  oraz pre-freeze wynik negatywny pozostają osobno i nie zastępują tej bramki.
- Oddzielnie 4/4 wspólnego schematu i real-relations consumer validation:
  records, bulk, historyczny brak profilu, caller vocabulary. Każdy przypadek
  odrzuca usunięty wymagany edge. Mandatory suite nie zależy od natywnej
  biblioteki ani jsonschema i nie dodaje skipów.
- Mały native proof: 18 result records, 33 Entities, 81 Claims, 18 Sources.
  Istniejące CAPI wykonało validate, actual accept, zamknięcie/restart,
  read/replay i identyczny retry. Rzeczywiste bindings/edges oraz hashe
  receipt zapisano w `native-small-bindings.json` i `native-small.json`.
  Native consumer poprawnie podaje `producer_execution_verified=false`.
  SHA256 użytej biblioteki:
  `61fd580b5461aaf27ad348e98ae21fc6d9c8a623782ad9f6ffd8b3e48d78d589`.
- Płatne/provider calls: 0. Nie czytano blind/holdout/private.

Małe receipts/logi oraz czytelny tiny fixture znajdują się w
`docs/verification/repo-hygiene-seeding-method-bridge-2026-10-05/`.
Nie dublujemy 27 MB pakietu ani istniejących wielomegabajtowych rankingów.
Original tiny gzip bytes odtwarza zapisany `test_method_graph.frozen_run`;
w dowodzie publikujemy ich kompletny odkodowany UTF-8 JSON oraz raw hashes.
Odtworzenie (z nowymi pustymi katalogami dowodów):

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s loom/tools/seeding -p 'test_*.py' -v
PYTHONDONTWRITEBYTECODE=1 python3 -m loom.tools.seeding.verify_method_graph \
  --run loom/tools/seeding/results/synthetic_lopo_v4_application_alternatives_first \
  --fixture loom/tests/fixtures/eval/synthetic_dev/ground_truth.json \
  --replay-frozen-producer --evidence-dir /tmp/seeding-v4-new-proof
PYTHONDONTWRITEBYTECODE=1 python3 -m loom.tools.seeding.verify_method_graph \
  --synthetic-small --evidence-dir /tmp/seeding-tiny-rejected-preflight
```

Ostatnia komenda celowo zapisuje odmowę preflight ×37 i kończy się z błędem;
nowe wykonanie małego native proof wymagałoby potwierdzenia właściciela po
przeczytaniu estimate. Nie jest częścią wykonanych nowych bramek.

Nie zrobiono: natywnego full-V4 accept/restart nie rozpoczęto. Najpierw
wstrzymywały go równoległe kompilacje, następnie właściciel zakończył bieżący
zakres prac przed jego uruchomieniem. Pełny pure replay i historyczny mały
native fixture proof są rzeczywiste, z korektą zegara/preflight opisaną wyżej.
Aktualną pełną macierz W8 opisuje
`repo-hygiene-2026-10-05.md`; nie przypisujemy temu testowi wyniku macierzy.
Nie podłączono backendów do W3 registry ani czatu,
nie wykonano nowych badań jakości modelu. Historyczny V4 nie miał zapisanego
projection profile: oznaczamy `profile_status=unrecorded`, nie dopisujemy mu
dzisiejszego profilu.

## Do wątku 9

Odbierać wraz z aktualnym raportem macierzy i własnymi pełnymi bramkami.
Zależność W3/W4 jest konsumowaniem istniejącego kontraktu
i **metadata capture** przez istniejące CAPI; brak zmiany ich zakresu. Przed
deklarowaniem pełnego native V4 persistence dołączyć oddzielny receipt z
`verify_method_graph --library ...` w stabilnym środowisku. Nie przypisywać
native acceptance weryfikacji dawnego producer execution ani prawdy kandydatów.
