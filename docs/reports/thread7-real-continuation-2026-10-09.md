# Wątek 7 / C — kontynuacja 01, 2026-10-09

Wykonano sześć etapów nowego pakietu: poszerzenie rzeczywistych źródeł,
preparacje kontekstu, admission Jev, testy kolejki/payera, analizę i przekazanie
danych dla B. **336 testów przeszło; nowych wywołań modeli: 0; nowy koszt: 0 USD.**
Nie uzyskano nowych wyników jakości modeli. Preparacje i lokalne admission
nie są wykonaniem ani zgodą dostawcy.

Praca pozostaje na `gpt/thread7-real-2026-10-09`, od
`69880859802267f1b38b82eeaecd6fdc5522a47a`. Nie zmieniono main, STATE/INDEX,
produkcyjnego runtime, UI, wspólnych schematów ani cudzych gałęzi. Poprzednie
freeze, specyfikacje, rachunki i wyniki zachowano. Nowy katalog dowodów:
[`continuation_01`](../research/thread7_real_2026-10-09/continuation_01/).

## Źródła i rzeczywisty zakres

Najpierw odzyskano i zweryfikowano wcześniejszy prywatny checkpoint:
`PRIVATE_Thread7_real_workflow_2026-10-09.zip`, SHA256
`137c49ab13681d036b955039a580aff3cb86bc342b62981c484abcfe9363c28e`;
291 payloadów zgadzało się z manifestem. Nie powtórzono opłaconych prób.

Następnie odzyskano **bajty** większego wycinka
`ChatADHD_Loom_LEM_takeout_test_2026-10-01.zip`, SHA256
`2bff52e83852efcd596bc5c717dd4e13840c4a6a36ef34daeb7356c2c4f17ec4`,
64 091 294 bajty. Zweryfikowano 146 elementów ZIP i 103 elementy towarzyszącego
audytu. Powtórnie wykorzystano istniejące selektory i 179 kanonicznych rozmów;
nie przetwarzano od nowa całego wcześniejszego archiwum. Odnalezione foldery
Drive nie były traktowane jako posiadanie ich zawartości.

| Zbiór | Rodziny/rozmowy | Wiadomości | Okres | Status |
|---|---:|---:|---|---|
| Historyczny panel C | 3 OpenAI | 44 | według wcześniejszego freeze | 8 pytań zależnych, zachowany osobno |
| Odzyskany większy wycinek | 65 OpenAI + 114 Anthropic | 14 652 | 2025-10-08–2026-09-12 | źródło do doboru, nie nowa ewaluacja |
| Nowy panel | 6 OpenAI + 6 Anthropic | 2079 | 2026-01-23–2026-09-09 | 2085 węzłów, 8 tuning + 4 prowizoryczna walidacja |

Nowy freeze ma SHA256
`324d33a17ac09cc3b1c1dd11fb875b10d2a7ccf1f4c7b2fb06c313f9ba7e209f`.
Zachowuje pełne rozmowy, natywne pola, gałęzie, tool calls i dostępne załączniki.
Metoda doboru i hashe są w `corpus-policy*.json`, `corpus-receipt.json` oraz
prywatnym `corpus/FREEZE.json`. Union rodzin wykorzystuje natywne identyfikatory
wiadomości i wspólne istotne fragmenty tekstu; eliminuje historyczny panel oraz
kontroluje duplikaty eksportów. Dodatkowa poprawka wymusza zgodność wykluczeń
z rzeczywistym checkpointem, zamiast przyjmować pusty katalog za brak duplikatów.

Panel ma po 4 rodziny krótkie, średnie i długie; 6 ze strukturą rozgałęzień i 6
z tool calls. Selekcja leksykalna wskazała kandydatów: korekty 7, zmiany preferencji
11, zmiany tematu 8, zadania wieloetapowe 10, checkpointy 4. To wskazania do
adnotacji, **nie potwierdzony semantyczny gold**. Nie dopisano zjawisk syntetycznie.

Z wcześniejszego audytu oznaczono 16 źródłowych obiektów uprzednio ocenianych.
W 4 rodzinach walidacji nie wykryto takiej ekspozycji; nieznana szersza historia
wyklucza nazwanie ich niezależnym blind/holdoutem. Dawnego blind nie odczytywano.
46 wywołań Jev z audytu 1 października należy do wcześniejszego, odrębnego
programu: nie dodano ich do rachunku tej kampanii ani nie odtworzono wywołań.

Luki: dostępny większy wycinek zawiera 103 pliki binarne; nowy panel zachowuje
11 wybranych dostępnych binariów i 75 nierozwiązanych rekordów referencji.
Audyt źródła wskazuje m.in. 168 brakujących kandydatów załączników OpenAI,
1 pominięty ucięty ZIP oraz 727 referencji Anthropic do zewnętrznych binariów
(692 unikalne). Te liczniki mają różne jednostki i nie należy ich sumować.
W sześciu rodzinach Anthropic zachowano sześć nierozwiązanych zewnętrznych
natywnych parent ID. Nie zastąpiono ich domniemanymi korzeniami.

## Wykonany kod i preparacje

`experiment_corpus_v1.py` odtwarza wycinek i rodzinny podział z istniejących
źródeł. `experiment_context_preparation_v1.py` w wersji preparacji 2 produkuje
rzeczywiste wejścia, wyjścia, hashe, dokładne JSON pointers, pochodzenie oraz
opis strat i dodanej interpretacji. Plan, prompty, parametry, profile i sposób
budowy widoku są danymi. Użyto istniejących danych profilu, bez nowego silnika
preferencji.

| Wymiar | Warianty |
|---|---|
| Reprezentacja | dokładny tekst, jawne pola strukturalne, graf źródłowy |
| Rozdzielczość | pełny kontekst, obserwowany checkpoint, wybrany podgraf |
| Odpowiedź | graf bezpośredni, tekst i struktura w jednym wywołaniu, tekst oraz zależna ekstrakcja w drugim |
| Preferencje | brak, jawne adnotacje źródłowe, wersjonowany wybrany profil |

Macierz to 12 zadań × 81 wariantów = **972 intencje**. Bieżący
`context/expanded-v2/MANIFEST.json` ma SHA256
`5ee93a5b92bd94cf6951474e1ae7c5f7250481f4c258347fa7a0b4e140377f64`;
zweryfikowano 790 payloadów. Statusy:

- 450 kompletnych preparacji;
- 225 gotowych pierwszych requestów, zależna ekstrakcja pozostaje pending;
- 297 intencji pending z powodu brakujących zweryfikowanych adnotacji preferencji.

Łącznie istnieje **675 dokładnych body pierwszych requestów**. Indeks obejmuje
1197 rekordów: pierwsze fazy, zależne ekstrakcje i niewykonalne jeszcze intencje.
Dwie dokładne źródłowe adnotacje preferencji dotyczą jednej rodziny; są
prowizoryczną pracą badacza, bez deklaracji zatwierdzenia przez właściciela.

Graf źródłowy opisuje strukturę eksportu, nie inferowaną semantykę. Checkpoint
jest deterministycznym widokiem obserwowanej granicy źródła, nie streszczeniem
asystenta ani checkpointem zadeklarowanym przez użytkownika. Podgraf ma jawny
wybór i straty; nie uzupełnia brakujących relacji. Wersja 2 naprawia pominięcie
alternatywnego tekstu Anthropic w 8 wiadomościach: wszystkie 1109 natywnych
pól tekstowych są obecne albo mają dokładny alias. Wersja 1 i nieudana próba
uzupełnienia adnotacji pozostają w prywatnych dowodach, bez wywołań modeli.

`materialize_extraction` wymaga rzeczywistej pełnej pierwszej odpowiedzi,
jej tożsamości i hashy; nie tworzy pozornej ekstrakcji. Obie fazy mają odrębne
błędy i koszty. `connect_prepared.py` łączy zamrożone źródła/preparacje z
istniejącą kolejką i formatem manifestu payera. Dla małego scope odrzuca
warianty zależne, zamiast udawać ich wykonanie.

## Jev: co rzeczywiście odblokowano

Stary `jev_live_pilot.validate_body` chronił dwa warunki: limit instrumentu
16 384 bajtów kanonicznego JSON oraz historyczny bound kosztu
`(bajty + 1024) × 0,000000042 USD ≤ 0,001 USD`. Jest egzekwowany także przez
manifest/adapter; pozostałe kontrole rezerwacji znajdują się w starym runnerze.

`jev_admission_v2.py` zachowuje stary profil i wprowadza wersjonowaną politykę
65 536 bajtów / 0,003 USD planowanej jednostkowej rezerwacji. Wszystkie **16/16**
pełnych intencji historycznego panelu ma jednoznaczne admission z uzasadnieniem:
stary profil 4/16, nowy 16/16 lokalnie. Body mają 9829–50 167 bajtów; zachowano
dokładne bajty i parametry, bez przycinania i przemianowania scope. Nieobsługiwane
modele, pola i parametry są jawnie odrzucane. Obowiązują cztery osobne granice:
instrument, polityka badania, rzeczywisty dostawca oraz budżet kampanii.

Historyczny model rachunkowy daje 0,025759020 USD, a suma planowanych minimalnych
rezerwacji 0,048 USD. **Nie są to aktualna wycena, dokonane rezerwacje ani
potwierdzony górny koszt fazy.** `provider_eligibility`, aktualna zgoda kampanii
i ceny pozostają unknown. Freeze Jev v2:
`ae7d21b780fb40aa318050270c4cff4dd1f468c5b7c0f4b3f573a2a0d7cf43ab`.

## Kolejka, payer i odporność

Rozbudowano `experiment_workflow_v1.py` i dodano `experiment_payer_boundary_v1.py`.
Jedynym ledgerem pieniędzy pozostaje istniejący `research_programme_runner.PrivateLedger`.
Wrapper sprawdza w nim rezerwację, tożsamość kampanii i klucza, manifest,
operację, dokładne body oraz trwały dowód rozpoczęcia przed POST.

Kontrolowany transport sprawdza restart po awarii przed rezerwacją, po niej,
po wysłaniu, przed zapisem odpowiedzi, po zapisie i przed rozliczeniem.
Sprawdzono współbieżne claim, duplikaty triggera/checkpointu, anulowanie,
częściową i błędną odpowiedź, niespójny manifest, zmianę spec oraz opóźnione
rozliczenie. Trigger dedup jest per-spec, a migracja zachowuje stary dziennik.
Nie uruchamia się drugi POST po niejednoznacznym wysłaniu; unknown/pending
nie staje się sukcesem ani zerowym kosztem. Pierwszy dowód jest niezmienny,
kolejne dopisywane.

Jawna granica: istniejący reconcile GET rozlicza kwalifikujący się zapisany
`billing_pending` z ID generacji. Niepełny dziennik z rezerwacją lub osierocona
odpowiedź po crashu wymaga zewnętrznego dowodu i osobno sprawdzonej procedury
odzyskania; pozostaje zablokowana. Nie ma obietnicy exactly-once inference.
Testy restartu po wyjątkach nie dowodzą odporności na awarię dysku/zasilania.

## Kolejność, cache i skalowanie mechaniki

Wybieralne strategie: blokowa, kontrolowana losowa oraz cache-aware w oknie.
Pełna tożsamość wariantu i planowana kolejność są zachowane; faktyczne
admission dispatch ma osobny dziennik. Zgodne prefiksy/model/provider/endpoint
służą grupowaniu bez zmiany promptu. Nie zakłada się wspólnego cache modeli.
Seed określa porządek mechaniki, nie deterministyczne inference.

`CacheObservedTransport` zachowuje ograniczenia istniejącego transportu,
ustawia `X-OpenRouter-Cache: false` i zapisuje obserwowany status response cache.
HIT nie jest niezależnym wykonaniem, brak nagłówka pozostaje unknown. Rozdzielono
prompt cache, cache odpowiedzi i replay. Oficjalne dokumenty OpenRouter
sprawdzono 2026-10-09; referencje i zakres są w `provider-documentation.json`.
Przed realnym dispatch wymagane jest ponowne aktualne sprawdzenie zasad,
cen i faktycznych endpointów.

Leniwa macierz ma 400 000 000 000 000 000 000 kombinacji z powtórzeniami;
bez materializacji całości zapisano po 1000 zadań każdej strategii:

| Strategia | Planowanie + trwały zapis | Szczyt alokacji Python | SQLite | Restart / duplikaty |
|---|---:|---:|---:|---|
| blokowa | 1,015 s | 40 273 B | 2 207 744 B | zgodny / 0 |
| kontrolowana losowa | 1,306 s | 32 195 B | 2 207 744 B | zgodny / 0 |
| cache-aware, okno 64 | 0,938 s | 349 436 B | 2 207 744 B | zgodny / 0 |

To syntetyczny test mechaniki, nie jakości modeli. `tracemalloc` nie mierzy
całego RSS ani pamięci SQLite. Wynik nie dowodzi pojemności dysku dla pełnej
macierzy. Historyczne globalne `prefix_grouped` pozostaje jawnie materializujące.

## Analiza, hipotezy i niedowiedziony zakres

`experiment_analysis_v1.py` rozdziela format, referencje i integralność grafu,
zgodność ze źródłem, cel/preferencje, stabilność, koszt i opóźnienie. Oceny
asystenta/modelu nie są utożsamiane z mechanicznym dowodem ani niezależną
adjudykacją. Protokół jest wersjonowany i prowizoryczny, bez domniemanej zgody
właściciela na jego sądy.

Porównania są sparowane na tych samych źródłach; powtórzenia i pytania agregują
się do rodziny. Bootstrap operuje na rodzinach i jest warunkowy względem
obserwowanych par; missingness może być informatywne. Analiza kompromisów
jakość–koszt–czas i stabilności odróżnia zmiany promptu/widoku/protokołu.
Dwufazowe odpowiedzi są wiązane z rzeczywistymi dowodami obu faz; nieznany
koszt/opóźnienie dowolnej fazy nie staje się zerem.

Uruchomiono analizę rzeczywistych 972 planowanych intencji przy 0 odpowiedzi:
972 missing, 0 par obserwowanych, 12 rodzin planowanych (8/4), jakość i
całkowity przyszły koszt `null`; obserwowany nowy wydatek 0 USD. Nie wyliczono
fikcyjnych metryk. Kandydaci na presety są niezwalidowani i nie zmieniają
ustawień użytkownika. Hipotezy o lepszej reprezentacji, preferencjach, grafie,
różnicach korpusów, stabilności i oszczędnościach cache nadal wymagają pomiaru.
Nie dowiedziono skuteczności na całym archiwum, wyższości modelu ani odmiennego
zwycięzcy dla każdego archiwum.

## Testy i pakiet B

Końcowe **336/336** testów, bez pominięć:

| Zakres | Liczba |
|---|---:|
| korpus / zgodność historycznego wykluczenia | 33 |
| preparacje kontekstu | 36 |
| Jev v2 i regresja starego pilota | 33 |
| workflow, granica payera i istniejący payer | 185 |
| analiza | 42 |
| połączenie prawdziwych body z kolejką/manifestem | 7 |

Receipt: `FINAL_TESTS.json`, SHA logu
`de290c694ab0566703732ac3d271d6afcea44f6a051824be7ee384f35c2f39df`.
Fixture tests służą wyłącznie mechanice; testy Jev obejmują odzyskane realne
16 intencji. Niezależny przegląd wykrył i skorygował m.in. pominięcie tekstu
Anthropic, wykluczenia historyczne, łączenie ocen między formami, dwufazową
analizę, dedup między spec i kolejność manifestu. Ostateczny przegląd potwierdził
7/7 testów konektora i kolejność 0–5 / 0–9; to te same testy, nie dodatkowe do 336.

[`HANDOFF_FINAL_B.md`](../research/thread7_real_2026-10-09/continuation_01/HANDOFF_FINAL_B.md)
opisuje projekcję Basic/Expert oraz JSON kontraktu. `handoff-final-artifact-v3.json.gz`
przeszedł istniejące schematy `loom.method_graph/1`, `loom.method_run_trace/1`,
kodek Python i odzyskanie wejść/wyników; SHA256
`2e96dd1af159d167d8c78bc38978f7ade4e88bbbbe58e4803f7cba39935ee577`.
Nie uruchamiano native/C ABI/runtime/UI; testy tej granicy należą do A i B.

## Koszty i blokady

| Pozycja | Stan |
|---|---|
| Historyczna kampania | 720 prób, 0,873216500 USD |
| Archiwalna pozostałość | 4,126783500 USD z nieodnawialnego 5 USD |
| Nowy etap | 0 wywołań, 0 USD; brak nowych rzeczywistych rezerwacji |
| Aktualne usage / rezerwacje / tożsamość klucza / ceny | unknown |
| Dostęp do nowej konkretnej autoryzowanej referencji sekretu | brak |

Nie ponownie przeszukiwano znanych checkpointów pod kątem credential; nie
wyświetlono ani nie commitowano klucza. Reset sesji/abonamentu nie odnowił
budżetu. Brak wiarygodnego preflight blokuje tylko płatny dispatch. W przygotowanych
małych scope bieżące górne oszacowanie kosztu jest `null`, a `dispatch_ready=false`.

## Commity, trwałość i dokładne wznowienie

Wypchnięte przyrosty na tej samej gałęzi:

| Commit | Zakres |
|---|---|
| `cbaffe58ce6b462de6a8815b18b47c211a1eec38` | źródła, nowy panel, manifest startu |
| `9416d6f3f573542b299deb77772418154e7806d0` | Jev v2 i rzeczywiste admissions |
| `804b783d680988517027333e500e26b588657416` | ścisłe powiązanie historycznych wykluczeń |
| `8f1480804ac437db09aeaa96693eb144006e7df0` | kolejka, payer, awarie, kolejność/cache |
| `8fe1fc8979068cfd82cc19a1e588c17d937f1db3` | rzeczywiste preparacje v2 |
| `b9b503f62bb8e8e00c94ab7401e1cd9579129af3` | analiza i pierwsza projekcja kontraktu |

Końcowy commit kodu i receipt prywatnego checkpointu zapisuje
`continuation_01/PUBLICATION.json`. Zewnętrzny receipt wiąże SHA ZIP bez
cyklicznego umieszczania jego własnego hasha w zawartości. Manifest etapu 6
wiąże kod, testy, kontrakty i koszty. Publiczna projekcja przechodzi kontrolę
źródłowych canaries, metadanych, logów i rozpakowanych payloadów grafu; wynik
i ograniczenia tej kontroli są w `public-projection-check.json`.

Punkt wznowienia:

1. Zweryfikować SHA i wszystkie payloady nowego prywatnego checkpointu,
   następnie bieżące `corpus/FREEZE.json`, `context/expanded-v2/MANIFEST.json`
   i `connected-v2/*/FREEZE.json`. Zachować wcześniejsze wersje.
2. Wybrać jedną z zamrożonych alternatyw `thread7-cont01-context-small3-v2`
   (3 warianty × 2 rodziny = 6 wywołań) lub `...small5-v2` (10 wywołań).
   Kolejki są przygotowane; faktycznych dispatch jest 0. Payer manifest zachowuje
   dokładne body i kolejność. Nie uruchamiać pełnej macierzy automatycznie.
3. Po pojawieniu się działającej autoryzowanej referencji do klucza przeprowadzić
   preflight istniejącego payera: tożsamość kampanii, aktualne usage/pending,
   limit nieodnawialny, ceny/bounds, obsługę parametrów i cache. Zapisać plan,
   górny koszt i rezerwacje przed fazą. Nie wyzerować unknown.
4. Utrwalać pierwsze odpowiedzi i późniejsze dowody, rozliczać istniejącym
   payerem. Ekstrakcję drugiej fazy przygotować dopiero z prawdziwej pierwszej
   odpowiedzi; nie powtarzać jej inference przy replay. Następnie użyć
   zamrożonego protokołu, raportować rodziny, missing i klasę adjudykacji.

Wyczerpano odblokowany pakiet. Pozostają nowe odpowiedzi modeli, świeży
preflight, brakujące źródłowe adnotacje/załączniki, niezależna adjudykacja oraz
testy runtime po stronie A/B. Żaden z tych braków nie jest zastąpiony fikcyjnym wynikiem.
