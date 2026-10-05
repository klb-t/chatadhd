# Wątek 11 — końcowe przypisanie statusów inwentarza

[Zbiorczy JSON](migration-status.json) obejmuje **213/213 pierwotnych pozycji wątku 11**, bez luk i dodatkowych ID. Każda pozycja ma aktualny cel danych, kotwicę pierwotnego inwentarza, odwołania do obecnego kodu i opis zachowanych kontraktów lub brakującego okablowania. Inwentarz całego repo zachowuje wszystkie bazowe pola, a przy 213 pozycjach dodaje oddzielne pola `current_migration`; bazowy wynik pozostaje: **695 grup, 559 plików, 39 496 mechanicznych kandydatów**, ukończonym przed edycjami. Późniejsze dodatki gałęzi wątku 8 nie zwiększają tych liczb.

| Status po przeglądzie | Grupy |
|---|---:|
| Dane przeniesione i konsumowane w wskazanej ścieżce | 187 |
| Częściowe: konkretny brak okablowania lub zachowany fragment kontraktu | 19 |
| Zachowany kontrakt formatu | 5 |
| Zachowany algorytm wykonawczy | 1 |
| Edycja przekazana innemu właścicielowi | 1 |
| Razem | 213 |

Nie zaliczono dostępności API do pełnego podłączenia wszystkich konsumentów. DIC-0055–0059 mają nowe dane `model.creation_defaults`, checked `normalize_creation`, siedem obsługiwanych rodzajów, jawne pochodzenie producenta i osobny receipt zastosowanych priors/hash. Historyczne dekodery pozostają zgodne; obecne zewnętrzne konstruktory nie korzystają jeszcze z tego API, więc wszystkie pięć pozycji pozostaje częściowe. Trzy nowe domeny `runtime_paths`, `config`, `usage_policy` są osobno oznaczone jako gotowe dane z oczekującym okablowaniem rdzenia.

## Co pozostaje częściowe i dlaczego

| ID | Pozostała praca lub zachowany kontrakt |
|---|---|
| 0004, 0008 | SemanticLLM nadal używa builtin analyzer/static conversion; efektywny profil w merge/fallback należy do wątku 1. |
| 0022 | Historyczne ustawienia Config wygrywają z fallbackami grafu; potrzebne rozróżnienie presetu i jawnego wyboru oraz policy preflight. |
| 0026, 0028, 0033 | Oba parsery JSON respektują obecność pól; legacy C++ DTO pozostaje jawne. Dowolne kombinacje metod, ważenie i ślad wyniku wymagają selektora wątku 3 oraz okablowania Runtime. |
| 0053, 0054, 0060 | Dane i checked przeciążenia modelu istnieją; wywołania resolve/generalize/context/kb nadal korzystają ze starych builtin wrapperów. |
| 0055–0059 | Dane i normalizacja tworzenia istnieją; producenci z innych zakresów jeszcze niepodłączeni. |
| 0099 | Clip jest ustawieniem; stabilny ID z prefiksem i szerokością hasha pozostaje kontraktem zapisu. |
| 0108, 0114 | Rozszerzenia i wielkość strumieniowego chunk są ustawieniami; zaufane parsery ZIP/JSON oraz fallback pełnego wrappera czekają na wspólny import wątku 5. |
| 0176 | Tokens/chars są ustawieniami; prompt i preview/hash/per-call composer należą do wątku 1. |
| 0177 | Flat koszt wiadomości jest konfigurowalnym szacunkiem; brak model/token/timestamp rozliczenia rzeczywistego. |

DIC-0011 pozostaje przekazane wątkowi 1: branding requestu SemanticLLM. Zachowane formaty to DIC-0072/0115/0130/0198/0199, a DIC-0171 opisuje faktyczne sześć etapów i ich zależności wykonawcze. Opisy pozycji mieszanych nie przedstawiają zachowanego protokołu jako nowej konfigurowalnej polityki.

## Dowody i granice weryfikacji

DIC-0158 domknięto dziewięcioma ustawieniami lexera, przycinania, markerów, wykluczeń i dopasowania znanych URI po sufiksie; rozszerzenia nadal czyta z danych. Dawne reguły lokalnych odnośników są heurystyką raportu, nie kontrolą dostępu ani rozwiązywaniem ścieżki. Silnik nie otwiera wskazanego pliku i nie pobiera URL.

Końcowe delty archiwum zostały nałożone na pierwotny przegląd 107 pozycji, następnie delty tekstu/słownictwa, syntezy i pięciu pozycji modelu. Dzięki temu starszy przegląd nie przywraca oznaczeń zamkniętych już luk. JSON zawiera hashe przeglądanych źródeł i wszystkich 19 kanonicznych packów; sam HEAD nie identyfikuje jeszcze niecommitowanych końcowych zmian. Sprawdzono komplet ID, istnienie plików/kotwic linii i zgodność sumy statusów.

[Raport analyzer/graph/materialize](implementation-semantics.md) pokazuje zachowane **9 990 B przed/po**, SHA256 `cf3c0ad78fe3660e49e6e5b7fa41c7465cf6ac16b6ec6ac5bd44a56ddd9688e3`, oraz **8/8 przypadków, 64/64 asercje** testu zakresu. Nie zastępuje to dwóch przypadków prawdziwego Runtime. Nowa regresja właściwego etapu materializacji z `dossier_header={{missing}}` sprawdza `complete:false`, dwa jawne `omitted_products` i cztery pozostałe produkty; jest częścią pełnego zestawu consumer. Bez błędów shape/hash wyniku materializacji pozostaje zachowany.

[Domknięcie tekstu archiwum](implementation-archive-text-policy.md) ma zgodność z dokładną bazą 161cc22: **49 458 B przed/po**, **9/9 przypadków, 94/94 asercje**. [Domknięcie syntezy](implementation-archive-synthesis-policy.md) ma **142 841 B przed/po**, **8/8 przypadków, 235/235 asercji** w [końcowym receipt](evidence/archive-source-references-policy/receipt.json) wobec zamrożonej syntezy przed tym domknięciem i tych samych helperów. [Nowe creation API modelu](implementation-model-creation-policy.md) przeszło **6/6 przypadków, 99/99 asercji**, obejmując 16 domyślnych pól, zachowanie danych producenta i walidację priors. Są to niezależne dowody zakresu; liczb nie sumuje się z pełnym CTest ani z wcześniejszymi receipts.

Pełny aktualny build, `ctest`, web i before/after z Runtime prowadzi integrator; [receipt końcowej bramki](evidence/final-validation/receipt.json) jest osobny od klasyfikacji migracji źródła. Testy nie zamykają19 jawnych pozycji częściowych ani zewnętrznego okablowania. Nie edytowano zakresów innych wątków w celu ukrycia tych braków. Płatnych wywołań nie wykonano.

Rzeczywisty CTest przeszedł **122/122**, a Runtime consumer **10/10 przypadków,116/116 asercji**. Pełny [probe analyzer/graph/materialize](evidence/fullcore161/receipt-after-semantics_graph_materialize.json) zachowuje **11 766B przed/po i4 produkty**, SHA256 `0db70356e9c30dfb937353908f2f6c79f464215f386d1a4fad5a5f86768b787d`; wcześniejsze9 990B to osobny zakres CORE_ONLY. Niezmieniony guard wątku8 potwierdził [776 przypadków native,26 758 asercji i1 276 przypadków Python](evidence/final-validation/coverage-full.json), zero pominięć Python, z jawnym niewykonanym `unit.test_catalog_scale`0/0. Oryginalny [pozytywny JUnit](evidence/final-validation/ctest.xml) i [negatywny wynik guard](evidence/final-validation/coverage.json) pozostają zachowane. Guard zaliczył pochodny [XML pełnych wyjść](evidence/final-validation/ctest-full-output.xml): [offline receipt](evidence/final-validation/output-restoration-receipt.json) dowodzi odtworzenia16 obciętych prefiksów ze strumieni tego samego pełnego logu,122 zgodnych mapowań i niezmienionych statusów/czasów. Nie jest to nowy CTest run.

## Do wątku N

- **Do wątku 1:** podłącz efektywny analyzer w SemanticLLM, branding/prompt composer, model policy przeciążenia i creation normalizer przy nowych rekordach; zachowaj prawdziwe origin/evidence, a priors raportuj jako priors.
- **Do wątku 2:** podłącz trzy kanoniczne domeny startowe zgodnie z `runtime-bootstrap-profile-handoff.md`, rozróżnienie Config preset/jawne ustawienie i preflight ×10. Profil/hash nie stanowi zgody na kosztowną operację.
- **Do wątku 3:** przejmij kanały/parametry i checked propagację błędów, dodaj rejestr metod/kombinacje/ślad oraz aktywny profil Runtime; zachowaj znaczenie pominiętych pól i jawnego zera.
- **Do wątku 4:** użyj creation normalizera w ścieżkach nowych rekordów grafu; historyczne dekodery pozostają zgodne.
- **Do wątku 5:** domknij wspólną strumieniową obsługę wrapperów i rejestr obsługiwanych formatów; DIC-0114 nie jest oznaczone jako pełny streaming wszystkich eksportów.
- **Do wątku 7:** flat koszt z DIC-0177 pozostaje oznaczonym szacunkiem; przekaż modelowe przepisy/cenniki z datą i rzeczywistym rozliczeniem bez paid calls poza twoim budżetem.
- **Do wątku 10:** pokaż schema/values/hash oraz błędy; wynik materialize z `complete:false` musi eksponować każdy receipt pominięcia. Nie przedstawiaj legacy DTO ani niepodłączonych domen bootstrapu jako aktywnych ustawień globalnych.
- **Do wątku 9:** zachowaj 19 pozycji częściowych w INDEX i przypisz ich okablowanie właściwym właścicielom; pełne bramki integracji i zgodność Runtime raportuj osobno. Po końcowym commitcie można odświeżyć snapshot hashów JSON bez zmiany klasyfikacji.
