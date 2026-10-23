# Interfejs współpracy — kąt właściciela: jego styl pracy i to, co mu się przyda (R90, projekt 1/3, Fable, 2026-10-23)

Jeden z trzech projektów pakietu R90 (pozostałe: kąt koordynatora i rutyn; kąt ekonomii i kanałów). Buduje na
`INTERFEJS_WSPOLPRACY_2026-10-23.md` (R89: interfejs = perspektywa grafu aplikacji + cienki widok na telefon; role jako
presety; korekta jako diff do spec) i na tym, co działa (stan 2026-10-23): strona drzewa spec z przyciskami i kolejką
„Czeka na Ciebie” (R88 v1, R89 P2), sekretarz co godzinę, `tools/spec.py`. Wszystko niżej to presety i przykłady
(R58, R83, R84); nic nie jest jego słowami, jeśli nie stoi w cudzysłowie z numerem R. Fakty o stanie są datowane (R60).
Kontrola przed użyciem (Haiku, 4 anty-wzorce) — do zrobienia przez koordynatora przed syntezą.

## §1 Teza

Jego słowa są danymi od pierwszej sekundy, a jego czytanie jest wyjątkiem, nie regułą. Interfejs od jego strony to nie
ekran do czytania, lecz trzy gesty: **wrzucić myśl** (o każdej porze, dyktowaną, urwaną — z pewnością, że jest zapisana
dosłownie i że ktoś ją skleił z resztą), **dotknąć raz** (zatwierdzić, poprawić, oznaczyć „to przykład”, dać wolną rękę,
zatrzymać — zawsze z zadeklarowanym skutkiem i odwrotem) i **zobaczyć różnicę** (co się zmieniło od jego wyjścia, co
czeka, co pamiętam — jako liczniki i diffy, nie jako dokumenty). Każda linia, którą ma przeczytać, musi zmieniać jakiś
licznik; inaczej nie powinna do niego trafić.

## §2 Dowody z jego słów i pomiarów

| # | Cecha stylu | Jego słowa (dosłownie, R-id) | Pomiar (stan 2026-10-23) | Wniosek dla interfejsu |
|---|---|---|---|---|
| 1 | Pisze seriami, gdy ma czas; potem długa cisza | CLAUDE.md: „komunikuje się jak mam czas i wtedy każdą myśl na bieżąco pisze później pracuje nad następną więc się możesz spodziewać dłuższych serii”; R62: „przez kilkadziesiąt godzin się nie będę odzywał” | 14 pozycji R ze stemplami 08:0x–09:5x UTC (co ≈ 8 min), potem jedna o 17:3x; 31 pozycji R z 23.10; cytaty: n=46, mediana 36 słów, max 255 | zapis natychmiastowy i tani; sklejanie serii krawędziami; digest po powrocie |
| 2 | Dyktuje, z telefonu, zdania się urywają | R60: „ASR się wczuło w klimat, musiałem poprawić z paradygraf ;D”; R70: „samo mi się wysłało”; R69: „może jak dziadek gadam” | 5 odczytów ASR w rejestrze R57–R90 (prąd=prompt, kmini, bajas, gra=graf, grafię); 2 wypowiedzi urwane i dokończone następną (R69→R70) | cytat nietykalny; odczyt ASR jako osobna warstwa; krawędź „ciąg dalszy” |
| 3 | Zmęczony, w nocy, idzie spać w trakcie | R62: „w szczególności o 2:12 w nocy”; R75: „ja mogę iść spać wreszcie”; R89: „ja jestem bardzo zmęczony”, „A ja jestem zmęczony więctutaj masz wolną rękę” | wypowiedzi o 00:1x–00:2x, 08:0x–09:5x i 17:3x UTC | tryb nocny: pytania tylko o drogim odwrocie; decyzje wstępne; nic nie czeka na niego bez zadeklarowanego skutku |
| 4 | Nie czyta długich tekstów; jego czas jest walutą | R62: „mój czas który bym zmarnował na przewijanie i szukanie jest więcej wart”; CLAUDE.md/QUEUE:150: „nie gadaj tyle bo ja i tak tego nie czytam” | dokumenty stanu, które miałby czytać: QUEUE 645, INDEX 1822, CLAUDE.md 677, rejestr 909, SPEC.md 621 linii (4674) | liczniki i diffy; pełny tekst o jedno dotknięcie dalej |
| 5 | Przypadkowe dotknięcia na telefonie | R89: „odruchowo stop nacisnąłem i nie wiem czy ci wszystkiego nie zatrzymałem”; R90: „za pierwszym razem coś przypadkiem nacisnąłem” | 2 przypadki jednego dnia | „stop” = pauza z pokazanym kosztem, gdy biegnie coś drogiego; każdy gest odwracalny |
| 6 | Koryguje przykładem i wskazaniem, nie formułowaniem reguły | R58: „widzę w tabelce wpisałeś przypisanie jako wymagania, co się kłóci z filozofią”; R83: „żadne dwa pokrętła”; VM 07:50 (QUEUE): „A słyszałeś kiedyś o mojej tzw. filozofii?” | pięć razy jednego dnia oznacza własne słowa jako przykład albo nie-specyfikację (R74, R76, R78, R89, R90) | przyciski „to przykład”, „to nie moje słowa”, „sprawdź z filozofią” zamiast pisania |
| 7 | Chce widzieć, co pamiętam, hierarchicznie i na żywo | R73: „żebym widział co pamiętasz, na żywo jak je zmiany wprowadzasz bo to możesz rozwinąć gałązkę którą edytujesz”; R57: „pamiętam że zgłaszałem takie wymaganie”; R65: „masz zebrane wytyczne co do interfejsu i funkcjonalności?” | 12 parafraz w stanie `proposed` czeka; drzewo spec i wyszukiwarka istnieją | „co pamiętasz o X” = gałąź drzewa z stanami, nie odpowiedź w czacie |
| 8 | Daje wolną rękę; oczekuje decyzji wstępnych, nie pytań | R75: „ostrożnie podejmij wstępną taką żeby się dało z małymi kosztami w razie w wycofać i z pomyślunkiem jedź do przodu”; R62: „działaj śmiało”; R63: „tobie troszkę bardziej ufam, w twój gutt feeling” | — | mandat jako węzeł (zakres, czas, wyjątki); brak odpowiedzi = skutek zadeklarowany z góry |
| 9 | Nie chce szukać nazw ani sprawdzać sam | R71: „żebym znów nie musiał szukać tych nazw wszystkich”; R90: „nie pamiętam czy ageds też ma serwer?”, „Masz podgląd na wszystkie sesje, nie?” | — | każda czynność, o którą kiedykolwiek prosił, jest przyciskiem albo jedną gotową linią |
| 10 | O stan pyta jednym zdaniem; chce odpowiedzi w perspektywach | R66: „przedstaw plan z perspektywami, kiedy będą funkcjonalności, kiedy interfejs”; R90: „czy ogarniesz stamtąd wszystko?” | — | pytanie o stan → jedna linia z liczbami, reszta zwinięta |
| 11 | Liczby traktuje orientacyjnie | SLOWA:66: „nie bierz bezwzględnych liczb za bardzo serio pokazuje tylko orientacyjnie” | — | zakresy z datą zamiast dokładnych liczb; koszt jako klasa |
| 12 | Reaguje na to, co widzi, nie na opis | R75: „o ładnie to wygląda” (po żywym drzewie); R65: zgłoszenia z ekranów telefonu | — | pokazać (strona, zrzut, diff), nie opisywać |
| 13 | Sam ustawia dyscyplinę pytań | R80: „agent niech wszystko sprawdzi sam zanim zapyta i pytając ma już przygotowany do wyboru i lub modyfikacji zestaw kryteriów”; R82: „o trudniejsze pytać, standardowo … żeby o wszystko pytać albo żeby o nic nie pytać” | — | pytanie zawsze z gotową odpowiedzią domyślną i kryteriami; preset „pytaj” to przestrzeń, nie trzy położenia (R83) |
| 14 | Sam się koryguje własną filozofią | R72: „nie wiem czy jest sens to w ogóle definiować, wyróżniać, moja filozofia sama mnie dopadła i skopała” | — | kontrola z filozofią działa też na jego słowa: advisory, nie wyrok |

## §3 Propozycje

Każda: co, dla kogo, mechanizm ogólny (silnik) vs przykład (preset), jak mierzyć, koszt, odwrót. Numeracja W1–W10 to
kolejność opisu, nie priorytet; priorytet ustala synteza (preset).

### W1 „Jeden rzut oka” — liczniki zamiast kolejki
- **Co:** pierwszy ekran na telefonie to kilka liczników, każdy rozwijalny do listy pozycji po ≤ 2 linie: czeka na Ciebie
  (z „bez odpowiedzi: …” i klasą kosztu odwrotu), zrobione od Twojej ostatniej wizyty, w toku (z czasem biegu), nie wyszło,
  oryginalne od agentów, koszt tygodnia (% i linia planu). Ziarno listy = jego słowa z 00:1x („co zaczynam implementować,
  co kończę, co buduję, co nie wyszło, co agenci wymyślili oryginalnego”); lista otwarta.
- **Dla kogo:** on, telefon, 10 sekund, także o 2 w nocy.
- **Mechanizm:** licznik = zapytanie do grafu (węzły z proweniencją i datą) zakotwiczone w węźle „ostatnia wizyta”
  (węzeł, nie tylko `localStorage`); zestaw, kolejność, progi i to, co widać o której porze — preset per urządzenie.
- **Przykład:** nocą widoczny tylko „czeka na Ciebie” o drogim odwrocie; rano wszystko.
- **Pomiar:** dotknięć do pierwszej decyzji; udział wizyt bez otwarcia czatu; długość wizyty.
- **Koszt:** generator strony (`spec_html.py`) + węzeł wizyty w bazie strony; bez modelu.
- **Odwrót:** strona bez sekcji liczników (dzisiejsza kolejka P2 działa).

### W2 „Wrzutka” — myśl o 2 w nocy staje się węzłem spec natychmiast
- **Co:** jedno duże pole (tekst/głos) na stronie i w aplikacji; wysłane = węzeł `statement` z cytatem dosłownym, czasem,
  źródłem (telefon, ASR), stanem `raw` — zero modelu, zero czekania. Sekretarz (tani model; bieg co godzinę jak dziś —
  preset) dokłada WARSTWĘ ODCZYTU: tytuł, temat, krawędzie `continues` (urwane zdanie → ciąg dalszy), `corrects` (którą
  parafrazę), `exemplifies` (co w tekście jest przykładem), odczyt ASR („prąd” = prompt) — osobno od cytatu; i jedną linię
  „co z tego zrozumiałem”. On widzi ją w „czeka na Ciebie” z gestami: OK / Popraw / To przykład.
- **Dla kogo:** on w nocy; koordynator rano (czyta węzły, nie czat); agenci (brief cytuje `spec:<id>`).
- **Mechanizm:** cytat nietykalny (`spec.py add` już tak działa); odczyty = węzły z pochodzeniem (sekretarz, model, data,
  stan) i własną wiarygodnością (R59 — jego poprawki kalibrują); sklejanie serii = krawędzie, nigdy edycja cytatu.
- **Przykład:** „samo mi się wysłało odpowiadający…” (R70) → `continues` do R69, wspólny tytuł.
- **Pomiar:** sekundy od wysłania do węzła (cel: natychmiast); udział wrzutek z odczytem w ciągu godziny; liczba jego
  poprawek do odczytów (spadek = sekretarz się kalibruje).
- **Koszt:** pole na stronie + istniejąca kolekcja `spec_inbox` + rozszerzony prompt sekretarza; bieg sekretarza już jest.
- **Odwrót:** `spec.py add` ręcznie przez koordynatora (dziś).

### W3 Pytanie zawsze z gotową odpowiedzią i czasem: dotknij raz albo milcz
- **Co:** każde pytanie do niego niesie odpowiedź, którą sam bym przyjął (R75), klasę kosztu odwrotu, czas, po którym ją
  przyjmę (pole otwarte, preset per klasa), kryteria wyboru (R80) i gesty: Tak / Nie / Inaczej (tekst lub głos) / Nie
  pytaj o takie (R58, R82) / Jednak do Fable. Milczenie po czasie = zadeklarowany skutek, zapisany jako decyzja wstępna
  z odwrotem. Istnieje częściowo (P2: `if_silent`, `cost_to_reverse`); brakuje czasu, kryteriów, „nie pytaj o takie”.
- **Mechanizm:** pytanie = węzeł z `if_silent`, `cost_to_reverse`, `ask_after`, `criteria[]`, klasą (otwartą); „nie pytaj
  o takie” dokłada krawędź do mapy preferencji (R85/R86) — ten sam mechanizm co „nie pytaj następnym razem” w aplikacji.
- **Pomiar:** pytań na dobę (cel: w dół); mediana czasu jego odpowiedzi; udział rozstrzygniętych milczeniem; odwrotów po
  milczeniu (cel: rzadkie — inaczej `if_silent` był zły).
- **Koszt:** pola w `understanding.json` + sekretarz; bez jądra.
- **Odwrót:** `if_silent` puste = czekaj (dzisiejszy stan dla pozycji bez pola).

### W4 Korekta przez wskazanie: „to przykład”, „to nie moje słowa”, „sprawdź z filozofią”
- **Co:** na każdym akapicie projektu, briefu, parafrazy — znaczniki jednym dotknięciem (lista otwarta; dalsze przykłady:
  „za dużo”, „pokaż mi”, „to pilne”, „to na później”). Znacznik = krawędź od niego (pochodzenie: owner, data, urządzenie)
  do fragmentu; kontrola przed użyciem (Haiku) i addendum (Sonnet) czytają znaczniki jako znaleziska; agent w biegu nie
  jest restartowany (kontrola marnotrawstwa 6).
- **Dowód:** §2 wiersz 6 — pięć razy „to tylko przykład” jednego dnia; bootstrap skorygował pytaniem o filozofię.
- **Mechanizm:** fragment dokumentu adresowalny (plik:linia lub id akapitu) jako węzeł; rodzaje znaczników = rodzaje
  krawędzi (dodawane przez użycie, R84); dokument renderowany z oznaczeniami; nic nie jest usuwane, znacznik `supersedes`.
- **Pomiar:** znaczników na dokument; czas znacznik → addendum; czy ta sama klasa błędu wraca po znaczniku.
- **Koszt:** render dokumentu z id akapitów na stronie + reguła w prompcie sekretarza; kontrola już biegnie.
- **Odwrót:** znacznik jako wpis `remark` w skrzynce (dziś).

### W5 „Co pamiętasz o …?” — zapytanie zwracające gałąź, nie odpowiedź
- **Co:** jedna linia (tekst/głos) → rozwinięta gałąź drzewa spec (R73) filtrowana tematem: stany (approved / proposed /
  corrected / raw), najnowsze pierwsze, jego cytaty zwinięte, moje parafrazy widoczne z R-id, decyzje wstępne z odwrotem,
  oraz „czego nie pamiętam” (wypowiedzi bez odczytu, parafrazy bez zatwierdzenia, luki). Warianty tego samego zapytania
  (przykłady): „co ustaliliśmy o…”, „co zmieniłeś beze mnie”, „co mówiłem o…” (szukanie w jego słowach).
- **Dowody:** R73, R57, R65, R66.
- **Mechanizm:** perspektywa grafu (spec to węzły) z filtrem; ten sam silnik co ekran „Ty” / „Co pamiętam” w aplikacji
  (W2b-1, stan 23.10); odpowiedź ≤ 1 ekran, reszta zwinięta.
- **Pomiar:** ile razy w czacie pyta o rzeczy obecne w spec (cel: w dół); długość odpowiedzi w ekranach.
- **Koszt:** wyszukiwarka strony już jest; dodać filtr stanów i „czego nie pamiętam”; w aplikacji — perspektywa.
- **Odwrót:** `spec.py list/show` w czacie.

### W6 Projekt czytany jako diff decyzji, nie jako proza
- **Co:** dokument projektowy dociera do niego jako: (a) 5 linii „co to robi z Twoich słów” (które R realizuje, gdzie
  wychodzi poza — oznaczone „dodane, nie mówiłeś”), (b) lista decyzji z odwrotem, (c) lista oznaczonych przykładów,
  (d) liczby jako zakresy z datą; pełny tekst o dotknięcie dalej. Wersja do wersji: diff decyzji i przykładów, nie akapitów.
- **Dowody:** R62; R65: „w ogóle wszystkie teksty to nic nie mówią, mi - autorowi niby”; SLOWA:66; R58.
- **Mechanizm:** dokument = węzeł z dziećmi `decision`, `example`, `addition`, `measure`, wydobytymi przez tanią kontrolę
  (check-before-use już biegnie) i zapisanymi jako dane; render z tych węzłów; nic nie zastępuje tekstu źródłowego.
- **Pomiar:** czy otwiera pełny tekst (rzadko = dobrze); jego poprawek na dokument przed/po; czas publikacja → gest.
- **Koszt:** kontrola Haiku emituje dane zamiast prozy (zmiana skryptu) + render; żadnego nowego modelu.
- **Odwrót:** link do `.md` (dziś).

### W7 Dwa strumienie: status i dyskusja — wybiera on
- **Co:** strumień „status” (zdarzenia zmieniające liczniki W1) i strumień „dyskusja” (czat), otwierany tylko przez niego;
  linia statusu powstaje ze zdarzenia (start/koniec pakietu, bramka, commit, karta, decyzja wstępna), nie z ręki
  koordynatora; w czacie koordynator pisze tylko to, czego nie ma w zdarzeniach.
- **Dowody:** „nie gadaj tyle … oprócz konwersacji gdzie dyskutujemy to w tym wątku wpisz tylko…” (CLAUDE.md, 00:1x); R62.
- **Mechanizm:** zdarzenia jako węzły z klasą; strumień = perspektywa z filtrem (urządzenie, pora, klasa) — preset;
  słowo sterujące „nie gadaj tyle” przełącza filtr (W8).
- **Pomiar:** linii koordynatora w czacie na dobę vs zdarzeń; udział linii bez zmiany licznika (cel: 0).
- **Koszt:** workflow już wytwarza karty i commity — dopisać ich zapis do grafu/strony.
- **Odwrót:** wszystko do czatu (dziś).

### W8 Słowa sterujące jako dane z zadeklarowanym skutkiem
Słowa niżej to PRZYKŁADY (lista otwarta; on dopisuje własne jednym zdaniem; rozpoznawane też w wariantach ASR):

| Słowo (przykład) | Skutek (preset) | Odwrót |
|---|---|---|
| „stop” | gdy biegnie coś drogiego: pauza + jedna linia „zatrzymać X (14 min, ≈0,4 pkt)? Tak / Nie / Pauza” z otwartym czasem na odpowiedź; gdy nic drogiego nie biegnie: stop od razu; zawsze zapis „kto, kiedy, co stanęło” | preset „stop zawsze natychmiast” |
| „wolna ręka”, „rób to co wiesz” | węzeł mandatu: zakres (temat / pakiet / wszystko), do kiedy, wyjątki stałe (jego pieniądze poza budżetem, jego dane u nowego dostawcy, ×10 — CLAUDE.md), tryb R75 dla zakresu; pytania z zakresu rozstrzyga `if_silent` od razu | mandat wygasa albo cofnięty jednym dotknięciem |
| „to przykład” | znacznik W4 na ostatnim lub wskazanym fragmencie | usunięcie znacznika (`supersedes`) |
| „nie gadaj tyle” | filtr strumienia: tylko status (W7) | „gadaj” |
| „idę spać”, „mogę iść spać” | tryb nocny: pytania tylko o drogim odwrocie, decyzje wstępne, digest W9 na powrót | pierwsza wizyta lub „wróciłem” |
| „podkręć” | plan tempa w górę o krok (preset) z pokazaną linią (`pace_plan.json`) | „zwolnij” |
| „co pamiętasz…” | zapytanie W5 | — |

- **Mechanizm:** pack słów sterujących (jak komendy regułowe `rule_command` w aplikacji, stan 23.10) z polami: wzorce,
  skutek, zakres, odwrót, potwierdzenie tylko gdy koszt przekracza próg (preset); każde użycie zapisane z pochodzeniem.
- **Pomiar:** przypadkowe „stop” bez straty (cel: 100 %); mandatów na tydzień i ich odwrotów; noce bez utraconej pracy.
- **Odwrót:** słowa = zwykły tekst do koordynatora (dziś).

### W9 „Od Twojego wyjścia” — jeden ekran po powrocie
- **Co:** po przerwie (próg — preset, przykład: > 6 h): co zdecydowałem wstępnie (z odwrotem), co skończone, co nie wyszło,
  co agenci wymyślili, co czeka (drogi odwrót pierwszy), koszt w punktach jako zakres; ≤ 1 ekran, wiersz → karta.
- **Dowody:** R62, R75; CLAUDE.md: „czyta ją w zrywach”.
- **Mechanizm:** W1 zakotwiczone w węźle poprzedniej wizyty; to samo zapytanie co „co zmieniłeś beze mnie” (W5).
- **Pomiar:** jego pierwsza wiadomość po powrocie — pytanie o stan (źle) czy decyzja/korekta (dobrze); takich pytań na
  tydzień.
- **Koszt:** jak W1. **Odwrót:** `QUEUE_S1.md` jak dziś.

### W10 Telefon jako konsola: aplikacja ChatADHD z perspektywą „współpraca” (opcja z kosztem)
- **Co:** skoro interfejs = perspektywa grafu aplikacji (R89 §6), a aplikacja ma już głos (Whisper/Google), komendy regułowe
  i ekran „Ty”, to konsolą na telefonie może być sama aplikacja z perspektywą „współpraca” (spec, kolejka, liczniki, słowa
  sterujące); strona-artefakt zostaje cienkim widokiem. Opcja, nie decyzja: wymaga synchronizacji grafu spec z telefonem.
- **Pomiar:** czas do pierwszego gestu na stronie vs w aplikacji; udział wrzutek głosem.
- **Koszt:** perspektywa + import `spec.json` jako węzłów + synchronizacja; wyższy niż strona.
- **Odwrót:** strona-artefakt (działa dziś).

Co może zrobić jednym dotknięciem (zestawienie; lista otwarta): zatwierdzić parafrazę, poprawić własnymi słowami (głosem),
oznaczyć „to przykład” / „to nie moje słowa”, dać wolną rękę na zakres, zatrzymać/wstrzymać z pokazanym kosztem, spytać
„co pamiętasz”, otworzyć rzecz (strona, APK, zrzut, diff), podkręcić/zwolnić tempo, „nie pytaj o takie”, „jednak do Fable”.

## §4 Czego nie robić i dlaczego (co usunąć z dzisiejszego doświadczenia)

| Co dziś kosztuje jego uwagę | Dowód | Zamiast tego |
|---|---|---|
| Odpowiedzi w czacie dłuższe niż kilka linii; parafrazy w prozie | R62; „nie gadaj tyle” | W6, W7; parafraza ≤ 5 linii i jako węzeł z gestami (W3) |
| Menu opcji i pytania techniczne | CLAUDE.md „Decisions” (decyzje techniczne są Claude'a); R84 | decyzja wstępna z odwrotem (R75); pytanie tylko z klas: pieniądze, dane, ×10, sprzeczność z wymaganiem |
| Stan projektu w 645-liniowej kolejce i 1822-liniowym INDEX | §2 wiersz 4 | W1, W9; kolejka zostaje dla koordynatora |
| Szukanie nazw, komend, adresów | R71 | każda czynność przyciskiem albo gotową linią do wklejenia (W8, zestawienie) |
| Powtarzanie wymagań, bo nie wie, czy je mam | R57 | W5 — szukanie w jego słowach i stan rozumienia przy każdym |
| Teksty, które nic nie mówią; nazwy etapów; angielskie resztki | R65; znaleziska W2b-1 (`minimal`, „zapisane przed M1”) | słownik z jego słów; audyt tekstów przed publikacją |
| Gest, który zatrzymuje coś drogiego bez pytania | R89, R90 | W8 „stop” z kosztem i pauzą |
| Pytania bez odpowiedzi domyślnej i bez „kiedy” | R80, R75 | W3 |
| Dokładne liczby bez daty i zakresu | SLOWA:66; R60 | zakresy z datą; koszt jako klasa; liczba w liście nigdy nie jest listą (R83) |
| Jego przykłady zapisane jako wymagania | R58 | W4 + kontrola przed użyciem jako dane |
| Ciche restarty i utrata pracy agentów po korekcie | kontrola marnotrawstwa 6 (3 restarty ≈ 0,9 pkt, 22.10) | addendum po karcie; W8 „stop” pokazuje koszt utopiony |
| Ciężar odczytu ASR przerzucony na jego poprawki | R60 | odczyt jako warstwa (W2); pytanie tylko gdy odczyt zmienia sens |
| Czekanie na jego odpowiedź, gdy decyzja jest tania do odwrotu | R75, R62 | `if_silent` i mandat (W3, W8) |
| Wybieranie za niego, co zobaczy (zamknięte widoki) | ZASADA WŁAŚCICIELA (CLAUDE.md) | każdy licznik, filtr, słowo i próg = preset w danych z „kto ustawił” |

Zasady negatywne projektu: nie budować osobnego produktu (R89 §6 — perspektywa grafu); nie zamykać listy słów sterujących
ani liczników (R83, R84); nie ukrywać niczego i nie usuwać (supersedes); nie wprowadzać trybu, którego on nie może
wyłączyć; nie oceniać użyteczności jego wrzutki — najwyżej advisory „nie wiem, do czego to dopiąć” (R58).

## §5 Pytania do dwóch pozostałych kątów

Do kąta koordynatora i rutyn:
1. Które rutyny mają być jego przyciskami (W1/W8), a które zostają wewnętrzne? Propozycja kryterium: czy on kiedykolwiek
   o to prosił lub pytał (R71, R90 „odpal”, „podkręć”) — reszta widoczna, ale zwinięta.
2. Jak brief konsumuje węzeł mandatu („wolna ręka”: zakres, czas, wyjątki) i tryb nocny — jako pola `args`
   `single-package.js`, czy jako węzły, które skrypt czyta sam z `spec.json`?
3. Jak zdarzenia workflow (start, koniec, karta, bramka, decyzja wstępna) stają się węzłami strumienia statusu (W7) bez
   ręcznej linii koordynatora — i co z zdarzeniami sesji VM (S1-VM)?
4. Jak znacznik „to przykład” (W4) na dokumencie w toku dociera do agenta: addendum po karcie (kontrola marnotrawstwa 6c)
   czy kolejka korekt czytana przez agenta na końcu pakietu?
5. Jego przykład „przy starcie nowego podwykonawcy całą filozofię mu zapodać” (R90): filozofia jedzie jako `CLAUDE.md`
   (dziś) czy jako wybrane węzły spec wg tematu pakietu — i jak zmierzyć różnicę liczbą znalezisk kontroli?

Do kąta ekonomii i kanałów:
1. Koszt jednej wrzutki (W2): bieg sekretarza pusty vs z wrzutką (P1 z R89 to mierzy); czy odczyt ASR i sklejanie serii
   mieszczą się w Haiku, a kiedy eskalować do Sonneta/Fable?
2. Jego czas czytania jako waluta na paragonie: co mierzyć (wizyty, dotknięcia, czas do gestu) bez nadzoru; gdzie to
   leży (węzeł wizyty — za jego zgodą; preset: włączone czy wyłączone?).
3. Kanał nocny: strona-artefakt, aplikacja (W10) czy czat — który jest najtańszy i najpewniejszy o 2 w nocy na telefonie;
   czy strona może budzić koordynatora (webhook) tylko dla klasy „drogi odwrót”?
4. Dwie sesje (chmura S1, VM S1-VM) i wyłączanie VM po 30 min: jak w W1 pokazać jeden status „co gdzie biegnie” i czy
   „stop” z telefonu ma dotrzeć do obu?
5. Digest W9 i liczniki W1: generowane statycznie po każdej zmianie (dziś `spec_html`) czy liczone na żywo z bazy strony —
   koszt, opóźnienie, co gdy baza strony niedostępna?

## §6 Po dyskusji (2026-10-23)

Przeczytane: `koordynator.md` (K1–K9) i `ekonomia.md` (E1–E10). Niżej tylko to, co zmienia mój projekt: co biorę, co odrzucam
albo osłabiam (filozofia / dowód / koszt), gdzie jest prawdziwy konflikt do jednej decyzji, i teza po poprawce. Wszystko dalej
to presety (R83, R84); fakty datowane 23.10.

### 6.1 Co biorę i dlaczego

| Skąd | Co biorę | Dlaczego (z mojego kąta) |
|---|---|---|
| K7 | **Zapis biegu jako dane, linie statusu jako RENDER** zdarzeń started / finished / building / failed / original_ideas | To jest ŹRÓDŁO liczników W1 i strumienia W7, którego mój projekt nie miał: dziś licznik „zrobione od Twojej wizyty” musiałby być liczony z prozy QUEUE. Jego lista z 00:1x (CLAUDE.md „Thread style”) jest schematem zdarzeń, nie prośbą o styl. Dowód kosztu obu walut: 37/60 commitów to księgowość Fable. |
| K3 | **Pakiet kontekstu jako lista odwołań z hashem**, w tym `spec:<id>` przez `spec.py cite --full` | Pomiar koordynatora: `spec:` w 0/21 briefów — jego słowa SĄ danymi od pierwszej sekundy (W2), ale żaden agent ich nie czyta, tylko moje parafrazy. Bez K3 znacznik W4 „to przykład” nie ma gdzie wylądować u agenta. Jego przykład z R90 („całą filozofię mu zapodać”) = jedna instancja pakietu, zgodnie z „przypominam przykład to jest”. |
| K9 (pyt. 2) | **Jedna tabela słów, zasiana jego słownikiem**, dwa rendery: telefon (W8) i warsztat (makra K9) | Koordynator pyta, jakich słów on używa — odpowiedź jest w cytatach: „stop” (R89), „wolna ręka” (R89), „rób to co wiesz” (R75), „podkręć” (R62), „nie gadaj tyle” (CLAUDE.md), „odpal” (R90), „idę spać” (R75), „co pamiętasz” (R73). Dwie tabele słów = dwa źródła błędów; jeden pack, pole `render`. |
| K6 | **Operator na tanim modelu wykonuje `if_silent` po `ask_after`** i zapisuje jego dotknięcia | W3 („milczenie = zadeklarowany skutek”) bez wykonawcy to obietnica; tura koordynatora na to kosztuje ≈ 0,28 mln kontekstu (R89 §4.2), a jego „Zatwierdzam” nie wymaga osądu. Warunek z mojego kąta (6.2 p. 6): między jego dotknięciem a zapisem nie stoi model. |
| K5 | **Warunki wstępne jako dane z powodem i datą** (dysk ≥ 5 GB, jeden build) | Ta sama klasa, co mój „stop” z kosztem (W8) i gaśnięcie VM (E5): polityka z pokazanym skutkiem, nie wiedza plemienna. Przenoszę kształt `{check, why, since}` na gesty: `{gesture, effect, cost, reversal, since}`. |
| E5 | **Mapa miejsc i usług** jako wiersz liczników W1 („miejsca: 3 żyją · 1 śpi · 2 nie postawiono”) z rozwinięciem | Jego pytania z R90 („czy ogarniesz stamtąd wszystko?”, „Masz podgląd na wszystkie sesje, nie?”, „nie pamiętam czy ageds też ma serwer?”) to dokładnie pytania do tabeli miejsc, na które dziś odpowiada tura Fable. Zamyka moje pyt. 4 do ekonomii. Wyłączanie VM po 30 min = jego pieniądze → pytanie W3 z liczbą i `if_silent: nie podtrzymywać` (zgodne z E5). |
| E3 | **Licznik PRZED startem** (`preflight`: rola→model, pkt, okno, miejsce, co przekroczy) | To jest źródło liczby na moim „stop” („zatrzymać X: 14 min, ≈ 0,4 pkt?”) i na każdej pozycji „czeka na Ciebie” z klasą kosztu odwrotu. Bez E3 W8 pokazuje koszt zmyślony. Dowód: poprawki check-before-use na Fable przez brak `model` (ledger 11:23). |
| E9 | **Routing klasa × pora × budżet jako dane** zamiast mojego „trybu nocnego” | Mój W8 „idę spać” był TRYBEM (flaga); u ekonomii to wiersz tabeli routingu. Wiersz jest lepszy: nic do wyłączania, odwrót = usunięcie wiersza, a jego słowo („idę spać”, „wróciłem”) tylko przestawia wartość `hour`/`mode` w danych. Usuwam z W8 pojęcie trybu; zostaje słowo → zmiana wiersza. |
| E4 (pyt. 3) | **Jego liczba („46 %”) jako próbka `source: owner`**: rozpoznawana w jego zdaniach przez sekretarza (regex „% tygodniowego”) i POTWIERDZANA jednym dotknięciem; pole na stronie jako drugie wejście | R71 („żebym znów nie musiał szukać”): nie każę mu szukać pola. Ale sam regex bez potwierdzenia to interpretacja jego słów bez niego — potwierdzenie w kolejce W3 z `if_silent: przyjmij` po czasie. |
| E7 (pyt. 5) | **Jeden kształt `handover`** dla teleportu, karty pakietu, rekordu sekretarza — **i dla mojego W9** | W9 „Od Twojego wyjścia” to przekazanie zmiany: od nocnej zmiany (koordynator + agenci) do niego. Czwarta instancja tego samego rekordu; render dla niego = liczniki, dla sesji = karta. Oryginalne w dyskusji; zapisać w syntezie. |
| E2 (część) | **Pomiar słów/dobę do niego** jako licznik W1 („dziś do Ciebie: N słów”) i baza do zmierzenia z QUEUE/czatu 21–23.10 | Moja teza (każda linia musi zmieniać licznik) bez liczby jest hasłem; E2 daje jej miarę. Biorę licznik i pomiar; budżet jako filtr odrzucam (6.2 p. 1). |

### 6.2 Co odrzucam albo osłabiam i dlaczego

1. **E1/E2 jako filtr tego, co widzi.** „Każda linia obciąża budżet” — tak, jako LICZNIK i advisory dla koordynatora; **nie** jako
   mechanizm, który linii nie pokaże. Filtr wybierający za niego, co zobaczy, to ZASADA WŁAŚCICIELA łamana po cichu (ukrywa). Preset:
   wszystko widoczne, zwinięte wg routingu E9, z wierszem „N pozycji zwiniętych — pokaż”; budżet przestawia on jednym słowem („mniej”,
   „pokazuj wszystko”), nie edycją `attention_plan.json`. Plik może istnieć, ale jego wartości ustawiają słowa W8.
2. **E1 paragon przy każdej pozycji w JEGO perspektywie** (odpowiedź na pyt. 1 ekonomii). Dla niego liczba przy każdej linii to szum
   i fałszywa precyzja (SLOWA:66 „nie bierz bezwzględnych liczb za bardzo serio”). Preset: koszt pojawia się **przy geście** (zanim
   zatrzyma, zatwierdzi start, podtrzyma VM) jako klasa/zakres, i jako jedna suma dnia; paragony per węzeł istnieją jako dane i są o
   dotknięcie dalej. Jednostka jego uwagi: nie wymyślam jednej — zapisuję to, co obserwowalne bez nadzoru (słowa pokazane, dotknięcia,
   czas do gestu); „minuty” to wyprowadzony preset. Odwrót: włączyć paragon per pozycja w jego perspektywie.
3. **E2/E9 pora wnioskowana z czasów wizyt** (pyt. 2 ekonomii). Wnioskowanie z `localStorage` to datowana obserwacja, która może
   ZAPROPONOWAĆ wiersz routingu („widzę, że czytasz o 00–01 i 08–10 — ustawić noc 01–07? Tak/Nie”); nigdy nie przestawia routingu
   sama. Powód: cicha decyzja o tym, co widzi i kiedy — ukrywa. Przełącznik to jego słowo lub jawny preset.
4. **K9 „Widok Warsztat” jako ekran dla niego** (pyt. 3 koordynatora). Warsztat jest koordynatora i operatora. Dla niego biegi to
   jeden wiersz liczników („w toku: 2 biegi · 14 min · ≈ 0,4 pkt”) i wariant zapytania W5 („co robisz / co biegnie” → gałąź biegów).
   Nie drugi ekran, którego ma się nauczyć; ten sam silnik. Nic nie jest przed nim ukryte — jest o jedno rozwinięcie dalej.
5. **K „confirm: owner” jako pauza biegu do jego odpowiedzi** (np. `apk.release`). Osłabiam do W3: pytanie niesie odpowiedź domyślną,
   `ask_after`, `cost_to_reverse` i `if_silent`; bieg czeka na niego **tylko** w klasach z CLAUDE.md (jego pieniądze poza budżetem,
   jego dane u nowego dostawcy, ×10) — reszta to decyzja wstępna z odwrotem (R75 „ostrożnie podejmij wstępną”). Koordynator sam
   to wpisuje w ryzykach; tu staje się decyzją.
6. **K6 operator „z routingiem po cechach” na JEGO wejściu.** Routing po cechach jest dla parafraz i korekt złożonych (R88
   „żonglerka modelami”), nie dla jego dotknięcia ani jego cytatu. Zasada z W2: cytat nietykalny, model dokłada tylko warstwę odczytu;
   „Zatwierdzam” → `spec.py approve` bez modelu po drodze. Koordynator tego nie łamie, ale K6 tego nie mówi — ma mówić.
7. **K1 „kto może uruchomić” jako podzbiór dla właściciela** (pyt. 2 koordynatora). R90: „wyeksponować wszystkie narzędzia” — wszystko
   widoczne i uruchamialne przez niego z kartą E3 przed startem; preset KOLEJNOŚCI na pierwszym ekranie: to, o co kiedykolwiek
   prosił (R71, R90 „odpal”, R62 „podkręć”), reszta o dotknięcie dalej. Zbiór „kto może” zostaje otwarty, ale żaden element nie
   jest dla niego niedostępny z definicji.
8. **E6 w moim kącie — tylko advisory.** Zlecenia na VM/webhooki to nie moja działka; z jego strony liczy się jedno: wiersz „w toku”
   pokazuje bieg zegarowy z „maszyna może zgasnąć o HH:MM” (E5). Nic więcej z E6 nie trafia na jego ekran.

### 6.3 Prawdziwe konflikty — po jednej decyzji każdy

| # | Konflikt | Strony | Moja propozycja (preset, odwracalna) |
|---|---|---|---|
| 1 | **Jedna skrzynka czy dwie.** Koordynator chce kolejki `routine_requests`; ja używam istniejącej `spec_inbox` (W2–W4); ekonomia routuje wiadomości klasą. | K6/K9 vs W2–W4 vs E9 | Jedna skrzynka: jego gest = datowana wypowiedź z polem `class` (statement / approval / correction / marker / routine_request / sample — lista otwarta); konsumenci (sekretarz, operator, koordynator) filtrują po klasie. Dwie skrzynki = dwa stany „czeka”, których on nie rozróżni. |
| 2 | **Widoczność paragonu dla niego.** E1: przy każdej pozycji; ja: przy geście i jako suma dnia. | E1 vs W1/W8 | Preset „przy geście + suma dnia”, per urządzenie; „przy każdej pozycji” jako preset do włączenia. Mierzyć: czy otwiera paragony (rzadko = preset dobry). |
| 3 | **Kto wykonuje `if_silent` po terminie** i czy operator (K6) to ten sam proces, co sekretarz. | K6 vs sekretarz (R88 v1, działa) vs tura koordynatora | Jedno zadanie zaplanowane (sekretarz = operator v1), rozszerzony prompt: wykonuje `if_silent` dla klas o niskim koszcie odwrotu, resztę eskaluje. Drugie zadanie dopiero, gdy pomiar pokaże, że jedno nie nadąża. Koszt: zero nowych biegów. |
| 4 | **Dom zapisu biegu a świeżość liczników.** K7: commit per bieg na gałęzi dokumentów; ekonomia (pyt. 7): `runs.jsonl` bez commitu, zbiorczo; moje W1/W9 potrzebują zdarzeń od razu. | K7 vs E vs W1 | Zdarzenie biegu idzie **od razu do bazy strony** (ta sama kolekcja, co skrzynka — 0 tokenów, liczniki żywe), do repo zbiorczo co N biegów (preset). Bieg niezacommitowany ginie z maszyną (R68) — stąd oba miejsca, nie jedno. |
| 5 | **Zasięg „stop” z telefonu.** Ja: pauza z kosztem; E5: miejsca (S1 chmura, S1-VM, S2…); K1: `interrupt` dostępne per maszyna, nie sprawdzone na VM. | W8 vs E5 vs K1 | „stop” adresuje MIEJSCA: lista tego, co drogie biegnie, per miejsce, z kosztem; preset: pauza wszędzie, odpowiedź jednym dotknięciem per wiersz. Warunek: mapa miejsc (E5) i katalog narzędzi (K1) istnieją wcześniej; do tego czasu „stop” = dzisiejsze zachowanie + zapis, kto i kiedy. |
| 6 | **Co jedzie do agenta jako filozofia** (pyt. 5 moje, K3). `CLAUDE.md` całe (≈ 39 tys. tokenów, 7,3 % wydatku — nasze-02) vs wybrane węzły spec wg tematu. | K3 vs E10 | Oba, mierzone: `CLAUDE.md` jako prefiks wspólny (trafienie cache) + `spec:` wg tematu pakietu w części własnej; miara = znaleziska check-before-use na pakiet przed/po. Nie decyzja z góry, pomiar na następnym pakiecie. |

### 6.4 Teza po dyskusji

Jego słowa są danymi od pierwszej sekundy i **docierają do agentów dosłownie** (`spec:` w pakiecie kontekstu, dziś 0/21), a jego
czytanie jest wyjątkiem, **mierzonym** (słowa/dobę do niego jako licznik, nigdy jako filtr). Trzy gesty zostają — wrzucić myśl,
dotknąć raz, zobaczyć różnicę — ale dostają fundament z dwóch pozostałych kątów: licznik ma ŹRÓDŁO (zapis biegu K7 i mapa miejsc
E5, nie proza QUEUE), gest ma CENĘ pokazaną przed nim tylko wtedy, gdy odwrót kosztuje (preflight E3, nie paragon przy każdej
linii), a jego powrót po przerwie jest **czwartą instancją `handover`** (obok teleportu, karty pakietu i rekordu sekretarza) —
nocna zmiana przekazuje mu zmianę. Jedna skrzynka dla wszystkich jego gestów z klasą jako polem; jedna tabela słów zasiana jego
słownikiem, renderowana dwa razy (telefon, warsztat); pora i uwaga jako wiersze routingu, które przestawia jego słowo albo
jawny preset — nigdy ciche wnioskowanie. Każda linia, którą ma przeczytać, nadal musi zmieniać jakiś licznik; teraz wiadomo, który
i skąd ten licznik bierze liczbę.
