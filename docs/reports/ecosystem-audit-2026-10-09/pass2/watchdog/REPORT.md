# Watchdog — przyrost audytu A, przebieg 2

Baza: `klb-t/Watchdog-JH16`, main `58a0c93bd0135e3715dcbc4d92fb80e61bd31215`. Produkt, schematy i cudze gałęzie niezmienione. Pierwszy raport pozostaje historycznym raportem; nowe dowody zapisano tutaj.

Wykonano **21 niezależnych testów** rzeczywistych konsumentów: **6 PASS reprodukcji błędu, 9 PASS i 6 FAIL akceptacji**. Proces kończy się kodem **1**. Zielona reprodukcja nie oznacza zielonego produktu. Testy przyjmują checkout/SHA i mogą zostać uruchomione przeciw poprawce B bez zmiany oczekiwań. Pełny indeks: `test-index.json`, dowody: `receipt-complete.json`, instrukcja: katalog narzędzi `pass2/watchdog/REPRODUCE.md`.

| Ustalenie | Wynik obecnej bazy | Pakiet dla B |
|---|---|---|
| WD-001 | Nieznany templateId/version nadal tylko opisuje wbudowany szablon | Resolve rzeczywistej wersji; jawnie odrzuć nieznaną. Samo przejście minimalnego testu nie zamyka ekstrakcji promptów. |
| WD-002 | `exclude` nadal zachowuje brakujący wiersz | Usuń brakujące wiersze zgodnie z wybraną strategią, zachowaj tożsamość i zgodność wektorów. |
| WD-003 | `request.params` przegrywa z profilem bez komunikatu | Jawna kolejność i walidacja, poprawne przekazanie lub jawne odrzucenie konfliktu. Dwa profile provider.parameters rzeczywiście zmieniają HTTP body. |
| WD-011 | Provider narrative z `-4` przechodzi względem źródła `4` | Odrzuć zmianę wielkości liczbowej. Artefakt nadal pozostaje PROPOSED; nie stwierdzono automatycznej aprobaty. |
| **A2-WD-001** | Legacy JSON przyjmuje `generation_params.model/messages`; adapter po kontroli allow-list nadpisuje nimi poprawny model/prompt | Chroń pola związane kontraktem i waliduj efektywne żądanie. Osobny personal-provider loader już chroni te pola. |
| **A2-WD-005** | Poprawnie zwalidowany profil z innymi identyfikatorami palet tworzy nieważną figurę, ponieważ helper wybiera literalne `accessible` | Defaulty nowej figury jako wersjonowane dane z poprawnymi referencjami. Zachowaj starą recepturę istniejących figur. |

Nowe pozytywne ustalenia mają osobne ID i ograniczony zakres:

- **A2-WD-002:** rzeczywiste migracje SQLite, zapis, zamknięcie i ponowne otwarcie pliku. Zapisane disabled i budżet zero przetrwały zmianę defaultów; nieznane ustawienia oraz uszkodzony JSON odrzucono. Timeout daje trwały FAILED_RESERVED, bez samoczynnego retry; hashe ustawień, profili i katalogu pozostają dostępne.
- **A2-WD-003:** zapisany deny po restarcie i zmianie packa nadal blokuje PublicHttp przed transportem. Błąd transportu ma osobny trwały receipt. Dwa identyczne payloady pozostawiają dwa zdarzenia i jeden deduplikowany blob. Ocena użytkownika nie została uznana za rzeczywistą zgodę dostawcy.
- **A2-WD-004:** import → przegląd → wykonanie rzeczywistego executora → SQLite i obiekty → restart → ZIP → reimport. Dawny wynik i zapisana figura zachowują dawny profil/metodę po zmianie bieżącego profilu. Nowa baza nie przejmuje aprobaty ani nie tworzy brakującego wyniku. Revoke blokuje dane wejściowe eksportu; nieznane pola/wersja, konflikt row ID i brakujące/obce referencje są odrzucane. Błąd zapisu obiektu daje FAILED bez finalnego manifestu. To runtime Watchdog/SQLite, nie odbiór natywnego grafu Loom.

Pokrycie: mianownik nadal **603 pliki śledzone / 266 plików produktu**. W tym przyroście nazwane zakresy dotyczą **29 plików**, z czego **21 nowych względem pierwszego przebiegu**. Łącznie przynajmniej jeden zakres semantyczny ma **46/266**; **220** plików nadal bez raportowanego zakresu. `coverage.json` podaje zakresy, funkcje, testy i nieprześledzone wywołania. Ten licznik nie oznacza kontroli wszystkich funkcji wewnątrz 46 plików. Trasy/UI prześledzone źródłowo są odróżnione od testów runtime. Nie powtórzono szerokiego skanu kandydatów.

R42 zastosowano do zachowania: nazwy kontraktu, schema identifiers, hash i stany cyklu życia są mechanizmem formatu/kontraktu. Nadpisanie parametrów w merge order i wybór domyślnej palety są decyzjami o działaniu. Nie potraktowano wszystkich literałów jako naruszeń. Nie narzucono Watchdogowi jednego fizycznego storage/grafu ani umieszczenia wszystkich bajtów w bazie.

Środowisko sprawdzono ponownie: native binding SQLite 3.53.4 działa; typecheck i build przeszły w odizolowanym checkoutcie. **66/66 istniejących testów hostowych PASS**, z blokadą sieci poza loopback. Chromium został dostarczony, ale własna próba uruchomienia Playwright kończy się SIGABRT, `socket() failed: Operation not permitted`, przed utworzeniem strony. Browser E2E pozostaje **zablokowany środowiskiem**, a nie rzekomo zaliczony przez host. Nie pobierano przeglądarki ponownie po otrzymaniu działającej ścieżki; zachowano log wcześniejszej nieudanej równoległej instalacji. Bez płatnych modeli i CI.

`handoff-b.json` zawiera sześć konkretnych pakietów: finding → reprodukcja → zachowanie → czerwony test → konsument → migracja → ryzyko → zależności. `risk-queue.json` wskazuje dalsze niezależne zadania. Nie wykonano poprawki produktu ani nie stwierdzono nowego SHA B w zakresie Watchdog. Ocena wspólnych przyrostów B/C znajduje się u integratora A.

Punkt wznowienia: użyć `run.py --mode accept --filter WD-011`, następnie `A2-WD-001`, `WD-003`, `WD-002`, `WD-001`, `A2-WD-005` przeciw konkretnym SHA poprawek. Bez poprawki można dalej badać diagnosykę/publiczną projekcję metadanych, granicę zmiany konta podczas oczekujących żądań oraz pozostałe kolektory i anulowanie akwizycji. Blokada browsera nie blokuje tych hostowych prac; nie oznaczono ich jako wykonanych.
