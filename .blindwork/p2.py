# ---------------------------------------------------------------------------
# PART 2 - RELEVANT: shared foundations / philosophy / multi-topic / duplicates
# ---------------------------------------------------------------------------

# --- shared_foundation (multi-label truth) -----------------------------------
R("s01", "claude", "baza", "2024-08-20", "shared_foundation", [K, L, F], [
    "Zanim zaczne cokolwiek z tych trzech rzeczy (dom, powiesc, faktury Halinki) chce jedna wspolna warstwe zapisu, zebym nie pisal trzy razy tego samego. Co proponujesz?",
    "Jeden plik SQLite na projekt, wspolna biblioteka z tabela events(id, ts, type, payload_json). Stan biezacy liczysz ze zdarzen, a dla szybkosci trzymasz tabele widokow, ktore mozna odbudowac.",
    "Czyli Szuflada, tak ja to nazwalem, zna tylko zdarzenia. Domena (temperatura, rozdzial, faktura) siedzi w payloadzie. Zgoda.",
    "Tak, i wersjonuj typy zdarzen, zeby stare payloady nadal dalo sie odczytac."],
  principles=["pr.append_only", "pr.boring_tech"], decisions=["dec.event_store_sqlite"], foundations=[ST])
R("s02", "chatgpt", "migracje", "2025-02-25", "shared_foundation", [K, L, F], [
    "how do I change the database schema so that a file from last year still opens? I use the same mechanism in several of my programs",
    "PRAGMA user_version plus a numbered list of migrations, forward only. A migration adds columns or tables, never drops them. Copy the file before migrating.",
    "copy before every migration, good. and a test: open a database from every old version and check that it still works",
    "Keep small fixture files in the repo, one per schema version."],
  principles=["pr.boring_tech"], decisions=["dec.forward_only_migrations"], foundations=[ST])
R("s03", "claude", "ladowanie modulow", "2024-11-05", "shared_foundation", [K, L, F], [
    "Chce zeby rdzen nie wiedzial jakie ma dodatki. Sterownik nowego urzadzenia, sprawdzacz spojnosci tekstu, eksporter do CSV - to wszystko ma sie doczepiac z folderu.",
    "Folder plugins/, w kazdym podfolderze manifest.toml: nazwa, wersja, wymagane zdolnosci, punkty zaczepienia. Rdzen skanuje folder przy starcie i laduje tylko to, co ma poprawny manifest.",
    "I lista zdolnosci: dodatek deklaruje czego chce (czytac zdarzenia, pisac zdarzenia, siec). Bez deklaracji nie dostaje.",
    "Dobrze. Dodaj wersje API rdzenia w manifescie, zeby odrzucac niezgodne."],
  principles=["pr.replaceable_pieces"], decisions=["dec.plugin_manifest_capabilities"], foundations=[PL])
R("s04", "chatgpt", "co jak modul padnie", "2025-06-01", "shared_foundation", [K, L], [
    "jak jeden dodatek rzuci wyjatek to caly program pada. nie chce tego",
    "Otocz kazde wywolanie dodatku try/except z limitem czasu, po N bledach z rzedu wylacz go i zapisz to w logu. Rdzen dziala dalej.",
    "tak, np. sprawdzacz rozdzialow moze sie wywalic a ja dalej pisze. albo modul czujnika - grzanie ma isc dalej",
    "Dokladnie, izolacja bledow. Pokaz uzytkownikowi mala plakietke 'dodatek X wylaczony'."],
  principles=["pr.replaceable_pieces", "pr.manual_fallback"], decisions=["dec.plugin_fault_isolation"], foundations=[PL])
R("s05", "claude", "komunikacja", "2024-12-01", "shared_foundation", [K, L], [
    "the modules have to talk to each other without knowing each other. the temperature sensor should not know who listens, and saving a chapter should not know either",
    "A simple in-process bus: publish(topic, payload), subscribe(topic, handler). Hierarchical topics like 'temperature.livingroom' or 'chapter.saved'. Handlers synchronous or queued - you decide.",
    "queued, because a slow handler must never block the write. order within a topic preserved",
    "One queue per subscriber gives you ordering per subscriber."],
  decisions=["dec.inproc_bus_queue_per_subscriber"], foundations=[BUS])
R("s06", "chatgpt", "duplikaty zdarzen", "2025-08-25", "shared_foundation", [K, L, F], [
    "co jak subskrybent dostanie to samo zdarzenie dwa razy po restarcie? np. wystawiona faktura poszla do eksportu dwa razy, odczyt temperatury policzony podwojnie, rozdzial zaindeksowany dwa razy",
    "Zaprojektuj handlery idempotentnie: kazde zdarzenie ma id, subskrybent zapamietuje ostatni przetworzony id (offset) i pomija starsze.",
    "offset zapisany w tej samej bazie i w tej samej transakcji co efekt. po awarii odtwarzam od offsetu z dziennika zdarzen",
    "Tak, at-least-once plus idempotencja daje efekt jak exactly-once."],
  principles=["pr.append_only"], decisions=["dec.idempotent_handlers"], foundations=[BUS, ST])
R("s07", "claude", "kolejnosc", "2025-11-20", "shared_foundation", [K, F], [
    "Mam wzorzec: najpierw zapisz zdarzenie do bazy, potem opublikuj na szynie. Co jak proces padnie pomiedzy?",
    "Outbox: zapisujesz zdarzenie i znacznik 'do wyslania' w jednej transakcji, osobny watek publikuje i oznacza. Po restarcie dokonczy niewyslane.",
    "dla grzania: komenda do przekaznika nie moze zginac. dla faktur: wysylka do ksiegowej tak samo. ok, outbox"],
  decisions=["dec.outbox_pattern"], foundations=[ST, BUS])
R("s08", "chatgpt", "porzadek w repo", "2026-05-14", "shared_foundation", [K, L, F], [
    "mam trzy projekty i wspolny katalog core/ z zapisem, dodatkami i szyna. kazda zmiana w core moze cos zepsuc w ktoryms z nich. jak to zorganizowac?",
    "Core jako osobny pakiet z wlasnymi testami i numerem wersji (semver). Kazdy projekt przypina wersje. Zmiany lamiace = nowa wersja glowna plus notka migracyjna.",
    "przypinam wersje, a testy kontraktowe core uruchamiam w CI kazdego z trzech. nie chce dnia, w ktorym poprawka do zapisu psuje mi piec, powiesc i faktury naraz",
    "Dobry plan."],
  principles=["pr.replaceable_pieces"], decisions=["dec.core_versioned_package"], foundations=[ST, PL, BUS])

# --- philosophy (no project named, principle statements) ---------------------
R("f01", "claude", "jak ja buduje", "2024-06-10", "philosophy", [], [
    "I keep thinking about how I build things. I prefer boring technology that has worked for fifteen years: SQLite, text files, cron. A fashionable tool is a cost you pay every time something changes.",
    "That is a coherent stance: predictability over novelty. Are there exceptions?",
    "Only when the boring option truly does not work. And every piece should be replaceable in one evening, so small blocks with clear borders.",
    "Small replaceable blocks and proven tools complement each other well."],
  principles=["pr.boring_tech", "pr.replaceable_pieces"])
R("f02", "chatgpt", "refleksja niedzielna", "2024-10-20", "philosophy", [], [
    "nigdy niczego nie kasuje w danych. dopisuje. to co widze to tylko widok na historie, a nie prawda sama w sobie. nauczylem sie tego jak stracilem tydzien notatek przez nadpisanie",
    "Niezmienny dziennik plus widoki pochodne. Zyskujesz audyt i mozliwosc naprawienia bledow w logice wstecz.",
    "wlasnie, jak logika byla zla to przeliczam widok i po sprawie. dane zrodlowe nietkniete"],
  principles=["pr.append_only"])
R("f03", "claude", "3 w nocy", "2025-04-27", "philosophy", [], [
    "Moj test na kazdy system: co sie stanie o trzeciej w nocy gdy internet lezy, a ja spie i nikt nie ma laptopa? Jesli odpowiedz brzmi 'zepsuje sie', to projekt jest zly.",
    "Kryterium operacyjne: lagodna degradacja i tryb reczny. Chcesz to spisac jako liste kontrolna?",
    "tak. kazda automatyka musi miec reczny tryb, ktory dziala bez jej mozgu. przelacznik, plik, komenda, cokolwiek.",
    "Sformulujmy: 'automatyka to nakladka, nie podpora'."],
  principles=["pr.manual_fallback"])
R("f04", "chatgpt", "notatka do siebie", "2025-09-14", "philosophy", [], [
    "notatka do siebie: zapisuje dlaczego, nie co. kod pokaze co, za pol roku nie bede pamietal czemu wybralem A a nie B. plus: pierwsza wersja ma byc brzydka i skonczona w dwa tygodnie",
    "Dziennik decyzji (kontekst, opcje, wybor, powod) i limit czasu na pierwsza wersje. Dobrze.",
    "dwa tygodnie i koniec, potem dopiero polerka. jak nie skoncze to obcinam zakres a nie przedluzam"],
  principles=["pr.write_the_why", "pr.ugly_first"])
R("f05", "claude", "wieczorne mysli", "2026-01-25", "philosophy", [], [
    "Chce zeby moje dane dalo sie otworzyc zwyklym notatnikiem za 20 lat. Zadnego formatu, ktory zalezy od firmy, ktora moze zniknac. Eksport do tekstu ma byc jednym klikiem, a nie funkcja premium.",
    "Otwarte formaty i prosty eksport. Zgodne z Twoja niechecia do uzaleznien.",
    "tak, zadnego vendor locka. jak cos przestaje mi sluzyc to wymieniam ten klocek i dane leca dalej"],
  principles=["pr.own_your_data", "pr.replaceable_pieces"])
R("f06", "chatgpt", "porada", "2026-05-30", "philosophy", [], [
    "perfekcjonizm zabija projekty. u mnie dziala tak: wersja brzydka w 14 dni, potem polerka, potem wyrzucam polowe funkcji, ktorych i tak nikt nie uzywa. mam kilka niedokonczonych bo probowalem od razu ladnie",
    "Iteracja: dziala, potem ladnie, potem mniej. Sprawdzone.",
    "no i zawsze najprostsza rzecz, ktora moze zadzialac. nie kubernetes na skrypt co robi trzy rzeczy"],
  principles=["pr.ugly_first", "pr.boring_tech"])

# --- multi_topic_late (relevant part late / in the middle of a drifting thread) -
R("m01", "chatgpt", "rozne pytania", "2024-09-30", "multi_topic_late", [K], [
    "jaki gulasz na niedziele, wolowina czy wieprzowina?",
    "Wolowina - dluzej dochodzi, ale wychodzi bardziej aksamitna. Podsmaz porzadnie, potem duszenie 2,5 godziny.",
    "a czy moge odliczyc laptop od podatku? prowadze mala dzialalnosc na boku",
    "Jesli laptop sluzy dzialalnosci, tak, ale proporcjonalnie do uzycia prywatnego. Skonsultuj z ksiegowa.",
    "dobra, dosc, teraz co innego. czujniki DS18B20 pokazuja 0.5 stopnia roznicy miedzy soba w tej samej wodzie. kalibrowac offsetem per czujnik?",
    "Tak: zmierz w lodzie z woda (0 C) i we wrzatku, zapisz offset per czujnik w konfiguracji i stosuj przy odczycie, nie zmieniaj surowych danych.",
    "surowe zostaja w historii, offset stosuje tylko przy wyswietlaniu i w regulach. ok"],
  principles=["pr.append_only"])
R("m02", "claude", "sobota", "2025-02-14", "multi_topic_late", [L], [
    "plan na sobote: rower rano, potem zakupy. a w Bieszczady jedziemy w lipcu czy sierpniu?",
    "W lipcu bedzie wiecej ludzi i drozej, w sierpniu pod koniec cieplo i spokojniej. Zalezy czy chcecie kapac sie w rzece.",
    "lipiec. co do noclegu - agroturystyka czy domek?",
    "Agroturystyka: sniadania, kontakt z ludzmi. Domek: niezaleznosc. Przy dzieciach domek.",
    "aha i jeszcze jedno: w scenie ze sztormem mam dzien trzeci, sztorm zaczyna sie zanim przyplynela lodz z zaopatrzeniem, a w rozdziale 5 lodz przyplywa w dzien drugi wieczorem i sztorm jest juz wtedy. da sie to pogodzic?",
    "Najprosciej przesunac lodz na wczesniej albo dodac zdanie, ze pierwsza fala sztormu byla slaba i lodz przeczekala w zatoce. Zapisz sobie os czasu dni 1-4.",
    "wole druga opcje, dopisze zdanie o zatoce"])
R("m03", "chatgpt", "lista rzeczy", "2025-06-25", "multi_topic_late", [F], [
    "lista: kupic pralke, oddac buty do szewca, zadzwonic do mamy. jaka pralka, 8 czy 9 kg?",
    "Dla 3-4 osob 8 kg wystarczy. Zwroc uwage na klase energetyczna i glosnosc schodzenia.",
    "ok 8 kg. dobra, teraz Halina: chce zamowienia na komplet 12 kubkow z indywidualnym nadrukiem i brac zaliczke, wiec faktury zaliczkowe. jak to sie ma do koncowej faktury?",
    "Faktura zaliczkowa przy wplacie, a koncowa zawiera pelna wartosc i odliczenie zaliczek. VAT rozliczasz przy zaliczce.",
    "czyli koncowa ma pozycje +pelna wartosc i -zaliczka. musze to dopisac w programie, dzis nie umie odliczac"],
  decisions=["dec.advance_invoices_as_negative_lines"])
R("m04", "claude", "dluga rozmowa", "2025-10-22", "multi_topic_late", [K, L], [
    "Jak w Excelu zsumowac wydatki z kolumny B tylko dla kategorii 'paliwo'? Prowadze budzet domowy.",
    "SUMIF: =SUMIF(A:A;\"paliwo\";B:B). Dla wielu warunkow SUMIFS.",
    "dziala. teraz cos innego: chce zmieniac format zapisywanych zdarzen bez psucia starych. mam pole temperatura w stopniach i chce dodac jednostke; w drugim programie pole tekst rozdzialu i chce dodac liczbe slow",
    "Upcasting: przy odczycie stare zdarzenie v1 przeksztalcasz w pamieci na v2, w bazie zostaje bez zmian. Funkcja per typ i wersja.",
    "czyli nigdy nie migruje starych rekordow, tylko czytam przez upcaster. dobre",
    "wracajac do arkusza: jak zrobic wykres kolowy z udzialem kategorii?",
    "Zaznacz kolumny kategorii i sum, Wstawianie -> Wykres kolowy. Przy wielu kategoriach lepszy slupkowy posortowany."],
  principles=["pr.append_only"], decisions=["dec.upcast_on_read"], foundations=[ST])
R("m05", "chatgpt", "misc", "2026-03-03", "multi_topic_late", [K, L, F], [
    "boli mnie plecy po siedzeniu przy biurku, jakies cwiczenia?",
    "Koci grzbiet, mostek, rozciaganie zginaczy biodra; co godzine krotka przerwa. Jesli bol promieniuje do nogi, lekarz.",
    "dzieki. inna sprawa: rozbijam wspolny rdzen na moduly: zapis, dodatki, szyna. kazdy ma miec wlasne testy i zero zaleznosci od tego co robia programy na nim, ani od pieca, ani od powiesci, ani od faktur",
    "Zaleznosci w jedna strone: programy zaleza od rdzenia, rdzen od nikogo z domeny. Kontrole w CI: import z domeny do rdzenia = blad.",
    "dobre, doloze taki test. a kiedy trzeba odnowic OC? konczy sie w kwietniu",
    "Odnowienie zwykle na 30 dni przed koncem; porownaj oferty, bo ceny sie rozjezdzaja."],
  principles=["pr.replaceable_pieces"], decisions=["dec.core_has_no_domain_deps"], foundations=[ST, PL, BUS])
R("m06", "claude", "wszystko naraz", "2026-06-18", "multi_topic_late", [K], [
    "Ktory laptop na prace zdalna do 4000 zl: ThinkPad czy MacBook Air?",
    "ThinkPad - klawiatura, serwisowalnosc, Linux. MacBook Air - bateria i cisza. Zalezy od systemu.",
    "kot znowu zwymiotowal, trzeci raz w tygodniu, czy to powod do weterynarza?",
    "Trzy razy w tygodniu to powod do wizyty, szczegolnie jesli spada apetyt lub jest apatyczny.",
    "umowie sie. a, i jeszcze: zeby dom nie zamarzl gdy nie ma pradu - UPS na malinke wystarczy na 40 minut. co potem?",
    "Potem przekaznik w stanie bezpiecznym i piec z wlasnym trybem awaryjnym; UPS ma tylko zapewnic czyste wylaczenie i wyslanie alarmu.",
    "czyli UPS nie do podtrzymania grzania tylko do porzadnego zamkniecia i SMS-a. tak zrobie"],
  principles=["pr.manual_fallback"])
R("m07", "chatgpt", "rozne 2", "2026-08-04", "multi_topic_late", [L], [
    "hola, kiedy uzywa sie subjuntivo a kiedy zwyklego czasu? ucze sie hiszpanskiego na wakacje",
    "Subjuntivo po wyrazeniach zyczenia, watpliwosci, emocji: quiero que vengas, dudo que sea. Zwykly czas gdy stwierdzasz fakt.",
    "ok czyli 'espero que' tez. dobra, inna rzecz: rozdzial 12 mam napisany brzydko, ale zostawiam, poprawie w drugiej turze. najpierw doprowadzic do konca calosc, nawet jak szkielet ledwo trzyma",
    "Pierwszy szkic sluzy do tego, by wiedziec, co historia w ogole mowi. Polerka ma sens dopiero po zamknieciu calosci."],
  principles=["pr.ugly_first"])

R("m08", "claude", "notatki z tygodnia", "2025-12-14", "multi_topic_late", [K, L, F], [
    "notatki z tygodnia: 1) zadzwonic do dentysty 2) kupic karme dla kota 3) odswiezyc CV. od czego zaczac zeby nie odkladac?",
    "Zacznij od telefonu do dentysty - najmniejszy koszt startu, najwiekszy ciezar psychiczny. Potem karma, CV na koniec.",
    "dentysta ok. a kolacja dzis? mam cukinie, jajka i ser feta",
    "Placki z cukinii z feta albo szakszuka z cukinia. 25 minut.",
    "placki. dobra, a teraz cos z pracy nad rdzeniem: dziennik zdarzen urosl do 2 GB po roku i start trwa dluzej niz minute bo odtwarzam wszystko od poczatku",
    "Zapisuj snapshoty stanu co N zdarzen i przy starcie ladowac ostatni snapshot plus zdarzenia po nim. Dziennik starszy niz snapshot mozna archiwizowac, ale nie kasowac.",
    "archiwizowac do osobnego pliku, nie kasowac - zgadza sie z ta zasada ze historia zostaje. snapshot to tylko cache, mozna go wyrzucic i odbudowac",
    "Dokladnie: snapshot jest pochodna, nie zrodlem prawdy. Dodaj hash ostatniego zdarzenia w snapshocie, zeby wykryc niezgodnosc.",
    "i to samo dla wszystkich trzech programow? wspolna biblioteka robi snapshoty, kazdy program tylko mowi jak zlozyc stan",
    "Tak - rdzen dostarcza mechanizm, domena dostarcza funkcje 'apply(stan, zdarzenie)'.",
    "super. wracajac do prywatnych spraw, jaka pogoda na swieta w gorach?",
    "Trudno prognozowac z takim wyprzedzeniem; sprawdz prognoze na 7 dni przed wyjazdem, spakuj warstwy i lancuchy na opony.",
    "dzieki. jeszcze pomysl na prezent dla szwagra, lubi wedkowanie",
    "Zestaw przynet, skladany fotelik wedkarski albo voucher na wyprawe z przewodnikiem."],
  principles=["pr.append_only"], decisions=["dec.snapshots_are_derived"], foundations=[ST])

# --- near_duplicate (same conversation re-exported / edited in the other export) --
DUP("d01", "e01", "chatgpt", "CO2 w kotlowni (wersja 2)", "2024-09-15",
    edits=[("co 30 sekund", "co 60 sekund")],
    extra=["A czy ten MH-Z19 trzeba kalibrowac?",
           "Tak, raz na jakis czas wystaw go na swieze powietrze (ok. 400 ppm) albo wlacz autokalibracje."])
DUP("d02", "a02", "claude", "awaria zima", "2024-12-12",
    edits=[("SMS-a", "SMS"), ("sprzetowy watchdog", "sprzetowy timer watchdog")], lower_first=True)
DUP("d03", "s01", "chatgpt", "wspolna baza - podejscie", "2024-08-21",
    edits=[("Szuflada", "szuflada")],
    extra=["I jedna rzecz: identyfikatory zdarzen maja byc rosnace, zeby offsety subskrybentow byly proste.",
           "Tak - ULID albo licznik z bazy; unikaj losowych UUID jako klucza porzadkujacego."])
DUP("d04", "a07", "chatgpt", "kolejne numery", "2025-03-06", edits=[("07/2025/003", "03/2025/012")])
DUP("d05", "f03", "chatgpt", "test trzeciej w nocy", "2025-04-29", edits=[("gdy internet lezy", "gdy internet nie dziala")])
DUP("d06", "p03", "chatgpt", "spojnosc fikcji", "2025-12-06", edits=[("40 a raz 45", "35 a raz 41")],
    extra=["Rozumiem - narzedzie ma tylko wskazywac, decyzja zostaje po Twojej stronie.",
           "A czy moze tez proponowac poprawki?",
           "Lepiej niech tylko wskazuje. Poprawki to decyzje autora."])

