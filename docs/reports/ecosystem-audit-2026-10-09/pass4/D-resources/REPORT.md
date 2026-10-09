# D — niezależny odbiór pass4

Przypięto `gpt/resource-graph-2026-10-09` na `1d3d133154f213733b7a69af863613cec2dd8ca2`, względem main `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`. To pierwszy opublikowany fragment D, nie ukończone discovery ani naprawa wszystkich wcześniejszych findingów. Przeczytano instrukcje repo, aktualny STATE, wymagania R15/R19/R20/R21/R40–42, raporty D oraz kontrakt pass3. Właścicielska korekta pass4 jest nadrzędna: referencja i projekcja na żądanie są prawidłową ścieżką bez obowiązkowej kopii całego źródła lub materializacji wszystkich węzłów. Office nie był przedmiotem odbioru.

Własny zestaw uruchamia oryginalne moduły D. Na każdym z dwóch wariantów native uzyskano **32 kryteria: 22 PASS, 2 FAIL, 8 BLOCKED**. To te same kryteria w dwóch warunkach, nie 64 niezależne przypadki. PASS obejmuje 17 testów kontraktu, 2 integracyjne, 2 reprodukcje błędów i 1 obserwację zachowania. **PASS reprodukcji nie oznacza naprawy produktu.** Oba testy akceptacji napraw są czerwone.

| Biblioteka za rzeczywistym C ABI | Dowód pochodzenia | Wynik |
|---|---|---|
| main `9e20f99a` | wcześniejszy pełny build main; hash biblioteki zapisany w `receipt-main.json` | 22/2/8 |
| B4 `387afe08587f179d47c013a2ea518ff4b68e36bc` | selektywne rzeczywiste obiekty i zweryfikowane archiwum, [manifest B](../B-native/object-manifest.json); nie pełny nowy build CMake | 22/2/8 |

Nie wykonywano płatnych wywołań, zewnętrznych żądań ani CI. Pythonowy transport sieciowy był zablokowany; natywny test wywołuje wyłącznie akceptację/odczyt/replay lokalnego packetu, w pustych katalogach bez credentials i z `start_workers=false`. Próbki są syntetyczne. Żadnego prywatnego archiwum nie czytano ani nie zmieniano. Nie zmieniono produktu ani cudzych gałęzi.

## Dwa potwierdzone problemy

**A4-D-001 — receptura parsera nie identyfikuje parametrów.** Osiem bajtów CSV `a;b\n1;2\n`, ta sama tożsamość źródła i dwa jawne delimitery dają dla `/0/0` wartości `a;b` i `a`. Identyfikatory pola, metody i wyliczonej rewizji są identyczne. `parser_options` pozostają w deskryptorze korzenia, więc nie twierdzimy, że cały packet je gubi; jednak `produced_by` nie wskazuje jednoznacznie zastosowanej receptury. Testy D4-17A/B. Naprawa musi powiązać efektywne parametry z wersją metody/wyniku, zachowując surowy hash i stare receipts.

**A4-D-002 — lokalny dostęp ma wyścig między autoryzacją a otwarciem.** Po autoryzacji pliku wewnątrz `local_roots`, kontrolowana podmiana na symlink kieruje rzeczywiste `os.open` do innego syntetycznego pliku poza dozwolonym katalogiem. Oryginalny konsument zwraca jego bajty jako `available`. Kontrole `fstat` sprawdzają typ i stabilność już otwartego pliku, lecz nie wiążą go z wcześniejszą zgodą. Testy D4-18A/B. To deterministyczna reprodukcja warunku współbieżności, nie odczyt prywatnego pliku. Bramka wymaga odrzucenia podmiany i niewydania bajtów spoza uprawnień; dalszy wariant obejmuje zmianę katalogu nadrzędnego.

Dokładne pliki, linie, wpływ, alternatywy, konsumenci, kryteria migracji i zależności są w [findings.jsonl](findings.jsonl) i [handoff.json](handoff.json). Kody stanów, klucze kontraktu, ZIP/JSON Pointer oraz nazwy algorytmów uznano za mechanizmy mieszczące się w R42. Problemy dotyczą zachowania, nie samych literałów.

## Co działa w zmierzonym zakresie

- Dołączenie brakującego nieznanego źródła wykonuje **0 odczytów**, tworzy jedną referencję i **0 pól**. Taka referencja przechodzi rzeczywisty native zapis i ponowne otwarcie.
- Wybrany fragment staje się adresem grafowym bez kopiowania źródła. Zmniejszenie zakresu projekcji nie odbiera możliwości późniejszego odczytu innych pól. Niepełny indeks i projekcja mają status `partial`.
- Zmiana kolejności kluczy, whitespace i BOM zachowuje strukturę JSON; hash surowych bajtów pozostaje osobny. Kolejność wiadomości jest zachowywana i nie została uznana za obojętną. Duża liczba całkowita pozostaje dokładna; nie uogólniano równoważności `1` i `1.0`.
- Plik, wybrany członek ZIP w ZIP i jawne osadzenie dają taką samą strukturę dwóch wiadomości, role i źródłową wartość parent. To **równość składniowej struktury**, nie dowód zakończonego importera domenowego.
- Pełny odczyt drzewa i selektywny odczyt, zimny i rozgrzany cache zachowują wybrany wynik na niezmiennym źródle. Cache, indeks i zapis snapshotu są niezależne w przebadanych konfiguracjach. Wersja snapshotu wykrywa zmianę źródła; jawnie osadzony snapshot działa offline.
- Nieudany odczyt fragmentu zachowuje wcześniejszy indeks. Wstrzyknięte przerwanie otwarcia deskryptora zwraca niedostępność; następny rzeczywisty odczyt działa. Nie jest to dowód wznowienia trwałego indeksu po restarcie.
- Niejednoznaczne rozpoznanie składni JSON/CSV zachowuje obie propozycje bez awansu do interpretacji domenowej ani rozszerzenia uprawnień. Brak implementacji adaptera daje poprawne `adapter_unavailable`.
- Rzeczywista projekcja D → GraphPacket → `NativeGraphStore` → C ABI → SQLite → zamknięcie/otwarcie → replay zachowuje wybrane pola, w tym parent wiadomości. Po usunięciu syntetycznego oryginału replay odtwarza zatrzymany packet. Nie twierdzimy, że odtwarza niematerializowaną resztę źródła.

Własne `resources`, `_cache` i `_index` D są stanem sesji dostępu. Trwałość korzysta z istniejącego `GraphPacketStore`/`KnowledgeStore`; nie znaleziono tu drugiej bazy, drugiego resolvera ustawień ani workflow engine. Parser jest dostępny headless i nie mieszka wyłącznie w rendererze. `overlay` jest propozycją, a write-back nie jest obiecywany. Samo parsowanie nie daje prawa bezstratnego zapisu.

## Otwarte granice, bez atrapy integracji

Dokumenty D trafnie opisują większość z nich jako następny krok. Publiczne `discover()` na tym SHA faktycznie kończy się `ModuleNotFoundError`, ponieważ moduł `.discovery` nie został opublikowany; osobny [probe](discovery-probe.json) utrwala tę obserwację. To **brak implementacji domenowego discovery**, nie błędna ocena poprawnego `executor unavailable`.

Brakuje opublikowanego konsumenta pełnego importu rozmów kontra referencja D, automatycznego dispatch z katalogu B, odtworzenia resolvera z natywnego packetu referencji, połączenia pola z rzeczywistym resolverem profilu B, publicznej zmiany lokalizacji przy przypiętej wersji i trwałego checkpointu indeksowania. Te bramki nie zostały zastąpione własnym kodem audytora.

Adapter syntax na żądanie czyta/par­suje **cały ograniczony dokument lub wybrany członek archiwum**, choć materializuje wyłącznie żądane węzły. Zero-read attach i wybiórcza materializacja są dowiedzione. Seekable JSONL i fragmentowe byte I/O wymienione jako dalszy etap nie występują w przypiętym drzewie. To precyzyjny limit dowodu, a nie żądanie obsługi dowolnego formatu.

E opublikowano podczas pass4. [Rzeczywisty packet D](D-E-packet.json), [deskryptor](D-E-source.json) i [przepis generatora](D-E-generator.json) przekazano wewnętrznemu podzadaniu audytu E do oddzielnego odbioru D → native → E. Nie jest to wiadomość wysłana sesji produktowej E. Lokalny D4-B07 oznacza brak dowodu w samym przebiegu D; aktualny wynik wspólnej granicy jest w module `E-perspectives`, szczególnie mapowanie `attrs.selector/source_version` na selektor i wersję E.

## Pokrycie i wznowienie

Przejrzano nazwane funkcje **6 nowych modułów Python D** oraz 3 pliki danych polityk; zakresy i luki są w [coverage.json](coverage.json). To rozszerzenie gałęzi D, nie przyrost do niezmienionego mianownika 432 plików main ChatADHD ani deklaracja kontroli wszystkich zachowań tych modułów. Istniejący `graph_store.py` wykonano ponownie na nowym wejściu D bez dodatkowego kredytu pokrycia. YAML/XML i HTTP mają przegląd źródła, a nie pełną niezależną macierz runtime tego modułu.

Następny odbiór: uruchomić ten sam runner na SHA naprawy A4-D-001/002, zachowując pozytywne przypadki; następnie podłączyć D4-B01/02/06 do rzeczywistych nowych konsumentów. Nie ponawiać zamkniętych prób bez zmiany kodu, warunków lub hipotezy. [REPRODUCE.md](REPRODUCE.md) i [test-index.json](test-index.json) podają wykonywalne wejścia.
