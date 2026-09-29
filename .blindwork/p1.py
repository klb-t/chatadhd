# ---------------------------------------------------------------------------
# PART 1 - RELEVANT: explicit / architecture-unnamed / inflected / paraphrase
# ---------------------------------------------------------------------------

# --- explicit_name (easy anchors) -------------------------------------------
R("e01", "claude", "czujniki", "2024-09-14", "explicit_name", [K], [
    "Kotwica: dodalem czujnik CO2 w koltowni, ESP32 + MH-Z19, publikuje na topicu dom/kotlownia/co2 co 30 sekund.",
    "Ok. Sensowny prog alarmu to ok. 1500 ppm; zrob histereze zeby alarm nie migal.",
    "No wlasnie, w Kotwica dorzuce regule: powyzej 1500 przez 5 minut -> wentylator + powiadomienie na telefon.",
    "Dodaj tez wpis w historii zdarzen, zebys pozniej widzial kiedy i jak dlugo bylo zle."])
R("e02", "chatgpt", "pomysl na scene", "2025-02-03", "explicit_name", [L], [
    "Latarnik rozdzial 7: sztorm, Anzelm wychodzi na galerie latarni, w reku lampa naftowa bo agregat padl. Chce zeby to bylo krotkie, duszne zdania.",
    "Moge zaproponowac szkic: 'Wiatr zabral mu z ust ostatnie slowo modlitwy. Szklo drzalo. Na dole Irma krzyczala cos, czego juz nie slyszal.'",
    "Dobre, tylko Irma nie krzyczy, ona jest cicha. Zmien na to ze stuka w rure.",
    "Poprawione: 'Na dole Irma stukala w rure kaloryfera. Trzy razy, potem dwa. Ich stary kod.'"])
R("e03", "claude", "pytanie o VAT", "2025-06-20", "explicit_name", [F], [
    "Fakturka - jaka stawka dla ceramiki uzytkowej, kubki i miski? Halina sprzedaje tez karty podarunkowe.",
    "Ceramika to co do zasady 23%. Karty podarunkowe to osobny temat (bon jednego/wielu przeznaczen) - sprawdz z ksiegowa zanim to wpiszesz na sztywno.",
    "Ok, w Fakturka zostawie stawke na pozycji, nie na produkcie, wtedy pani Grazyna moze poprawic bez zmiany kodu."])
R("e04", "chatgpt", "Untitled", "2025-11-08", "explicit_name", [K], [
    "kotwica dashboard wykres temperatur z 7 dni, salon i sypialnia na jednym, kropka. chce zeby byly widoczne dziury gdy czujnik nie dzialal a nie interpolacja",
    "Rysuj przerwy: jesli brak odczytu > 2x interwal, przerwij linie. W Chart.js ustaw spanGaps=false.",
    "super. dodam jeszcze pasek ze stanem pieca, wlaczony na szaro"])
R("e05", "claude", "epub", "2026-01-12", "explicit_name", [L], [
    "Latarnik: eksport do epub. Kindle ma sie nie krzaczyc na polskich znakach a spis tresci ma byc z naglowkow rozdzialow.",
    "Uzyj pandoc lub wlasny generator: mimetype, META-INF/container.xml, content.opf, nav.xhtml. Sprawdz epubcheck przed wysylka na Kindle.",
    "Wybieram wlasny generator, pandoc robi mi dziwne style. Epubcheck wpinam na koniec eksportu."])
R("e06", "chatgpt", "kwestia rachunkow", "2026-03-30", "explicit_name", [F], [
    "W Fakturka klient zwrocil jeden kubek. Jak wystawic korekte zeby suma i VAT sie zgadzaly?",
    "Faktura korygujaca: numer, data, odwolanie do faktury pierwotnej, pozycje przed i po (albo roznica ujemna). VAT rowniez korygowany.",
    "Ok - dodaje typ dokumentu 'korekta' z polem original_id."])

# --- architecture_unnamed (project never named) -----------------------------
R("a01", "claude", "harmonogram", "2024-10-02", "architecture_unnamed", [K], [
    "Piec ma sie wlaczac tylko gdy w salonie spada ponizej progu, ale w tanich godzinach nocnych chce podgrzewac z wyprzedzeniem. Jak to opisac zeby nie zrobic spaghetti z if-ow?",
    "Rozdziel: (1) czujniki dostarczaja stan, (2) regula zwraca zadanie 'grzej/nie grzej' z uzasadnieniem, (3) wykonawca wysyla je do przekaznika. Harmonogram to dane, nie kod.",
    "Czyli tabela: godzina od-do, temp docelowa, tryb (dom / poza domem / wakacje). A reguly czyste funkcje, latwo testowac bez pieca.",
    "Tak. Dodaj minimalny czas pracy przekaznika, zeby nie klikal co minute."])
R("a02", "chatgpt", "co jak padnie", "2024-12-11", "architecture_unnamed", [K], [
    "co jak raspberry padnie w srodku zimy a ja jestem na wyjezdzie. dom nie moze zamarznac",
    "Warstwy: (1) sprzetowy watchdog restartuje malinke po zawieszeniu, (2) przekaznik ma tryb 'fail-safe' czyli bez sygnalu grzeje do minimum, (3) reczny przelacznik na scianie omija cala elektronike.",
    "Wlasnie - ten reczny przelacznik jest nienegocjowalny. Automatyka to dodatek, bez niej dom ma dzialac.",
    "Dobra zasada. Dodaj jeszcze SMS-a gdy watchdog zrestartuje urzadzenie wiecej niz dwa razy na dobe."],
  principles=["pr.manual_fallback"], decisions=["dec.manual_override_wall_switch"])
R("a03", "chatgpt", "urzadzenia", "2025-04-09", "architecture_unnamed", [K], [
    "Mam Shelly, lampy po zigbee2mqtt i wlasne plytki ESP32. Kazde gada inaczej. Jak to ogarnac w jednym modelu?",
    "Wspolny model urzadzenia z zestawem zdolnosci: temperature, switch, dimmer, power_meter. Adapter per rodzina tlumaczy payload na zdolnosci. Reszta systemu nie wie kto jest producentem.",
    "Czyli regula 'wylacz swiatlo' nie zna Shelly, zna tylko switch. Dobre, nowy sprzet = nowy adapter.",
    "Dokladnie. Dodaj do kazdej zdolnosci jednostke i zakres, wtedy walidacja jest za darmo."])
R("a04", "claude", "ciaglosc", "2025-01-22", "architecture_unnamed", [L], [
    "Irma w rozdziale 3 ma zielone oczy, w rozdziale 11 szare. Nie zauwazylem az czytelnik napisal. Chce narzedzie ktore pilnuje takich rzeczy.",
    "Trzymaj liste faktow o postaciach: (postac, cecha, wartosc, rozdzial, fragment). Skaner szuka konfliktow tej samej cechy z rozna wartoscia i pokazuje oba cytaty obok siebie.",
    "I do tego os czasu: wydarzenia z dniem i godzina, zeby wykryc ze ktos jest w dwoch miejscach naraz.",
    "Tak. Zacznij od recznego wpisywania faktow, wykrywanie automatyczne dopiero potem, bo bedzie mylic."])
R("a05", "chatgpt", "wersje szkicow", "2025-05-16", "architecture_unnamed", [L], [
    "chce moc cofnac sie do dowolnej wersji rozdzialu z zeszlego miesiaca. kazdy zapis ma zostac, nie nadpisujemy",
    "Zapisuj kazdy zapis jako zdarzenie: (rozdzial, czas, tresc lub diff). Aktualna wersja to ostatnie zdarzenie, wczesniejsze to historia. Co jakis czas snapshot zeby nie liczyc od zera.",
    "Czyli podroz w czasie za darmo. A porownanie dwoch wersji obok siebie to zwykly diff.",
    "Tak, i widok 'ile slow dodalem w tym tygodniu' tez wynika z historii."],
  principles=["pr.append_only"], decisions=["dec.chapter_versions_as_events"])
R("a06", "claude", "statystyki", "2025-09-03", "architecture_unnamed", [L], [
    "daily word counter, target 500 words, a streak chart for days in a row that I wrote. plus detection of crutch words I keep repeating (suddenly, somehow, just)",
    "Counter: difference in word count between the first and last save of the day, not the sum of edits. Repetition: sliding window of 1000 words, flag words above a frequency threshold, excluding stop words.",
    "I will make it an optional add-on that can be switched off, because sometimes while writing I do not want to see the counter."])
R("a07", "claude", "numeracja", "2025-03-05", "architecture_unnamed", [F], [
    "Numer ciagly w miesiacu, nie wolno robic dziur, format 07/2025/003. Co gdy dwa okna wystawiaja naraz?",
    "Numer nadawaj dopiero w chwili zatwierdzenia, w transakcji: odczytaj ostatni numer i zapisz nowy jednym krokiem z blokada. Projekt roboczy nie ma numeru.",
    "Czyli szkic bez numeru, numer przy 'wystaw'. Jak cos sie wywali po nadaniu numeru, transakcja sie cofa i numer wraca.",
    "Tak. Dodaj test dwoch rownoleglych zatwierdzen."],
  decisions=["dec.gapless_numbering_at_commit"])
R("a08", "chatgpt", "pdf", "2025-07-18", "architecture_unnamed", [F], [
    "szablon PDF: pola sprzedawcy, tabela pozycji, podsumowanie VAT per stawka. zaokraglenia groszy per pozycja czy per faktura? bo mi sie suma nie zgadza o grosz",
    "Wybierz jedna metode i trzymaj ja wszedzie. Najprostsza: licz netto pozycji, VAT per stawka od sumy netto stawki, zaokraglaj raz na koncu. Zapisz metode w dokumencie.",
    "ok, per stawka od sumy. i test na 1000 losowych faktur czy suma brutto = suma pozycji."])
R("a09", "chatgpt", "korekty", "2026-02-10", "architecture_unnamed", [F], [
    "wystawiona faktura nie moze byc edytowana. jak w takim razie poprawic literowke w adresie klienta?",
    "Nie edytujesz wystawionego dokumentu. Wystawiasz dokument korygujacy wskazujacy oryginal, oryginal zostaje nietkniety w historii.",
    "Czyli tak samo jak w moich innych rzeczach: dopisujemy, nie zmieniamy przeszlosci. Zgoda."],
  principles=["pr.append_only"], decisions=["dec.issued_documents_immutable"])

# --- inflected_alias (only inflected alias forms) ----------------------------
R("i01", "claude", "wieczorne pytania", "2025-01-30", "inflected_alias", [K], [
    "Mam problem z Kotwica - po restarcie brokera czujniki z piwnicy nie wracaja, dopiero po recznym restarcie ESP.",
    "Zwykle to brak automatycznego reconnectu po stronie klienta MQTT. Dodaj retry z rosnacym opoznieniem i 'last will' na kazdym czujniku.",
    "Ok, w Kotwicy dorzuce tez heartbeat co minute, a po Kotwicy spodziewam sie ze sama powie mi co padlo."],
  note="only inflected forms of the project name (Kotwica/Kotwicy)")
R("i02", "chatgpt", "rozmowa", "2025-08-12", "inflected_alias", [L], [
    "Wrzucilem do Latarnika nowy rozdzial. Bohaterowi Latarnika brakuje motywacji, czytelnik nie wie czemu zostaje na wyspie.",
    "Daj mu konkretny koszt wyjazdu: dlug, obietnica, kogos kogo nie moze zostawic. Motywacja to zwykle cos do stracenia.",
    "Kaze mu zostac przez obietnice zlozona zonie. Zapisze to w Latarniku jako watek przewodni."])
R("i03", "claude", "prosba", "2025-10-01", "inflected_alias", [F], [
    "Z Fakturka mam taki klopot: Fakturki po polnocy dostaja date jutrzejsza. Halina sie denerwuje.",
    "To strefa czasowa. Trzymaj czas w UTC, a date dokumentu wyliczaj w Europe/Warsaw w momencie wystawienia.",
    "No tak, o Fakturce myslalem jak o programie a nie jak o dokumentach z data prawna. Poprawie."])
R("i04", "chatgpt", "aktualizacja", "2026-04-19", "inflected_alias", [K], [
    "jak zaktualizowac oprogramowanie sterownika domu w piwnicy nie przerywajac grzania? ten hub jest krytyczny zima",
    "Aktualizuj poza sezonem albo dwuetapowo: zainstaluj nowa wersje obok, przelacz symlink, uruchom, a w razie bledu wroc. Grzanie niech ma stan bezpieczny w trakcie.",
    "dobra, robie wersje obok i przelaczam symlinkiem. w hubie piwnicznym zostawiam stara wersje przez tydzien"])
R("i05", "claude", "drobiazg", "2026-06-02", "inflected_alias", [L], [
    "W mojej ksiazce o latarniku brakuje mi zakonczenia. Mam trzy warianty i zadnego nie czuje.",
    "Opisz je krotko a powiem ktory zamyka watki: Anzelm odchodzi, Anzelm umiera, Anzelm zostaje z nowym powodem.",
    "Powiesc o latarniku powinna konczyc sie tym ze on zostaje, ale swiat wokol sie zmienil. Wariant trzeci."])
R("i06", "chatgpt", "hmm", "2026-07-07", "inflected_alias", [F], [
    "w programiku do faktur dla Halinki brakuje rabatu procentowego na cale zamowienie, kupujacy na jarmarku zawsze negocjuja",
    "Rabat mozesz trzymac jako pozycje ujemna albo pole na dokumencie. Pozycja ujemna jest prostsza dla ksiegowosci, VAT liczy sie sam.",
    "Biore pozycje ujemna, w programiku dla Halinki zostaje jedna tabela pozycji."])

# --- paraphrase_only (no names, no module terms) ----------------------------
R("p01", "claude", "cieplo w domu", "2024-11-22", "paraphrase_only", [K], [
    "Chce zeby dom sam decydowal kiedy grzac, patrzac na cene pradu i na to czy ktos jest w domu. Jak nikogo nie ma, obnizyc o 3 stopnie.",
    "Obecnosc mozna wykrywac po telefonach w domowej sieci wifi albo po czujnikach ruchu. Do ceny: godziny tanie i drogie w taryfie.",
    "telefony w wifi, zgoda. cena z tabeli godzin. z tego wychodzi jedna liczba: docelowa temperatura teraz."])
R("p02", "chatgpt", "wilgotnosc", "2025-03-27", "paraphrase_only", [K], [
    "male plytki z radiem co mierza wilgotnosc w lazience i wysylaja do pudelka w piwnicy. jak wilgotnosc skoczy wlacz wentylator",
    "Plytki na baterii: wysylaj tylko przy zmianie o wiecej niz 3% albo co 10 minut, potem gleboki sen. Wentylator z histereza i minimalnym czasem pracy.",
    "ok. i zeby po 20 minutach sam wylaczal nawet jak wilgotnosc dalej wysoka, bo czasem czujnik klamie"])
R("p03", "claude", "kto ma jakie oczy", "2025-12-05", "paraphrase_only", [L], [
    "chcialbym program ktory czyta moja ksiazke i mowi mi, ze wdowa na wyspie ma raz 40 a raz 45 lat, albo ze ktos otwiera drzwi ktore w poprzednim zdaniu byly zamkniete",
    "To jest sprawdzanie spojnosci fikcji. Wyciagnij fakty o postaciach i liczbach, porownaj miedzy rozdzialami, pokaz konflikty do decyzji czlowieka.",
    "Tak, ale nie ma poprawiac za mnie. Tylko wskazac dwa miejsca i cytaty."])
R("p04", "chatgpt", "do czytnika", "2026-02-27", "paraphrase_only", [L], [
    "chce z moich rozdzialow w zwyklych plikach tekstowych zrobic jeden plik do czytnika ksiazek elektronicznych, zeby sprawdzic jak sie czyta na papierze cyfrowym",
    "Sklej rozdzialy w kolejnosci, dodaj strone tytulowa i spis, spakuj do formatu e-bookow. Sprawdz walidatorem.",
    "ok, plik dla czytnika co wieczor po ostatnim zapisie, zebym mogl czytac w lozku swoje wlasne wypociny"])
R("p05", "claude", "papiery zony", "2025-04-30", "paraphrase_only", [F], [
    "my wife sells mugs and bowls at craft fairs and online. she needs a one-page PDF for a buyer who gives a tax id, and a monthly file for the accountant",
    "Two outputs from one source: a customer document (PDF) and a periodic summary (CSV/XLSX). The source of truth is the sales documents themselves, not copies of them.",
    "yes, one source. the accountant does not want PDFs, she wants a table."])
R("p06", "chatgpt", "raport dla ksiegowej", "2026-01-08", "paraphrase_only", [F], [
    "zestawienie sprzedazy za miesiac w rozbiciu na stawki, plik ktory Excel otworzy z polskimi literami i bez rozjechanych kolumn",
    "CSV: separator srednik, kodowanie UTF-8 z BOM, przecinek dziesietny. Naglowki po polsku.",
    "ok srednik + BOM. i kolumna z numerem dokumentu jako tekst zeby Excel nie zamienil na date"],
  principles=["pr.own_your_data"], decisions=["dec.csv_bom_semicolon"])

