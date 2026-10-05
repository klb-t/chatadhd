# W12 — drugi przyrost: dane prezentacji i jedno źródło presetów

Gotowy do odbioru W9: osiem grup danych przeniesionych do packa, zachowane
domyślne wyniki, pełny CTest **125/125 PASS** bez osłabienia progów.

Gałąź: `gpt/onboarding-2-2026-10-05`. Baza nowej gałęzi:
`66da570d3b5379492e128d940ad474467082c59f` integratora (main e4109df + wybrana implementacja W5; odbiór W5 przez 9 nadal w toku).
Poprzedni W12 `3c0bc36552ef9851f1174946cfb108549aae3228` pozostaje bez zmian i w kolejce.
Zamrożone źródło produktu:
`6adbf2ad00671da40a356b922a8c1a8091b1e5ea`. Końcowy pin testów:
`5115477fda183572acb901cfd2ea98979fb0f45d`. W wejściach buildu po 6ad zmieniły się tylko dwa
pliki testów: `.get<std::string>()` w dwóch asercjach doctest oraz dwa fixture’y
aktualizacji packa z twardego 2 na bieżącą rewizję + 1. Asercje i progi nie
zostały osłabione. Produkcyjne src/include/data i biblioteka core są identyczne.
Native/data: `a41901ce0934bc6dc07686aa1799ce1e1747236e`; ostatni commit dotyczy
wyłącznie błędów bootstrapu UI. Wspólne 375 wejść native/data przed poprawką typów testu ma SHA256 manifestu
`46429f1e77228518979f12db80ab238748f6baf9928a24d2b9938aa6d5153915`.
Po pierwszej poprawce typów manifest miał również 375 wejść i SHA256
`adeb5547db69cba8cfc92b70b4814728106abb8a7ea9d05eb52f2c6a56119c2c`.
Ten pośredni diff manifestów to dokładnie jeden plik testu; końcowy closure
obejmuje także jawnie opisaną korektę fixture’ów grafu. Końcowy manifest 375
wejść ma SHA256 `c49709798ed4035cdc619c21ebb08aea16323ccc8da779614029e59b8c180bb3`;
jego diff względem 6ad to dokładnie te dwa pliki testów.
Testy punktowe, build web, rzeczywisty natywny parytet i końcowy pełny CTest
przeszły. To gotowość tego przyrostu do odbioru, nie zamknięcie całego R39–R42
ani produkcyjnego wpięcia konsumentów 3/10/11.

Oryginalny inwentarz W11 powstał przed W12 i nie miał
`docs/reports/data-in-code/thread-12.md`; ta gałąź zawiera nowy suplement.
W12 przygotował osobny suplement swoich nowych plików, bez dopisywania grup
ani wyników do pierwotnych 695/213. Przeczytano thread-10/thread-11 z W11
`2eb65d475cb6671dbe6388c53bb63eec74ea92a8`. Świeży fetch potwierdził main
`e4109df`, integrator/INDEX `0a81480bd1a70b394bac72e98f6b5e7cb49f5d0b`
i niezmieniony stary W12 `3c0bc36`. Otwarte przekazania INDEX są poniżej.
R42 odczytano z wymagań właściciela na gałęzi Claude, commit
`3cd5848e7484d50d00f19f0855bc2cd228ad7f3b`; main nie zawiera jeszcze tej sekcji.

Zakres następnego przyrostu: teksty i presety prezentacji własnego UI, sześć
wyjaśnień warstw, usunięcie drugiej ręcznej kopii settings/privacy z deklaracji
RuntimeProfile. Domyślne angielskie teksty, wartości, akcje i zachowanie mają
pozostać identyczne. Mechanizmy lifecycle, CAS, prywatności i wykluczeń nie są
przedmiotem zmiany. Testy offline, zapisane syntetyczne odpowiedzi, zero modeli.

## Commit gotowy do odbioru

**`5115477fda183572acb901cfd2ea98979fb0f45d`** — końcowy commit implementacji
i testów, z pełną zieloną bramką. Późniejsze commity na tej gałęzi zawierają
wyłącznie raport, STATE i zamknięte dowody; nie zmieniają źródeł produktu/testów.
Commit dowodów: `a730d15fc81c51526fec692fbc9499e1bfa1dc84`.
Baza `66da570`, niezmieniony prerequisite do `aa887242`. Nowy diff W12 zaczyna
się po prerequisite; integrator może odebrać go po przyjęciu oryginalnego W12.
Poprzednia gałąź pozostaje na `3c0bc36`. Na main nic nie wypchnięto.

## Wykonany zakres

Osobny [thread-12.md](data-in-code/thread-12.md) i JSON opisują osiem nowych grup
`W12-DIC-0001…0008`. Nie zwiększają historycznych 695 grup W11. Native proza
decyzji prywatności, format natywnej inspekcji i etykiety grafu pozostają kolejnymi zadaniami;
diagnostyka i identyfikatory kontraktu czekają na uzasadnioną allowlist W11.
Nie deklarujemy pełnego zamknięcia R42.

Kanoniczny `loom/data/onboarding/ui.pack` zawiera 153 komunikaty EN i 153 PL,
presety trybu/edytorów, kolejność kontrolek, deskryptory prywatności i tokeny CSS.
EN pozostaje domyślny. Dane są wartością węzła `presentation.onboarding`,
ID `onboarding.presentation/v1`, obszar `onboarding/presentation`, revision 2.
Pack użytkownika ma revision 3. Wcześniejszy checkpoint packa 2 jest publiczny,
więc końcowa migracja stylu obramowania dostała nową rewizję, bez ponownego
wykorzystania numeru. Pozostałe wpisy i ich rewizje są niezmienione.
Ta wartość korzysta z dotychczasowych override/disable/exclude/proposal/direct;
trwałe wykluczenie nie jest kasowane przez nowszy pack.

`DefaultLayers` renderuje sześć dawnych objaśnień z efektywnego katalogu,
z opcjonalnym jawnym locale. Dla starszych packów bez bindingu korzysta z danych
wygenerowanych z tego samego packa prezentacji. Jawne suppression nie uruchamia
fallbacku: objaśnienie to null, a native snapshot ma `presentation.available=false`.
Brak prezentacji nie zmienia operacyjnych bindings settings/privacy. Żaden katalog
nie ustawia personalnego `communication.language` ani nie dopisuje informacji
o użytkowniku. UI przyjmuje jawny locale hosta albo default_locale katalogu.

Generator rozwiązuje kanoniczne `$include` pod `loom/data`, wykrywa cykle/wyjście
poza ten katalog i kompiluje `$layer_bindings` z istniejących entries oraz
runtime_bindings. Ręczna druga kopia defaults została usunięta z user.pack;
rozwinięte settings/privacy pozostają takie same. Generuje deterministycznie
`builtin.inc` i `web/src/onboarding/generated/ui.json`; `--check` sprawdza oba.
Brak/uszkodzenie danych daje błąd. RuntimeProfile loader i adapter W11 nie zostały
zmienione. Dyrektywy są formatem źródłowym tego kompilatora, nie obietnicą, że
niezmieniony runtime loader W11 potrafi sam rozwiązać pliki źródłowe.
Baza nie zawiera `loom/runtime_profile.h`: niezmieniony adapter ma warunek
`__has_include`, a inspekcja runtime zgłasza unavailable. Dowód rozwiniętego
presetu nie jest dowodem nowego wpięcia W11. Dotychczasowy mechanizm warstw
nadal rozwiązuje operacyjne defaults; pełne podłączenie loadera pozostaje otwarte.

Istniejący native profil zachowuje zapisany stary pack przy `open/read`. Nowy
katalog i jego grafowy węzeł przychodzą przez jawne `update_pack(user, CAS,
builtin_pack, scenario)`, a nie niejawną migrację na starcie. Część frontendowa
potrafi obsłużyć brak descriptor w starszym adapterze. Native CAS, tombstone
i wartości użytkownika mają pozostać zachowane przy jawnym dosiewie.

Szablony używają inertnego `{{parameter}}`. Nie ma wykonywania wyrażeń ani
ponownego rozwijania wartości. Serializacja strukturalnego JSON między JS/C++
ma ograniczenie dla floatów i kolejności niektórych kluczy Unicode; nie obiecujemy
ogólnego parytetu tych serializerów. Sześć dostarczonych objaśnień nie ma takich
parametrów i jest porównywanych z dokładnymi dawnymi wynikami.

## Weryfikacja

| Sprawdzenie | Wynik | Zakres |
|---|---|---|
| Generator | 18/18 PASS | prawdziwy CLI, źródło defaults, dwa wyjścia, błędy references i stale artifacts |
| Kontroler/helpery UI | 28/28 PASS, 0 skip | niezmienione 17 + 11 prezentacji, syntetyczne fixture; bez testu renderowanego ReactDOM |
| Build native | PASS, strict GCC/Werror | końcowy all-target build, shared/CLI/server/tests; source/test pin 5115477 |
| Build web | PASS, 85 modułów | TypeScript + Vite na nowej bazie |
| Niezależne helpery | 12 przypadków / 47 asercji PASS | katalogi, suppression, szablony i tłumaczenia; bez React/native verdict |
| Comparator dowodów | 25/25 + niezależne 5/5 PASS | kontrola narzędzia; rzeczywisty parytet jest osobnym przebiegiem |
| Rachunek na actual traces | control PASS, 10 mutacji odrzuconych | osobny niezależny runner, kompletność pól/ID/body/support; bez dopisywania do liczby przypadków native |
| Natywny parytet BEFORE/AFTER | PASS; rachunek sprawdzony niezależnie | 9 etapów sesji + 12 native, 21 wyników przygotowania (18 sukcesów, 3 oczekiwane odmowy), 120 wyników oceny polityki |
| Istniejąca baza użytkownika | 15/15 PASS | rzeczywisty producer BEFORE; konsument AFTER na kopii, jawny pack 1 → 3 |
| Pełny CTest | 125/125 PASS, 306,35 s | jeden kompletny przebieg na 5115477, aktualna baza integratora + prerequisite + drugi przyrost |
| Wykonane native | 775 przypadków / 29387 asercji PASS | suma z pełnego LastTest = niezależne doctest --count 775 |
| Native W12 | 65 przypadków / 2988 asercji PASS | 7 zestawów onboarding, w tym nowe 3 + 7 + 4 przypadki |
| Wykonane Python unittest | 1340, 0 skip | 28 wpisów; server.smoke i cli.smoke osobno, bez wymyślonego mianownika przypadków |

Dowód: [archiwum i odtwarzanie](onboarding-2-2026-10-05-evidence/README.md).
Raw LastTest stanowi podstawę mianowników; JUnit z obciętym stdout nie służy
do ich wyliczania. Opt-in `unit.test_catalog_scale` wykonuje domyślnie 0 przypadków;
nie włączano powolnego korpusu około 1 GB ani nie zmieniano tego istniejącego
ustawienia. CTest ma 0 statusów skipped; nie ukrywamy tego osobnego 0/0.

Pełna stara bramka nie jest przeliczana jako bramka nowej bazy. BEFORE jest czystym
`3c0bc36`; AFTER użył zamrożonych źródeł nowej gałęzi. Nie luzujemy żadnego progu,
nie czytamy holdoutu ani prywatnych archiwów. Zero wywołań modeli i usług dostawców.

Ten sam probe jest linkowany do rzeczywistych bibliotek BEFORE i AFTER. Dane
profilu, promptów, odpowiedzi i decyzji pozostają identyczne; metadane pochodzenia
packa 1/3 są rozliczane wyłącznie w jawnych ścieżkach. Każdy z 12 zapisanych
grafów ma dokładnie jeden nowy węzeł prezentacji, cztery claims i jedno źródło.
Pełne body dawnych wierszy i ich powiązania są sprawdzane; surowe grafy mają
różne hashe i nie są przedstawiane jako równe bajtowo. Osobny producer starego
kodu tworzy bazę z known/declined/never, wykluczeniem i nieznanym rozszerzeniem.
AFTER zachowuje jej stan przy open/read; dopiero jawne update_pack przechodzi
do 3. Oryginalny plik bazy ma niezmieniony SHA256.

## Do wątku 9

Właściciel przeniósł R39–R40/onboarding z pierwotnego przydziału W10 do W12.
W10 odpowiada za wpięcie naszej części w nawigację/transport; nowy INDEX odzwierciedla
ten podział. Nie zmieniamy tego indeksu ani cudzych implementacji.

Pierwszy W12 nadal wymaga przyjęcia. Drugi przyrost przygotowywany jest w
odrębnym, odłączonym worktree do porównań; nie zmienia oryginalnej gałęzi.
Kolejka nie doszła jeszcze do W12. Aby przygotować kompletny przyrost i wykonać
pełną bramkę na przyjętych kontraktach 3/4 i wybranej implementacji 5, odtworzono liniowo siedem
niezmienionych commitów poprzedniego W12 jako jawną zależność. Odtworzenie
kończy się na `aa8872421d7f0a727ae3b986592ab7c1c956d787`; nie zmienia starej
gałęzi ani jej źródeł. Porównanie wszystkich własnych katalogów z `3c0bc36`
jest puste. Nowe zmiany mają osobne commity po tej zależności.
Nie deklarujemy przyjęcia foundation przez 9 ani ponownego autorstwa tych grup.
Dotychczasowa kolejka i progi obowiązują; 9 może odebrać sam diff następnego
przyrostu po przyjęciu oryginalnego foundation.

## Do wątku 10

R39–R40/onboarding to zakres W12 zgodnie z przeniesieniem przez właściciela;
Wasz zakres obejmuje wpięcie UI/HTTP/nawigacji poza naszym katalogiem.

Prezentacja onboarding jest danymi i wartością istniejącej warstwy domyślnych.
Kontroler zachowuje akcje i filtrowanie/model token/CAS. App/transport/nawigacja
pozostają po Waszej stronie; nie edytujemy innych plików web ani serwera.
Zachowaj dekorowany envelope `presentation` przez normalizer. Brak descriptor
w starym adapterze używa wygenerowanych danych; jawne unavailable/suppression
musi pozostać jawne. Przy takim stanie onboarding nie odbudowuje swoich kontrolek;
hostowe ustawienia warstw muszą nadal umożliwiać reenable. Osobne UI locale jest
jawną decyzją hosta/użytkownika, nie wnioskiem z profilu komunikacji. Zwykły UI
mapuje błędy przez katalog, szczegóły diagnostyki są osobno do inspekcji.
Starszy native profil nie dostaje nowego packa przez samo open/read; przewidź
jawne forward update z CAS i zachowaniem wykluczeń/istniejących wartości.

## Do wątku 3

Policy gate i ślad metod pierwszego W12 pozostają bez zmiany. Ten przyrost
nie jest produkcyjnym podłączeniem selektora/writera.
Używaj efektywnej polityki i revision przez `OnboardingStore::policy_decision`,
a nie surowych zachowanych reguł w profilu. `presentation.onboarding` jest
prezentacją aplikacji; suppression tego węzła nie cofa zgód ani nie jest zgodą
na wysłanie danych. Nowy katalog nie jest automatycznie dopisywany do model-facing
whitelist kreatora. Selekcja danych grafu do zwykłej rozmowy pozostaje u 3.

## Do wątku 11

Nie powstaje drugi loader/schema interpreter. W12 usuwa własną drugą kopię
presetów runtime przez wyprowadzenie z istniejących wpisów i bindings.
Wasz bloker duplikacji usage W2 nadal występuje po świeżym fetch na refie
`2ac630a72e8ddc5ad126888f7e083a5b30235db8` (runtime/usage_policy.pack);
sam nowszy commit nie oznacza jego zamknięcia.
Własny compiler W12 wyprowadza definicję runtime z kanonicznych entries;
`runtime_bindings` nie zawiera presentation. Nie dodawaj drugiego literalnego
presetu lub ręcznego fallbacku. Wspólne przyszłe ładowanie źródłowych dyrektyw
include/bindings wymaga uzgodnienia z W11, bez edycji loadera przez W12.
W11-DIC guard powinien odróżniać generowany builtin, schema/keys/lifecycle od
komunikatów UI i native reason-prose pozostającej otwartą grupą 0009.

## Do wątku 12

Otwarte sekcje INDEX: odbiór foundation, produkcyjni consumers 3, most/nawigacja 10
i wspólne profile 11. Retencja czasowa nie jest w tym przyroście implementowana;
dotychczasowy max_events/metadata/full/none pozostaje. Następne własne grupy to
native privacy reasons i preset formatu inspekcji. Nie przejmujemy zakresu 10.

## Niedokończone i od czego zacząć

Bieżący przyrost jest zamknięty. Nie zaczęto kolejnych migracji ani płatnych badań.

1. **W9: odbiór.** Przyjąć oryginalny W12 z kolejki, potem diff tego przyrostu
   po `aa887242`; zachować rozdzielenie prerequisite i nowych ośmiu grup.
   Rozpocząć od tego raportu i `data-in-code/thread-12.md`, następnie wykonać
   własny rebase/mixed gates na docelowym stanie integratora.
2. **W10: wpięcie.** Podłączyć własne komponenty onboarding przez istniejący
   adapter do App/HTTP/nawigacji. Zacząć od `loom/web/src/onboarding/README.md`
   oraz sekcji Do10: zachować envelope prezentacji i jawne suppression;
   host musi umożliwiać reenable. Nie ma dowodu renderowanego ReactDOM.
3. **W11: wspólny loader.** Baza nie zawiera runtime_profile.h, więc inspekcja
   warunkowego adaptera jest unavailable. Zacząć od `user.pack#/runtime_bindings`
   i generowanego runtime_definition; uzgodnić odczyt kanonicznych dyrektyw,
   bez drugiego ręcznego presetu. Duplikacja usage W2 i guard R42 pozostają u11.
4. **W3: produkcyjni konsumenci.** Podłączyć selector/writer do efektywnego
   `OnboardingStore::policy_decision` i revision, zgodnie z Do3; katalog
   prezentacji nie jest zgodą ani automatycznym kontekstem modelu.
5. **Dalszy W12/uzgodnienie10:** pozostałe grupy0009 (privacy reasons),0011
   (format inspekcji),0013 (etykiety grafu) są zapisane w suplemencie.
   Zacząć od pinned lokalizacji i ustalenia wspólnego katalogu danych z3/11.
   Retencja czasowa pozostaje osobnym zadaniem; nie mylić jej z obecną polityką
   przechowywania historii. Nie zmieniano istniejącego self.json.
