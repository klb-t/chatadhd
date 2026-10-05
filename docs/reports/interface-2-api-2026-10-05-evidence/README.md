# Powtórzenie UI/API W10

Wyniki, SHA i dokładne zależności: [RESULTS.json](RESULTS.json).
Archiwa `.tar.xz` zawierają pełne surowe odpowiedzi/receipts, manifests i źródła fixture; bez binarek. Wszystkie dane syntetyczne/publiczne, modele tylko ScriptedTransport albo loopback HTTP. `native-ui-positive.tar.xz` zawiera Methods14/GraphChat14/Analysis5/Onboarding8/Host21 i wcześniejsze84/20/16; `whole-app-positive.tar.xz` jest końcowym8-case na aktywnym bundle po geometry fix. Pełne negative receipts i replay sources są na osobnej gałęzi archive/2026-10-05/interface-2-api-negatives.

Z repo, w `loom/`:

```sh
cmake --preset dev -DLOOM_BUILD_SERVER=ON -DLOOM_BUILD_TESTS=ON -DLOOM_BUILD_CLI=ON -DLOOM_SHARED=ON -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_WERROR=ON
cmake --build build/dev -j1
ctest --test-dir build/dev --output-on-failure -j1 --output-junit ctest.xml
```

W `loom/web/`, z zainstalowanym Playwright/Chromium:

```sh
npm run build
npm run test:interface-2
npm run test:interface-2-native
npm run e2e
node e2e/onboarding-host.mjs
node e2e/onboarding-native.mjs
node e2e/methods-ui.mjs --native
node e2e/graph-chat-ui.mjs --native
node e2e/analysis-ui.mjs
node e2e/app-native-navigation.mjs
```

Harnessy same wybierają porty i katalogi tymczasowe. Własny dowód: `METHODS_UI_EVIDENCE_DIR`, `ONBOARDING_NATIVE_EVIDENCE_DIR`, `LOOM_GRAPH_CHAT_EVIDENCE_DIR`, `LOOM_ANALYSIS_EVIDENCE`, `APP_NATIVE_EVIDENCE_DIR`; wymagania istnienia świeżego katalogu zgodne z kodem harnessu. Analiza natywna kompiluje własny fixture na aktualnym static core; nie używa publicznego dostawcy.

Pełna bieżąca bramka oczekuje negatywu knowledge_semantic opisanej tożsamości próby; nie wymieniać go na zielony fixture. Dwa manifesty CTest i execution counts dokumentują rzeczywistą zmianę wyłącznie serwera podczas pełnego przebiegu. Po poprawce1 wykonać ponownie pełny CTest na zamrożonych inputs.
