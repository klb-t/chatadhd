# Reprodukcja

Uruchom `python run.py --repo CHECKOUT --sha EXACT_SHA --typescript /path/to/typescript/lib/typescript.js --native-lib /path/to/libloom.so --native-source BUILD_SOURCE_CHECKOUT --native-sha BUILD_SOURCE_SHA --output NEW_DIRECTORY`.

Wymagany Node z Web Crypto/Response i Python. Runner ładuje całe aktualne moduły produktu; nie kopiuje implementacji. Zakazuje rzeczywistych transportów Node i przechwytuje wyłącznie końcowy fetch. NativeGraphStore używa istniejącego C ABI z wyłączonymi workerami; żadne żądanie modelu nie jest inicjowane. Wszystkie dane są syntetyczne. Output musi być nowy. Kod 1: FAIL; kod 2: pozostały BLOCKED; kod 0: wszystkie wybrane bramki przechodzą. Reprodukcja PASS nie jest akceptacją produktu.

`acceptance-request.json` i `native-read.json` zawierają wyłącznie wygenerowany publiczny profil; oryginałów właściciela nie dotykano.
