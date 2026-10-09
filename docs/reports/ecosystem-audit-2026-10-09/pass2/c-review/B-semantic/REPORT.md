# CH-003: niezależna weryfikacja B na rzeczywistym Runtime

**B jest częściową naprawą CH-003 i wprowadza potwierdzoną regresję A2-BSEM-001.** Ten sam niezależny caller C++ został uruchomiony przeciw rzeczywistemu `libloom_core` z obu SHA:

| Źródło | SHA | PASS akceptacji | FAIL akceptacji |
|---|---|---:|---:|
| main | `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` | 22/32 | 10/32 |
| B | `ddcaeaf7a25b70f0d4dc2e0eed2466016233eeb8` | 24/32 | 8/32 |

B naprawia wszystkie **10 wcześniejszych porażek** tej baterii: profil startowy dociera do analizatora, dwie nakładki zmieniają worker regex/fallback i live fallback, hash opisuje wybrany profil, błędny pack zatrzymuje przetwarzanie przed transportem i pozostawia pending, startup odrzuca wadliwy pack, a poprawienie packa umożliwia wznowienie. Live regex na main był już prawidłowo podłączony — nie zaliczono go do nowych napraw.

**Osiem nowych porażek to warianty jednej regresji.** Runtime startuje z niestandardową nakładką A. Następnie użytkownik przywraca wartości builtin przez usunięcie pliku lub jawną pustą nakładkę `overrides={}`. `RuntimeProfile::load` poprawnie zwraca builtin i jego hash. Świeży analizator utworzony z tych samych danych nie rozpoznaje markera A. Jednak działający Runtime B nadal używa startowego A: powstaje jedna encja i nowa krawędź `mentions`, wiadomość dostaje `done`, a metadane nie zawierają hasha A. Dotyczy czterech ścieżek: live regex, worker regex, live LLM fallback, worker LLM fallback. Restart usuwa rozbieżność. Na main wszystkie te osiem prób przechodzi.

Przyczyna jest konkretna: `Runtime::open` teraz tworzy konstruktorowy analyzer z nakładki. `GraphEngine` i `SemanticWorker` budują nowy analyzer tylko dla `!is_builtin()`, a w pozostałym przypadku wracają do obiektu konstruktora. Obiekt konstruktora może już być niestandardowy. `is_builtin()` wyraża równość efektywnych wartości, nie tożsamość tego obiektu ani intencję użytkownika.

Nie skopiowano implementacji badanego mechanizmu. Caller tworzy `Runtime`, używa jego GraphEngine/workera/DB, sprawdza rzeczywiste krawędzie od nowej wiadomości i reopen. Referencja oczekiwanego zachowania pochodzi z istniejącego `SemanticAnalyzer::create_from_data_dir`. LLM transport przejmuje istniejący `ScriptedTransport`, zawsze zwracając syntetyczny timeout. Nie uruchomiono modeli ani rzeczywistej sieci.

## Pakiet odbioru dla B

- **Finding:** `A2-BSEM-001`, szczegóły, SHA i hashe zakresów w `findings.jsonl`.
- **Reprodukcja:** start z A → usunięcie packa albo `overrides={}` → nowa wiadomość z markerem A → live albo drain. Nie restartować przed tą operacją.
- **Oczekiwane zachowanie:** rzeczywisty analizator odpowiada aktualnemu resolverowi; w kolejnej operacji marker A nie tworzy nowych krawędzi. Historyczne wyniki pozostają zachowane.
- **Akceptacja:** ten sam `run.py` z checkoutem/buildem poprawki; wszystkie 32 kontrole PASS, w tym zachowanie dziesięciu już naprawionych bramek. PASS odtworzenia regresji nie oznacza PASS produktu.
- **Konsumenci:** Runtime, GraphEngine, SemanticWorker, przekazany analyzer w SemanticLLM, metadane `mark_analysed`.
- **Alternatywy:** instancja z każdego efektywnego profilu, także builtin; albo cache według faktycznego hasha z osobnym, prawdziwie builtin obiektem. Jawny freeze na całe życie Runtime jest inną strategią, ale nie spełnia obecnego kontraktu przeładowania na operację/drain.
- **Migracja/ryzyko:** zapisane błędne wyniki mogą nie mieć hasha wykorzystanej nakładki. Nie dopisywać zgadniętej proweniencji i nie usuwać źródeł. Reanaliza musi mieć jawny zakres, zachować poprzedni wynik i nie wyzwalać płatnych powtórek automatycznie.

Pełne pokrycie tego modułu to zakresy sześciu plików produktu, nie kompletna kontrola Runtime. Nie objęto osobnego transportu Anthropic batch, konsumentów Python, kosztów/admission, ChatEngine/MemoryEngine ani UI. Jawny legacy `SemanticLLM::analyse(text)` pozostaje analizatorem konstruktora i nie jest przedmiotem tej regresji.

`main-initial-check-receipt.json` zachowuje pierwszy przebieg harnessu. Miał zbyt wąski wymóg hasha tylko na głównym poziomie metadanych. Ostateczny test akceptuje także istniejące `semantic.analyzer_profile_hash`, dzięki czemu dwie sprawne ścieżki live regex na main nie są fałszywymi ustaleniami. Tylko `main-receipt.json` i `B-receipt.json` tworzą powyższe porównanie 32 identycznych kontroli.
