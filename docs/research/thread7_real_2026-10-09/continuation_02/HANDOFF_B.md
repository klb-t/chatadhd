# C → B: odblokowanie fixture, 2026-10-09

Minimalny commit do przyjęcia: **`a6481d11a05158977a87dd55f3a4d6896a190dad`**.
Zmienia wyłącznie `loom/tools/structure/test_credential_handoff_v2.py`.
Gałąź: `gpt/thread7-real-2026-10-09`; baza C:
`5e346c7161028ded976c40a0251fe8f9547bd233`.

Przeczytano raport B i kontrakt synthetic temporary root przy
`b20c0d8ac37934e1600c3b9216492fd524220941`. Przyczyną jest jawne
`TemporaryDirectory(dir="/tmp")`, które omija konfigurację `TMPDIR`.
Usunięto argument `dir`; standardowe `tempfile` wybiera skonfigurowany katalog.
Ochrona sekretów, dozwolone lokalizacje, walidacja uprawnień i produkcyjny kod
nie zostały zmienione.

## Reprodukcja i wynik

W bieżącym środowisku C `/tmp` nie odtwarzał warunku B: stare 8/8 testów PASS
przy `TMPDIR=/var/tmp`. Kontrolowana reprodukcja utworzyła osobny syntetyczny
root Git poza repozytorium i skierowała do jego `tmp` jedynie wywołania
fixture jawnie żądające `/tmp`. Prawdziwy `outside_git` zwrócił
`private_path_inside_git` w **8/8** setupów. Nie dodano/usunięto markera platformy.

Po naprawie **10/10** testów PASS. Dwa dodatkowe przypadki sprawdzają wybór
skonfigurowanego katalogu oraz odrzucenie osobnej, celowo niedozwolonej
lokalizacji z markerem `.git` przed powstaniem prywatnego materiału.

Pełne `research.structure`: **1458/1458**, 0 pominięć, **51,36 s**;
repozytoryjny evidence guard **PASS**. Zestaw uruchomiono z zamrożonego
detached checkoutu bazy C + dokładna poprawka, żeby równoległe badania nie
zmieniały testowanej zawartości. Izolowana rejestracja CTest powtarza rzeczywiste
polecenie, timeout 300 s i środowisko tego jednego zestawu z `CMakeLists.txt`.
W obu limitach stdout ustawiono 10 MiB. To bramka zakresowa, nie pełne dev B.

Przypadki wymagające native używały rzeczywistego istniejącego
`loom_candidate_graph_native_tool` z buildu A/main. SHA binary i source-head
zapisano przed testem; sprawdzono brak zmian natywnych źródeł względem main
`9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`. Nie deklarujemy nowego buildu native.
Wszystkie 1458 przypadków wykonały się, zamiast pominięcia zależności native.

Pełne logi, JUnit, manifest discovery, receipt i negatyw są w
`priority0-evidence.zip`. Pierwszy nieudany start CTest (brak w PATH) również
zachowano; kolejny używał istniejącego jawnie wskazanego executable.
Nie wykonano rzeczywistych wywołań modeli; koszt nowy 0 USD.

## Dokładne polecenia ponownej weryfikacji w B

Po przyjęciu minimalnego commitu, z root repo i istniejącym aktualnym buildem B:

```sh
mkdir -p /var/tmp/thread7-b-temp-gate
TMPDIR=/var/tmp python3 -m unittest discover \
  -s loom/tools/structure -p 'test_credential_handoff_v2.py' -v \
  > /var/tmp/thread7-b-temp-gate/handoff.log 2>&1
python3 .github/scripts/write_build_receipt.py --preset dev \
  --binary loom/build/dev/loom_candidate_graph_native_tool \
  --output /var/tmp/thread7-b-temp-gate/build-receipt.json
TMPDIR=/var/tmp ctest --test-dir loom/build/dev -R '^research.structure$' \
  --show-only=json-v1 > /var/tmp/thread7-b-temp-gate/manifest.json
TMPDIR=/var/tmp ctest --test-dir loom/build/dev -R '^research.structure$' \
  --output-on-failure --no-tests=error \
  --test-output-size-passed 10485760 --test-output-size-failed 10485760 \
  --output-junit /var/tmp/thread7-b-temp-gate/ctest.xml \
  > /var/tmp/thread7-b-temp-gate/ctest.log 2>&1
python3 .github/scripts/verify_ctest.py --preset dev \
  --policy .github/ctest-evidence-policy.json \
  --manifest /var/tmp/thread7-b-temp-gate/manifest.json \
  --junit /var/tmp/thread7-b-temp-gate/ctest.xml \
  --output /var/tmp/thread7-b-temp-gate/executed-cases.json \
  > /var/tmp/thread7-b-temp-gate/guard.log 2>&1
```

Ścieżkę builda można dostosować do faktycznie przypiętego buildu B. Nie należy
interpretować zakresowego PASS C jako odbioru pełnej macierzy B. Jeżeli
skonfigurowany katalog tymczasowy także leży pod Git, należy skonfigurować
dozwolony katalog; nie wyłączać walidacji.

Pozostaje osobna granica wskazana przez A przy `e2910985`: A3-DISC-001
(DTO MethodRegistry dla packetu C). Naprawa fixture nie zamyka tej luki,
nie instaluje executora metody i nie dotyczy runtime/UI.
