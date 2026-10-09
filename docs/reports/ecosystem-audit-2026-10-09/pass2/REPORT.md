# Audyt A — drugi pakiet wykonawczy, 2026-10-09

Pakiet przeszedł wszystkie sześć etapów: pogłębienie wybranych ścieżek, niezależne testy konsumentów, trwałość/kompatybilność grafu, ponowne próby środowiska, weryfikację B/C oraz pakiety odbioru. **Nie oznacza to pełnego audytu zachowania każdego pliku.** Wzrost pokrycia obejmuje 77 dodatkowych plików produktu z nazwanymi zakresami; 811 plików nadal nie ma udokumentowanego przeglądu semantycznego. Także w przejrzanych plikach pozostały jawnie wskazane wywołania i warianty.

Wynik: **19 nowych potwierdzonych ID naruszeń, 10 dopuszczalnych mechanizmów i 1 niepewna granica migracji**. To 30 stabilnych ID i31 obserwacji; A2-C-001 ma dowody na dwóch SHA. Aktualizacje wcześniej znalezionych problemów, w tym19 wskazanych seedów oraz CH-001/003, są oddzielne. Reprodukcja błędu PASS nie jest akceptacją produktu. Wstępna hipoteza CH-P2-N001 została odrzucona po wykonaniu właściwego kontraktu.

Pierwszy raport i receipts na `baa9e30c12a29ab7f14fc060a13676b1bd35b036` pozostają nienaruszone. Bieżące artefakty są wyłącznie w `pass2/` raportów i narzędzi. Nie zmieniono kodu produktu, wspólnych schematów, głównych STATE/INDEX, main ani cudzych gałęzi. Nie wywołano płatnych modeli ani płatnego CI.

## Przypięte repozytoria i mianowniki

Wykorzystano odkrycie połączonego GitHub z pełną paginacją: 7 dostępnych repozytoriów, strony0/100 i100/100 dla właściciela i całego połączenia. Wszystkie sześć publicznych main pozostały na tych samych SHA; nie wygenerowano ponownie wielkiej listy kandydatów. Prywatny zakres pozostaje w poprzednim prywatnym raporcie; pass2 nie zawiera nowych prywatnych danych.

| Repo | SHA main | Pliki produktu z przejrzanymi zakresami: wcześniej→teraz | Nadal bez zakresów |
|---|---|---:|---:|
| chatadhd | `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` |33→51 /432|381|
| Watchdog-JH16 | `58a0c93bd0135e3715dcbc4d92fb80e61bd31215` |25→46 /266|220|
| Custom-Keyboard-Pro | `192f820f2d8c653768237e1bff3a1a6d950d69c0` |28→47 /233|186|
| AGEDS | `9c1d513bc19d177bd324d7506a21fbab98c2e268` |20→39 /63|24|
| LEM-Workbench | `1755e1dfaa69fcaac6fbdd12aa95ea1a593ec456` |24→24 /24|0 plików; nadal luki runtime|
| standalone loom | `0b23fa64c1de955c947349feef7bb67c05763f97` |9→9 /9|bez nowej bramki runtime|

Mianowniki zachowują klasyfikację pierwszego przebiegu; produkt, historia, testy, vendor, wygenerowane pliki i narzędzia pozostają osobne. Watchdog ma automatyczną kategorię268 wpisów, lecz ręczny manifest produktu266 — nie mieszamy tych liczb. [coverage-index.json](coverage-index.json) zawiera pełny wykaz pozostałych plików. C2 ma osobny mianownik64 zmienionych plików; pogłębiono4/11 wykonawczych plików Python. Standalone loom jest zachowaną historią, nie backlogiem aktywnego kernela chatadhd/loom.

## Potwierdzone problemy i dobre mechanizmy

Nowe ustalenia dotyczą rzeczywistych konsumentów: nadpisania zwalidowanego modelu/promptu przez legacy parameters oraz zaszytej palety w Watchdog; odrzucania wymaganego transformu i pozornego sukcesu generic setting w CKP; pomijania parametrów/case, sztywnej interpretacji dat, ukrytego budżetu skanu i schema999 w AGEDS; niepoprawnego JSON błędów w LEM; utraty pamięci, kolejności ASR, wymyślonej pewności i fałszywego synced w Pythonowym chatadhd. Nowe ustalenia B/C są niżej.

Dodatkowe dowody starszych problemów obejmują trwałe `done` po timeout CH-011, relację bez provenance CH-006, wysyłkę z ręczną temperaturą0.7 po zapisaniu błędnego typu CH-001 oraz utratę ledgeru i wyników przez rzeczywisty Room v1→v2 i version99 w LEM-005. CKP-A-012 jest potwierdzone na JVM dla dokładnych metod: późny rewrite trafia do zmienionego odbiorcy/rewizji; rzeczywisty cykl życia IME na urządzeniu nadal jest osobną bramką.

Dobre mechanizmy też wykonano: odmowa push przy pull_only i nieznanego providera; jawny błąd braku kryptografii; dwie konfiguracje zmieniają parametry HTTP; wyłączenia i deny przetrwały restart/pack update Watchdog; Workbench zachował historyczną recepturę przez SQLite/blob/ZIP/reimport; native zachował exclusion, CAS i dawną recepturę; CKP wykonał zapis/odczyt profilu przez AtomicFile; aktualny config LEM przeszedł zamknięcie/otwarcie Room.

[findings.jsonl](findings.jsonl) i [finding-index.json](finding-index.json) oddzielają klasyfikacje. [backlog.json](backlog.json) zawiera odwołania do pakietów: finding→reprodukcja→oczekiwane zachowanie→test→konsument→migracja→ryzyko→zależności. Nie skopiowano starej, przyjętej kolejki z października5.

## Niezależna weryfikacja B

Na `ddcaeaf7a25b70f0d4dc2e0eed2466016233eeb8` naprawa **B-POLICY001** jest potwierdzona: cztery akceptacje, które nie przechodziły na main, przechodzą w rzeczywistym OnboardingStore/policy_decision. Nie rozszerzamy tego na zwykły transport czatu.

**CH-003: częściowa naprawa + regresja A2-BSEM-001.** Te same32 próby rzeczywistego Runtime dają main22PASS/10FAIL, B24PASS/8FAIL. Dziesięć wcześniejszych porażek naprawiono. Osiem wariantów jednego nowego problemu dotyczy powrotu do builtin: resolver wskazuje builtin, lecz działający runtime używa reguły nakładki ze startu i nie zapisuje jej hasha. Dotyczy live/worker oraz regex/LLM fallback; restart przywraca prawidłowy wynik.

Jednorazowy fetch po zamknięciu podstawowych modułów ujawnił B2 `384c5e1686cd3a58a7d89a8a6813a18c764f697d`. Niezależnie potwierdzono naprawę **ośmiu etykiet grafu**: rzeczywisty konsument czyta prezentację, natywny zapis/restart/exclusion i jawna migracja działają. Przebudowano dokładny zakres sześciu zależnych jednostek. Jedyny FAIL jest zalecaną bramką komunikowania migracji starego pack3, **nie potwierdzonym naruszeniem wymogu właściciela**. A2-CH-B2-001 pozostaje niepewne; jawne odrzucenie brakującego katalogu i obsługiwana ręczna migracja są dopuszczalne. Źródła analizatora CH-003 nie zmieniły się względem B; porównanie hashy nie jest udawanym ponownym uruchomieniem B2.

Dowody: [native](native/README.md), [B-semantic](c-review/B-semantic/REPORT.md), [B2](watchdog/B2/REPORT.md).

## Niezależna weryfikacja C

C `69880859802267f1b38b82eeaecd6fdc5522a47a`: publiczna projekcja ma5 porażek akceptacji; native9/9 przechodzi. A2-C-001 dotyczy pomocniczych pól projekcji, A2-C-002 przypisania wyniku innego powtórzenia. Native rzeczywiście zapisuje packet, odtwarza go w nowym kontekście i zachowuje dawne referencje receptury.

Nowsze C2 `b9b503f62bb8e8e00c94ab7401e1cd9579129af3` również sprawdzono. Nowy runner7PASS/4FAIL. Poprawny pełny ciąg queue→PrivateLedger→verified dispatch→sync→GraphPacket→native restart przechodzi. **A2-C-003:** ten sam operation_id przy innym request_sha256 wystarcza do zapisania zweryfikowanego rozliczenia/wyniku; wymagana jest pełna zgodność żądania. **A2-C-004:** diagnostyka odrzuconego wejścia CLI ujawnia jego treść. A2-C-001 ma dodatkowy dowód dla nowego entrypointu. Użyto wyłącznie syntetycznych znaczników, bez ujawniania rzeczywistej prywatnej treści.

Stary probe na C2 ma dodatkowy FAIL porównujący historyczny program z bieżącym plikiem. To nie regresja C: historyczna receptura prawidłowo zachowała dawny SHA. Receipt zachowano i opisano błąd założenia testu. C/C2 klasyfikujemy jako częściową naprawę/integrację; udany storage nie dowodzi poprawnego payer binding ani bezpiecznej publicznej projekcji. Dowody: [C](c-review/REPORT.md), [C2](c-review/C2/REPORT.md).

## Kompatybilność i środowisko

Historyczna paczka `loom.kb.stemming/1` została faktycznie podana aktualnemu loaderowi/validatorowi. Odrzucenie jest jawne i zgodne z udokumentowanym kontraktem. Audytowy kandydat ręcznej migracji do/2 przeszedł rzeczywisty loader/Normalizer i pięć próbek; oryginału nie nadpisano. Nie przedstawiamy tej zgodnej, udokumentowanej zmiany jako nowej regresji.

Uruchomione bramki: native112/112 grup (897 przypadków), ABI3/3, CKP istniejące974/974 AndroidJVM oraz niezależne AtomicFile/Settings; LEM istniejące3/3 i niezależny rzeczywisty Room/KSP; Watchdog66/66 istniejących hostowych testów; AGEDS Python/Kotlin/serialization/CorpusStore oraz istniejące Python/JS. Liczby istniejących testów nie zastępują niezaliczonych testów audytowych.

ASan+UBSan: 37 rzeczywistych jednostek C++, SQLite/miniz i driver,50 procesów bez diagnostyki sanitizer; cztery logiczne porażki bazy pozostają. **LSan osobno BLOCKED** przez `/proc/<pid>/task`. Chromium pobrano i rzeczywiście uruchomiono; SIGABRT/socketEPERM oznacza0 asercji browsera. AGEDS pinnedGradle9.7.0 startuje po poprawieniu proxy, lecz konfigurację projektu blokuje cache/checksum pustego pobrania; pełne JDK21/SDK37 i device pozostają otwarte. Nie używamy starego „brak kompilatora” jako aktualnego uzasadnienia.

[environment-matrix.json](environment-matrix.json) rozdziela host/JVM, Android/device, codec, rzeczywistą bazę i E2E. [test-index.json](test-index.json) prowadzi do19 wykonywalnych suite entrypointów. Sam indeks został uruchomiony: po niezaliczonym pierwszym zadaniu wykonał następne, a całość zwróciła1.

## Pozostałe luki i wznowienie

CH-004/005: wykonano różnicę realnych ścieżek, ale pełna akceptacja pozostaje BLOCKED_MISSING_CONTRACT. Ręczna metadana pamięci nie jest istniejącym kontraktem user/revision/category. UsagePolicy nie ma pola budget=0; zero initial baseline oznacza potwierdzenie wzrostu. Ordinary ChatOptions nie wiąże operacji/estymaty/zgody. CH-006 ma minimalną bramkę provenance i osobną brakującą bramkę candidate/ask; same niepuste referencje nie są dowodem prawidłowego admission.

Nie ma dowodu pełnego backup/import profilu+warstw+workflow chatadhd ani wspólnego browserowego stanu Basic/Advanced/Expert. Pozostały resolver surowych artefaktów LEM, device IME/Keystore CKP, pełne urządzenie AGEDS, browser i LSan. Osobno pozostaje811 plików bez zakresów oraz wymienione callees w przejrzanych plikach.

Dokładny punkt wznowienia jest w [RESUME.md](RESUME.md) i [resume-queue.json](resume-queue.json): najpierw brakujące kontrakty/odbiorcy o wysokim ryzyku i pozostałe wejścia import/sync/policy, nie ponowny skan setek tysięcy literałów. Dostępne naprawy należy sprawdzać tym samym runnerem na nowym checkout/SHA; nie czekać bezczynnie na B/C i nie odświeżać ich gałęzi w pętli.
