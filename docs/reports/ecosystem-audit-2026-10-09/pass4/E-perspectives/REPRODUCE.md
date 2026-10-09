# Odtwarzanie

Narzędzia znajdują się w `tools/ecosystem-audit-2026-10-09/pass4/E-perspectives/`. Każdy test produktu weryfikuje `git rev-parse HEAD` względem jawnego SHA i wymaga świeżego pliku wynikowego. Nie nadpisuj istniejących receipts. Poniżej symbole oznaczają lokalne checkouty/bibliotekę przypięte do commitów w REPORT; pole `CHECKOUT` może wskazywać późniejszą poprawkę B/E, a SHA musi odpowiadać jej rzeczywistemu HEAD.

Python wymaga `requests` (w tej sesji zainstalowano 2.34.2 do własnego scratch). Node używa istniejących zależności `loom/web/node_modules`: React 18.3.1, TypeScript, Playwright. `e_acceptance.mjs` kompiluje 8 rzeczywistych jednostek C++20 wskazanych przez istniejący bridge E. Binarne źródła i wersję kompilatora zapisuje w `native_build`; nie wymaga modelu ani sieci.

```sh
python tools/ecosystem-audit-2026-10-09/pass4/E-perspectives/python_consumers.py --repo CHECKOUT --sha SHA --out FRESH_PYTHON_JSON
node tools/ecosystem-audit-2026-10-09/pass4/E-perspectives/perspectives.mjs --repo CHECKOUT --sha SHA --node-modules NODE_MODULES --out FRESH_PERSPECTIVES_JSON
```

Pierwszy program wykonuje tylko oryginalne Python `ConversationImporter`, `Database` i `ModelRegistry` na syntetycznych plikach. Drugi transpiluje pełne źródła TS/TSX i dopisuje w pamięci same eksporty prywatnych rendererów/filtra; żadna funkcja produktu nie jest zastępowana. React SSR nie jest mounted UI.

Rzeczywisty styk D → native → E:

```sh
python tools/ecosystem-audit-2026-10-09/pass4/E-perspectives/native_packet.py --d-repo D_CHECKOUT --d-sha D_SHA --packet docs/reports/ecosystem-audit-2026-10-09/pass4/D-resources/D-E-packet.json --native-source NATIVE_CHECKOUT --native-sha NATIVE_SHA --library NATIVE_LIB --out FRESH_D_NATIVE_PACKET_JSON
node tools/ecosystem-audit-2026-10-09/pass4/E-perspectives/e_acceptance.mjs --repo E_CHECKOUT --sha E_SHA --node-modules NODE_MODULES --native-temp SCRATCH_BUILD_DIRECTORY --native-packet FRESH_D_NATIVE_PACKET_JSON --out FRESH_E_RESULTS_JSON
```

Pierwszy krok używa oryginalnego `loom.tools.resource_graph.native.roundtrip`: akceptacja, native SQLite, zamknięcie, ponowne otwarcie i replay. Drugi faktycznie czyta packet z wyniku replay. Zestaw nie podkłada wymyślonego mapowania selector/version. `E4-10`, `E4-11`, `E4-12` są **ręcznie wskazanymi brakami połączeń aktualnego kontraktu**. Po dodaniu rzeczywistej integracji podłącz konkretne API do tych bramek; obecne placeholdery nie wykrywają samodzielnie dowolnej przyszłej implementacji.

W tej sesji użyto B4 native library z `pass4/B-native/object-manifest.json`; pełny hash binarny jest w `d-native-packet.json`. `--native-source/--native-sha` sprawdzają checkout, ale same nie dowodzą pochodzenia dowolnego dostarczonego pliku binarnego. Odbiorca ma zestawić bibliotekę z przypiętym manifestem builda. D źródłowy packet pochodzi z oryginalnego D `ResourceGraph.project`; sposób jego wytworzenia dokumentuje moduł D.

```sh
node tools/ecosystem-audit-2026-10-09/pass4/E-perspectives/browser_probe.mjs NODE_MODULES FRESH_BROWSER_JSON
```

Ta ostatnia próba uruchamia Chromium, ustawia wyłącznie syntetyczny lokalny HTML i odczytuje DOM. Nie otwiera serwisu ani nie testuje nieistniejącego jeszcze UI E.

`test-index.json` zawiera argumenty oraz każdy identyfikator testu z kategorią: reproduction, acceptance, contract, integration. PASS reprodukcji oznacza obecność błędu, nie naprawę. Exit 1 oznacza co najmniej FAIL; exit 2 w JS oznacza wyłącznie BLOCKED bez FAIL. W Pythonie brakujący kontrakt nie jest zastępowany testem swojej implementacji. Sieć Node/Python jest przechwycona/zablokowana; te gardy nie są sandboxem C++. Natywne operacje ograniczono do lokalnego store i resolvera, z workerami wyłączonymi.
