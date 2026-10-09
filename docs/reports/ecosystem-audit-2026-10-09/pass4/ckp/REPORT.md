# PASS4 · IO Matrix / Custom-Keyboard-Pro

Baza nadal `192f820f2d8c653768237e1bff3a1a6d950d69c0`. Nowy zakres:
przechwytywanie struktury accessibility, trwała receptura eksportu oraz opóźnione
wklejanie. Nie powtarzano 974 dawnych testów bez zmiany produktu. Kod produktu,
wcześniejsze raporty i receipts pozostały bez zmian.

Wykonano **23 przypadki Kotlin/JVM: 11 PASS kontraktu, 6 PASS reprodukcji,
6 FAIL akceptacji**. Łączne 17 PASS nie jest oceną produktu. `host-receipt.json`
zawiera każdy wynik i hashe rzeczywiście kompilowanych źródeł/zależności.
Transport jest zablokowany; wszystkie wejścia są syntetyczne.

| Ustalenie | Odtworzone zachowanie | Zakres odbioru |
|---|---|---|
| **A4-CKP-001** | Dwie równe mapy semantyczne w innej kolejności wstawienia dostają różne fingerprinty. Sześć identycznych semantycznie odczytów: stała kolejność daje 3 żądania scroll, permutowana 0 i `changing_content_no_actions`. | Potwierdzone w rzeczywistych CaptureFrame/CaptureSession. Obecny adapter Android buduje mapy w stałej kolejności; nie wykazano wystąpienia na telefonie. Lista węzłów pozostaje uporządkowana. |
| **A4-CKP-002** | `settleMillis=300` i `1700` dają identyczny JSON opcji. Rzeczywisty scheduler używa tej wartości; eksport ją pomija. | Rzeczywisty serializer wykonany; połączenie z Handler i zapisem dokumentu prześledzone w źródłach. Brak wartości to luka pochodzenia, nie dowód regresji odczytu nieistniejącego importera. |
| **CKP-A-012 — rozszerzenie** | Wynik konwersji po zmianie pola A→B trafia do B. Błąd uruchamia natywny fallback również dla B. Zmiana sensitive state nie zatrzymuje późnego wyniku. | Nowy konsument istniejącej przyczyny, bez kolejnego ID. Wykonane oryginalne metody pasteOrConvert/pasteNativeClip/refreshBusyLamp i EditorController.commitText; Android i transport są granicami testowymi. |
| **A4-CKP-M001** | Zachowane nieznane fakty, jawna niedostępność potomka i ucięcie, redakcja prywatnego poddrzewa/tekstu agregowanego, stan błędu z poprzednią obserwacją i bez payloadu wyjątku. | Dopuszczalne mechanizmy. Wybrane offscreen ≠ widoczne; niepełne ≠ kompletne puste. Naprawy muszą zachować te kontrole. |

Reprodukcja błędu wklejania jest próbą rzeczywistego asynchronicznego konsumenta
na hoście. Dla sukcesu oryginalny `commitText` rozwiązuje aktualne połączenie;
samo połączenie zapisuje efekt testowy. Dla błędu `pasteClip` jest rejestrującą
granicą platformy; jego rzeczywista ścieżka MIME/commitContent została przeczytana,
ale nie uruchomiona. Nie jest to test Android/device ani dowód odbioru przez OEM.
Poprawność pod niezmienionym celem ma oddzielną kontrolę PASS.

W mapach zmieniano jedynie kolejność kluczy, nie wartości, listę węzłów, wiadomości,
liczby czy uprawnienia. Oracle wynika z równości Map i kontraktu CaptureSession
„dwie równe obserwacje”; nie z zaakceptowania bieżącego wyniku produktu. Różnica
fingerprintu i zablokowane akcje to jedna przyczyna, jeden finding. Osobna kontrola
wymaga zmiany fingerprintu po przestawieniu kolejności węzłów.

Pokrycie nazwanych zakresów rośnie **47 → 56 / 233 plików produktu**; **177**
nie ma jeszcze takich zakresów. Przyrost to 9 plików. Przegląd obejmuje 11 plików
z tej części, z czego 2 miały już wcześniejsze zakresy. Nie są to 56 całkowicie
zweryfikowane pliki. `coverage.json` rozróżnia wykonane źródła/metody, przegląd
źródłowy i nieprześledzone wywołania. Mianownik 233/461 tracked entries nie zmienił się.

R42 zastosowano dokładnie: SHA-256, klucze kontraktu, maszynowe stany i zgodne
kodowanie JSON mogą pozostać mechanizmem. Nie nadaje to wyjątku progom, słownikom
domenowym czy tekstom UI. Rejestr decyzji wskazuje niezamknięty osobny przegląd
polityki DisclosurePolicy/limitów; poprawnej odmowy nieznanej akcji nie nazwano
błędem tylko dlatego, że nie ma adaptera dla każdej etykiety.

Pakiety dla B są w `handoff-B.json`, uruchamialny indeks w `test-index.json`,
mapa wejście→konfiguracja→decyzja→wykonanie→zapis→odtworzenie→prezentacja w
`flow-map.json`. Publikacja tych plików nie jest deklaracją wysłania wiadomości B.

Otwarte granice: fizyczny IME, document provider i restart procesu; crash między
DB/settings/sync-state z wcześniejszego etapu; intercept rzeczywistego
CallEngine EOF/timeout oraz MIME przy pliku audio wklejonym z innego źródła niż
mikrofon. Te ostatnie są kolejką sprawdzenia, bez potwierdzonego findingu z samego
odczytu kodu. Dokładna kolejność i przesłanki są w `resume-queue.json`.
