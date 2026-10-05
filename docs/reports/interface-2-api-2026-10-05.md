# Wątek 10 — kolejny przyrost UI/API, 2026-10-05

Gałąź: `gpt/interface-2-2026-10-04`. Status: implementacja i bramki w toku; ten przyrost nie jest jeszcze gotowy do odbioru. Zero płatnych wywołań i danych prywatnych; tylko fixture/HTTP atrapy.

## Przypięte zależności

- Integrator: `66da570d3b5379492e128d940ad474467082c59f` (3+4 oraz5), świeży main `e4109df7e4af22b461def5f7d62e268d9b9a8825`.
- W1: `50e6bb9f80b0cd855e4dd1efedaf3399cac5e4e6`; osiem commitów API/promptów dołożonych bez zmian implementacji.
- W12: `3c0bc36552ef9851f1174946cfb108549aae3228`; siedem commitów store/UI dołożonych bez zmian implementacji.
- Poprzedni przyrost10: `fa7538d650da2f4ad37f5ff9254b60a6ee72938f` zachowany na `archive/2026-10-05/interface-2-before-api-intake`. Rebase zachowuje liniową historię.

## Checkpoint 1 — adapter onboardingu

Rebased API stack publiczny: `3d7e5fd2246ffd94f803ec405dd371cf6a7a8f32` (29 liniowych commitów nad66da570d). Dodany host/komponenty W12, natywny most i transport HTTP; końcowe podpięcie tras/nawigacji będzie w następnym commicie.21/21 testów hosta (15 pure +6 Chromium React/W12/LoomHttpApi),0 page errors/0 external requests/0 provider calls, identyczne SHA źródeł przed/po. Native HTTP8 grup czeka na aktualną binarkę; nie zastępujemy ich fixture. Przypadek utraty odpowiedzi zachowany: Chromium może ponowić identyczny POST; CAS chroni podwójny zapis, adapter wymusza odczyt i nie ponawia sam.

Negatywy budowy zachowane: przerwanyj4, syntaxOOM, następnie pusty `prompt_contract.cpp.o` po przerwaniu. Wymuszona wyłącznie rekompilacja tego0-byte artefaktu, bez zmiany źródeł/testów.

## Checkpoint 2 — widok metod

Panel metod:11/11 offline (10 wcześniejszych +1 filtr po rzeczywistych produced-by Claims), TypeScript PASS. Profile, nested/signed combinations, nowe wersje, raw definicje i oddzielne przyjęcie/aktywacja; aktywacja używa natywnego CAS i zachowuje siblings/selection overlay. Most lossless JSON oraz14 grup native są w trakcie; nie deklarujemy gotowości na podstawie samych atrap.

Onboarding HTTP:8/8 real native grup,0 provider completions. Pierwszy negatyw miał błędne wymaganie opcjonalnego RuntimeProfile W11; korekta testu jawnie sprawdza brak tej zdolności/reason na bazie1+12, żadnego skip. Stary test i pełny negatyw pozostają zachowane. Podłączona droga read-only ustawień metod przechodzi na GET, aby utrzymać niezmieniony sentinel zerowych zapisów przy samym otwarciu/odtworzeniu widoków. Pierwsza próba baseline ten problem wykryła; stary test pozostaje bez zmian.

Świeży fetch: integrator `0a81480` dokłada wyłącznie receipt5 do66da570d; main nadal e4109df. Przypięte API pozostaje bez zmian.

## Checkpoint 3 — dokładne zapytania analizy

Ekspercki panel W1/3/4: pełne body provider request jako exact bytes, read-only prepare bez HTTP i bez wpisu wykonania, osobny autoryzowany dispatch przez istniejący strażnik zużycia. Nowe wersje promptów i presetów trafiają do MethodRegistry/GraphPacket; nie nadpisują starszych wersji. Pierwsza odpowiedź zachowana przed parsowaniem, także błędna/binarna; pochodzenie modelowe tylko dla rzeczywistej treści modelu. Ścisłe JSON wejście odrzuca duplicate keys.5/5 grup (4 browser +1 C++ native fixture) PASS. Prepared handle jest process-local; nie jest trwałym TaskEngine resume.

84/84 niezmienionych wcześniejszych fixtures PASS po poprawce lazy-read ustawień grafowych: montowanie/przywrócenie widoku nie wykonuje dodatkowego żądania. Natychmiastowe otwarcie szczegółów wykonuje prawdziwy GET. Grafowe tryby czatu14/14 real native PASS na binarce b97ac7cc, ze zgodnymi hashami źródeł przed/po;4 żądania lokalnego fake providera,0 zdalnych. Końcowa wspólna bramka nadal w toku.

Wykryto dodatkowy rzeczywisty negatyw granicy HTTP config: zwykły JSON serwera sortował klucze native DTO. PUT/PATCH są poprawiane w zakresie W10; testy obu ścieżek zachowują oryginalny golden i wymagają dokładnego readback. Pełne negatywne receipts pozostają zachowane.

## Checkpoint 4 — czat z odpowiedzią grafową

Dane presetów w `loom/web/src/profiles/presets/graph-chat.json`: trzy tryby grafowe oraz kontrola off, method/recipe/prompt/version jako byty grafu. Panel instaluje definicje natywnie, dopiero osobny CAS aktywuje globalne ustawienie; raw JSON zachowuje tokeny liczb i nieznane pola. `GraphReplyWorkbench` korzysta z zapisanej kompilacji i sprawdza jej hash przed adresowaniem fragmentu. Rozwiń/popraw wpisuje edytowalną prośbę do kompozytora bez samoczynnego wysłania.14/14 native PASS,29 pełnych HTTP exchanges,3 method acceptance receipts i4 fake model calls. Zapisany graf, fragmenty i pierwsze błędy/odpowiedzi pozostają dostępne. Brak publicznego Chat.resume jest jawnie opisany niżej.

## W trakcie

Most HTTP korzysta z tego samego statycznego Runtime i rzeczywistej deklaracji opaque context w `src/capi/context.h`. Nie odtwarza layoutu, nie tworzy drugiej DB/silnika i nie dodaje C ABI. To jawna zależność źródłowa serwera; stare JNI pozostaje bez nowej zdolności.

- `/api/onboarding`: natywne open/read/apply/update_pack/model_request/policy_decision; exact int64 revision przekazywany jako decimal string, CAS w OnboardingStore. Formularz, what-app-knows, prywatność, warstwy/wykluczenia i scenariusz korzystają z jednego adaptera oraz jawnej tożsamości.
- `/api/methods`: rzeczywisty MethodRegistry, wersje/parametry/presety/kombinacje, immutable edit i oddzielny native graph accept. Wyniki przeglądane po rzeczywistych produced-by/run Claims.
- Graph chat: obecne3 ma config `context_execution.graph_reply`, bez per-call ChatOptions. Kontrolka pokazuje globalny zakres; nie dodaje ignorowanego pola żądania. Recorded native reply i fragmenty korzystają z3/4.
- `/api/analysis`: W1 catalog/resolve/prepare; exact bytes trzymane przez server prepared handle. Dispatch musi używać dokładnego guard/receipt, retain first response oraz metod/run/edges3/4. Preview nie wykonuje HTTP ani nie rejestruje fikcyjnego wykonania.

## Liczby przed i po

Przed: zaakceptowany autorski przyrost10:108/108 CTest,659native/24465 assertions,1276Python0skip,84fixture,20nativeUI,16E2E,webPASS. To historyczny receipt pinnedfa7538d, nie wynik obecnej mieszanej bazy. Po: bramki jeszcze nie wykonane; bez deklaracji gotowości na podstawie samych kontrolek.

## Do wątku 3

Publiczny per-call context_execution i immutable Chat PreparedRequest/resume nadal nie istnieją w przyjętym3. Obecny UI używa realnej globalnej konfiguracji; nie ponawia Chat.send po potwierdzeniu jako substytutu resume.

## Do wątku 12

Most UI/HTTP korzysta z3c0bc36, zachowuje outer revision, stable layer keys, source_refs i review. Model completion pozostaje oddzielnym privacy-filtered transportem, nigdy ordinary api.chat z dodatkowym kontekstem.

## Do wątku 9

Nie odbierać nowego przyrostu przed końcowymi mixed bramkami. Najpierw dokładnie1 i12, następnie10; deklaracje APIs nie zastępują bramek. Pierwotne źródła i wszystkie negatywy pozostają odtwarzalne na przypiętych gałęziach.
