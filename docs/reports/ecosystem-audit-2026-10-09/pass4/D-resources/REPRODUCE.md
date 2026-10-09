# Odtworzenie D pass4

Python 3.12, standard library oraz zależności istniejącego packet codec (`jsonschema`). Bez modeli, credentials, serwerów ani sieci. Uruchamiać z katalogu gałęzi audytu; `--repo` wskazuje niezmieniony checkout produktu. Wszystkie fixtures powstają we własnym katalogu tymczasowym. Runner sprawdza SHA checkoutów przed importem.

```bash
python tools/ecosystem-audit-2026-10-09/pass4/D-resources/run.py \
  --repo /path/to/D4 --sha 1d3d133154f213733b7a69af863613cec2dd8ca2 \
  --library /path/to/main-build/libloom.so.0.1.0 \
  --native-source /path/to/main --native-sha 9e20f99ab27e7cd45e1892f83bf60fbe60db3de9 \
  --output /path/to/new-receipt.json
```

W drugim przebiegu użyto `--library .../scratch/pass4/B-native/libloom.so`, `--native-source .../checkouts/B4` i `--native-sha 387afe08587f179d47c013a2ea518ff4b68e36bc`. Jego manifest kompilacji/linkowania znajduje się w `../B-native/object-manifest.json`. Podanie SHA biblioteki źródłowej nie zastępuje dowodu budowy — sprawdzić hash binary zapisany w receipt i manifest. Własny runner nie kompiluje native.

Oczekiwany stan przypiętego D4: 22 PASS / 2 FAIL / 8 BLOCKED; exit **1**. D4-17A i D4-18A potwierdzają reprodukcje; D4-17B i D4-18B są nieprzechodzącymi odbiorami napraw. Nie usuwać FAIL ani nie zamieniać ich w expected-pass. Każdy pozostały BLOCKED jest nazwanym brakującym połączeniem, nie testem wykonanym z atrapą. Aktualne rozszerzenie D–E sprawdza osobno moduł audytu E.

Rzeczywisty packet do przekrojowego odbioru E można odtworzyć przez:

```bash
python tools/ecosystem-audit-2026-10-09/pass4/D-resources/export_packet.py \
  --repo /path/to/D4 --sha 1d3d133154f213733b7a69af863613cec2dd8ca2 \
  --output-dir /path/to/new-artifacts
```

Generator czyta wyłącznie własną syntetyczną `fixtures/conversation.json` i wywołuje oryginalne `ResourceGraph.attach/project`. Packet: 19 entities, 34 claims, 10 sources. Metadata lokalnego stat i wyliczone IDs zależą od miejsca/czasu zapisania fixture; to nie oczekiwanie identyczności bajtowej między maszynami. Przepis, wartości, kolejność i źródłowe selektory są powtarzalne. Bramka przypiętej relokacji pozostaje osobna.

Brakujący moduł domenowego discovery sprawdza mały rzeczywisty probe:

```bash
python tools/ecosystem-audit-2026-10-09/pass4/D-resources/probe_discovery.py \
  --repo /path/to/D4 --sha 1d3d133154f213733b7a69af863613cec2dd8ca2 \
  --output /path/to/discovery-probe.json
```

Na przypiętym SHA kończy się exit 1 i zapisuje `BLOCKED_MISSING_IMPLEMENTATION`. Przyszłe istnienie implementacji nie da automatycznej akceptacji jakości ani uprawnienia do aktywacji; ten probe mierzy tylko dostępność wejścia.
