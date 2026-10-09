# A2-C-003 — powiązanie kolejki z opłaconą próbą

Baza C: `cb16b336ee0c59a1ff80b4899dadebce33aa7610`. Audyt A: `9f931da3` (pełne SHA w receipt). Badanie mechaniki z kontrolowanym transportem; **0 wywołań dostawcy, 0 USD**. Nie odczytywano klucza ani nie zmieniano ledgera właściciela.

## Reprodukcja na własnym aktualnym kodzie

Wykorzystano istniejący fixture `ProgrammeRunnerTests`, jego rzeczywiście walidowany `PrivateLedger` oraz obsługiwaną ścieżkę odtworzenia kolejki `mark_dispatched`. Ten sam `operation_id`, ale zmienione `max_tokens` i hash requestu, zostały przed poprawką przyjęte jako `captured` z `billing_verified=true`. Dopisano dowód rozliczenia. Test nie podmieniał SQLite ani rachunków. Poprawna kontrola również przechodziła.

Po poprawce zły request pozostaje `pending`, bez wyniku i bez nowego dowodu. Błąd to stały kod `payer_queue_request_hash_mismatch`. Poprawna kontrola nadal przechodzi. Sam `sync_verified` przed i po poprawce wykonał zero wywołań transportu. Dokładne wyniki: `payer-binding-before-reproduction.json` i `payer-binding-after-reproduction.json`.

## Zmiana

`PayerBoundary` sprawdza identyfikator operacji, kampanię, klucz przez fingerprint, manifest, dokładny hash zachowanych bajtów, sparsowane ciało requestu, model, pełną deklarowaną konfigurację providera oraz trasę. Porównanie ciała zachowuje rozróżnienie JSON `10` i `10.0`; nie zastępuje hasha oryginalnych bajtów hashem kanonicznej serializacji. Oryginalne białe znaki requestu nadal są obsługiwane.

Kontrola działa przed dispatch i przed projekcją zakończonej próby. Wszystkie skojarzenia w paczce są sprawdzane przed pierwszym zapisem wyniku lub dowodu: późniejszy błędny request nie zostawia częściowo przyjętej paczki. Poprzedni błędny pierwszy wynik nie jest nadpisywany. Taka kolejka wymaga odizolowania i jawnego wyjaśnienia powiązania; naprawa nie daje prawa do ponowienia POST.

Historyczny receipt nie zawiera uwierzytelnionego URL endpointu. `endpoint_identity` pozostaje metadanymi żądanej trasy, a nie dowodem zaobserwowanego endpointu. Tożsamość źródła i widoku sprawdza connector preparacji. Nie dopisano pozornego dowodu do historycznego rachunku.

## Weryfikacja

**91/91 testów PASS, 0 pominięć**, w tym 12 nowych metod testowych. Zbadano rozbieżność SHA, zmianę ciała przy zachowanym SHA, typ liczbowy, model, brak parametru providera, trasę, manifest, błędną drugą próbę w paczce, nienaruszalność istniejącego pierwszego dowodu, odmowę przed wysłaniem oraz poprawny dispatch i idempotentną synchronizację.

Dwa istniejące pliki fixture zawierały niespójne metadane: pominięte `allow_fallbacks`, pozostawiony model `fixture/a` albo ciało innego requestu. Poprawiono wyłącznie setup zgodnie z faktycznymi bajtami requestu; asercje pozostały bez zmian. Pierwszy nieudany przebieg przed korektą setup zachowano prywatnie.

Pełny stdout/stderr: `payer-binding-tests.log.gz`, hash nieskompresowanych bajtów `08c6dfd61752cbd25a17644078f5df51da6ff061e6123f1e4299317ea084c7cf`. Dokładna komenda i hashe kodu są w `payer-binding-receipt.json`. Wyniki fixture nie są nowymi badaniami jakości modeli ani rozliczeniami kampanii właściciela.
