# Przegląd projektu: strażnik kontekstu, wspólna warstwa adnotacji, środowiska i agent (2026-10-15)

Przegląd projektowy (nie wdrożenie) względem `STRAZNIK_I_SRODOWISKA_2026-10-14.md`, sekcji P21 w `docs/reports/INDEX.md`
i tego, co faktycznie zbudowano (`loom/src/guardian/*`, `loom/data/runtime/guardian.pack`, `ChatEngine::select_representation`).
Nadrzędne pozostają `GRAF_HUB_KOD_SILNIK_2026-10-13.md` i R1–R56. Zasada czytania: przykłady właściciela to przęsła,
most ma być ogólny; wszystko konfigurowalne, presety to nie reguły; graf to hub, kod to silnik; wszystko z okna rozmowy (R54).

Skrót werdyktu: kierunek jest dobry (sekretarz obok surowej listy, nic nie znika, selektor decyduje o wysyłce, zadania
jako dane), ale w pierwszej wersji **przykłady stały się zakresem** w trzech miejscach (rodzaje sygnałów, kategorie szumu,
sposób łączenia wystąpień w hipotezę), warstwa adnotacji jest dziś **jednoproducentowa** i oderwana od analizatora, a strona
„środowiska + agent” nie ma jeszcze ani jednego pojęcia w grafie. Poniżej: słabe punkty z poprawkami, czysty model warstwy
adnotacji (razem z pracą GPT), model środowisk i agenta, oraz kolejność kroków z kryteriami.

---

## 1. Słabe punkty strażnika — i poprawki

### 1.1 Hipoteza to dziś cytat wiadomości, nie twierdzenie

Przy metodzie heurystycznej treść hipotezy to pierwsze 400 znaków wiadomości (`guardian.cpp:298`), wstawione w szablon
„Użytkownik chce odtąd: {{text}}” (`guardian.pack:324–329`). Czyli „zasada”, którą strażnik zaproponuje po trzech
poprawkach, brzmi jak surowa wiadomość użytkownika z całym kontekstem chwili („nie, nie tak, zrób nagłówki krótsze i
wróć do poprzedniego pliku”). To nie jest „najbardziej oszczędna i precyzyjna reprezentacja” — to kopia.

**Poprawka (poziom mechanizmu):** hipoteza ma mieć *treść znormalizowaną* oddzielną od dowodu. Trzy stopnie, wszystkie jako
dane w `kinds.<rodzaj>.hypothesis`: (a) heurystyka zostawia pole `claim` puste i hipoteza jest „nienazwana” — pokazywana
użytkownikowi z cytatami dowodów, a nazwę nadaje on sam albo model przy awansie (jedno wywołanie `use_model` *dopiero przy
propozycji*, nie przy każdej wiadomości — koszt zero do momentu, gdy coś jest warte pytania); (b) model przy odczycie
daje `subject` (już jest: `guardian.cpp:204`); (c) użytkownik może sam napisać zasadę w rozmowie („zapamiętaj, że…”) —
to też wystąpienie, z siłą 1 i ze źródłem `user`. Treść hipotezy = pole wersjonowane (jak każda adnotacja, §2), a nie
pierwsze zdanie, jakie padło.

### 1.2 Łączenie wystąpień w jedną hipotezę działa na pokrywaniu się słów

Nowe wystąpienie dołącza do hipotezy, jeśli ≥50 % tokenów się pokrywa (`guardian.cpp:300, 324–340`; `weighting.overlap_min`
w pakiecie). Skutek: ta sama poprawka powiedziana innymi słowami („krócej”, „za długie”, „skróć”) tworzy trzy osobne
hipotezy o wadze 1 i próg 2,0 nigdy nie zostaje osiągnięty — czyli *najważniejszy przykład właściciela* („dwa razy
poprawia, potem się wkurza → zasada”) nie zadziała, chyba że użytkownik powtórzy frazę. Odwrotnie: dwie różne poprawki
dzielące rzeczowniki („plik”, „nagłówek”) zostaną sklejone.

**Poprawka:** łączenie to osobna, wymienna *metoda* (jak trasowanie P16 i tryby analizy): `overlap` (dziś), `embedding`
(istniejący selektor 3‑poziomowy `core/selector`/`loom/src/context/retrieval.cpp` — tanie, lokalne), `model`
(jedno pytanie: „czy to ta sama poprawka co X?” — tylko gdy są ≥2 kandydatki), `user` (pytanie w rozmowie). Wybór metody
i próg to ustawienie `guardian_match_method` z presetem; wynik łączenia zapisany jako krawędź `same_as` z pewnością, więc
złe sklejenie można cofnąć bez utraty wystąpień.

### 1.3 Zakres rośnie sam od wagi

`scopes.by_weight` podnosi zakres hipotezy do „osoba” przy wadze ≥6 (`guardian.pack:171–179`, zastosowane w
`guardian.cpp:238–246`). Waga rośnie od powtórzeń *w jednej rozmowie*; sześć poprawek tego samego w jednym długim zadaniu nie
mówi nic o innych rozmowach. To automatyczna ekstrapolacja poza dowody — ryzyko błędnego awansu z najgorszym efektem
(reguła „wszędzie” z jednej rozmowy).

**Poprawka:** zakres rośnie tylko od *różnorodności dowodów*, nie od ich liczby: liczba różnych rozmów, różnych rodzajów
zadań, odstęp czasu. `by_weight` zastąpić `by_evidence: [{distinct_conversations: 2, scope: person}, …]`. Jawny znacznik
(„zawsze”, „od teraz”) nadal podnosi zakres, ale jako *wystąpienie* o wadze, nie natychmiastowa decyzja — i z pytaniem, bo
to najdroższy błąd. Poza tym reguły o zakresie szerszym niż rozmowa i tak dziś nie trafiają do wysyłki (INDEX „Otwarte (1)”),
więc do czasu ich faktycznego odbiorcy awans ponad rozmowę jest ozdobą.

### 1.4 Kategorie szumu usuwają znaczenie

L1 wycina „fillery” z listy, w której są „you know”, „kind of like”, „actually no” (`guardian.pack:654–684`), a „powtórzenie”
to każde sąsiednie powtórzenie n‑gramu z `min_token_len: 1` (`guardian.pack:686–689`, `text.cpp:179–190`). „No no”,
„bardzo bardzo”, „nie, nie tak” to emfaza i *sygnał korekty* (ten sam pakiet używa „nie nie” jako znacznika korekty —
`guardian.pack:355`), a nie szum. Strażnik jedną ręką liczy sygnał, drugą go wycina. Właściciel mówił o usuwaniu tego, „co
nic poza wagą w tokenach nie wnosi” — a tu wnosi.

**Poprawka:** każda kategoria szumu dostaje *pewność z kontekstu*, nie 1,0 na sztywno (`levels.cpp:97`): powtórzenie
jednowyrazowe = 0,3, wielowyrazowe = 0,9; filler na początku zdania = 0,9, w środku frazy = 0,5. Poziomy już filtrują po
`min_confidence` (`levels.cpp:110`), więc to wystarcza, żeby L1 nie dotykał emfazy. Druga rzecz: fragment oznaczony
jednocześnie jako sygnał (korekta) i jako szum nie może zostać wycięty — konflikt kategorii to reguła warstwy adnotacji (§2.4),
nie strażnika.

### 1.5 „Jeden cykl” ma na sztywno trzy rodzaje sygnałów — a reszta przykładów właściciela nie ma miejsca

Rodzaje: korekta, doprecyzowanie, odrzucenie (`guardian.pack:314–494`). Właściciel wymienił też: zgadywanie preferencji
i *flow*, ton, akceptację, wyjątki podane implicite. Pakiet pozwala dopisać rodzaj (dobrze), ale **mechanizm rozpoznaje
tylko jeden najlepszy rodzaj na wiadomość** (`detect_heuristic` zwraca jeden `best`, `guardian.cpp:82–141`), więc
wiadomość „nie, krócej — i w ogóle zawsze pisz po polsku, ale ten plik zostaw” (korekta + preferencja osoby + wyjątek)
zostaje jedną korektą. Wyjątki są ciągane z tej samej wiadomości (`guardian.cpp:112–121`) i tylko tam.

**Poprawka:** sygnały to *adnotacje segmentów* (zakres znaków → kategoria), wiele na wiadomość; hipoteza bierze dowód z
segmentu, nie z wiadomości. To zresztą zgodne z tym, co projekt już deklaruje (wspólna warstwa adnotacji), a kod robi
inaczej: `record()` liczy wystąpienie na `occ:<wiadomość>:<rodzaj>` (`guardian.cpp:293`), czyli najwyżej jedno na rodzaj
na wiadomość. Drugi brak: *sygnał pozytywny* (akceptacja, „świetnie, tak dalej”) — bez niego hipoteza może tylko rosnąć od
negatywów, a właściciel chce optymalizować *flow i preferencje*, nie tylko zakazy. Dodać rodzaje `acceptance` i
`preference_statement` jako wpisy danych; silnik nic nowego nie potrzebuje, gdy 1.5 jest zrobione.

### 1.6 „Cel” doprecyzowania to pierwsza wiadomość mająca ≥4 słowa

Doprecyzowanie wymaga celu; cel to pierwsza aktywna wiadomość użytkownika o ≥4 tokenach (`guardian.cpp:537–546`). W rozmowie
wielotematycznej (właściciel: skoki między 2–3 tematami) każde doprecyzowanie drugiego tematu zostanie przypięte do celu
pierwszego i skompilowane w jedno polecenie z nim. To właśnie przypadek, w którym kompresja robi szkodę.

**Poprawka:** cel = najbliższy w górę segment z kategorią `instruction`/`thread` z warstwy adnotacji (wątek od analizatora
albo od samego użytkownika: „wracając do…”). Do czasu, aż ktoś pisze `thread`, doprecyzowanie bez pewnego celu dostaje
status „bez celu” i nie kompiluje się — lepiej nic niż zły zlepek.

### 1.7 Koszt: tryb `model` to drugie wywołanie modelu na każdą wiadomość

`detect_model` wysyła każdą wiadomość użytkownika (i poprzednią odpowiedź asystenta) do modelu (`guardian.cpp:166–176`).
Pod strażnikami ×10 i limitami (dobrze), ale przy `guardian_method=model` to podwojenie liczby żądań w stosunku do czatu.
Ponowne użycie adnotacji innego producenta jest zaimplementowane (`guardian.cpp:514–527`), ale **żaden inny producent
jeszcze nie pisze** (`guardian.pack:713–737`, wszystkie `planned`), więc oszczędność jest teoretyczna.

**Poprawka:** jedno wywołanie modelu na wiadomość dla *wszystkich* odbiorców (analizator semantyczny + strażnik) z jednym
kontraktem wyjścia: encje, tematy, relacje **i** kategorie segmentów. To sprowadza się do dopisania kategorii segmentów do
istniejącego kontraktu promptu analizatora (`loom/src/extract/prompt_contract.*`) i do zapisu adnotacji przez `SemanticWorker`
— a `guardian.model_read` staje się trybem awaryjnym, gdy analizator jest wyłączony. Dodatkowo propozycja zawsze
pokazuje użytkownikowi, ile wywołań strażnik zużył w rozmowie (dane są w pokwitowaniach `use_model`).

### 1.8 Prywatność: reguły o osobie i cytaty wędrują między rozmowami

Reguła awansowana na „osobę” niesie `text` = cytat wiadomości i listę dowodów (`actions.cpp:164–167`) do
`profiles/guardian.pack`. Gdy w przyszłości taka reguła trafi do każdej rozmowy (a to planowane), cytat z prywatnej
rozmowy wyjdzie do dostawcy w rozmowie innego rodzaju (np. firmowej). Druga sprawa: `detect_model` wysyła tekst do modelu
z ustawienia `guardian_model` z zapasem `default_model` — czyli strażnik może domyślnie słać do *innego dostawcy* niż ten,
którego użytkownik wybrał w tej rozmowie.

**Poprawka:** (a) reguła szerszego zakresu niesie *tylko treść znormalizowaną* (1.1), dowody zostają w grafie jako krawędzie;
(b) do wysyłki w rozmowie trafia reguła tylko, gdy jej `scope_id` i klasa widoczności (R47) na to pozwalają — ten sam
mechanizm, który ma dziś selektor dla wycinków grafu; (c) zapas modelu strażnika = model *bieżącej rozmowy*, nie
`default_model` — nowy dostawca to pytanie do właściciela (jego dane), więc preset nie może sam go zmieniać.

### 1.9 Dryf: reguła nie ma daty ważności ani sprawdzenia, czy nadal obowiązuje

Starzenie dotyczy wystąpień (półokres 336 h, `guardian.cpp:39–46`), ale awansowana reguła jest wieczna, dopóki użytkownik
jej nie cofnie. Użytkownik, który zmienił zdanie, musi *wiedzieć*, że reguła istnieje (R54 mówi „z okna rozmowy”, więc musi
dać się zapytać „jakie masz dla mnie reguły?” — akcja `guardian.status` jest, ale per rozmowa).

**Poprawka:** reguła ma cykl życia jak hipoteza: nadal zbiera wystąpienia *przeciw* (korekta przeciwna, odrzucenie) i
*za* (akceptacja); gdy waga spada poniżej progu, strażnik proponuje jej wyłączenie — ten sam cykl, odwrócony. Plus
`guardian.status` na poziomie osoby („co o mnie pamiętasz?”) — wpis w katalogu, nie kod.

### 1.10 Drobne, ale zmieniające zachowanie

- Poziomy stratne tylko dla roli `user` (`guardian.pack:563–565`); odpowiedzi asystenta — najdroższa część historii —
  nie podlegają kaskadzie. Rola to dane, ale preset powinien od razu dopuszczać `assistant` z osobnym zestawem kategorii
  (np. „powtórzenie poprzedniej odpowiedzi”, „preambuła”), bo tam są tokeny.
- Bieżąca wiadomość idzie zawsze surowa (INDEX „Otwarte (3)”) — słusznie dla L3, ale L1 (wypełniacze z dyktowania) ma sens
  właśnie dla bieżącej wiadomości głosowej. Ustawienie, nie reguła.
- Propozycja pokazuje się *po* odpowiedzi asystenta (`on_event`, `guardian.cpp:617–622`), więc odpowiedź, która wywołała
  trzecią poprawkę, już poszła bez reguły. Alternatywny preset: przy `apply` zastosować regułę do *tej* wysyłki i powiedzieć o
  tym w odpowiedzi; przy `propose` — bez zmian.
- `do_not_propose` bez odbiorcy (`guardian.pack:475–480`): odbiorca to router propozycji (`chat_route::offer`) i jest to
  jedno sprawdzenie reguły z zakresem, nie nowy mechanizm — warto domknąć, bo bez tego odrzucenia są martwą wagą.

---

## 2. Wspólna warstwa adnotacji — czysty model

Dziś: tabela adnotacji wiadomość→węzeł z zakresem znaków, pochodzeniem `recorded|model|user`, stanem `active|retracted`,
rewizją, hashem tekstu źródła i historią (`include/loom/db.h:264–284`, `src/db/annotations.cpp`). To dobra podstawa.
Pisze do niej **jeden** producent (`guardian_heuristic`, `levels.cpp:71–104`). Analizator semantyczny (`src/extract/*`,
`src/worker/*`) pisze węzły i krawędzie grafu, ale nie adnotacje segmentów. Praca GPT (rozpoznawanie struktur
niewymienionych z nazwy) pisze *pokwitowania rozpoznania* w `Instance.recognition` w KnowledgeStore (raport GPT, sekcja
„Relational recognition”) — trzeci, osobny ślad. Trzy producenty, trzy formaty, zero wspólnych odbiorców.

### 2.1 Czym jest adnotacja (jedno zdanie)

**Adnotacja = twierdzenie producenta, że fragment źródła ma kategorię (albo stoi w relacji do innego fragmentu), z pewnością,
kosztem i śladem; nigdy nie zmienia źródła; może zostać zastąpiona nowszą wersją albo wycofana.**

Pola (wszystkie już są albo mieszczą się w `metadata`):

| Pole | Dziś | Uwagi |
|---|---|---|
| źródło: wiadomość + zakres znaków | jest | rozszerzyć o *inne źródła* (plik, transkrypcja, węzeł) — zakres w jednostce źródła |
| kategoria = węzeł (`annotation:<kategoria>`) | jest | kategorie to dane; nowy producent dopisuje własne, bez zmian kodu |
| producent + metoda + wersja metody | `producer` w metadanych | dodać `method_version` (hash pakietu/promptu), żeby stara adnotacja po zmianie list nie udawała nowej |
| pochodzenie `recorded|model|user` | jest | zostaje jako oś zaufania; `user` zawsze wygrywa |
| pewność 0–1 | jest, ale stała 1,0 | producent *musi* ją liczyć (1.4) |
| koszt (wywołania, bajty, pieniądze) | brak | odnośnik do pokwitowania `use_model`/przebiegu; heurystyka = 0 |
| hash tekstu źródła | jest | zmiana źródła = adnotacja nieświeża, nie usunięta |
| wersja/rewizja + historia | jest | zostaje |
| relacja do innej adnotacji (opcjonalnie) | brak | `same_as`, `conflicts_with`, `part_of` — to daje strukturę (2.3) |

### 2.2 Producenci i odbiorcy

Producenci (każdy to wpis w `annotations.producers`, z `status`, `origin`, `cost`, `categories_written`):
heurystyka strażnika (szum, znaczniki sygnałów), analizator regex (encje, tematy, *wątek*), analizator model/batch (kategorie
jakości modelu: dygresja, żart, instrukcja, korekta, doprecyzowanie, wątek), rozpoznawanie struktur GPT (role w strukturze:
„to jest specyfikacja”, „to jest test”, „to jest ocena”), użytkownik (ręczna poprawka: „to nie dygresja”).

Odbiorcy: strażnik (poziomy, cykl hipotez), selektor kontekstu (poziom na segment, pomijanie wątków), analizator (graf),
projekcje perspektyw GPT (widok projektu przez kod/pomysły/testy), interfejs (podświetlenie, cofnięcie).

Reguła: **producent nie zna odbiorców, odbiorca nie zna producentów — zna kategorie i pewność.** Strażnik dziś łamie to w
jednym miejscu: pomija adnotacje tylko własnego producenta po nazwie (`guardian.cpp:518`, `levels.cpp:81`); to powinno być
„pomijaj adnotacje tej samej kategorii o niższej pewności”, niezależnie od nazwy.

### 2.3 Rozpoznawanie struktur GPT w tej samej warstwie

Praca GPT i strażnik robią *to samo* na innym poziomie: obserwacja → wystąpienie (z dowodem) → hipoteza/kandydat (z
pewnością, alternatywami, pokwitowaniem) → ewentualny awans. GPT ma lepszy fragment: alternatywne interpretacje zachowane
obok siebie, pokwitowanie z wersją metody i hashem pakietu, admisja dowodów wg kwalifikatorów (twierdzenie negatywne nie
dowodzi wzorca pozytywnego). Strażnik ma lepszy fragment: cykl awansu z pytaniem, eskalacją i cofaniem.

Fuzja, bez równoległej warstwy:

1. **Pokwitowanie rozpoznania = adnotacja o kategorii `structure_role:<rola>`** na każdym źródle, które je wspiera, z
   `confidence` i odnośnikiem do kandydata (węzła). Kandydat (dziś `Instance.recognition`) zostaje węzłem w KnowledgeStore —
   nic nie trzeba przenosić, tylko dopisać adnotacje jako *dowody* i zwracać ich identyfikatory w pokwitowaniu.
2. **Kandydat GPT = hipoteza strażnika** innego rodzaju (`kinds.structure_recognition`, `promotes: proposal`) — ten sam cykl:
   alternatywy to hipotezy połączone `contradicts`, awans wg `guardian_promotion`, propozycja w rozmowie („wygląda na to, że
   ten projekt ma już specyfikację i testy, ale nie ma harnessu — dodać?”), odrzucenie liczone jak każde inne.
3. **Jedna skala pewności i jedna polityka admisji**: kwalifikatory GPT (negative/hypothetical/quoted) stają się
   `metadata.qualifier` adnotacji; strażnik ich dziś nie ma i powinien (korekta w cytacie „on mi powiedział, żebym nie…” to
   nie korekta).
4. **Wersja metody w każdej adnotacji** (GPT: generalizer/pipeline version, hash pakietu) — przejąć jako standard dla
   wszystkich producentów; strażnik dziś ma tylko hash tekstu źródła.
5. Nie przenosić kodu GPT do `src/guardian`; przenieść *kontrakt* do pakietu (`annotations.producers.structure_recognition`,
   `kinds.structure_recognition`). Fuzja gałęzi to dopisanie dwóch wpisów danych i jednej funkcji zapisu adnotacji po stronie
   GPT, nie przepisanie żadnego z dwóch silników. Uwaga praktyczna: gałąź GPT stoi na starszej bazie (różnica wobec
   `main` to setki plików), więc najpierw rebase, potem kontrakt.

### 2.4 Konflikty i wersje

- Dwie adnotacje tej samej kategorii na pokrywającym się zakresie: wygrywa `user`, potem wyższa pewność, potem nowsza
  wersja metody; przegrana zostaje (stan `superseded`, nowy stan obok `retracted`).
- Adnotacja‑sygnał (korekta, instrukcja) ma pierwszeństwo przed adnotacją‑szumem na tym samym zakresie: szum nie może
  wyciąć sygnału (1.4). To jedna reguła w danych warstwy, nie w każdym odbiorcy.
- Zmiana tekstu źródła: wszystkie adnotacje nieświeże, poziomy i reprezentacje przeliczane leniwie (już tak jest dla
  poziomów, `levels.cpp:112`); hipotezy *nie* tracą wystąpień (dowód był prawdziwy w chwili obserwacji), tylko dostają
  znacznik „dowód zmieniony”.
- Koszt na adnotację: odnośnik do pokwitowania; suma per rozmowa widoczna w `guardian.status` i w propozycji.

---

## 3. Środowiska i agent — model ogólny

Stan: w grafie nie ma żadnej klasy „środowisko”, „maszyna”, „konto” (brak trafień w `data/graph/classes.pack`);
`accounts.h` to konta *naszego serwera* (użytkownicy Loom), nie konta użytkownika w obcych serwisach; `use_model` nie ma
pętli narzędzi (`ModelRequest` w `include/loom/use_model.h:242–273` nie zna narzędzi); MCP nie występuje nigdzie.
Co jest i nadaje się: urządzenia jako węzły z dostępnością per platforma (`data/graph/devices.pack`), porty + adaptery +
przebiegi portów jako węzły z redakcją sekretów (`include/loom/port_graph.h:4–17`), rejestr adapterów z wpisami
`implemented|planned` (`include/loom/adapter_registry.h`), katalog akcji z cechami/wymaganiami/flagami skutków
(`actions.pack`) i reguły z zakresami (`action_rules.pack`), przebiegi z pokwitowaniem i potwierdzeniem (`run_gated`,
`runner.cpp:845`), strażnik ×10 i limity wydatków, sejf poświadczeń (`Secrets`, koperta `credential_envelope/3`).

### 3.1 Sześć pojęć, wszystkie jako węzły

| Pojęcie | Czym jest | Na co się mapuje dziś | Czego brak |
|---|---|---|---|
| **Środowisko** | to, *na czym* można działać: konto w serwisie, maszyna, telefon, repozytorium, katalog, przeglądarka | urządzenia (`devices.pack`) to już środowiska lokalne; magazyny (`storage.*`) to środowiska zdalne | jedna klasa `environment` z podklasami, cechy jak w §5 GRAF_HUB, dostępność per platforma i per *dostęp* |
| **Zdolność** | co środowisko potrafi: „ma system plików”, „wysyła wiadomości”, „uruchamia procesy”, „ma kalendarz” | cechy klas (`classes.pack`) i `requires/produces` akcji — mechanizm jest | zdolności jako cechy środowisk; akcja dopasowuje się do środowiska dokładnie tak, jak do wiadomości |
| **Dostęp** | *jak* mogę tam działać: klucz, OAuth, hasło+2FA, sesja przeglądarki, SSH, certyfikat, „fizycznie to mój telefon” | sejf + redakcja + koperta | węzeł `access` (klasa, sposób, odniesienie do sekretu, ważność, zakres uprawnień, kto przyznał, kiedy wygasa); *wartości* nigdy w grafie |
| **Akcja** | jak dziś; wymaga zdolności, deklaruje skutki | `actions.pack` | flaga skutku „działa poza urządzeniem na koncie użytkownika” i „tworzy koszt u dostawcy” |
| **Przebieg** | jak dziś: akcja × element × parametry, z potwierdzeniem i cofnięciem | `action_run`, `port_run` | przebieg na środowisku zapisuje *które* środowisko i *który dostęp* |
| **Pokwitowanie** | co się faktycznie stało, ile kosztowało, co zostało stworzone/zmienione, czy da się cofnąć | pokwitowania `use_model`, `port_run` | pokwitowanie „zewnętrzne”: identyfikator u dostawcy (wiadomość wysłana, maszyna uruchomiona), żeby *cofnięcie* miało adres |

Kluczowe uogólnienie: **maszyna, konto i telefon to ta sama klasa**, różniąca się zdolnościami i sposobem dostępu.
„Uruchom kontener u dostawcy X” = akcja `environment.create` na środowisku‑dostawcy (adapter), która *tworzy nowy węzeł
środowiska* z dostępem (klucz SSH w sejfie) — dalej działa na nim to samo, co na lokalnym katalogu.

### 3.2 Agent = `use_model` + katalog akcji, pod istniejącymi regułami

Pętla: `use_model` dostaje listę *dopasowanych* akcji (silnik dopasowania już liczy, które akcje pasują do elementu —
`Engine::decide`, `engine.cpp:654`) jako narzędzia; model wybiera akcję i argumenty; interpreter wykonuje ją jak każdą inną
(te same reguły, potwierdzenia, ×10, limity, pokwitowanie w grafie); wynik wraca do modelu jako węzeł; aż model skończy albo
skończy się budżet kroków/pieniędzy (ustawienia, presety). Nowe w kodzie: (a) `ModelRequest.tools` + odpowiedź typu
„wywołanie narzędzia” w adapterach dostawców (każda rodzina API ma swój kształt — to jest praca adaptera, nie silnika);
(b) jeden krok interpretera `agent_loop` (kolejno: użyj modelu → jeśli wybrał akcję, wykonaj → dopisz wynik → powtórz).
Wszystko inne jest.

Zasady, które mają obowiązywać agenta bez wyjątku i bez nowego kodu: akcje z flagą „nieodwracalna”, „wysyła poza
urządzenie”, „kosztuje u dostawcy” → wg reguł zakresu (preset: potwierdzenie, użytkownik może zmienić na „stosuj”, bo R1);
budżet kroków i pieniędzy per przebieg (preset, nie limit); każdy krok w grafie jako `part_of` przebiegu. Odrzucenie
propozycji agenta = wystąpienie dla strażnika (ten sam cykl — agent uczy się preferencji tak samo jak czat).

### 3.3 Źródła narzędzi (MCP i inne) jako adaptery

Serwer MCP = adapter portu `tool_source`, który przy podłączeniu *dopisuje akcje do katalogu* (nakładka, status
`external`, z `requires` wyprowadzonym z deklaracji narzędzia i flagami skutków **nieznanymi** → preset: potwierdzaj).
Narzędzie MCP wykonane przez interpreter jak każda akcja; pokwitowanie z przebiegu portu. To samo dla: funkcji dostawcy
(„web search” OpenRouter), wtyczek, skryptów lokalnych. Nic nie wie, że to MCP, poza adapterem.

### 3.4 Minimalna bezpieczna kolejność

1. Klasa `environment` + `access` w grafie, lokalne środowiska (katalogi, urządzenia, nasz serwer) jako pierwsze wpisy —
   zero nowych uprawnień, zero kosztów.
2. Pętla narzędzi w `use_model` z katalogiem *lokalnych, odwracalnych* akcji (czytaj, szukaj, streszczaj) — można
   sprawdzić na żywo bez ryzyka.
3. Źródła narzędzi (adapter MCP) z akcjami zewnętrznymi pod presetem „potwierdzaj”.
4. Konta użytkownika: sposób dostępu OAuth/klucz w sejfie, pierwsze konto = pytanie do właściciela (jego dane).
5. Maszyny: dostawca jako adapter, `environment.create` z kosztem w propozycji, pierwszy dostawca = pytanie do
   właściciela (jego pieniądze).

### 3.5 Co jest decyzją właściciela, a co techniczną

Właściciel (tylko tak/nie): podłączenie pierwszego konta w nowym serwisie (jego dane idą do nowego miejsca); włączenie
pierwszego dostawcy maszyn (jego pieniądze); każde ustawienie, które spodziewanie zwiększa zużycie o rząd (×10 już to
łapie); wysyłanie treści rozmów do innego dostawcy niż wybrany (1.8). Techniczne (Claude decyduje, zapisuje, da się
cofnąć): kształt klas i cech, sposób dopasowania narzędzi do akcji, presety potwierdzeń, format pokwitowań, które akcje są
odwracalne, kolejność kroków.

---

## 4. Kroki w kolejności, z kryteriami przyjęcia

Każdy krok: jeden tor, brama, wpis w `docs/reports/INDEX.md` z cofnięciem. Poziom ogólności: właściciel sprawdza
kryterium w oknie rozmowy, nie w testach.

1. **Warstwa adnotacji jako kontrakt, nie szczegół strażnika.** Producent z wersją metody, pewność liczona, stan
   `superseded`, reguła „sygnał przed szumem”, adnotacje na segment zamiast jednego wystąpienia na wiadomość (1.4, 1.5, 2.1,
   2.4). *Przyjęcie:* wiadomość „nie, nie tak — krócej, ale zawsze po polsku, oprócz nazw plików” daje w `guardian.status`
   trzy osobne rzeczy (korekta, preferencja osoby, wyjątek) i L1 nie wycina „nie, nie”.
2. **Analizator pisze do warstwy.** Jeden kontrakt promptu daje encje+tematy+kategorie segmentów (dygresja, żart,
   instrukcja, wątek); regex pisze `thread` z heurystyki tematów; strażnik nie robi drugiego wywołania (1.7). *Przyjęcie:*
   przy `guardian_method=model` i włączonym analizatorze liczba wywołań modelu na wiadomość = 1; L2 ≠ L1 na rozmowie z
   dygresją; selektor umie pominąć inny wątek (`thread_filter` przestaje być `planned`).
3. **Hipoteza z treścią znormalizowaną i łączeniem jako metodą.** (1.1, 1.2) *Przyjęcie:* trzy różnie sformułowane
   poprawki o tym samym dają jedną hipotezę i jedną propozycję; propozycja brzmi jak zasada, nie jak cytat; złe sklejenie
   cofa się jednym „nie”.
4. **Zakres z różnorodności dowodów, cykl życia reguły, prywatność reguł.** (1.3, 1.8, 1.9) *Przyjęcie:* sześć poprawek w
   jednej rozmowie nie daje reguły o osobie; dwie rozmowy — tak, z pytaniem; reguła o osobie trafia do wysyłki tylko w
   rozmowach, w których wolno (R47), i niesie treść, nie cytaty; przeciwna poprawka po tygodniu wywołuje propozycję
   wyłączenia; „co o mnie pamiętasz?” odpowiada listą.
5. **Fuzja z pracą GPT.** Rebase gałęzi, kontrakt `structure_role` i rodzaj `structure_recognition` w danych, pokwitowanie
   rozpoznania zapisuje adnotacje‑dowody (2.3). *Przyjęcie:* rozpoznana struktura pokazuje się jako propozycja strażnika w
   rozmowie z alternatywami; odrzucenie liczy się jak każde; jeden format pewności w obu.
6. **Środowisko i dostęp w grafie** (3.1, krok 3.4.1). *Przyjęcie:* „co mogę zrobić na moim telefonie / w tym katalogu?”
   z okna rozmowy daje listę dopasowanych akcji z flagami skutków; żaden sekret nie pojawia się w grafie ani w odpowiedzi.
7. **Pętla narzędzi w `use_model`** z lokalnymi, odwracalnymi akcjami (3.2). *Przyjęcie:* „znajdź w moich rozmowach, gdzie
   ustalaliśmy format raportu, i streść” wykonuje się w kilku krokach, każdy widoczny jako część przebiegu, z sumą kosztu;
   budżet kroków to ustawienie.
8. **Źródła narzędzi jako adapter** (3.3). *Przyjęcie:* podłączenie serwera MCP dopisuje akcje do katalogu; ich pierwsze
   użycie pyta (preset), użytkownik może przełączyć na „stosuj”; odłączenie zdejmuje akcje bez śladu w kodzie.
9. **Konta użytkownika** (3.4.4) — po pytaniu do właściciela o pierwszy serwis. *Przyjęcie:* dostęp jako węzeł z ważnością,
   użycie z pokwitowaniem, cofnięcie dostępu z okna rozmowy.
10. **Maszyny jako środowiska** (3.4.5) — po pytaniu do właściciela o pierwszego dostawcę. *Przyjęcie:* „postaw mi maszynę
    z Pythonem i uruchom testy tego repo” = propozycja z kosztem, wykonanie, pokwitowanie z identyfikatorem u dostawcy,
    „zatrzymaj” kończy naliczanie i zapisuje to w grafie.

Cofnięcie całości przeglądu: dokument jest opisem; żaden krok nie zmienia zachowania, dopóki nie zostanie wdrożony pod
własnym wpisem w INDEX z własnym cofnięciem.
