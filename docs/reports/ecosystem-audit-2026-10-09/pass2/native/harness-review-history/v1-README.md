# Native — drugi przebieg, checkpoint

Baza: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`. B: `ddcaeaf7a25b70f0d4dc2e0eed2466016233eeb8` — osobny pełny build i niezależne porównanie wykonane.

Realny C++ engine został skompilowany (GCC13.3.0, Debug, vendoredSQLite, OpenSSL, WERROR). Niezależny transport audytu wywołuje rzeczywiste Pack/Normalizer/OnboardingStore/DefaultLayers/KnowledgeStore. Każde żądanie otwiera i zamyka osobny proces oraz natywną bazę. Brak wywołań modeli i ścieżek sieciowych.

`main-receipt.json`:31 kryteriów —26 PASS,5 FAIL. Cztery FAIL dotyczą zaniżania/pomijania detail/sensitivity w policy_decision; piąty dotyczy przyjęcia nieobsługiwanego ustawienia. PASS reprodukcji opisuje błąd, nie zgodność produktu.

Autentyczna pełna historyczna paczka z `b302df25e1a65f20c395eadc5ad5ef065d26e33d` została odczytana aktualnym loaderem: odrzucenie z jednym błędem `unknown schema id loom.kb.stemming/1`. Stary overlay też jest odrzucany, bez ukrytego fallbacku. Audytowy kandydat migracji zmienia schema na /2 i jawnie dodaje normalization z przypiętych danych. Realny loader i Normalizer przyjmują go, a pięć próbek daje wynik równy obecnej paczce. To **nie jest automatyczny migrator produktu** ani pełny dowód zgodności ze starym algorytmem. Istniejący `NORMALIZER_RECIPE.md:37–47` jawnie dokumentuje ręczne przejście /1→/2 z zachowaniem tablic; nasza próba wykonuje ten sposób. Nie klasyfikujemy jawnego odrzucenia /1 jako nowej regresji. Oryginalne bajty historyczne nie zmieniły się.

Potwierdzone native PASS: zapis/odczyt po restarcie; zachowanie nieznanych rozszerzeń profilu; trwałe disable/exclude przy aktualizacji packa; wyjaśnienie źródła efektywnej wartości; CAS; kolizje ID i rozgałęzione wersje; odrzucenie przyszłej wersji bez nadpisania; stary wynik nadal wskazuje starą recepturę po zmianie promptu. Dziedzina schema/codec jest oddzielona od realnego native SQLite.

Nie wykazano pełnego backupu profilu+warstw+workflow przez publiczny import/export; istniejący `open(legacy_profile)` sprawdzono w jego faktycznym węższym kontrakcie. Basic/Advanced/Expert w przeglądarce pozostaje zakresem nadrzędnego audytu UI. Native GraphPacket roundtrip i granice kontraktu są w `../c-review/`.

Pierwszy receipt zawierał wadliwy test nieobsługiwanego ustawienia: odrzucenie następowało wcześniej z powodu brakującego id/time. Zachowano go jako **superseded-invalid-settings-fixture.json**, bez prawa liczenia jako PASS produktu. Poprawiona koperta ujawnia FAIL akceptacji.

Wstępne środowisko: brak CMake/Ninja rozwiązano lokalną instalacją. Pierwszy link wykrył zerowy nagłówek jednego obiektu, ponowna kompilacja tego obiektu pomogła. Link dużego zestawu testów dostał SIGKILL przy równoległym buildzie; powtórka -j1 z `--no-keep-memory` zakończyła link bez zmiany kodu, WERROR i asercji. Pełne logi zachowane. ASan/UBSan zostanie sprawdzony po porównaniu B.

## Zamknięty moduł porównania B

`B-receipt.json`:26 PASS,1 FAIL,4 NOT_REPRODUCED. B-POLICY001 jest **naprawą w prześledzonym natywnym konsumentcie polityki**: cztery akceptacje przechodzą, cztery reprodukcje przestają się odtwarzać. Pozytywna kontrola nadal dopuszcza pole mieszczące się w limicie. CH-P2-N001 pozostaje na obu SHA. To nie dowodzi, że wszystkie ścieżki send/worker wywołują politykę; ten oddzielny warunek sprawdza root.

Istniejąca bramka `ctest -R "^unit\."`:112/112 grup,897 przypadków C++,28601 asercji,0 pominięć. Nie zaliczamy tego jako niezależnych testów badań C ani całego CTest. Biblioteki obu SHA zbudowane; test C ABI:3/3 PASS (podpisy, dokładny zestaw eksportów, ctypes smoke).
