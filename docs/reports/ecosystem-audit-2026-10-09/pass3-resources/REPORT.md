# Zasoby zewnętrzne i discovery — wykonany etap audytu A

2026-10-09. Ten etap wykonuje [doprecyzowanie właściciela](OWNER_CLARIFICATION.md) jako uszczegółowienie R15/R20/R21. Nie zastępuje wcześniejszych wymagań ani nie oznacza wdrożenia funkcjonalności. [Sześciostopniowy pakiet kontynuacji](../pass2/REPORT.md) został wcześniej opublikowany i zweryfikowany: 307 plików zgodnych z manifestem; pierwszy przebieg oraz jego dowody zachowane.

Sprawdzono istniejących konsumentów i uruchomiono testy na danych syntetycznych. Produkt, wspólne schematy, główne STATE/INDEX, `main` i gałęzie B/C pozostały nietknięte. Płatne wywołania modeli: 0; płatne CI: 0. Prywatne archiwa i repo nie były wejściem tego etapu.

| Źródło | Przypięty SHA | Faktyczny zakres |
|---|---|---|
| chatadhd main | `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` | Native katalog/import/ekstrakcja, profile, registry, Python importer, web konsumenci |
| B2 | `384c5e1686cd3a58a7d89a8a6813a18c764f697d` | Ponowne native resource i Python/web testy; discovery porównane źródłowo |
| C2 | `b9b503f62bb8e8e00c94ab7401e1cd9579129af3` | Publiczny method graph przez native store i istniejący MethodRegistry |
| Watchdog-JH16 main | `58a0c93bd0135e3715dcbc4d92fb80e61bd31215` | Mapowanie danych, źródła i snapshoty projektów, rzeczywisty zapis/odczyt, wspólny renderer SSR |

Wykorzystane biblioteki/buildy są opisane w receipts. Web na B2 używa istniejącej biblioteki B `ddcaeaf7a25b70f0d4dc2e0eed2466016233eeb8`, ze sprawdzeniem zgodności badanych jednostek GraphPacket. Native B2 linkuje sześć rzeczywistych przebudowanych obiektów przed niezmienionym archiwum B i sprawdza mapę linkera. Nie deklarujemy pełnego nowego builda B2. Discovery wykonano na main; zgodność źródeł z B2 nie jest przedstawiana jako drugi przebieg.

## Co jest już zaimplementowane i zostało sprawdzone

- `Catalog::read_unit` czyta fragment ZIP z selektorem i weryfikacją hashy. Rzeczywisty `extract::run_stage` działa bez UI i zapisuje do KnowledgeStore identyczne cztery obserwacje w trybach copy/link, z rolami, parent/node/branch/current i pochodzeniem. Parser nie jest ograniczony do renderera.
- Przy zmianie lub utracie syntetycznego źródła native zwraca jawny błąd i zachowuje wcześniejsze dane. Nieudana ponowna ekstrakcja nie czyści dawnych obserwacji. Copy snapshot działa offline; ponowny skan zachowuje historię wersji.
- Zewnętrzne pliki `net.pack` zmieniają timeout rzeczywistego żądania i polityki transportowej. Osobno aplikacyjny profil JSON przechodzi rzeczywisty native zapis, zamknięcie i odczyt obu rewizji, a potem steruje istniejącym workflow/adapterem/encoderem HTTP. Końcowy transport jest przechwycony. To dwie działające części, jeszcze bez wspólnego grafowego resolvera pól.
- Rejestr metod jest konsumowany przez ContextEngine. Dwa profile zmieniają faktyczny wynik wyboru i hash. Deklaracja fikcyjnej możliwości nie instaluje executora. Embedding adapter rozróżnia implementację i zgodę; brak zgody daje zero wysyłki.
- Watchdog zachowuje sprawdzone kopie źródeł, historyczne bajty eksportu i działa po ponownym otwarciu SQLite. Ten sam FigureCanvas jest użyty przez rzeczywisty eksport SSR. To nie browser E2E ani ogólny resolver referencji ZIP.

## Potwierdzone problemy i pakiety dla B

Dziewięć nowych potwierdzonych ustaleń obejmuje konkretne różnice wykonania i ograniczone luki spełnienia kontraktu. Nie stwierdzono, że są nowymi regresjami B2.

| ID | Dowód / skutek | Odbiór |
|---|---|---|
| CH-RES-N001 | Native link zapisuje placeholdery dla konsumenta rozmowy, mimo działającej osobnej ekstrakcji grafowej | Ta sama rozmowa i relacje dostępne po imporcie i przez referencję |
| CH-RES-N002 | Catalog full-copy zachowuje teksty, lecz gubi obie relacje parent; pełny importer je zachowuje | Wybór storage nie zmienia struktury rozmowy |
| A3-WEB-001 | Tożsamość profilu jest w attrs, ale wnętrze workflows/presentation pozostaje w `raw_source` | Grafowo adresowalne pola i rzeczywisty konsument tych pól |
| A3-DISC-001 | MethodRegistry odrzuca równoważną kolejność kluczy; publiczny packet C2 przyjęty przez store nie przechodzi load registry | Równoważne DTO akceptowane, nieznane/utracone pola nadal jawnie odrzucane |
| A3-DISC-002 | Kolejność kluczy nieznanego eksportu wybiera jedną z dwóch wiarygodnych interpretacji | Alternatywy i jawna polityka mapowania, z zachowaniem raw i niepewności |
| A3-IMP-CH001 | Starszy Python importer spłaszcza gałęzie, role i powiązania źródła | Pełna projekcja albo sprawdzalna referencja zachowująca te informacje |
| A3-IMP-CH003 | Nieznany JSON wygląda jak pusty import; błędy części ZIP nie są strukturalnym wynikiem | Rozróżnienie pusty/nieznany/częściowy/niedostępny |
| A3-IMP-CH004 | Ten sam obsługiwany kształt JSON daje 1 rozmowę, a po przekroczeniu 5 MB daje 0 | Strategia strumieniowa zachowuje znaczenie dokumentu |
| A3-IMP-CH006 | HTML user/assistant/user/assistant staje się user/user/assistant/assistant | Zachowana kolejność źródłowa i jej pochodzenie |

Każdy pakiet zawiera SHA, zakresy, reprodukcję, kryterium akceptacji, konsumentów, migrację, ryzyko i zależności: [backlog](backlog.json), [findings](findings.jsonl). Brak nowego API ma status BLOCKED; testy nie implementują zastępczego silnika.

Problem C2 na granicy MethodRegistry jest **częściową integracją**, a nie dowodem nieważności całego research packetu. Native GraphPacketStore przyjmuje ten sam packet. Kontrolny roundtrip przez aktualne DTO usuwa odrzucenie; dodatkowo zmienia 57 dokładnie równowartościowych reprezentacji `1` na `1.0`, bez utraty pól. Osobne testy permutacji entity i claim zachowują identyczne canonical bytes i nadal zawodzą. Kontrola przyczyny nie jest naprawą produktu. Brak executora zapisanej metody C pozostaje prawidłowo unavailable; discovery nie dostało uprawnienia do uruchomienia kodu.

## Wyniki testów — bez sumowania powtórek jako nowych przypadków

| Zestaw | Zakres | PASS | FAIL | BLOCKED |
|---|---|---:|---:|---:|
| Native zasoby | osobno main i B2 | 16 | 2 | 3 |
| Web profil + native store | osobno main i B2, finalne receipts | 9 | 0 | 3 |
| Python importer | osobno main i B2 | 8 | 3 | 5 |
| Watchdog zasoby | main | 10 | 0 | 3 |
| Discovery / C2 → native registry | wykonanie main, dane C2 | 14 | 5 | 1 |

Native PASS obejmuje 10 akceptacji, pięć obserwacji zachowania i jedną reprodukcję. Web: osiem akceptacji i jedna reprodukcja. Python: trzy pozytywne akceptacje i pięć reprodukcji. Cztery discovery FAIL dotyczą jednego problemu DTO, nie czterech osobnych błędów. **PASS reprodukcji nie oznacza PASS produktu.** Historie korekt oracles i wcześniejsze receipts pozostają dostępne; ich wyników nie doliczono ponownie.

## Pierwszy pion i granice

- **V01:** pełna równość rozmów/relacji nie przechodzi na native consumerach; współdzielony surowy reader i headless observations działają. Nested ZIP sprawdzono w Pythonie tylko dla obecnej projekcji tekstu; pełny natywny nested graph pozostaje niezweryfikowany.
- **V02:** katalogowy preview i zadanie używają `read_unit`. Preview nie udostępnia całego wnętrza rozmowy; wspólny kontrakt sparsowanej rozmowy dla widoku i workflowu pozostaje BLOCKED. Przeglądarkowego E2E nie powtarzano po udokumentowanej blokadzie środowiska.
- **V03:** rzeczywisty wpływ plików profilu na runtime oraz zapis profilu w grafie są dowiedzione osobno. Złożenie graph-field → wspólny resolver → runtime wymaga B.
- **V04:** raw i nieznane metadane są zachowywane w części native ścieżek; ścisłe schematy wykonawcze odrzucają nieobsługiwane pola. Pełne katalogowanie nieznanych pól oraz alternatywnych niepewnych mapowań nie jest zamknięte.
- **V05:** wybrane native scenariusze zmiany/braku źródła i trwałość snapshotów przechodzą. Pełny live watch, źródła zdalne, równoczesne zmiany i odtwarzanie dostępu pozostają otwarte.

Niezależne osie osadzenie/referencja/oba, eager/lazy, cache/indeks, snapshot/live i readonly/overlay/write-back zachowano w [kontrakcie V01–V12](CONTRACT.md). Nie wymagamy wszystkich bajtów w jednym storage ani write-back tylko dlatego, że źródło jest parsowalne. Cztery aspekty discovery — deklaracja, implementacja, dowody i zgoda — mogą korzystać z istniejących facets; audyt nie narzuca nazw nowych pól.

Przyrost wynosi **51** nowych plików z nazwanymi zakresami: chatadhd **51→94/432**, Watchdog **46→54/266**. Łącznie w zachowanym mianowniku publicznym **267/1027**, nadal **760** bez zakresów. Pozostałe liczniki: CKP 47/233, AGEDS 39/63, LEM 24/24, historyczny standalone loom 9/9. To nie kontrola całych plików. Dokładne przyrosty względem wcześniejszego mianownika plików są w [coverage-index.json](coverage-index.json). Mierzą pliki z nazwanymi zakresami, a nie pełną kontrolę wszystkich ich zachowań. Pozostałe repo i wcześniejsze luki pozostają w pass2; standalone loom nie stał się backlogiem aktywnego kernela.

## Uruchomienie i wznowienie

[REPRODUCE.md](REPRODUCE.md) i [wykonywalny indeks](../../../../tools/ecosystem-audit-2026-10-09/pass3-resources/run_index.py) prowadzą do pięciu zestawów. Indeks wykonano: po nieudanej akceptacji kontynuował reprodukcje; pozostałe BLOCKED nadal dają niezerowy wynik. To nie osłabienie bramki.

B powinien zacząć od istniejącego `read_unit`, strukturalnej ekstrakcji i pełnej projekcji importera, połączyć ich konsumentów, następnie podłączyć wersjonowane pola profilu i mapowania do tych samych kontraktów metod. Równolegle może naprawić izolowane błędy MethodRegistry i starszego importera. A ponawia te same testy na SHA poprawki oraz wiąże oznaczone BLOCKED z nowym rzeczywistym API. Szczegółowy punkt wznowienia: [RESUME.md](RESUME.md). Nie oczekiwano bezczynnie na nowe commity B/C.
