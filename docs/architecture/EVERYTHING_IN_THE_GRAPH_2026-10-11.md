# „Wszystkie dane systemu są w grafie” — definicja paradygmatu (T9, S2)

Napisane 2026-10-12 w sesji S2 (plan `docs/reports/THREADS_2026-10-11.md` §3 T9, §9; nazwa pliku
zgodnie z tabelą §9). Dokument nadrzędny: `DUCH_GRAFU_2026-10-11.md` §0 — tam jest **decyzja**
właściciela, tu jest jej **precyzacja do postaci sprawdzalnej**. Zgodność: R15 (wszystko, z czego
aplikacja korzysta, jest w grafie), R20 (kopia czy link — wybór użytkownika), R44 (nierozpoznane
wchodzi), R50 (ekonomia zasobów), R51 (magazyny i bezpieczeństwo), I1–I9 z
`LOOM_CONCEPTUAL_MODEL.md`. Nie zmienia kodu; propozycje nowych terminów są w §9.

## 0. Definicja

> Każdy byt, na którym system operuje, ma w grafie **adres**, co najmniej jeden **lokator** albo
> **wyprowadzenie**, **pochodzenie**, **położenie w czasie** i **status rozpoznania**. Bajty leżą tam,
> gdzie każe ustawienie magazynu. Graf jest nad magazynami **indeksem z pochodzeniem**: nie kopią
> danych i nie jedną tabelą.

Trzy odczyty tego zdania, które odrzucamy, bo łamią wymagania:

| Odczyt | Dlaczego odrzucony |
|---|---|
| „fizycznie w jednym magazynie grafowym” | R51: magazyny to serwer, chmury użytkownika, lokalnie, w dowolnej kombinacji per kategoria; R20: kopia czy link jest wyborem użytkownika; R50: forma grafowa zajmuje ~6× więcej niż tekst, więc wymuszona wszędzie byłaby marnotrawstwem, którego łatwo uniknąć |
| „wszystko zmaterializowane jako węzły i krawędzie” | korekta właściciela w pass4 (`pass4/TASK.md`): referencja + parsowanie/projekcja na żądanie jest pełnoprawną ścieżką; cache, indeks, osadzenie i snapshot są niezależnymi możliwościami |
| „graf jako druga kopia danych obok magazynów” | AGENTS.md: stan pochodny musi dać się odbudować; DUCH §0: nie ma drugiej pamięci obok grafu — graf jest jedyną pamięcią *gdzie i skąd*, nie drugą pamięcią *co* |

Konsekwencja praktyczna: „czy X jest w grafie?” nie pyta, gdzie leżą bajty X. Pyta, czy istnieje
węzeł, przez który X da się znaleźć, sprowadzić do bajtów (albo do przepisu), wyjaśnić i umieścić
w czasie.

## 1. Pięć pól, które czynią byt „w grafie”

| Pole | Znaczenie | Nośnik dziś (Loom) | Reguła |
|---|---|---|---|
| **Adres** | stały identyfikator węzła; tam gdzie się da, wyprowadzony z treści (I5) | `id` węzła, `loom_sources.id`, id encji/twierdzeń | nigdy nie zmienia się, gdy bajty zmieniają miejsce |
| **Lokator** | (magazyn, ścieżka / członek archiwum / wskaźnik JSON / zakres bajtów lub czasu, hash, rewizja) | `Locator` z `LOOM_CONCEPTUAL_MODEL` §1, `loom_sources`, BlobStore, hashowany plik | 0..n na węzeł; 0 tylko dla węzłów z wyprowadzeniem; brak = jawny węzeł `absent` albo `opaque`, nigdy milczenie |
| **Pochodzenie** | `produced_by` → {metoda, wersja (hash promptu i kodu), parametry, wejścia, koszt, czas, kto}; albo obserwacja → lokator; albo `user` / `pack` | `loom_provenance`, krawędzie `produced_by`, `Assessment.basis` | każda operacja zapisuje też swoje własne wykonanie (DUCH §0) |
| **Czas** | `known_at`, `valid_from`/`valid_to`, `supersedes`; rozgałęzienia zostają | kwalifikatory twierdzeń, `version_group`, statusy per gałąź | nic nie jest nadpisywane; „co myślałem rok temu” jest zapytaniem (DUCH §2 p. 4) |
| **Status rozpoznania** | `recognized` / `partial` / `unrecognized` / `opaque` / `excluded_by_user`, dla odpowiedzi modeli także `unvalidated` | `GRAPH_ENTRY_PARTIES_ECONOMY` §2 (projekt, T1 wdraża) | to, czego silnik nie rozumie, też ma adres |

Szóste pole nie dotyczy prawdy, tylko ekonomii:

| **Materializacja** | poziom, na jakim bajty lub ich postać grafowa są trzymane: `reference` (sam lokator) → `projection` (parsowanie na żądanie) → `cache` → `index` → `embedding` → `snapshot` → `copy` | `economy.mode`, retencja per źródło (R20) | ustawienie per klasa bytu i per zakres (R47), nigdy wymuszone globalnie (R50) |
|---|---|---|---|

## 2. Trzy warstwy

```
WIDOKI / PROJEKCJE       widok wejściowy (R45), perspektywy, profile UI, dossier, SELF.md
   ↑ generowane, odtwarzalne, nigdy drugie źródło prawdy (R35)
GRAF-INDEKS              adresy + bogate krawędzie + lokatory + pochodzenie + czas
   ↑ jedna pamięć „gdzie, skąd, kiedy, czym”
MAGAZYNY                 SQLite (loom_* i legacy v4) · BlobStore · pliki w katalogu danych ·
                         packi w drzewie kodu (loom/data) · packet store · chmury użytkownika ·
                         serwer aplikacji · urządzenie · repozytoria git · strona dostawcy
                         (to, co wysłano) · nośniki archiwalne
```

- **Magazyn jest węzłem.** Ma klasę położenia, dostęp, szyfrowanie, odniesienie do opieki nad
  kluczem, regułę synchronizacji i konfliktu, jurysdykcję i profil prywatności (R19). Inwentarz
  dzisiejszych magazynów robi T5; ten dokument tylko wymaga, żeby każdy z nich był węzłem.
- **Graf-indeks nie wymaga jednej tabeli.** Dziś jest rozłożony na `loom_sources`,
  `loom_provenance`, KnowledgeStore (twierdzenia), MethodRegistry, DefaultLayers (ustawienia),
  katalog jednostek, legacy `nodes`/`links` v4. Paradygmat wymaga, żeby każdy z tych rejestrów był
  osiągalny po adresie i wyliczony w widoku wejściowym, nie żeby je scalić.
- **„Jedna forma dla wszystkiego”** (DUCH §0) to forma **wymiany i widoku** (GraphPacket z bogatymi
  krawędziami), nie forma przechowywania. Czytane dosłownie jako forma przechowywania wymusiłoby
  narzut ×6 wszędzie (R50).

## 3. Niezmienniki G1–G10 (każdy z pomysłem na test)

Numeracja ciągła względem I1–I9 z modelu pojęciowego; G nie zastępuje I, tylko je domyka od strony
magazynów i adresowalności.

| # | Niezmiennik | Test, który go wymusza |
|---|---|---|
| G1 | **Adresowalność.** Wszystko, co kod produktu czyta lub zapisuje trwale, rozwiązuje się do węzła; brak = jawny węzeł `absent`/`opaque` | skan kodu produktu na wywołania trwałego zapisu/odczytu (pliki, SQLite, `localStorage`, `SharedPreferences`, Room, cache HTTP) → każde ma deklarację w packu magazynów; analog strażnika literałów R42 |
| G2 | **Domknięcie pochodzenia.** Z każdego węzła pochodnego po `produced_by` dochodzi się do wersji metody i do źródeł; z każdej obserwacji do lokatora i hasha | losowy spacer po `produced_by` kończy się w źródle; test fixtures: brak ścieżki = FAIL |
| G3 | **Niezależność od magazynu.** Przeniesienie bajtów zmienia lokatory, nigdy adresu; stary lokator dostaje status `stale`, zostaje | test metamorficzny relokacji: te same bajty w nowym miejscu → ten sam adres, nowy lokator osiągalny, stary zachowany (dziś łamane: A4-B-RES001) |
| G4 | **Indeks, nie kopia.** Domyślna materializacja to `reference` + `index`; postać grafowa na żądanie; duplikaty dozwolone i zgłaszane (R50) | dwukrotny import tego samego źródła → jeden blob adresowany treścią (albo dwa z raportem), dwa węzły źródeł z krawędzią `same_bytes`, raport ekonomii z propozycją |
| G5 | **Brak drugiej pamięci.** Każdy stan trwały poza graf-indeksem jest zadeklarowanym cache albo projekcją: ma węzeł magazynu, rolę (`primary`/`cache`/`projection`) i przepis odbudowy | ten sam skan co G1 plus: rola `cache`/`projection` wymaga przepisu odbudowy; `primary` poza grafem = FAIL |
| G6 | **Samozawieranie.** Metody, przebiegi, koszty, ustawienia, magazyny i *odniesienia* do kluczy są węzłami; bajty sekretów nigdy | skan ładunków węzłów na wzory kluczy = 0 trafień; każdy przebieg ma `produced_by`; każdy magazyn ma węzeł |
| G7 | **Nierozpoznane wchodzi** (R44) | import nieznanego formatu → węzeł `unrecognized` z bajtami i lokatorem; import nie upada |
| G8 | **Kierunek czasu.** Krawędzie mają `known_at`; zapytanie „stan na T” nie widzi późniejszej wiedzy | zapytanie z `as_of=T` na fixture z trzema wersjami |
| G9 | **Inferencja ≠ obserwacja; osąd właściciela wygrywa** (I3, I4, R5) | istniejące testy klasy dowodu; żaden widok nie renderuje `inferred` jak `observed` |
| G10 | **Wyjaśnialność.** Każda wartość efektywna i każdy węzeł odpowiada na „skąd?”: warstwy, zakresy, inicjator, czas (R40, R43) | `explain()` na losowym ustawieniu zwraca łańcuch zakresów i regułę konfliktu; na losowym węźle — lokator albo wyprowadzenie |

## 4. „Wszystkie dane systemu” — inwentarz klas bytów i ich adresowalność dziś

Klasy z DUCH §1. Kolumna „kto” wskazuje wątek T1–T8 z `THREADS` albo brief T9
(`docs/reports/t9-briefs/`). Oceny „dziś” pochodzą z audytu 2026-10-09 (pass1–4) i inwentarza
danych w kodzie; nie są nowym audytem.

| Klasa bytu | Gdzie dziś bajty | Adresowalne dziś | Czego brakuje | Kto |
|---|---|---|---|---|
| Źródła i jednostki (eksporty, pliki, członkowie ZIP) | DB + BlobStore + katalog | tak (`loom_sources`, `Locator`) | relokacja gubi stary lokator (A4-B-RES001); nierozpoznane znika (R44) | T1, B-CH-06 |
| Rozmowy, wiadomości, gałęzie | SQLite v4 | tak (legacy) | bogate krawędzie częściowe; `known_at` brak | T2, T3 |
| Kod i repozytoria (własne i ekosystemu) | git | częściowo (repo jako źródło, R2) | 7 repozytoriów nie są węzłami; audyt o nich nie jest w grafie | B-ECO-01 |
| Ustawienia i preferencje | `config.json`, `Config`, RuntimeProfile, DefaultLayers | dane gotowe, okablowanie częściowe (CH-001, CH-004; 19 pozycji `partial` wątku 11) | łańcuch zakresów (R47) i reguły konfliktu | T3, T13, B-CH-01 |
| Teksty UI i języki | `onboarding/ui.pack` (tak); web workspace (nie, CH-008); pozostałe repo (nie) | częściowo | katalog komunikatów per repo | B-CH-05, B-WD-03, B-CKP-03, B-AG-03, B-LEM-03 |
| Metody, prompty, wersje | MethodRegistry, `prompts/`, `philosophy/` | częściowo (CH-002, CH-003; DIC-0009…0014 `open`) | legacy SemanticLLM poza rejestrem; jeden efektywny snapshot per zadanie | B-CH-02 |
| Przebiegi, koszty, pochodzenie | `loom_provenance`, rejestr W2 | częściowo (CH-006: legacy ingest bez `produced_by`; DIC-0177: koszt płaski) | admission policy i `produced_by` na każdej relacji | B-CH-03, T4 |
| Wyniki, propozycje, eksperymenty | TaskEngine, badania 7 | częściowo (CH-015 niepotwierdzone) | opt-in i snapshot eksperymentu jako węzły | T11, T12 |
| Podmioty | profil onboardingu | częściowo | `parties.pack`, role w kontekście | T3, T13 |
| Samomodel | `profiles/self.json`, `philosophy/*` | częściowo | audyt ekosystemu i relacje repo jako twierdzenia | B-ECO-01 |
| Magazyny i klucze | `secrets.json`, BlobStore, packet store, logi | **nie** | inwentarz, węzły magazynów, opieka nad kluczami | T5 |
| Stan interfejsu (workspace, perspektywy, checkpointy) | `localStorage`, klucz config | **nie** (CH-009, A4-E-001, A3-WEB-001) | perspektywa i sesja workflow jako węzły; `localStorage` jako zadeklarowany cache | B-CH-05 |
| Cache modeli (aplikacja Python) | `models.json` | częściowo, destrukcyjnie (A4-CH-MOD001) | migracja bez utraty pól | T7 |
| Packi (dane w drzewie kodu) | `loom/data` | częściowo (`data_domains.pack` ma digesty) | pack jako węzeł z rewizją w widoku wejściowym | T3 |
| Nierozpoznane, niepewne, wyłaniające się | brak | **nie** | statusy rozpoznania (R44) | T1 |
| Dane innych repozytoriów (wyniki Watchdog, dowody AGEDS, przechwycenia IO Matrix, wyniki LEM) | ich własne magazyny | **nie** (żadnego odniesienia) | koperta odniesienia §7, potem adaptery | B-ECO-01, §7 |

Wniosek z tabeli: paradygmat jest już kierunkiem (`loom_sources`, `loom_provenance`, GraphPacket,
DefaultLayers), a trzy białe plamy to **magazyny i klucze**, **stan interfejsu** oraz **inne
repozytoria**. To nie są braki funkcji; to trzy klasy bytów bez adresu.

## 5. Zgodność z R50 — ekonomia

- **Skąd „×6”.** Postać grafowa tekstu (węzeł na zdanie, krawędzie, metadane, pochodzenie) jest
  kilkakrotnie większa od samego tekstu. Paradygmat nie każe jej trzymać: **bajty raz, adresowane
  treścią; indeks zawsze; postać grafowa generowana** do widoku albo do kontekstu modelu, z cache
  tam, gdzie każe ustawienie.
- **Decyzja o materializacji to dane** (`economy.pack`, T4): tryb `off` / `report` / `propose` /
  `auto` per klasa; działania `link` / `cache` / `generator` / `compress` / `nothing`; model
  korzyść–koszt z `GRAPH_ENTRY_PARTIES_ECONOMY` §9.
- **Generator zamiast bajtów.** Węzeł może nieść przepis (metoda, wersja, wejścia, parametry,
  znany koszt odtworzenia) zamiast wyniku, gdy odtworzenie jest deterministyczne i tanie: indeksy,
  ekstrakcja regułowa, projekcje. Odpowiedzi modeli nie są odtwarzalne, więc zostają zawsze (to
  jest granica, którą wyznacza nieredukowalność obliczeniowa, nie oszczędność).
- **Duplikaty dozwolone, zgłaszane.** Krawędź `same_bytes` i raport z propozycją przypięcia;
  przypięcie nigdy nie jest wymuszone (R50: „nie może zabraniać”).
- **Stabilny prefiks promptu** (R49) to ta sama ekonomia zastosowana do tokenów; realizuje T2.

## 6. Zgodność z R51 — magazyny, szyfrowanie, bezpieczeństwo

- **Węzeł magazynu** (propozycja schematu, jako dane): `id`, klasa położenia
  {`app_server`, `user_cloud`, `local`, `device`, `provider_side`, `repo`, `archive_media`}, dostęp
  (kto), szyfrowanie {algorytm, odniesienie do opieki nad kluczem, rotacja}, synchronizacja
  {reguła, konflikt}, jurysdykcja, profil prywatności (R19), zdolności (odczyt/zapis/lista/
  obserwacja nowych plików, R20).
- **Macierz kategoria × magazyn** jest ustawieniem w zakresach (R47). Każdy węzeł ma kategorię
  danych; lokator może wskazywać tylko magazyn dozwolony dla kategorii. „Zdrowie zostaje na
  telefonie” = kategoria `health` → magazyny {`device`}. Wysyłka do dostawcy jest tą samą
  macierzą z magazynem klasy `provider_side`: to, co wysłano, jest węzłem z lokatorem po stronie
  dostawcy i czasem, więc „klucz wyciekł; co było wysłane, komu, kiedy?” (DUCH §5) jest zwykłym
  zapytaniem, a widok „co faktycznie wysłano” (R26) jest projekcją tych węzłów.
- **Klucze.** Bajty klucza nigdy nie są ładunkiem węzła. Węzeł klucza to odniesienie do chronionego
  magazynu (keystore systemu, `secrets.json` z `chmod 600`, sprzętowy) plus metadane opieki i
  zdarzenia rotacji jako węzły.
- **Kolejność:** inwentarz T5 → węzły magazynów → macierz jako ustawienie → preset hybrydowy
  (ML-KEM + X25519, ML-DSA, AES-256-GCM, Argon2id; decyzja w `THREADS` §6) → widok w obszarze
  „Magazyn i bezpieczeństwo” widoku wejściowego.

## 7. Federacja między repozytoriami: adres zamiast wspólnej bazy

`ECOSYSTEM.md` wyklucza jedną aplikację, bazę i model danych („różne grafy nie muszą mieć tej
samej semantyki”), a `relationships.json` z audytu stwierdza, że brainstorm nie dowodzi jednego
centralnego runtime. Paradygmat spełnia to, federując **po adresie**, nie po magazynie.

**Koperta odniesienia** (kontrakt kod↔dane, kategoria 1 R42; jako dane):

```
loom.ecosystem_ref/1 = { repo, revision, path, pointer?, range?, sha256, schema, schema_revision }
```

Spostrzeżenie: `findings.jsonl` audytu już używa dokładnie tej postaci (`repo`, `sha`, `file`,
`line_start`/`line_end`, `range_sha256`). Nie projektujemy więc nowego formatu; nazywamy istniejący
i czynimy go schematem. Pierwsze użycie: B-ECO-01 wprowadza audyt do grafu jako twierdzenia
o węzłach repozytoriów.

Zasady federacji:
1. Każde repo jest najpierw **samoadresowalne**: ma katalog profilu opisujący własne magazyny,
   ustawienia, metody, katalog komunikatów i samoopis (odpowiednik `profiles/self.json`). Dopiero
   potem może być **odniesione**.
2. Semantyka zostaje lokalna. Wspólna jest tylko koperta i sześć kategorii R42.
3. Most (np. ChatADHD jako interfejs do AGEDS) jest adapterem **jako dane**, który czyta samoopis
   drugiego repo i tłumaczy jego typy na twierdzenia z pochodzeniem `origin=repo`, klasą dowodu
   nie wyższą niż w źródle. Mosty są rozważane w `ECOSYSTEM_INTEGRATION_2026-10-11.md` §5.

## 8. Audyt przeczytany przez paradygmat

Każde ustalenie audytu 2026-10-09 łamie któryś z niezmienników §3. To jest mapa, której audyt nie
mógł zrobić, bo nie miał definicji:

| Niezmiennik | Ustalenia, które go łamią |
|---|---|
| G1 adresowalność | CKP-A-006 (przepływy ustawień/AI/agenta bez adresu), EA-AGEDS-006 i LEM-007 (konfiguracja poza grafem), A4-CH-MOD001 (cache modeli usuwa źródło) |
| G2 pochodzenie | CH-006 (legacy ingest materializuje relacje bez `produced_by`), LEM-003 (wyniki tracą surową odpowiedź), A4-WD-002 (manifest gubi braki i pochodzenie), A4-D-001 (parametry parsera poza tożsamością metody), A4-C-001/002 i A4-CKP-001/002 (tożsamość zależna od kolejności kluczy lub pomijająca parametry) |
| G3 niezależność od magazynu | A4-B-RES001 (relokacja zostawia nieosiągalny lokator), LEM-005 (brak migracji Room = kasowanie), LOOM-005 (bezwarunkowe oznaczenie wersji) |
| G4 indeks, nie kopia | DIC-0177 i klasa 3 z `GRAPH_ENTRY` §9 (~0,7 KB profilu kopiowane do każdej próby), R50 bez konsumenta (T4) |
| G5 brak drugiej pamięci | WD-004 (`localStorage` cicho wraca do default), EA-AGEDS-007 (wykluczenia w pamięci), CH-009 (sesje workflow i perspektywy w `localStorage`/kluczu config), CKP-A-011 (profile bez konsumenta) |
| G6 samozawieranie | CH-001, CH-002, CH-007, WD-001/005/009/010, CKP-A-001/002/003/005/007, EA-AGEDS-001/004/005/009, LEM-001/002/004, LOOM-001/004 (polityka w kodzie = byt bez węzła); A4-LEM-001/002 (poświadczenia bez tożsamości operacji) |
| G7 nierozpoznane wchodzi | A3-IMP-CH004 (próg białych znaków usuwa rozmowę), A3-WEB-001 (nieznane pole blokuje znane), LOOM-003 (wiadomość bez tekstu pomijana) |
| G8 kierunek czasu | A4-WD-003 (sprzeczne obserwacje rozstrzyga niejawne last-wins), LOOM-002 (kolizje cicho nadpisują) |
| G9 inferencja ≠ obserwacja | CH-006, CH-010 (dedup odrzuca alternatywny typ), WD-011 (strażnik liczb pomija znak; wynik pozostaje PROPOSED, więc częściowo) |
| G10 wyjaśnialność | CH-004 (prywatność onboardingu nie dociera do zwykłego czatu), CH-005 (strażnik ×10 nie na zwykłej ścieżce), A4-E-001 (nieużywany adapter potwierdza zdolność), CKP-A-003 (plan.best bez uzasadnienia) |

Dwa wnioski. Po pierwsze, pięć repozytoriów w czterech językach łamie te same dziesięć
niezmienników, więc naprawa jest wspólną **metodą** (dane gotowe → konsument → test A/B na
przechwyconym żądaniu → `produced_by`), nie wspólnym kodem. Po drugie, najliczniejsza klasa (G6:
polityka w kodzie) jest najmniej groźna dla użytkownika dziś; najgroźniejsze są G3 i G5 (utrata
danych, druga pamięć), bo tych użytkownik nie widzi, dopóki nie straci.

## 9. Propozycje rozszerzeń `LOOM_CONCEPTUAL_MODEL` (v1.2, addytywne)

Model wymaga, żeby komponent potrzebujący nowego znaczenia zatrzymał się i poprosił o zmianę
modelu. Ta sesja projektuje, więc prosi. Przyjęcie należy do S1 przy pierwszym pakiecie, który
tych terminów użyje; do tego czasu są propozycją.

| Termin (EN / PL) | Znaczenie | Nie mylić z |
|---|---|---|
| **Address / adres** | stały identyfikator węzła niezależny od położenia bajtów | locator (gdzie są bajty) |
| **Storage / magazyn** | węzeł opisujący miejsce przechowywania bajtów z dostępem, szyfrowaniem, opieką nad kluczem i regułą synchronizacji | source (co jest przechowywane) |
| **Materialization level / poziom materializacji** | `reference` · `projection` · `cache` · `index` · `embedding` · `snapshot` · `copy`; ustawienie ekonomii per klasa i zakres | resolution (ziarno, z jakim byt wchodzi do kontekstu) |
| **Recognition status / status rozpoznania** | `recognized` · `partial` · `unrecognized` · `opaque` · `excluded_by_user` · `unvalidated` | evidence class (jak wartość została uzyskana) |
| **known_at** | chwila, od której krawędź jest znana systemowi | `valid_from` (od kiedy obowiązuje w świecie) |
| **Ecosystem reference / odniesienie ekosystemowe** | koperta `{repo, revision, path, pointer?, range?, sha256, schema, schema_revision}` | locator (wewnątrz jednego katalogu danych) |
| **Second memory / druga pamięć** | stan trwały poza graf-indeksem bez węzła magazynu i roli | cache (dozwolony, gdy zadeklarowany z przepisem odbudowy) |

## 10. Jak sprawdzić, że paradygmat obowiązuje

Lista dla S3 (przypadki graniczne, T14) i dla briefów. Każdy punkt jest niezmiennikiem, nie
oczekiwanym wynikiem, bo wyrocznia nie istnieje (`THREADS` §7 p. 4).

1. **Relokacja** (G3): przenieś katalog danych i jeden blob; wszystkie adresy niezmienione; każdy
   stary lokator `stale` i zachowany; nowy osiągalny.
2. **Druga pamięć** (G1, G5): skan kodu produktu zwraca listę miejsc trwałego zapisu; każde ma
   węzeł magazynu i rolę; każde `cache`/`projection` ma przepis odbudowy; odbudowa daje bajty
   równe (test bajt w bajt na fixture).
3. **Domknięcie pochodzenia** (G2): dla 100 losowych węzłów pochodnych spacer po `produced_by`
   kończy się w źródle z lokatorem i hashem zgodnym z bajtami.
4. **Czas** (G8): zapytanie `as_of=T` na fixture z wersjami nie zwraca wiedzy późniejszej niż T.
5. **Nierozpoznane** (G7): import pliku o nieznanej strukturze kończy się sukcesem z węzłem
   `unrecognized`; bajty i lokator zachowane; ponowne rozpoznanie nowym adapterem nie tworzy
   drugiego źródła.
6. **Sekrety** (G6): żaden ładunek węzła nie zawiera bajtów klucza; każdy magazyn ma odniesienie
   do opieki nad kluczem.
7. **Ekonomia** (G4): dwukrotny import tego samego eksportu → raport ekonomii z propozycją; brak
   zakazu; po przyjęciu propozycji jeden blob, dwa adresy.
8. **Wyjaśnialność** (G10): dla 100 losowych ustawień efektywnych `explain()` zwraca zakres,
   regułę konfliktu, inicjatora i czas; brak = FAIL.
9. **Federacja** (§7): twierdzenie o innym repo niesie kopertę; hash koperty zgadza się z bajtami
   pod `revision`.

## 11. Decyzje podjęte w tej sesji (CLAUDE.md: decyduj i zapisuj; każda odwracalna jednym słowem)

- **D-G1** Domyślna materializacja to `reference` + `index`; postać grafowa na żądanie. Zmiana to
  preset, nie kod.
- **D-G2** Bajty sekretów nigdy w grafie; węzeł klucza = odniesienie do opieki.
- **D-G3** Federacja repozytoriów po adresie (koperta §7), bez wspólnej bazy i wspólnego runtime.
- **D-G4** Strażnik drugiej pamięci (G1/G5) jako test, analog strażnika literałów R42; deklaracja,
  nie zakaz (R43: inicjatorem jest `owner_rule` R15).
- **D-G5** GraphPacket jest formą wymiany i widoku, nie formą przechowywania.
- **D-G6** Pierwsze dane ekosystemowe w grafie to sam audyt 2026-10-09 (B-ECO-01), bo już ma
  postać koperty i zerowy koszt pozyskania.
- **D-G7** Nazwy plików zgodnie z `THREADS` §9 (data 2026-10-11 w nazwie, data napisania w treści).

## 12. Krytyka założeń (na życzenie właściciela)

1. „Dane **są w** grafie” miesza dwa znaczenia „w”: położenie bajtów i pozycję w indeksie. §0
   rozstrzyga na rzecz indeksu; bez tego rozstrzygnięcia każdy agent wybrałby inaczej.
2. „Jedna forma dla wszystkiego” (DUCH §0) czytana dosłownie przeczy R50. Czytana jako forma
   wymiany (GraphPacket) jest spójna. Ta sesja wybrała drugie czytanie (D-G5); jeśli właściciel
   miał na myśli pierwsze, trzeba zmienić R50, nie ten dokument.
3. Ryzyko odwrotne: „graf jako indeks” może zdegenerować się do katalogu metadanych bez semantyki,
   czyli dokładnie do „porozrzucanych słów” z R3. Strażnikiem jest to, że nośnikiem powiązania
   pozostają bogate krawędzie-twierdzenia (DUCH §1), a celem indeksu jest dobór kontekstu (R12),
   nie sam indeks.
4. G5 może być odczytany jako nowe ograniczenie dla programistów. Nie jest: wymaga deklaracji,
   nie zabrania zapisu. Zapis bez deklaracji to byt bez adresu, czyli naruszenie R15, nie decyzji
   tej sesji.
5. Tabela §4 ocenia adresowalność na podstawie audytu z 9.10 i inwentarza z 4.10; Watchdog
   (`STATE.md`: 644/644 po A4-WD001/003) i gałąź B już się od tego czasu przesunęły. Oceny są
   wejściem do briefów, a brief zaczyna od ponownego sprawdzenia na bieżącym SHA.
