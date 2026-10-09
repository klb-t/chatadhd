# Zasoby zewnętrzne — wykonany audyt native

Źródło wymagań: cytat właściciela w `../OWNER_CLARIFICATION.md`, uszczegółowienie R15/R20/R21. Opis poniżej oddziela istniejące mechanizmy od brakujących kontraktów; nie wprowadza drugiego silnika.

Zbadano main `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` oraz B2 `384c5e1686cd3a58a7d89a8a6813a18c764f697d`. Na obu: **21 kryteriów — 16 PASS, 2 FAIL, 3 BLOCKED**. Składają się na to: akceptacja 10 PASS/2 FAIL, pięć udanych obserwacji zachowania, jedna udana reprodukcja różnicy i trzy brakujące kontrakty. PASS reprodukcji nie oznacza PASS produktu. Każde SHA wykonano w 32 osobnych procesach natywnego drivera; 28 uruchamia Runtime, cztery bezpośrednio badają profil/transport policy. Rzeczywista wysyłka jest zablokowana; liczba prób transportu: zero.

## Co działa i nadaje się do wykorzystania

`Catalog::read_unit` naprawdę czyta ZIP member i wybrany fragment JSON, weryfikuje hash, korzysta z zachowanych bajtów po kopiowaniu. Surowe jednostki rozmów są identyczne dla copy/link. `extract::run_stage` korzysta z tego czytnika bez UI, parsuje strukturalny JSON i zapisuje cztery identyczne `KnowledgeStore` observations w obu trybach. Obserwacje zawierają teksty, role, pochodzenie, selektory oraz atrybuty node/parent/branch/current. To realny odczyt i zapis grafu, nie sam codec ani obietnica.

Zmiana oryginalnego ZIP daje jawny `Conflict`, zniknięcie — `Io`; dane rozmowy pozostają bez zmian. Ponowne zadanie ekstrakcji po utracie źródła odmawia odczytu **przed** czyszczeniem wyniku i zachowuje cztery poprzednie obserwacje. Copy nadal odczytuje snapshot bez źródła. Skan zmienionego źródła zachowuje stare jednostki i tworzy dwie nowe wersje z `prev_version`. Pełny importer zachowuje syntetyczne nieznane pola rozmowy/wiadomości w metadanych.

Widok katalogu ma wspólną część odczytu: `catalogPreview` → HTTP route → C API → `Catalog::preview` → `read_unit`. Wynikiem są `verified` i fragmenty, a nie sparsowane wnętrze rozmowy. Natywny `preview` wykonano; przeglądarkowego renderera w tym pakiecie nie uruchamiano. Nie wolno z tych wyników wywodzić braku headless parsera ani pełnej zgodności UI.

## Potwierdzone różnice konsumentów

**CH-RES-N001 / RES-NATIVE-001:** ten sam ZIP daje w pełnym `ConversationImporter` dwie rozmowy, cztery wiadomości i dwie relacje rodzic–dziecko. `Catalog::import_selected(mode=full, store_mode=link)` zapisuje dwa placeholdery rozmów i trzeci placeholder nieznanego pliku. `Database::get_msgs` zwraca placeholdery. Prześledzony klient getMessages i route HTTP delegują do tego samego odczytu; pętla historii ChatEngine też czyta ten tekst (jej wysyłki nie uruchamiano). Headless graf obserwacji działa osobno; brakuje wspólnej projekcji dla konsumenta rozmowy. Obecny skrót referencji może pozostać opcją prezentacji, lecz nie powinien ograniczać dostępu do sparsowanej struktury.

**CH-RES-N002 / RES-NATIVE-002:** catalog full-copy zachowuje wszystkie cztery teksty, lecz zapisuje `parent_id=null` także dla obu odpowiedzi. Pełny importer zachowuje obie relacje. Oryginał ZIP i jego surowe jednostki pozostają dostępne; ustalenie dotyczy utraty relacji w projekcji, nie zniszczenia surowych bajtów. Wariant płaski może pozostać jawną strategią, oddzielną od wyboru miejsca przechowywania.

Oba zachowania występują na main i B2. Nie są nową regresją B2. Dokładne lokalizacje, hashe zakresów, alternatywy, ryzyka migracji i testy są w `findings.jsonl` oraz `handoff-B.json`.

## Profil zewnętrzny i brakujące bramki

Zwykły `profiles/net.pack` naprawdę wpływa na `HttpRequest::from_json_with_profile` i `HttpTransportPolicy::for_request`: dwa pliki ustawiają timeout odpowiednio 701 i 1701 ms, zmieniają hash efektywnej receptury. Nieznany klucz operacyjny daje jawny błąd bez naruszenia pliku. Gdy opcjonalnego pliku nie ma, obecny loader wybiera builtin — to obserwacja istniejącego kontraktu opcjonalnej nakładki, nie dowód obsługi powiązanego z grafem zasobu live/snapshot.

Trzy bramki pozostają **BLOCKED**, a nie FAIL atrap:

- wspólna sparsowana projekcja rozmowy dla widoku i zadania, ponad już wspólny `read_unit`;
- arbitralny profil plik/ZIP → wersjonowany zasób grafu → rzeczywisty konsument runtime; istnieje tylko węższy loader profilu w stałym lokalnym katalogu;
- wspólny kontrakt niezależnych polityk osadzenia/referencji/obu, eager/lazy, cache/indeks, snapshot/live i readonly/overlay/write-back wraz z mapowaniem. Obecne locatory, source hashes, copy/link i walidacja profili są komponentami do wykorzystania, nie dowodem pełnego kontraktu.

Nie testowano zagnieżdżonych ZIP-ów, transportu zdalnego, równoczesnej mutacji pliku ani całego zbioru możliwych formatów. Niejednoznaczne discovery jest osobnym zakresem C; zachowanie nieznanego pola nie jest samo w sobie dowodem zachowania alternatywnych interpretacji.

## Pochodzenie wykonania i wznowienie

Main używa istniejącego pełnego archiwum `loom_core` z przypiętej bazy. B2 jest jawnym montażem sześciu rzeczywistych obiektów przebudowanych przez audyt WD z B2, połączonych **przed** niezmienionym archiwum B `ddcaeaf7…`. Runner sprawdza SHA źródeł/obiektów manifestu oraz mapę linkera, która wyklucza pobranie zastąpionych starych obiektów. To nie jest deklaracja pełnego builda/CTest B2. Manifest i dokładne komendy są w receipt; surowe źródła produktu pozostają nietknięte.

Pokrycie: 20 jawnie wymienionych plików i zakresów w `coverage.json`; nie nazywamy ich całymi plikami w pełni skontrolowanymi. 19 ścieżek jest nowych względem samego modułu `pass2/native`; root musi policzyć sumę z innymi modułami, zanim użyje tej liczby dla repo.

Punkt wznowienia B: wykorzystać obecny zweryfikowany reader i pełną projekcję importera, wystawić rzeczywisty wspólny kontrakt i podłączyć go do konsumentów rozmowy/profilu. Ponowić niezmienione akceptacje N001/N002 i zachować pozytywne bramki trwałości/ekstrakcji. Dopiero po wystawieniu kontraktu zmienić trzy BLOCKED na uruchamialne testy end-to-end. Żaden produktowy plik ani wspólny schemat nie został zmieniony.
