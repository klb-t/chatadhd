# Thread 7 — odblokowanie dostępu i bramek do rzeczywistych eksperymentów

2026-10-09. Kontynuacja C na `gpt/thread7-real-2026-10-09`, od
`cb16b336ee0c59a1ff80b4899dadebce33aa7610`. Pakiet offline pozostaje zamknięty.
Manifest nowego przyrostu znajduje się w
`docs/research/thread7_real_2026-10-09/continuation_03_live_2026-10-09/`.
Przypięto audyt A `9f931da3bb1d1001f9b7d865914ae195d9255b7e` i B
`949a86efefe899c961daad835f5fed86abebd114`.

## Najpierw działający kanał przekazania klucza

W tym środowisku uruchomiono istniejący `credential_handoff_v2.py`.
Użytkownik otrzymał rzeczywisty plik HTML do pobrania, instrukcję otwarcia
lokalnie i dołączenia zaszyfrowanego JSON. Nie proszono o jawny klucz.
Część prywatna odbiorcy jest zachowana poza repo; potwierdzono zgodność
pary kluczy, uprawnienia 0700/0600 i **10/10** istniejących testów mechanizmu.
Testy nie zużyły właściwej sesji odbiorczej. Skrypt kryptograficzny nie
został zmieniony. Opisy starego formularza skorygowano na kontynuację
istniejącej kampanii i nieodnawialnego limitu USD5.

Dopiero po udostępnieniu formularza rozpoczęto poniższe naprawy. Nie
powtarzano poszukiwania credential w checkpointach oznaczonych jako
niezawierające sekretu. Do chwili zapisu raportu nie otrzymano zaszyfrowanej
koperty, więc dostęp uwierzytelniony i bieżący budżet pozostają nieznane.

## Odtworzone i naprawione błędy

| Ustalenie | Stan na bazie C | Zmiana i dowód |
|---|---|---|
| A4-C-001 | Odtworzone | Connector v2 sprawdza rzeczywiste bajty i rozdziela hashe kontekstu, natywnej rozmowy oraz normalizacji. Kontrola istniejących źródeł: 12 rodzin, 1197 rekordów indeksu; bez ponownej preparacji. |
| A4-C-002 | Odtworzone | Sortowane są tylko techniczne pola koniunkcji `$match.fields`; kolejność list zachowuje znaczenie. Równoważne obiekty JSON dają tę samą tożsamość. |
| A2-C-001 | Odtworzone w odpowiednich historycznych builderach | Publiczne role wymagają oddzielnego przeglądu i wersjonowanego deskryptora hashy. Sprawdzane także ukryte bajty profilu projekcji. Aktualny builder miał już kontrolę własnych zamrożonych wejść; zamknięto osobno jego przyjmowanie samodzielnie zmienionej polityki. |
| A2-C-004 | Odtworzone | Błędne wejście jest nadal odrzucane, lecz publiczny wyjątek nie zawiera prywatnej instancji ani łańcucha kontekstu. |
| A2-C-003 | Odtworzone dodatkowo przy granicy payera | Ten sam operation ID z innym requestem nie może dostać cudzego wyniku i rozliczenia. Sprawdzane są dokładny hash, treść, model, provider, route i kampania, przed jakimkolwiek zapisem paczki. |

Przegląd w tej samej sesji wykrył dodatkowo ponowny odczyt pliku po
zatwierdzeniu w builderze delivery. Teraz zużywa on dokładnie wcześniej
zatwierdzone bajty. Kontrolowana podmiana pliku nie zmienia publicznego
kontraktu. To przegląd współpracowników tej sesji, nie niezależna
adjudykacja badań.

Wypchnięte przyrosty kodu:

- `a522ff1509c7b6dd733294a2c7b66c56b20c072b` — tożsamość źródła i payer/request.
- `37d40d3d0759552a135ec6576a9065ba0e39bab2` — publiczne wejścia i diagnostyka.

Testy celowane: connector **13 nowych + 7 istniejących PASS**; payer
**91/91 PASS**; projekcja **60/60 PASS**. Niezmienione funkcje audytu A
przechodzą wszystkie właściwe kryteria akceptacji (7 dotyczących połączenia,
3 publicznych i prawidłowa kontrola publiczna). Asercje wymagające obecności
naprawionego błędu teraz zawodzą, zgodnie z ich odwróconym celem. Nie liczymy
ich jako regresji. Zestawy częściowo pokrywają bramkę, więc nie sumujemy ich
jako niezależnych przypadków. Bramka na `37d40d3d`: **1597/1597 PASS, zero pominięć**, 46,86 s; repozytoryjny verifier pełnego stdout PASS. Wyniki i pełne logi wiąże `FINAL_TESTS.json`. Brakujący CTest i dawny executable odtworzono w izolowanym środowisku; zbudowano wyłącznie istniejący mały walidator native z niezmienionych źródeł. Nie jest to pełny build runtime ani bramka B.

Nie zmieniono produkcyjnego runtime, UI, wspólnych schematów, STATE/INDEX ani
cudzych gałęzi. Stare freeze, kolejki, odpowiedzi i koszty zachowano.

## Rzeczywiste sprawdzenia dostępu i budżetu

Publiczny GET aktualnego cennika endpointów OpenRouter zwrócił HTTP200
przez istniejącą konfigurację proxy środowiska. Nie wymagał klucza i nie
wykonywał inference. Oficjalne zasady cache i metadata klucza sprawdzono;
odnośniki oraz bezpieczny zapis ceny są w katalogu przyrostu. Bezpośredni DNS
był niedostępny, ale zapisany transport kampanii już używa działającego
trybu `environment` — nie obchodzono izolacji ani kontroli dostępu.

Archiwalny ledger odczytano wraz z **714 późniejszymi rozstrzygnięciami**:
potwierdza **720 completed, koszt 0,873216500 USD**. Początkowe wiersze
samodzielnie nie przedstawiają końcowego rozliczenia. Nie zmieniono ledgera
i nie utworzono drugiego. Archiwalna pozostałość **4,126783500 USD** nie jest
bieżącym saldem ani nową autoryzacją.

Gotowa historyczna alternatywa ma 6 requestów: trzy reprezentacje kontekstu,
dwie rodziny źródłowe, jeden model GPT-4.1-mini. Jej konserwatywna wycena
z aktualnego publicznego odczytu wynosi **0,9615540 USD**, bez dyskonta
cache. Jest to inwentaryzacja gotowych requestów, nie świeży plan płatnej
fazy ani rezerwacja. Nie nazywamy jej porównaniem wielu modeli. Ostateczny
mały zakres i pułap wymagają uwierzytelnionego preflight; cała macierz nie
zostanie automatycznie uruchomiona.

## Koszt, ograniczenia i punkt wznowienia

**Nowe płatne próby: 0. Nowy koszt API: 0 USD.** Nie ma nowych odpowiedzi
modeli ani nowego dowodu ich jakości. Nie powtarzano opłaconych prób,
benchmarku offline, rankingów ani preparacji.

Dokładny następny krok: odebrać zaszyfrowany JSON przy użyciu zachowanego
odbiorcy; sprawdzić fingerprint kampanii, aktualne usage, nieodnawialny limit
USD5, istniejące rezerwacje i ceny. Następnie podłączyć wybrane istniejące
requesty bez zmiany bajtów przez connector v2, zapisać nowy mały freeze i
plan kosztu, po czym uruchomić wyłącznie ten zakres przez dotychczasowy payer.
Nie wolno zresetować historii do pustego ledgera ani nadać dawnym płatnym
próbom nowych identyfikatorów w celu ponownego wykonania.

Pozostałe granice: historyczny receipt payera nie uwierzytelnia URL endpointu;
ta informacja pozostaje żądanym routingiem. Deskryptor publiczny jest
przeglądem konkretnych snapshotów, nie ogólnym detektorem danych prywatnych.
Nowe rzeczywiste wyniki wymagają osobnej przejrzanej projekcji. Sama lokalna
walidacja nie dowodzi dopuszczenia requestu przez dostawcę.
