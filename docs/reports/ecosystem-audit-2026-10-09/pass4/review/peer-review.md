# PASS4 — niezależny przegląd twierdzeń B/C/D/E

Przeczytano raporty, końcowe receipts, wybrane oracles oraz rzeczywistych konsumentów źródłowo. Nie uruchamiano ponownie testów produktu i nie edytowano modułów. `peer-review.json` zapisuje 23 pliki, zakres odczytu, ich SHA-256 i przypięte commity B/C/D/E.

Nie znaleziono nieuprawnionego zamknięcia naprawy ani zadeklarowanej fikcyjnej integracji w przejrzanych raportach. Sumy przypadków/statusów/kategorii zostały ponownie policzone z receipts i zgadzają się z raportami. Jest jedna uwaga do siły oracle:

**A4-REVIEW-001, luka testu, nie finding produktu:** `E4-06.visibility-analysis-permission` jednocześnie ustawia filtr `evidence=['unmatched']`, `renderBudget=1` i `deny-a`. Brak `a` w widoku nie izoluje działania odmowy dostępu, ponieważ filtr ukrywa również normalnie dostępny obiekt `a`. `complete=false` dowodzi zarejestrowanej niepełności, nie samodzielnie ochrony wartości pola. Kod `select.ts` ma sprawdzenia uprawnień, a sam `evidence_filter` nie ustawia tam niepełności; **nie twierdzimy**, że usunięcie wszystkich guardów na pewno pozostawiłoby PASS. Nie zaobserwowano obejścia zgody.

Przed użyciem tej bramki jako niezależnego dowodu egzekwowania uprawnień należy dodać izolowane porównanie: bez filtrów i ograniczenia renderu `allow` zwraca `a` i jego wartość, a `deny-a` nie ujawnia wartości oraz zwraca jawny status/omission. Placeholder odmowy pozostaje dopuszczalny. Osobno zmiana wyłącznie widoczności powinna zachować wybór analizy i dane kanoniczne. Wystarcza nowy mały przypadek; nie ma podstaw do ponawiania pozostałych zamkniętych prób.

Warunki, które należy zachować w agregacji:

- **B N001/N002 pozostają częściowe.** Nowa projekcja działa; C ABI i historia czatu nadal czytają `Database.get_msgs`, którego stare ścieżki nie odzyskują tej struktury. Rozwiązaniem może być podłączenie odczytu przez referencję; brak materializacji nie jest sam w sobie naruszeniem. Przyszły równoważny konsument może wymagać cienkiego adaptera testu.
- **B profil grafowy nie został uznany za aktywowany.** PASS dotyczy wspólnego pliku zewnętrznego; edycja grafowego pola → resolver pozostaje BLOCKED. Native preview/task nie jest browser E2E.
- **C rozróżnia wejścia.** Receipt-backed registry przechodzi, surowy profil nadal nie. Poprawne `executor unavailable` nie jest usterką. Canary w pomocniczym payloadzie/błędzie nie jest dowodem rzeczywistego ujawnienia prywatnego materiału w publikacji C.
- **D dowodzi projekcji składni i rzeczywistego packet/store roundtrip.** Nie nadaje temu znaczenia pełnego importu domenowego ani odtwarzania niematerializowanej reszty. `discover()` z brakującym modułem jest oddzielone od poprawnego `adapter_unavailable`.
- **E dowodzi D → native → selektor po ID encji.** Adres źródłowy/wersja, produkcyjny hook UI i aktywacja pola profilu pozostają BLOCKED. Nowy proces z przekazanym JSON nie staje się trwałym restartem ustawień urządzenia.
- D to **32 kryteria w dwóch warunkach bibliotek**, nie 64 niezależne przypadki. Dziesięciu testów credentials autora nie dodaje się ponownie do 28 bramek C. Historyczny BLOCKED „brak E” jest zastąpiony odbiorem E4.

Wynik jest przeglądem twierdzeń względem zapisanych dowodów, nie niezależnym powtórzeniem całego runtime, binarnego buildu, kontroli ACL ani testów UI. Publikacja tej opinii nie oznacza kontaktu z sesjami produktowymi.

## Uzupełnienie końcowe — A4-REVIEW-001 zamknięte w harnessie

Po pierwszej opinii właściciel modułu audytu rozdzielił warunki i wykonał rozszerzony zestaw na tym samym E4 `3eaac2953c3ee0d01d085e595d68edc091c284f2`. Reviewer przeczytał nowe assertions i receipts bez ponownego uruchamiania konsumentów.

- E4-06 zmienia tylko widoczność przy `allow-all`, zachowując wybór analizy i dane kanoniczne.
- E4-06a porównuje `allow` i `deny-a` na **tym samym pełnym planie**, bez filtra evidence: dodatnia kontrola zwraca `a` wraz z wartością; odmowa usuwa jego dane i incydentne relacje oraz jawnie zgłasza `permission_filtered`/niepełność.
- E4-06b sprawdza osobno focus na zabronionym adresie: pozostaje placeholder `denied`, bez `properties` i z zerem wywołań resolvera adaptera.

Wszystkie trzy wiersze mają PASS. Finalne E4 to **20 kryteriów: 15 PASS, 2 FAIL, 3 BLOCKED**. Pozostałe 17 wcześniejszych przypadków jest identycznych; dodano dwa przypadki i poprawiono jeden oracle. `E4-results-initial.json` zachowuje dokładny SHA-256 receipt ocenionego w pierwszej opinii.

**Bieżący status A4-REVIEW-001: rozwiązane w testach audytu.** Nie jest to naprawa produktu ani certyfikacja natywnej ACL. Pierwotna uwaga i jej uzasadnienie powyżej pozostają jako historia przeglądu; nie są już otwartą bramką. Nowe hashe plików i wynik porównania zapisano w `followups` JSON.
