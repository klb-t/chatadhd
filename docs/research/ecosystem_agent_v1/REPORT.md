# Wspólny agent ekosystemu — badania i eksperymenty V1

2026-10-01, Europe/Amsterdam. Wymaganie właściciela: agent jest podłączanym
komponentem całego ekosystemu; ChatADHD jest jednym z klientów. Ręczny skrypt
startu maszyny jest osobnym narzędziem. Użytkownik może wybrać maszynę niezależnie
od tego, czy wykonanie na niej jest najwygodniejsze.

## Co już istniało

Na bazie `33fb30a5e8c226b64e67ee6531304046392b6ad2` Loom ma natywny `Runtime`,
`TaskEngine`, konfigurację dostawców i sekretów, graf/proweniencję oraz referencje
`AnalysisPlan` i trwałych lease/receipt. Wbudowany manifest OpenRoutera znajduje
się w `loom/src/providers/providers.cpp`, a domyślny endpoint w
`loom/src/core/config.cpp`. Rejestracja zadań dotyczy m.in. archiwum i wiedzy.
Nie znaleziono podłączonej do `ChatEngine` ogólnej pętli wyboru narzędzi ani
gotowego wykonawcy VM/browser/GUI. Istniejące materiały o agencie opisują kierunek
prac; nie są dowodem wdrożenia takiego wykonawcy. DevBox nie jest tym agentem.

## Wykonany zakres

Powstał izolowany prototyp `loom/tools/agent_runtime_v1/`. Wykorzystuje istniejący
`LeaseStore`, bez drugiej implementacji kolejki. Pętla dostaje wymienne narzędzia,
planer, wybrane środowisko i limity. Każde działanie wiąże rewizję narzędzia,
schemat, argumenty, środowisko i źródło kodu; pierwsza odpowiedź planera i pierwszy
wynik narzędzia zostają zapisane przed interpretacją. Wznowienie odtwarza
ukończone działania, a nieznany skutek zatrzymuje automatyczne powtórzenie.
Potwierdzenie wykonania nie oznacza prawdziwości danych ani sukcesu dziedzinowego.

| Tor | Rzeczywiście wykonana próba | Znaczenie wyniku |
|---|---|---|
| Kompozycja | Istniejący GraphPacket → osobny proces aplikacji MCP → proces OS; następne argumenty wynikają z obserwacji | Jedna pętla obsługuje różne adaptery bez redukowania payloadów do wspólnej ontologii |
| Środowisko | Surowe bajty stdout/stderr, kod wyjścia 7, timeout, ograniczony prefix, dostęp poza cwd, sprzątnięcie dziecka procesu | Wynik zachowuje bajty lub jawnie opisuje utratę; cwd nie jest izolacją |
| Awaria przed efektem | SIGKILL rzeczywistego workera po dispatch, przed zapisaniem efektu | Efektów 0; wznowienie blokuje ponowny dispatch mimo braku obserwowanego efektu |
| Awaria po efekcie | SIGKILL po fsync syntetycznego efektu, przed zwrotem wyniku | Efektów 1; po wznowieniu nie pojawia się drugi efekt |
| Budżet nadrzędny | Pętla jako callback istniejącego `AnalysisPlan`, osobno dopuszczona i odrzucona rezerwacja | Odrzucona rezerwacja nie wywołuje agenta; nie dowodzi to przeniesienia budżetu do dostawcy |

Pięć torów uruchamia się równolegle w niezależnych katalogach i bazach. Testy
obejmują również równoczesne wznowienie tej samej sesji, zmianę schematu/rewizji,
zmianę wybranego środowiska, brak capability fizycznego urządzenia, zachowanie
jednostek, niepoprawny wynik MCP, błąd narzędzia i błąd protokołu oraz blokadę
odczytu/zapisu przez milczący serwer. Nowe narzędzie innej aplikacji dodaje się
przez rejestrację adaptera, bez zmiany pętli.

Planer jest deterministyczny i wstrzyknięty. To działająca próba mechaniki
agentowej, nie pomiar jakości decyzji rzeczywistego LLM. Aplikacja MCP jest
syntetycznym procesem testowym, nie wdrożonym Watchdogiem czy inną aplikacją
właściciela. Pythonowy adapter GraphPacket używa rzeczywistej algebry istniejącej
w repozytorium, ale nie zapisuje natywnego canonical store. W tym kroku nie
zmieniono C++, ABI, migracji ani natywnego `ChatEngine`.

## Kontrakty i standardy

Własny kontrakt działania zachowuje pełne schematy i payloady. MCP mapuje
odkrywanie i wywołanie narzędzi, a A2A jest kandydatem do późniejszego połączenia
samodzielnych agentów. To wniosek projektowy, nie wymóg tych standardów.

MCP V1 próby przypina `2025-11-25`: initialize, initialized, tools/list,
tools/call, rozdzielenie stdout protokołu i stderr. Zachowuje descriptor wraz
z rozszerzeniami i waliduje structuredContent według outputSchema. Eksperymentalny
klient wymaga outputSchema, chociaż specyfikacja dopuszcza jego brak: to jawna
granica tego adaptera, nie ograniczenie przyszłego agenta. Powtórne tools/list
wykrywa zmianę descriptor przed tools/call; nie gwarantuje, że serwer nie zmieni
zachowania między tymi wiadomościami. Nie wdrożono HTTP, paginacji, notifications
serwera, pełnej autoryzacji ani MCP Tasks. Przesłanie dużej ramki i oczekiwanie na
odpowiedź mają osobne timeouty, więc adapter nie obiecuje jednego globalnego SLA.

Aktualne MCP Tasks są rozszerzeniem w wersji roboczej, odrębnym od historycznej
wersji eksperymentalnej. Anulowanie jest kooperacyjne i nie dowodzi cofnięcia
efektów. A2A `1.0.0` opisuje własny cykl zadań i protokoły samodzielnych agentów.
Nie wolno bez dowodu utożsamiać ich stanów z lokalnym `TaskEngine` lub uznawać
timeoutu/ack anulowania za skuteczne wycofanie operacji.

| Przypadek | Zachowane | Dodane | Utrata / odwracalność |
|---|---|---|---|
| Loom GraphPacket | Rekordy, źródła, locator, proweniencja, ocena treści i historia | Koperta akcji i receipt wykonania | Brak celowego zawężenia; istniejąca algebra diff zachowuje historię |
| Aplikacja MCP | Descriptor, schema, x-domain, structuredContent, jednostka, content i dokładne ramki w dowodzie kompozycji | Namespace, hash rewizji, status wykonania | Brak konwersji degC/K; niezgodna jednostka zostaje zachowana i blokuje wynik |
| Proces OS | Rozmiar i SHA całego odczytanego strumienia, prefix bajtów, returncode | Pomiar wall, timeout, opis sprzątania | Przy przekroczeniu capture_bytes ogon jest utracony i odwracalność=false |
| Fizyczne urządzenie | Wymaganie odpowiedniej capability | Wynik unavailable | Brak zmyślonej wartości i brak automatycznego zamiennika |

## OpenRouter i GCP

OpenRouter dostarcza decyzje modeli; GCP dostarcza środowisko wykonania. Są to
dwa niezależne adaptery. `connections.py` offline zachowuje schematy narzędzi,
mapuje ich nazwy na nazwy funkcji OpenRoutera, odczytuje wszystkie zgłoszone
tool_calls oraz zachowuje pełną odpowiedź i pola reasoning. Osobno opisuje
konkretny projekt/strefę/instancję GCP bez fallbacku. Żadna z tych funkcji nie
ładuje klucza, nie wykonuje HTTP, nie łączy z maszyną ani nie tworzy zasobów.
Kompilator jest bezstanowy; prawdziwy adapter rozmowy musi jeszcze zachować
historię assistant/tool oraz opaque reasoning i wpiąć opłacane żądania do
trwałych rezerwacji/receipts przed wysłaniem. Nie wystarczy wywołać go w planie
i potem zakładać, że budżet nadrzędny gwarantuje rozliczenie każdego żądania.

Sprawdzenie tej sesji: brak udostępnionego connectora Compute Engine/OpenRouter
i brak `gcloud` w środowisku wykonawczym. Wyszukiwanie pluginów nie znalazło
połączenia obsługującego te operacje; katalog wyszukiwania nie jest wyczerpujący.
Nie badano prywatnych magazynów sekretów i nie wykonano próby dostępu do kont.
Istniejący manifest OpenRoutera nie oznacza, że ta sesja ma klucz lub dostęp.

Do próby na istniejącej VM potrzebne będą: project ID, strefa i instancja,
wybrana metoda połączenia, zatwierdzony zakres IAM oraz konfiguracja ADC lub
impersonacji konta serwisowego. Google opisuje ADC z kontem podpiętym do zasobu
i impersonację z krótkotrwałymi poświadczeniami. Osobno potrzebne będą sekret
OpenRouter w magazynie konfiguracji, wybrane modele i budżet eksperymentu.
Klucze nie należą do rozmowy, raportu ani publicznego repozytorium. Tworzenie VM
wymaga dodatkowych parametrów rozmiaru, dysku, sieci, kosztów i zakończenia pracy.
W tym kroku: płatne wywołania 0, provision VM 0. Nie przyjęto nowego budżetu.

## Granice i następne dowody

LeaseStore koordynuje jedną lokalną bazę; nie zapewnia globalnego exactly-once.
Ponowny efekt po nowym ID nadal może się zdarzyć. Nieznany wynik wymaga
reconciliation z dowodami. SIGKILL bada awarię procesu, nie pełną odporność
katalogów na utratę zasilania. Uszkodzony/niepełny journal zatrzymuje wznowienie.
Pełne surowe ramki MCP są zapisane w dowodzie kompozycji; dziennik transportu
wewnątrz klienta pozostaje w pamięci i sam nie przeżywa SIGKILL.

Proces OS nie jest sandboxem. Sprzątanie killpg dotyczy tej grupy procesów,
bez gwarancji dla odłączonych potomków. Capture limit ogranicza zwracany prefix,
bez twardego limitu dysku tymczasowego. Brak Docker/bwrap w tej sesji nie
dowodzi, że VM/container są nieprzydatne; oznacza brak ich weryfikacji tutaj.
Rzeczywiste VM, przeglądarka/GUI, izolacja, uprawnienia, live modele, zdalne
checkpointy i rozliczenie opłacanych żądań wymagają osobnych prób.

Następny próg: adapter OpenRouter w istniejącym mechanizmie sekretów i ledger,
mały jawnie budżetowany zestaw rzeczywistych zadań z oceną doboru narzędzi; potem
ta sama próba na wybranej istniejącej VM GCP. Dołączanie produkcyjnych aplikacji
powinno odbywać się przez ich własne kontrakty semantyczne. Nie ma jeszcze
dowodu uzasadniającego nowy niezależny serwis ani osobną uniwersalną ontologię.

## Źródła pierwotne i odtworzenie

Protokół ustalono w commicie `f6012e87ad4f6bc9f07ba69c316d1082fe74c4b9`.
Implementation/result checkpoint oraz wyniki walidacji są w [STATE](STATE.md).
Pierwsze nieudane przebiegi zachowano jako exploratory; ich source_commit
wskazywał bazę, gdy kod nie był jeszcze commitowany. Nie są poprawnym dowodem
wykonania tej rewizji. Końcowa próba zapisuje dirty flag i SHA instrumentów.

- [MCP tools, 2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)
- [MCP lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle)
- [MCP stdio transport](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)
- [MCP Tasks, draft](https://tasks.extensions.modelcontextprotocol.io/specification/draft/tasks)
- [A2A, 1.0.0](https://a2a-protocol.org/v1.0.0/specification/)
- [Docker Engine security](https://docs.docker.com/engine/security/)
- [OpenRouter tool calling](https://openrouter.ai/docs/guides/features/tool-calling)
- [OpenRouter authentication](https://openrouter.ai/docs/api_reference/authentication)
- [Google ADC for Compute Engine](https://docs.cloud.google.com/compute/docs/authentication)
- [Google service account impersonation](https://docs.cloud.google.com/docs/authentication/use-service-account-impersonation)

Polecenia odtworzenia są w [README prototypu](../../../loom/tools/agent_runtime_v1/README.md).
