# Lista akceptacyjna: ścieżki właściciela z telefonu, odporne na ASR (T14, 2026-10-11)

Sesja S3 (Fable 5.1), `docs/reports/THREADS_2026-10-11.md` §3 (T14) i §9.
Rozszerza `ACCEPTANCE_TESTS_2026-09-29.md` (kryteria 0–17 zostają bez zmian;
tu dochodzą ścieżki P1–P7 i kryteria 18–31) o przypadki z
`EDGE_CASES_2026-10-11.md` (EC-*, relacje MR-*). Nadrzędny: `DUCH_GRAFU_2026-10-11.md`
§3 (przepływ zapytania) i §4 (wielotorowość). Zasada właściciela (`CLAUDE.md`):
lista nie wprowadza żadnego ograniczenia; sprawdza, czy ustawienia pozwalają
na wszystko i czy każda bramka ma inicjatora.

## 0. Co znaczy „zaliczone”, skoro nie ma oracle’a

Właściciel powiedział: „musi działać wszystko, co chcę, lepiej niż gdzie indziej”.
To miara produktu (THREADS §7.5). Przekład na test:

1. **Ścieżka przechodzi**, gdy: (a) każdy krok daje obserwowalny wynik wymieniony
   w kolumnie „Musi być widać”, (b) niezmienniki ścieżki (odwołania do EC-*/MR-*)
   zachodzą na śladzie, (c) ślad jest kompletny i da się odtworzyć bez telefonu
   (§3), (d) właściciel zapisał osąd „tak/nie” na numerowane pytania ścieżki.
2. **„Lepiej niż gdzie indziej”** mierzy się tylko porównaniem na tym samym
   wejściu: ścieżka ma kolumnę „Odniesienie” (co robi aplikacja dostawcy z tym
   samym eksportem/pytaniem) i właściciel odpowiada na jedno pytanie tak/nie.
   Nie jest to metryka silnika; jest to zapis osądu z datą (klasa `user`).
3. **Brak zdolności nie oblewa ścieżki**, jeśli jest zgłoszony (`invariant:capability`,
   R43) i reszta ścieżki działa; oblewa, gdy jest cichy.
4. **Ścieżka nie jest zaliczona** bez śladu, nawet jeśli „działało”.

## 1. Reguły sesji testowej z telefonu

- **Jedna ścieżka naraz**, w jednym podejściu ≤ 10 minut; przerwana ścieżka
  jest wznawialna z tego samego śladu.
- **Właściciel mówi**, aplikacja słucha (ASR) albo właściciel pisze z literówkami.
  Każdy krok ma wypowiedź kanoniczną i co najmniej trzy warianty szumu (§2);
  tester wybiera wariant losowo i zapisuje, który.
- **Komunikaty krótkie**: echo interpretacji w jednym zdaniu („Rozumiem: import
  pliku X, duplikaty linkuj. Tak?”), potem wynik w ≤ 3 zdaniach z liczbami.
  Wszystko dłuższe jest pod „więcej” (rozwijane), nie w pierwszym komunikacie.
- **Pytania tylko tak/nie, numerowane**, zadawane wyłącznie gdy odczyty dają
  materialnie inną pracę (R43; preferencja właściciela). Właściciel odpowiada
  „1 tak, 2 nie” głosem; ASR tego formatu jest testowany osobno (§2.4).
- **Akcje nieodwracalne i sprzeczne z `never`** zawsze pytają (EC-G05), niezależnie
  od ustawień automatyzacji; to jedyne pytanie, którego ustawienie nie wyłącza
  (inicjator: `invariant:integrity` / `owner_rule` ×10 — oba wymienione w R43).
- **Koszt** przed każdą wysyłką płatną: jedna liczba i nazwa dostawcy; ×10 → stan
  `requires_confirmation` jako pytanie numerowane.
- **Powierzchnie** (decyzja Claude, odwracalna): ścieżki P3–P7 na **Loom web
  w przeglądarce telefonu** (serwer `loom/server` na osiągalnym hoście); P1 i P2
  dodatkowo na **aplikacji Python/Kivy na Androidzie**, bo właściciel używa jej
  codziennie (THREADS T7). Kolumna „Dziś” mówi, co istnieje na każdej powierzchni.
- **Dane**: prawdziwe eksporty właściciela wolno użyć lokalnie (katalog, import,
  widoki). Wysyłka ich fragmentów do dostawców w P2–P5 wymaga odpowiedzi na
  pytanie Q2 (§7). Do czasu odpowiedzi ścieżki płatne idą na korpusie syntetycznym
  z `loom/tests/fixtures` (LOOM §7.3) i na `ScriptedTransport`.

## 2. Odporność na ASR

### 2.1 Zasady (dane, nie kod)

Leksykon mowy jest packiem (proponowany `loom/data/lexicons/asr.pack`,
R42: słownik dziedzinowy = dane). Zawiera: klasy synonimów poleceń, składanie
diakrytyków, tolerancję literówek per słowo-klucz, wzorce liczb i odpowiedzi
„N tak/nie”, identyfikatory kodu w mowie („podkreślenie”, „kropka”, „ukośnik”).
Silnik stosuje go na wejściu (DUCH §3.1) i zapisuje interpretację jako osobny
węzeł `read_as` z pewnością (EC-G03, abstrakcja A11).

| Zjawisko | Reguła | Test (MR-4) |
|---|---|---|
| brak polskich znaków | składanie `ą→a, ć→c, ę→e, ł→l, ń→n, ó→o, ś→s, ź/ż→z` w obie strony przed dopasowaniem | „zaimportuj eksport” ≡ „zaimportuj eksport” ≡ „ZAIMPORTUJ EKSPORT” |
| literówki | odległość edycyjna ≤ 1 dla słów ≤ 6 liter, ≤ 2 dla dłuższych; tylko na słowach z leksykonu, nigdy na nazwach plików/encji bez echa | „zaimprotuj”, „eskport”, „magazn” |
| synonimy | klasa poleceń w leksykonie; każda klasa ma słowo kanoniczne i listę | import: wczytaj, załaduj, zaimportuj, weź plik, dodaj archiwum; graf: grafem, jako graf, pokaż graf, w grafie; ekspert: tryb ekspercki, zaawansowany, pokaż prompt, pokaż co wysyłasz; magazyn: storage, gdzie leżą dane, gdzie to jest zapisane; tor: ścieżka, wariant, prompt numer; eksperyment: porównaj, puść przez, sprawdź na kilku |
| liczby | „pięć/piec/5/piątka” → 5; „dwa tysiące/2000/2k”; „×10 / razy dziesięć / dziesięciokrotnie” | P5: „pięć torów” w 4 wariantach |
| urwane zdanie | brakujący argument → `absent` → echo z pytaniem o ten jeden argument (nie o całość) | „zaimportuj eksport z…” → „Który plik? 1. ostatni pobrany 2. wybierz” |
| zgubiona negacja | akcje klasy `irreversible` i sprzeczne z `never` zawsze echo + potwierdzenie | „wyślij zdrowie do gpt” → pytanie, nigdy wysyłka |
| homofony w nazwach | nazwy encji dopasowane do grafu (aliasy, R35) z echem; brak trafienia → `absent` + 2 propozycje | „projekt lum” → „Loom?” |
| dwa języki | język odpowiedzi z ustawienia, nie z pytania (chyba że ustawienie „jak pytanie”) | „show me the graf” |
| identyfikatory kodu | „resolve podkreślenie lineage kropka cpp” → `resolve_lineage.cpp`; brak → `absent` | EC-G06 |
| odpowiedź na pytania numerowane | wzorce „1 tak 2 nie”, „jeden tak, dwa nie”, „tak, nie”, „pierwsze tak”, „wszystko tak” | §2.4 |

### 2.2 Protokół wariantów

Dla każdego kroku ścieżki: wypowiedź kanoniczna K i warianty
V1 (bez diakrytyków), V2 (1–2 literówki w słowach-kluczach), V3 (synonim z
leksykonu), V4 (urwane zdanie, tylko tam, gdzie krok ma argument). Zaliczenie
kroku: K, V1, V2, V3 dają **tę samą interpretację** (ten sam `read_as`: intencja +
argumenty) albo to samo pytanie numerowane; V4 daje pytanie o brakujący argument.
Różne interpretacje = regresja (MR-4). Ślad zapisuje wszystkie użyte warianty.

### 2.3 Czego ASR nie może zepsuć (niezmienniki globalne)

- surowy tekst ASR jest zachowany w śladzie i w metadanych wiadomości (EC-G03);
- żadna operacja płatna nie wychodzi z wejścia ASR z pewnością poniżej progu
  (preset, zmienialny) bez echa;
- echo interpretacji ma zawsze ≤ 1 zdanie i zawiera argumenty, które silnik
  rozpoznał, i `absent` dla brakujących.

### 2.4 Test formatu odpowiedzi

Pytania „1. … 2. …” i odpowiedzi „1 tak, 2 nie” w 6 wariantach głosowych
(polski, bez diakrytyków, z „jeden/dwa”, z „pierwsze/drugie”, „tak tak”, „nie na
wszystko”). Zaliczenie: wektor odpowiedzi identyczny we wszystkich wariantach
albo pytanie o jedno nieodczytane pole.

## 3. Ślad ścieżki (nagranie/log do odtworzenia)

Każda ścieżka zapisuje jeden plik `acceptance/<P>-<data>-<n>.json`, schemat
`loom.acceptance_trace/1` (propozycja; dane w `exports/` katalogu danych, nie w repo):

```json
{
  "schema": "loom.acceptance_trace/1",
  "path": "P1", "surface": "loom-web-phone" | "python-android",
  "device": {"installation_id": "…", "clock_source": "device|ntp", "offline": false},
  "steps": [{
    "n": 1,
    "utterance_raw": "zaimprotuj eskport z downloads",
    "asr": {"provider": "…", "confidence": 0.71, "nbest": ["…"]},
    "read_as": {"intent": "import", "args": {"file": "…", "duplicates": "absent"}, "confidence": 0.9},
    "echo": "Rozumiem: import pliku X. Duplikaty: 1. linkuj 2. kopiuj?",
    "questions": [{"n": 1, "text": "…", "answer": "yes", "answer_raw": "jeden tak"}],
    "gates": [{"initiator": "owner_rule", "reason": "x10", "state": "requires_confirmation"}],
    "effective_settings": [{"key": "import.duplicates", "value": "link", "explain": {"scope": "conversation", "rule": "most_specific_wins", "initiator": "user"}}],
    "requests": [{"provider": "…", "model": "…", "payload_sha256": "…", "cost": {"value": 0.004, "provenance": "provider_reported"}}],
    "result_summary": "…",
    "invariants": [{"id": "EC-D01/MR-8", "holds": true, "evidence": "units_before=…, units_after=…"}],
    "duration_ms": 1830
  }],
  "owner_judgement": [{"q": 1, "answer": "yes"}],
  "outcome": "pass" | "pass_with_capability_gap" | "fail",
  "capability_gaps": [{"what": "graph entry view", "initiator": "invariant:capability", "thread": "T3"}]
}
```

Odtworzenie: `requests[].payload_sha256` i `effective_settings[].explain` muszą
pozwolić odtworzyć payload z grafu (EC-H03, MR-9) bez telefonu; odpowiedzi
modeli zapisane (nie wywoływane ponownie). Ślad jest źródłem w grafie
(obszar Metody → dowody), więc każda ścieżka zostawia dowód, nie notatkę.

## 4. Ścieżki

Legenda „Dziś”: **jest** (powierzchnia ma tę funkcję), **częściowo** (jest, ale
bez elementu wymienionego), **brak** (wątek, który to dostarcza). Stan na
2026-10-11 z inwentarza serwera (`/api/import`, `/api/chat`, `/api/media/transcribe`,
`/api/usage-policy`, `/api/graph/*`, `/api/knowledge/*`) i panelu web
(ImportPanel, ChatView, GraphReplyWorkbench, AnalysisPanel, MethodsPanel,
UsagePolicyPanel, UserProfilePanel, SettingsPanel).

### P1 — Import eksportu

**Cel.** Właściciel bierze eksport (OpenAI albo Anthropic ZIP) z telefonu i ma
go w grafie bez strat, z duplikatami zgłoszonymi.
**Odniesienie.** Aplikacja dostawcy nie importuje cudzych eksportów; odniesieniem
jest „widzę w ChatADHD to samo, co w aplikacji źródłowej” (R17/R30).

| Krok | Wypowiedź kanoniczna (K) | Warianty | Musi być widać | Niezmienniki |
|---|---|---|---|---|
| 1 | „Zaimportuj ostatni eksport z pobranych.” | „zaimprotuj ostatni eskport”, „wczytaj paczkę z downloads”, „załaduj eksport z…” (V4) | echo: plik, rozmiar, rozpoznany format albo `unrecognized`; wycena czasu lokalnego; 0 wysyłek płatnych | EC-D02, EC-D03 (katalog przed importem), MR-3 |
| 2 | „Duplikaty linkuj, nic nie trać.” | „duplikaty laczyc nic nie gubic”, „jak coś jest podwójnie to połącz”, „nie dubluj ale…” (V4) | echo ustawienia z zakresem (ta operacja); liczba linków-propozycji po imporcie | EC-D01, EC-D05, MR-7, MR-8 |
| 3 | (po imporcie) „Ile rozmów, ile wiadomości, ile nierozpoznanych?” | „ile rozmow i wiadomosci”, „co nie rozpoznałeś”, „statystyki importu” | trzy liczby + liczba `absent` załączników z lokatorami; `{known:false}` tam, gdzie nie policzono | EC-D07, EC-D11, R44 |
| 4 | „Pokaż jedną nierozpoznaną.” | „pokaz nierozpoznana”, „co tam było nie do odczytania” | bajty (fragment) + status + wersja rozpoznawacza + powód | EC-D02, EC-D09 |
| 5 | „Zaimportuj to samo jeszcze raz.” | „jeszcze raz ten sam plik”, „powtorz import” | komunikat „ten sam plik i parser — zwracam poprzedni import; `force` zrobi kopię”; graf bez zmian | MR-1, EC-D05 |

**Pytania tak/nie do właściciela po ścieżce.** 1. Czy w ChatADHD widzisz te same
rozmowy co w aplikacji źródłowej? 2. Czy coś, co pamiętasz z eksportu, zniknęło?
**Dziś.** Loom web: ImportPanel + `/api/import` **jest**; nierozpoznane jako węzły i
duplikaty z propozycją **brak (T1)**; zwrot poprzedniego importu **jest**.
Python Android: ImportPanel **jest**; nierozpoznane psują import (**brak**, T1/T7 nie
obejmuje Pythona — decyzja: Python raportuje, nie naprawia).
**Ślad.** liczby jednostek przed/po, lista `absent`, hash ZIP.

### P2 — Rozmowa

**Cel.** Zwykła rozmowa z telefonu z kontekstem z grafu, bez domyślnej blokady
(WP-A), z podglądem kosztu.
**Odniesienie.** Aplikacja dostawcy z tym samym pytaniem (bez kontekstu grafu).

| Krok | K | Warianty | Musi być widać | Niezmienniki |
|---|---|---|---|---|
| 1 | „Nowa rozmowa w projekcie Loom.” | „nowa rozmowa projekt lum”, „zacznij rozmowe o loomie” | rozmowa utworzona w zakresie projektu; echo zakresu | EC-B04 (zakres), MR-6 |
| 2 | „Co ustaliliśmy o imporcie duplikatów?” | „co ustalilismy o duplikatach importu”, „jak było z duplikatami przy imporcie”, „co z duplikatami” (V4) | przed wysyłką: koszt + model + liczba elementów kontekstu; odpowiedź z cytatami i lokatorami; „pominięte z budżetu: n” | EC-H03 (powody włączenia), ACCEPTANCE #1/#3 |
| 3 | „Więcej szczegółów, ten sam zakres.” | „wiecej szczegolow tak samo szeroko”, „rozwiń ale nie szerzej” | szczegółowość rośnie, zakres bez zmian (dwa pokrętła) | R28, ACCEPTANCE #3 |
| 4 | „Dziś krótko, mam zły dzień.” | „dzis krotko zly dzien”, „krótkie odpowiedzi dzisiaj” | echo: ustawienie długości na zakresie „dziś” (wygasa); kolejna odpowiedź ≤ ustalonej długości | EC-B10, MR-6 z wygaśnięciem |
| 5 | „Dlaczego tak odpowiedziałeś, co wysłałeś?” | „co wyslales do modelu”, „pokaż co poszło” | cztery przekroje (historia / obowiązuje / graf / wysłano) z hashem payloadu | R26, EC-H03, MR-9 |

**Pytania.** 1. Czy odpowiedź z kontekstem grafu była lepsza niż w aplikacji
dostawcy na to samo pytanie? 2. Czy coś z kontekstu było zbędne?
**Dziś.** Loom web: ChatView **jest**, podgląd kosztu na czacie **brak (T6)**,
`ContextSet` na ścieżce czatu **brak (P4b)**, zakres „dziś” z wygaśnięciem **brak (A3/T13)**,
przekroje audytu **częściowo** (ConversationSourceView). Python: rozmowa **jest**,
reszta **brak**.
**Ślad.** payload hash, lista elementów kontekstu z powodami, koszt.

### P3 — Odpowiedź grafowa obok liniowej

**Cel.** Każda odpowiedź ma pakiet grafowy osadzony z pochodzeniem; właściciel
widzi oba i może wyłączyć tekst.
**Odniesienie.** brak u dostawców (cecha własna); osąd: „czy graf mówi to, co tekst”.

| Krok | K | Warianty | Musi być widać | Niezmienniki |
|---|---|---|---|---|
| 1 | „Odpowiedz grafem i tekstem.” | „odpowiedz grafem i tekstem”, „pokaż jako graf obok”, „w grafie też” | dwa panele; pakiet z `produced_by`; liczba twierdzeń w pakiecie | EC-C05, MR-20 |
| 2 | „Które twierdzenie w tekście nie ma źródła?” | „co w tekscie jest bez zrodla”, „gdzie nie masz cytatu” | lista twierdzeń „bez źródła” (może być pusta, wtedy „0”) | MR-20 |
| 3 | „Odpowiedz tylko grafem.” | „tylko graf”, „bez tekstu”, „sam graf” | tekst ukryty; pakiet bajtowo ten sam co w kroku 1 przy tym samym przebiegu | EC-C05 (A19) |
| 4 | „Osadź to w grafie.” / „Nie osadzaj.” | „zapisz do grafu”, „dodaj do wiedzy”, „nie zapisuj” | polityka przyjęcia (ask/candidate/auto) jako echo; węzły ze statusem `candidate`, nie fakt | R39, DUCH §3.5 |
| 5 | „Pokaż, z czym to się łączy, czego się nie spodziewam.” | „co to laczy nieoczywistego”, „jakie nieoczywiste powiązania” | krawędzie z pewnością < próg jako osobna lista, z pytaniem „przyjąć? 1…n” | DUCH §2.2, EC-C04 |

**Pytania.** 1. Czy graf mówi to samo co tekst? 2. Czy wśród „nieoczywistych”
było coś trafnego?
**Dziś.** GraphReplyWorkbench + chat-graph **jest**; tryb „tylko graf” i test
MR-20 **brak (A19)**; propozycje nieoczywiste jako osobna lista **częściowo**
(kandydaci w KnowledgeWorkbench).
**Ślad.** pakiet (hash), lista „bez źródła”, decyzje przyjęcia.

### P4 — Tryb ekspercki z edycją promptów (R48)

**Cel.** Właściciel widzi każdy krok przepływu (DUCH §3) i edytuje prompt jako
dane; zmiana tworzy wersję metody, nie nadpisuje.
**Odniesienie.** system prompt w aplikacji dostawcy (jeden, globalny).

| Krok | K | Warianty | Musi być widać | Niezmienniki |
|---|---|---|---|---|
| 1 | „Tryb ekspercki.” | „tryb ekspert”, „zaawansowany”, „pokaż co wysyłasz” | kroki 1–5 przepływu jako panele: interpretacja, wybór kontekstu, tory, odpowiedź, uczenie | DUCH §3 |
| 2 | „Pokaż prompt analizy.” | „pokaz prompt analizy”, „jaki prompt do analizy”, „prompt semantyczny” | treść promptu + wersja (hash) + metoda + gdzie używany (liczba przebiegów) | R41 |
| 3 | „Zmień: dodaj ‘zawsze podawaj datę decyzji’.” | „dopisz zawsze podawaj date decyzji”, „dodaj do promptu żeby podawał datę” | nowa wersja v+1 z hashem; stara zostaje; echo: „20 000 przebiegów na v; przeliczyć? 1. nie 2. próbka 50 3. wszystko (koszt X, ×10 → potwierdzenie)” | EC-C03, MR-1, MR-13 |
| 4 | „Próbka.” | „probka”, „na próbce”, „2” | eksperyment na próbce; porównanie v vs v+1 na tych samych wejściach; koszt | EC-C01, MR-12 |
| 5 | „Wróć do starej wersji w tym projekcie.” | „wroc do starej w tym projekcie”, „stary prompt dla tego projektu” | ustawienie wersji na zakresie projektu; wyjaśnienie wartości efektywnej | EC-F04, MR-6, MR-16 |

**Pytania.** 1. Czy widzisz każdy krok, który chciałbyś zmienić? 2. Czy coś
było ukryte?
**Dziś.** AnalysisPanel (podgląd dokładnego zapytania) i MethodsPanel (wersje)
**jest**; edycja promptu jako nowa wersja z UI **częściowo** (prompty są packami,
edycja przez plik); przeliczenie przyrostowe **brak (A5/T2)**; wersja na zakresie
projektu **brak (A3/T3)**.
**Ślad.** hashe v i v+1, wyniki próbki, ustawienie zakresu.

### P5 — Eksperyment 5-torowy przy imporcie (R48)

**Cel.** Zaraz po imporcie: 5 presetów na próbce warstwowej, ocena
wielokryterialna, rekomendacja presetu do przyjęcia na zakresie.
**Odniesienie.** brak u dostawców; osąd: „który tor wyciąga decyzje najlepiej”.

| Krok | K | Warianty | Musi być widać | Niezmienniki |
|---|---|---|---|---|
| 1 | „Puść próbkę z tego importu przez pięć promptów.” | „pusc probke przez piec promptow”, „sprawdź na 5 torach”, „porównaj pięć wariantów analizy”, „przez piątkę promptów” | próbka: k rozmów warstwowo (liczby); 5 torów z nazwami, modelami, wersjami; **wycena przed wysyłką**: suma = Σ torów; ×10 → pytanie 1 | GRAPH_ENTRY §7, EC-C01, MR-13 |
| 2 | „Tak.” / „1 tak” | §2.4 | wykonanie z postępem; przerwanie w połowie nie gubi (test: zamknij przeglądarkę, wróć) | EC-C09, MR-1 |
| 3 | „Pokaż, w czym się nie zgadzają.” | „gdzie sie roznia”, „tylko niezgodności” | zbiory: zgodne / sprzeczne / tylko-u-X; symetria | EC-C02, MR-12 |
| 4 | „Który najlepiej wyciąga decyzje i ile kosztował?” | „ktory najlepszy do decyzji i koszt”, „rekomendacja z kosztem” | ranking po kryteriach (schemat, ugruntowanie, zgodność, pokrycie, koszt, stabilność) z liczbami; rekomendacja jako propozycja | R48 |
| 5 | „Przyjmij na ten projekt.” | „przyjmij dla projektu”, „ustaw to w tym projekcie” | preset przyjęty na zakresie projektu z inicjatorem; eksperyment w grafie jako dowód metody | EC-C11, MR-6 |

**Pytania.** 1. Czy rekomendowany tor zgadza się z Twoim odczuciem? 2. Czy koszt
był taki, jak wycena (±)?
**Dziś.** **brak** jako przepływ (A6: tor jako dane, ExperimentSpec P5 otwarte);
składniki istnieją: MethodRegistry, W2 wycena (`/api/usage-policy`), `/api/knowledge/run`.
Ścieżka wykonalna ręcznie przez 5 osobnych przebiegów — to nie zalicza P5.
**Ślad.** eksperyment (DAG torów), pokwitowania per tor, ranking, decyzja przyjęcia.

### P6 — Widok wejściowy (R45)

**Cel.** Zapytanie bez parametrów z telefonu pokazuje obszary z licznikami
`{known}` i `expand`; rozmiar stały niezależnie od grafu.
**Odniesienie.** ekran główny aplikacji dostawcy (lista rozmów).

| Krok | K | Warianty | Musi być widać | Niezmienniki |
|---|---|---|---|---|
| 1 | „Co jest w grafie?” | „co jest w grafie”, „pokaz wszystko”, „widok startowy”, „co masz” | obszary (z packa `entry`), każdy: licznik albo `{known:false, stale_since}`, `expand` | EC-H06, MR-7 |
| 2 | „Rozwiń Metody.” | „rozwin metody”, „pokaż metody”, „metody” | metody, wersje, presety, dowody; licznik rozjazdów rekomendacja/ustawienie | EC-C11 |
| 3 | „Co wiesz o mnie?” | „co o mnie wiesz”, „moj profil”, „co wiesz o mnie i skąd” | sloty ze stanami `unknown/declined/never`, pochodzenie każdego, edycja | EC-A06, R39 |
| 4 | „Ukryj Ontologię, Ekonomię daj na górę.” | „schowaj ontologie ekonomia na gore”, „ekonomia pierwsza” | kolejność/ukrycie jako ustawienie użytkownika (pack entry nadpisany na zakresie osoby) | R40, MR-6, MR-15 |
| 5 | „Gdzie leżą moje dane?” | →P7 | obszar Magazyn widoczny w pierwszym widoku (R51) | — |

**Pytania.** 1. Czy od razu wiesz, co jest i ile? 2. Czy czegoś brakuje w pierwszym widoku?
**Dziś.** **brak (T3)**; UserProfilePanel (krok 3) **jest**; GraphView **jest**,
ale nie jest widokiem wejściowym (lista węzłów, nie obszary z licznikami).
**Ślad.** odpowiedź widoku (JSON), czas, rozmiar bajtów vs rozmiar grafu.

### P7 — Magazyn (R51)

**Cel.** Właściciel widzi, gdzie fizycznie leżą dane per kategoria, kto ma
dostęp, czym zaszyfrowane; przenosi kategorię; ustawia regułę egressu.
**Odniesienie.** ustawienia prywatności dostawcy (jeden przełącznik).

| Krok | K | Warianty | Musi być widać | Niezmienniki |
|---|---|---|---|---|
| 1 | „Gdzie leżą moje dane?” | „gdzie leza dane”, „storage”, „pokaż magazyn” | tabela kategoria × magazyn (serwer / chmury / lokalnie) z rozmiarami, szyfrowaniem, gestią klucza, ostatnią synchronizacją; `{known:false}` dla niesprawdzonych | R51, EC-E04 |
| 2 | „Zdrowie tylko na telefonie.” | „zdrowie tylko lokalnie”, „zdrowie zostaje na telefonie” | ustawienie rozmieszczenia na zakresie osoby; lista rekordów do przeniesienia z liczbą i rozmiarem; wykonanie z postępem, wznawialne | EC-E01, EC-E04, MR-17 |
| 3 | „Nic o rodzinie do żadnego dostawcy.” | „rodzina nigdzie nie idzie”, „rodziny nie wysyłaj” | reguła egressu; test: wysyłka pytania o rodzinę w P2 → payload bez tych rekordów, komunikat „pominięto n z powodu reguły (Ty, dziś)” | EC-E01, EC-E06, MR-10, R43 |
| 4 | „Co wysłałeś do OpenRouter w tym tygodniu?” | „co poszlo do openroutera”, „wysyłki z tygodnia” | rejestr egressu: liczba, kategorie, hashe, klucz (id), koszt | EC-E05 (A8) |
| 5 | „Włącz szyfrowanie kwantowo odporne.” | „szyfrowanie kwantowe”, „postkwantowe”, „pqc” | preset hybrydowy (ML-KEM+X25519, ML-DSA, AES-256-GCM, Argon2id) jako ustawienie; echo gestii klucza i ostrzeżenie o braku odzyskiwania, jeśli wybrane | EC-E03, EC-E09, T5 |

**Pytania.** 1. Czy widzisz na jeden rzut oka, gdzie co leży? 2. Czy reguła
egressu zadziałała tak, jak chciałeś (krok 3)?
**Dziś.** **brak (T5: inwentarz → projekt → implementacja)**; SettingsPanel i
`/api/secrets` **jest** (klucze API, nie magazyn); szyfrowanie AES w Pythonie **jest**
(bez PQC, bez per kategoria).
**Ślad.** tabela magazynu, zdarzenia przeniesienia z hashami, rejestr egressu.

## 5. Nowe kryteria akceptacyjne (kontynuacja numeracji z 2026-09-29)

| # | Kryterium | Wymaganie | Rodzaj / obszar | Status repo 2026-10-11 |
|---|---|---|---|---|
| 18 | Warianty szumu ASR (K, V1–V3) dają tę samą interpretację `read_as` albo to samo pytanie; V4 pyta o brakujący argument; surowy tekst zachowany (MR-4) | R53, DUCH §3.1 | U (leksykon jako pack) + E (telefon) | — (A11; leksykon nie istnieje) |
| 19 | Akcja nieodwracalna albo sprzeczna z `never` z wejścia ASR nigdy nie wykonuje się bez potwierdzenia numerowanego; potwierdzenie zapisane | R39, R43 | U (ScriptedTransport) | — |
| 20 | Strażnik ×10 zachowuje się identycznie na czacie, wsadzie, odpowiedzi grafowej i eksperymencie; skalowanie kosztów i budżetu przez k nie zmienia liczby pytań (MR-13) | zasada właściciela, R43 | U | częściowo (usage_policy; tylko odpowiedź grafowa — T6) |
| 21 | Import: nierozpoznane wchodzą jako węzły z lokatorem i historią prób; liczba bajtów wejścia = rozpoznane + opaque; ponowny import tego samego = brak zmian (MR-1, MR-8) | R44, R50 | U + E | — (T1) |
| 22 | Dwa eksporty tej samej rozmowy → jedna rozmowa logiczna z dwoma `projection_of`, różnice jako twierdzenia; `force` = kopia z adnotacją (MR-3, MR-7) | R35, R50 | U | — (T1, A9) |
| 23 | Odpowiedź grafowa i liniowa z jednego przebiegu: każde twierdzenie liniowe ma odpowiednik z cytatem albo „bez źródła”; pakiet identyczny niezależnie od trybu widoku (MR-20) | DUCH §3.4 | U + E | — (A19) |
| 24 | Edycja promptu = nowa wersja z hashem; stare wyniki zostają; przeliczenie tylko domknięcia zależności, z wyceną i ×10 (MR-1, MR-9, MR-13) | R41, R48 | U | — (A5) |
| 25 | Eksperyment N-torowy: wycena = Σ torów przed wysyłką; permutacja torów nie zmienia ocen (MR-12); przerwanie nie gubi; tor niedostępny = `incomplete` z inicjatorem | R48, R43 | U + E | — (A6) |
| 26 | Widok wejściowy: stały rozmiar odpowiedzi niezależnie od grafu; liczniki `{known:false, stale_since}` zamiast zer; kolejność/ukrycie obszarów jako ustawienie przeżywające pack (MR-7, MR-15) | R45, R40 | U + E | — (T3) |
| 27 | Wartość efektywna każdego ustawienia ma wyjaśnienie (zakresy, reguła, inicjator, data), z którego da się ją odtworzyć (MR-16); nadpisanie na zakresie tury wygasa | R47 | U | — (A3) |
| 28 | Reguła egressu kategorii: payload różni się dokładnie o rekordy kategorii i ich domknięcie przesłanek; rejestr egressu ma każdą wysyłkę (MR-10) | R51, R19 | U (ScriptedTransport) | — (A7, A8, T5) |
| 29 | Przeniesienie kategorii między magazynami: każdy lokator rozwiązuje się do tego samego hasha; w każdej chwili ≥ 1 kompletna kopia; wznawialne (MR-17) | R51 | U + E | — (T5) |
| 30 | Kwarantanna źródła („nie ucz się”): obserwacje i FTS bez zmian; aktywne inferencje minus domknięcie; zdjęcie = identyczność (MR-14) | DUCH §5 | U | — (A4, A5) |
| 31 | Ślad ścieżki (`loom.acceptance_trace/1`) pozwala odtworzyć każdy payload z grafu bez urządzenia (MR-9) i jest źródłem w grafie | R26, R41 | E | — |

Reguły pomiaru z 2026-09-29 (§16 notatki) obowiązują. Dodatkowo dla telefonu:
czas odpowiedzi i rozmiar odpowiedzi są raportowane z urządzeniem i siecią;
nie są bramką, dopóki właściciel nie ustawi progu (preset, nie reguła).

## 6. Kolejność i co odblokowuje co

| Kolejność | Ścieżka | Odblokowana przez | Można zacząć dziś |
|---|---|---|---|
| 1 | P2 kroki 1–2, 5 (rozmowa, audyt) | WP-A (zamknięty), T6 (koszt) | tak, bez kroku 3–4 i bez podglądu kosztu |
| 2 | P1 (import) | T1 | kroki 1, 3, 5 tak; 2, 4 po T1 |
| 3 | P3 (graf obok) | — | kroki 1, 4 tak; 2, 3, 5 po A19 |
| 4 | P4 (ekspert) | T2 (przeliczenie), T3 (zakresy) | kroki 1–2 tak |
| 5 | P6 (widok wejściowy) | T3 | krok 3 tak |
| 6 | P7 (magazyn) | T5 | nie (inwentarz jako dokument można pokazać) |
| 7 | P5 (eksperyment) | A6 (język torów), T11 | nie |

ASR (§2) jest warunkiem wszystkich: leksykon jako pack i `read_as` w śladzie
(A11) wchodzą pierwsze — decyzja Claude: do T7 (Python zapisuje surowy ASR
w metadanych) i jako nowy mały pakiet po T3 dla Loom (pack + test MR-4 na
`ScriptedTransport`), bez zmiany schematu.

## 7. Pytania do właściciela (tylko tak/nie, tylko z zakresu THREADS §6)

1. **Pieniądze.** Czy ścieżki P2–P5 na telefonie mogą zużyć łącznie do 2 USD
   z pozostałego salda klucza OpenRouter (3,55 USD), poza budżetem wątku 7?
2. **Prywatność.** Czy w P2, P3 i P5 wolno wysłać fragmenty Twojego prawdziwego
   eksportu do modeli przez OpenRouter (Anthropic, OpenAI, Google), czy tylko
   korpus syntetyczny?

Do odpowiedzi: ścieżki płatne idą na korpusie syntetycznym i `ScriptedTransport`;
reszta na prawdziwych eksportach lokalnie. Nic innego nie czeka na właściciela.

## 8. Decyzje podjęte w tym dokumencie (Claude; każda odwracalna jednym słowem)

- Powierzchnia główna testów z telefonu: Loom web w przeglądarce telefonu; Python
  Android dla P1/P2 jako równoległa (DUCH §6: aplikacja Python żyje równolegle).
- „Zaliczone” = niezmienniki + ślad + osąd właściciela (§0), nie „oczekiwany wynik”.
- Leksykon ASR jest packiem; literówki tolerowane tylko na słowach z leksykonu;
  nazwy plików i encji zawsze z echem.
- Ślad ścieżki jest źródłem w grafie (dowód metody), format `loom.acceptance_trace/1`.
- Pytania numerowane tak/nie jako jedyna forma pytań w ścieżkach; jedyne pytania,
  których ustawienie nie wyłącza: nieodwracalne, sprzeczne z `never`, ×10.
- Python: nierozpoznane dane raportuje, nie naprawia (T7 nie rozszerza się o T1).
