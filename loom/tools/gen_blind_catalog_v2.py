#!/usr/bin/env python3
"""Deterministic generator for the FICTIONAL blind catalog validation corpus
`loom/tests/fixtures/eval/blind_catalog_v2/`.

Persona "Witold Sowa" is wholly invented (hobbyist, Gdynia). No real people,
data or projects. Output: chatgpt_export.zip, claude_export.zip,
ground_truth.json. No wall clock, no randomness.

    python3 loom/tools/gen_blind_catalog_v2.py

The corpus content lives in this file (relevant / noise / traps sections),
followed by the export writers and the ground-truth builder.
"""
from __future__ import annotations

import datetime
import json
import zipfile
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "tests/fixtures/eval/blind_catalog_v2"

CATS = ["explicit_name", "architecture_unnamed", "inflected_alias", "paraphrase_only",
        "shared_foundation", "philosophy", "multi_topic_late", "near_duplicate"]
K, L, F = "proj.kotwica", "proj.latarnik", "proj.fakturka"
ST, PL, BUS = "found.storage", "found.plugins", "found.bus"

CONVS: list[dict] = []


def _add(kind, cid, prov, title, date, msgs, **kw):
    assert prov in ("chatgpt", "claude")
    assert all(c["id"] != cid for c in CONVS), cid
    assert len(msgs) >= 2, cid
    CONVS.append(dict(kind=kind, id=cid, provider=prov, title=title, date=date, msgs=msgs, **kw))


def R(cid, prov, title, date, cat, projects, msgs, principles=(), decisions=(), foundations=(), note=""):
    """Relevant conversation. msgs alternate user, assistant, user, ..."""
    assert cat in CATS
    _add("relevant", cid, prov, title, date, msgs, category=cat, projects=list(projects),
         principles=list(principles), decisions=list(decisions), foundations=list(foundations), note=note)


def N(cid, prov, title, date, topic, msgs):
    _add("noise", cid, prov, title, date, msgs, topic=topic)


def T(cid, prov, title, date, term, sense, collides_with, msgs):
    _add("trap", cid, prov, title, date, msgs, term=term, real_sense=sense, collides_with=collides_with)


def DUP(cid, base, prov, title, date, edits=(), extra=None, lower_first=False):
    b = next(c for c in CONVS if c["id"] == base)
    assert b["provider"] != prov
    msgs = list(b["msgs"])
    for old, new in edits:
        assert any(old in m for m in msgs), (cid, old)
        msgs = [m.replace(old, new) for m in msgs]
    if lower_first:
        msgs[0] = msgs[0][:1].lower() + msgs[0][1:]
    if extra:
        msgs.extend(extra)
    _add("relevant", cid, prov, title, date, msgs, category="near_duplicate",
         projects=list(b["projects"]), principles=list(b["principles"]),
         decisions=list(b["decisions"]), foundations=list(b["foundations"]),
         duplicate_of=base, note="re-export / edited version of " + base)


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

# ---------------------------------------------------------------------------
# PART 3 - GENERIC NOISE (life / admin / cooking / health / travel ...)
# ---------------------------------------------------------------------------
N("n01", "chatgpt", "program na weekend", "2024-07-06", "cooking", [
    "jak zrobic zurek na zakwasie zeby nie byl za kwasny? mam zakwas od tygodnia",
    "Zakwas nie moze byc za mlody - 4-5 dni to minimum. Dodaj go na koncu, nie gotuj dlugo. Marchewka, pietruszka, seler, suszone grzyby dla glebi.",
    "a biala kielbasa dodawac surowa czy podsmazona?",
    "Sparz lub podsmaz krotko, zeby nie oddala nadmiaru tluszczu. Jajko na twardo na koniec."])
N("n02", "claude", "architektura", "2024-08-03", "travel_leisure", [
    "planuje spacer po Gdyni szlakiem modernizmu, co obejrzec? mamy pol dnia",
    "Skwer Kosciuszki, Dom Marynarza i okolice, Sklep Tadeusza Wenda, Bulwar. Mozesz zakonczyc na Kamiennej Gorze widokiem na port.",
    "super, dzieci wytrzymaja? czy lepiej podjechac trolejbusem czesc trasy",
    "Trolejbus to dobry pomysl, zwlaszcza pod gore. Zaplanuj przerwe na lody."])
N("n03", "chatgpt", "sernik", "2024-09-21", "cooking", [
    "sernik na zimno czy pieczony na urodziny tesciowej? goscie 12 osob",
    "Pieczony w tortownicy 26 cm, nizsza temperatura, studzenie w wylaczonym piekarniku - nie popeka. Sery z wiaderka gotowe do sernika.",
    "ok pieczony. ile serow?",
    "Okolo 1 kg na tortownice 26 cm, na 12 osob upiecz dwa sernik albo jeden wysoki i dodatkowe ciasto."])
N("n04", "chatgpt", "kregoslup", "2024-10-14", "health", [
    "boli mnie dolna czesc plecow rano po wstaniu, potem przechodzi. to normalne?",
    "Poranna sztywnosc, ktora mija po kilkunastu minutach, to czesto kwestia materaca i pozycji do spania. Jesli trwa dluzej niz pol godziny lub budzi w nocy, warto do lekarza.",
    "materac mam 12 lat, moze to on. jakie cwiczenia na rano?",
    "Kot-krowa, kolana do klatki lezac na plecach, delikatne skrety. Spokojnie, bez bolu."])
N("n05", "claude", "system", "2024-11-09", "health", [
    "od kilku tygodni budze sie o 4 nad ranem i nie moge zasnac. kawa tylko rano. co robic?",
    "Stala pora wstawania, swiatlo dzienne rano, brak ekranow godzine przed snem, kolacja lekka. Jesli utrzymuje sie ponad miesiac - porozmawiaj z lekarzem rodzinnym.",
    "probowalem melatoniny, nic nie dala",
    "Melatonina pomaga glownie przy przesunieciu fazy snu, nie przy wczesnym budzeniu. Sprawdz stres i alkohol wieczorem."])
N("n06", "chatgpt", "dane", "2024-12-02", "health", [
    "wyniki krwi: cholesterol calkowity 228, LDL 142, HDL 51. czy to duzo?",
    "Wynik lekko podwyzszony wg wiekszosci norm laboratoryjnych. Znaczenie zalezy od wieku i innych czynnikow ryzyka - omow z lekarzem. Dieta: wiecej blonnika, ryb, mniej tluszczow nasyconych.",
    "lekarz mowil zeby zmienic dieta i powtorzyc za trzy miesiace. co konkretnie jesc rano?",
    "Platki owsiane, jogurt naturalny, owoce, orzechy. Unikaj wedlin i bialego pieczywa jako bazy."])
N("n07", "claude", "PIT", "2025-02-01", "life_admin", [
    "Rozliczam PIT za zeszly rok razem z zona. Czy wspolne rozliczenie sie oplaca jesli ona ma nizsze dochody?",
    "Zwykle tak, gdy roznica dochodow jest znaczaca, wspolne rozliczenie moze obnizyc podatek. Sprawdz w kalkulatorze urzedu lub programie do rozliczen.",
    "a ulga na dziecko?",
    "Ulge odliczasz w rozliczeniu; przy wspolnym rozliczeniu suma korzysci moze byc korzystniejsza. Zajrzyj do instrukcji formularza."])
N("n08", "chatgpt", "urzad", "2025-03-18", "life_admin", [
    "jak wymienic dowod osobisty, wygasa za dwa miesiace? mieszkam w Gdyni",
    "Wniosek mozna zlozyc online przez gov.pl z profilem zaufanym lub w urzedzie miasta po umowieniu wizyty. Potrzebne zdjecie biometryczne.",
    "profil zaufany mam. ile trwa?",
    "Zwykle do 30 dni, poinformuja o gotowosci. Odbior osobisty."])
N("n09", "claude", "moduly", "2025-04-16", "family", [
    "siostrzenica wybiera moduly na studiach - ma do wyboru statystyke, historie sztuki i podstawy prawa. czym sie kierowac?",
    "Zainteresowaniem i ocena wymagan. Statystyka przydaje sie w wielu zawodach, historia sztuki to szersze horyzonty, prawo - praktyczne. Niech sprawdzi opinie starszych rocznikow.",
    "wybierze statystyke chyba. dzieki"])
N("n10", "chatgpt", "harmonogram", "2025-05-08", "car", [
    "samochod ma 96 tys km, kiedy nastepny przeglad z wymiana oleju i klockow? mam ksiazke serwisowa",
    "Olej zwykle co 15 tys. km lub raz w roku. Klocki zaleznie od stylu jazdy, kontrola przy przegladzie olejowym. Sprawdz zalecenia producenta w ksiazce.",
    "ok rok temu byl olej wiec teraz. ile to kosztuje mniej wiecej?",
    "W ASO 700-1200 zl, niezalezny warsztat 400-700 zl w zaleznosci od modelu."])
N("n11", "chatgpt", "opony", "2025-10-25", "car", [
    "zimowki czy caloroczne? jezdze glownie po miescie i rzadko w gory",
    "Do miasta w Polsce, gdzie bywa snieg i temperatura ponizej 7 C, zimowki nadal bezpieczniejsze. Calorocznych z znaczkiem sniezynki mozna uzywac, ale to kompromis.",
    "zimowki. jaka marka do 400 zl za sztuke, rozmiar 205/55 R16?",
    "Kilka sensownych opcji srednich cen; sprawdz test opon z ostatniego sezonu i etykiete UE - przyczepnosc na mokrym."])
N("n12", "claude", "planning", "2026-04-03", "travel_leisure", [
    "planning 4 days in Lisbon in May with a two year old. any advice?",
    "Base yourself near Alfama or Baixa, use trams sparingly with a stroller, morning outings and afternoon naps. Oceanario is great for toddlers.",
    "we do not want to rent a car. is it doable without?",
    "Absolutely. Metro, trams, taxis for hills. Sintra day trip by train from Rossio."])
N("n13", "chatgpt", "trasa", "2025-07-30", "travel_leisure", [
    "trzy dni na Kaszubach z rowerami, jaka trasa wokol Wdzydz?",
    "Wokol jeziora Wdzydzkiego jest kilkadziesiat km, mozna podzielic na dwa dni z nocleg w agroturystyce. Skansen we Wdzydzach Kiszewskich obowiazkowo.",
    "spimy w namiocie, czy sa pola biwakowe?",
    "Tak, sa pola przy jeziorze; sprawdz zasady i rezerwacje w sezonie."])
N("n14", "claude", "pomysl", "2025-11-30", "gifts", [
    "co kupic mamie na 70 urodziny? lubi ogrod i kryminaly",
    "Ksiazka ilustrowana o ogrodach, czytnik z podswietleniem na kryminaly, albo zestaw ekskluzywnych sadzonek i wyjazd do ogrodu botanicznego.",
    "czytnik. ale czy ona sobie poradzi z obsluga?",
    "Duza czcionka, prosty czytnik z przyciskami do przewracania, wstepnie zaladuj kilka ksiazek."])
N("n15", "chatgpt", "nauka", "2026-02-19", "language", [
    "roznica miedzy por i para w hiszpanskim? ciagle mieszam",
    "Por: przyczyna, srodek, okres. Para: cel, odbiorca, termin. Np. gracias por la ayuda, un regalo para ti.",
    "a 'estoy aqui por una semana' czy 'para'?",
    "Por, bo okres trwania. Para bylby przy terminie: 'para el viernes'."])
N("n16", "claude", "email", "2026-03-09", "life_admin", [
    "help me write a polite email to my landlord about a broken window latch, third time asking",
    "Subject: Broken window latch - follow-up. Dear Mr Nowak, I am writing about the latch in the bedroom, first reported on 12 January. Could you please confirm when a repair can be scheduled this week? Thank you.",
    "make it firmer but still polite",
    "Add: 'If I do not hear back by Friday, I will arrange the repair and deduct the cost as permitted by the lease.' Check the lease first."])
N("n17", "chatgpt", "kot", "2026-05-02", "pets", [
    "kot ma lyse plamy przy ogonie, ale drapie sie tylko troche. pchly?",
    "Moga to byc pchly, alergia pokarmowa lub grzybica. Przejrzyj siersc pod swiatlo, zastosuj pipete przeciwpchlowa i idz do weterynarza jesli plamy sie powiekszaja.",
    "pipety uzywamy. moze alergia. czy karma bez zboz pomoze?",
    "Nie zawsze. Najpierw wykluczenie: dieta eliminacyjna przez 8 tygodni pod okiem weterynarza."])
N("n18", "claude", "kran", "2026-05-22", "household", [
    "kran w kuchni kapie mimo wymiany uszczelki. co jeszcze moze byc?",
    "Zuzyte siedlisko zaworu, luzna glowica lub zla uszczelka. Zakrec wode, wymontuj glowice i obejrzyj siedlisko - jesli jest zarysowane, wymien bateria.",
    "wymienie baterie, kosztuje 120 zl. dzieki"])
N("n19", "chatgpt", "zakup", "2026-06-05", "shopping", [
    "odkurzacz pionowy czy klasyczny z workiem, mieszkanie 60 m2 z dywanami i kot",
    "Przy kocie i dywanach lepsza moc ssania i pojemnosc - klasyczny z workiem lub mocny pionowy z wymiennym akumulatorem. Sprawdz szczotke do siersci.",
    "biore klasyczny. worek czy bezworkowy?",
    "Worek: higieniczniej przy alergiach, koszt workow. Bezworkowy: mniej kosztow, ale oproznianie pylu."])
N("n20", "claude", "CV", "2026-06-27", "career", [
    "pomoz poprawic opis w CV dla kuzynki: 'pracowalam w sklepie i obslugiwalam klientow'. aplikuje na stanowisko koordynatora.",
    "Konkretne wyniki: 'Koordynowalam zmiane 6-osobowa w sklepie, obsluga ok. 200 klientow dziennie, wdrozylam nowy uklad ekspozycji zwiekszajac sprzedaz o 8%'. Dostosuj do prawdziwych danych.",
    "dzieki, dopasuje liczby"])
N("n21", "chatgpt", "plan", "2026-07-11", "sports", [
    "plan biegowy na 10 km w 8 tygodni, obecnie biegam 5 km bez zatrzymania trzy razy w tygodniu",
    "Tydzien 1-2: 3 biegi po 5-6 km, jeden dluzszy. Tydzien 3-5: dluzszy bieg 7-8 km. Tydzien 6-7: 9 km. Tydzien 8: lekki, start. Co czwarty tydzien lzejszy.",
    "a rozciaganie i sila?",
    "Dwa razy w tygodniu kroki: przysiady, wykroki, plank, mostek. Rozciaganie po biegu 5 minut."])
N("n22", "claude", "hobby", "2026-07-25", "music", [
    "ucze sie gry na gitarze od pol roku, utknalem na barre chordach. wskazowki?",
    "Uzywaj kciuka jako oparcia z tylu gryfu, palec wskazujacy plasko na krawedzi, cwicz krotko ale codziennie. Zacznij od F w wersji uproszczonej.",
    "dziala troche lepiej. jaka piosenka na cwiczenie?",
    "Knockin on Heavens Door w wersji z F, albo Hotel California."])
N("n23", "chatgpt", "projekt", "2026-08-08", "home_renovation", [
    "remont lazienki: projekt i harmonogram prac. mamy ekipe, zaczynamy we wrzesniu, trwa 3 tygodnie. co w jakiej kolejnosci?",
    "Kolejnosc: demontaz, instalacje wod-kan i elektryka, tynki i hydroizolacja, plytki, montaz ceramiki i armatury, silikony i sprzatanie. Zamow plytki z wyprzedzeniem.",
    "ekipa chce zaliczke 30 procent, to normalne?",
    "Czesto 10-30 procent. Ustal etapy platnosci i zapisz zakres w umowie.",
    "ok umowa jest. a kiedy zamawiac wanne?",
    "Przed etapem instalacji, bo wymiary rozstawiaja odplywy."])
N("n24", "claude", "mowa", "2026-08-21", "small_talk", [
    "musze wyglosic toast na weselu brata za dwa tygodnie i jestem przerazony. pomozesz napisac?",
    "Struktura: krotki wstep, jedna zabawna historia o bracie, jedna o parze, zyczenia. Maks 2 minuty. Opowiedz mi kilka faktow.",
    "brat zawsze sie spoznia, zona jest bardzo punktualna",
    "Puenta: 'Dzis pierwszy raz w zyciu przyszedl na czas, bo wiedzial, ze ona nie czeka.' Rozwin i dostosuj do siebie."])

# ---------------------------------------------------------------------------
# PART 4 - LEXICAL TRAPS: project/module vocabulary used in another sense
# ---------------------------------------------------------------------------
T("t01", "claude", "lodka", "2024-07-19", "kotwic", "boat anchor (marine hardware), unrelated to software",
  [K], [
    "kolega kupil uzywany jacht 7 metrow i prosi mnie o rade: jaka kotwica? Bruce 10 kg czy Delta 8 kg? plywa po Zatoce Gdanskiej",
    "Na jacht 7 m zwykle Delta lub Bruce 8-10 kg. Wazne jest lancuch: 5-6 m lancucha plus lina. Kotwica na piaszczystym dnie trzyma lepiej z wiekszym stosunkiem dlugosci do glebokosci.",
    "a jak sie kotwiczy przy wietrze? ile wypuscic?",
    "Zasada 5:1 do glebokosci plus wysokosc burty. Przy silnym wietrze 7:1. Sprawdz zaczepienie cofajac silnikiem.",
    "dzieki, przekaze. jeszcze mu polece druga kotwice zapasowa"])
T("t02", "chatgpt", "wycieczka", "2025-08-05", "latarnik", "an actual lighthouse keeper on a tourist visit",
  [L], [
    "jedziemy na Hel i do Rozewia, czy latarnik nadal oprowadza turystow po latarni? bilety trzeba kupowac wczesniej?",
    "Latarnie morskie sa udostepniane zwiedzajacym w godzinach otwarcia, najczesciej sezonowo. Bilety zwykle na miejscu, w sezonie kolejki - lepiej rano.",
    "a czy latarnik naprawde tam mieszka? dzieci pytaja",
    "Dawniej latarnicy mieszkali przy latarniach z rodzinami; dzis wiekszosc jest zautomatyzowana, a budynki sluza muzeom i zwiedzaniu.",
    "fajne. wchodzimy na gore, ile schodow?",
    "Rozewie ma okolo 100 schodow do galerii, Hel podobnie."])
T("t03", "claude", "kurs malarstwa", "2025-03-12", "faktura", "texture of a painted surface (art term), not an invoice",
  [F], [
    "zapisalem sie na kurs malarstwa olejnego. prowadzaca ciagle mowi o fakturze obrazu, ze moja jest plaska. co to znaczy?",
    "Faktura to sposob, w jaki farba lezy na plotnie: gladka, szorstka, gruba, cienka. Plaska faktura to brak zroznicowania pociagniec.",
    "jak ja zroznicowac? szpachla czy pedzel?",
    "Szpachla i grube pociagniecia dla impasto, pedzel sztywny dla wyraznych smug. Mozesz dodac medium zageszczajace do farby, by faktura byla wyrazniejsza.",
    "ok, sprobuje impasto na niebie. jakie farby polecasz na start?"])
T("t04", "claude", "halas", "2025-05-20", "szyn", "tram rails outside the window (noise), not software",
  [BUS], [
    "pod moim oknem w Gdyni jest tramwaj? nie, trolejbus, ale remontuja szyny tramwajowe na sasiedniej ulicy i halas jest nie do zniesienia od 6 rano",
    "Prace przy szynach, szlifowanie i spawanie, bywaja bardzo halasliwe. Mozesz zglosic uciazliwosc do zarzadcy drogi lub urzedu miasta, poprosic o harmonogram prac nocnych.",
    "napisz mi krotkie pismo z pytaniem o terminy zakonczenia prac przy szynach",
    "Szanowni Panstwo, zwracam sie z prosba o informacje o planowanych terminach zakonczenia prac przy wymianie szyn na ulicy X oraz o godzinach prowadzenia prac halasliwych. Z powazaniem, ..."])
T("t05", "chatgpt", "adapter do UK", "2025-09-09", "wtyczk", "electrical wall plug for travel, not a software plugin",
  [PL], [
    "jade do Londynu we wrzesniu, potrzebuje adaptera. jaka wtyczka jest w UK? mam ladowarke do laptopa z wtyczka europejska",
    "W UK wtyczka typu G (trzy prostokatne bolce), 230 V. Kup adapter z bezpiecznikiem; wiekszosc ladowarek laptopow i telefonow obsluguje 100-240 V.",
    "czyli laptop i telefon ok, a suszarka zony?",
    "Suszarka europejska 230 V zadziala napieciowo, ale sprawdz tabliczke znamionowa i uzyj solidnego adaptera; tanie adaptery moga sie grzac przy duzej mocy."])
T("t06", "claude", "boks", "2025-11-12", "storage", "self-storage unit for winter tires, not a data storage layer",
  [ST], [
    "considering renting a self storage unit for our winter tyres and bikes, about 3 square metres. how much should I pay in Gdynia?",
    "Around 200-350 PLN per month for 3 m2 depending on location and access hours. Check insurance, access 24/7, and climate control if you store anything sensitive.",
    "is it worth it vs a garage rental?",
    "Garage rental is often cheaper long term but storage gives flexible short contracts. For seasonal storage, pick a monthly contract with notice of one month."])
T("t07", "chatgpt", "komoda", "2026-01-17", "szuflad", "a piece of furniture drawer, not a software module",
  [ST], [
    "szuflada w komodzie sie zacina, prowadnice rolkowe, mebel z 2015. co zrobic?",
    "Wyjmij szuflade, oczysc prowadnice, nasmaruj smarem silikonowym lub parafina. Sprawdz czy rolki nie sa wyrobione lub czy szuflada nie jest przeciazona.",
    "rolka pekla. da sie dokupic prowadnice?",
    "Tak, zmierz dlugosc i typ prowadnic (rolkowe, kulkowe), kup zamiennik w sklepie meblowym. Dolna szuflada zwykle ma ta sama dlugosc, co gorna."])
T("t08", "claude", "usb-c", "2026-02-05", "hub", "a USB-C hub for a laptop, not a home-automation hub",
  [K], [
    "potrzebuje dobrego huba usb-c do laptopa: hdmi, dwa usb-a, czytnik kart, ladowanie przelotowe. budzet 200 zl",
    "Szukaj huba z HDMI 4K/30 Hz lub 60 Hz, Power Delivery przelotowym 100 W i aluminiowej obudowy. Nie kupuj najtanszych, bo sie grzeja.",
    "ktory ma dobre opinie do dlugiej pracy przy monitorze?",
    "Sprawdz recenzje huba z chipsetem Genesys lub Realtek; unikaj modeli z wieloma portami na jednej magistrali, bo zwalniaja transfer."])
T("t09", "chatgpt", "drukarka", "2026-03-14", "sterownik", "a printer driver, not a home controller",
  [K], [
    "sterownik drukarki HP nie chce sie zainstalowac na Linuxie, pisze ze urzadzenie nie jest obslugiwane. co robic?",
    "Zainstaluj pakiet HPLIP (hplip) i uruchom hp-setup. Wiele modeli HP dziala przez IPP Everywhere bez dodatkowego sterownika.",
    "hp-setup widzi drukarke po wifi, ale skanowanie nie dziala",
    "Doinstaluj hplip-gui i sane, sprawdz zapore, port 8080 i tryb eSCL. Zrestartuj demon cups po zmianach."])
T("t10", "claude", "ptaki", "2024-10-09", "migracj", "bird migration (ornithology), not a database migration",
  [ST], [
    "moje dziecko pyta kiedy zurawie odlatuja i dokad. czy migracja zurawi zaczyna sie we wrzesniu?",
    "Migracja jesienna zurawi z Polski zaczyna sie zwykle we wrzesniu i trwa do listopada. Leca przez Niemcy i Francje do Hiszpanii lub Afryki polnocnej.",
    "a jak sie orientuja? slyszalem o polu magnetycznym",
    "Kombinacja pola magnetycznego, gwiazd, Slonca i doswiadczenia starszych osobnikow. Mlode ucza sie trasy od rodzicow.",
    "gdzie w Polsce moge zobaczyc zurawie?",
    "Bagna Biebrzanskie, Zalew Wislany, Warmia; najlepiej o zmierzchu."])
T("t11", "chatgpt", "muzeum", "2025-06-14", "manuskrypt", "a medieval manuscript on display in a museum, not a novel draft",
  [L], [
    "w niedziele idziemy do muzeum na wystawe sredniowiecznych rekopisow. na plakacie pisze o wyjatkowym manuskrypcie z XIV wieku. czym sie ten zwykle rozni od ksiazki drukowanej?",
    "Manuskrypt pisany recznie, zwykle na pergaminie, z iluminacjami. Kazdy egzemplarz unikalny, tworzony w skryptorium klasztornym.",
    "czy to prawda ze pisarze robili bledy i dopisywali na marginesach?",
    "Tak, marginalia to czesto komentarze pisarzy, zarty, skargi na zimno. Wystawa moze pokazywac takie dopiski."])
T("t12", "claude", "stluczka", "2026-04-28", "zdarzeni", "a traffic incident reported to an insurer, not an event log",
  [BUS, ST], [
    "mialem drobna stluczke na parkingu przed sklepem. jak zglosic zdarzenie do ubezpieczyciela sprawcy? mam oswiadczenie",
    "Zglos szkode na infolinii lub online w towarzystwie sprawcy (numer polisy z oswiadczenia). Opisz zdarzenie, zalacz zdjecia i oswiadczenie. Likwidator przydzieli numer szkody.",
    "czy musze czekac na ogledziny zanim naprawie?",
    "Tak, zaczekaj na ogledziny lub wycene, albo zapytaj czy mozna naprawic i przedstawic faktury. Dokumentuj kazdy krok w korespondencji dotyczacej zdarzenia."])
T("t13", "chatgpt", "hydraulik", "2025-12-19", "faktur", "a real plumber's invoice the user disputes as a customer, not the invoicing tool",
  [F], [
    "hydraulik wystawil mi fakture na 1800 zl za wymiane pionu, a umowa mowila o 1500. do tego VAT 23% zamiast 8% jak przy mieszkaniu. co mam zrobic?",
    "Popros o korekte faktury z uzasadnieniem. Dla robot w lokalu mieszkalnym w okreslonym zakresie obowiazuje obnizona stawka VAT, ale wymaga oswiadczenia klienta. Sprawdz umowe i oswiadczenie.",
    "napisz mi krotki mail z prosba o faktura korygujaca",
    "Szanowny Panie, w zwiazku z faktura nr 214/2025 z dnia 10 grudnia prosze o wystawienie faktury korygujacej z kwota zgodna z umowa 1500 zl brutto oraz stawka VAT 8%. Zalaczam oswiadczenie."])
T("t14", "claude", "klub ksiazki", "2026-05-06", "rozdzia", "reading someone else's novel for a book club, not writing one",
  [L], [
    "w czwartek klub ksiazki omawia Solaris Lema. przygotowuje notatki: ciaglosc narracji, chronologia rozdzialow, spojnosc postaci Kelvina. od czego zaczac?",
    "Zacznij od struktury: rozdzial 1 przylot na stacje, rozdzialy o bibliotece Solaris, spotkanie z Harey. Postaci pobocznych jest niewiele, spojnosc Kelvina to jego zmiana stosunku do Harey.",
    "a interpretacja? czy ocean to bog czy cos innego?",
    "Lem odrzucal proste analogie; ocean jest inny niz czlowiek i to jest sedno. Przygotuj dwa pytania do dyskusji o granicach poznania."])
T("t15", "chatgpt", "parkowanie", "2026-06-14", "czujnik", "a car parking sensor, not a home sensor",
  [K], [
    "czujnik parkowania w samochodzie pika ciagle, nawet gdy nic nie ma za autem. co moze byc?",
    "Zabrudzony lub mokry czujnik, uszkodzenie kabla, albo blad modulu. Umyj wszystkie czujniki, sprawdz czy nie ma zaslonietego; odlacz akumulator na 10 min, by zresetowac.",
    "po umyciu nadal. ktory czujnik jest winny?",
    "Wiele aut pokazuje w menu diagnostycznym, ktory czujnik zglasza blad; jesli nie, sprawdz je po kolei, dotykajac palcem sluchajac klikniecia."])

# ---------------------------------------------------------------------------
# PART 6 - Polish diacritics: the sources above are ASCII-folded (as typed on a
# phone keyboard). ~55% of conversations are re-rendered WITH diacritics using
# this word list (keys are derived by folding), so the archive mixes both, like
# a real one. Only messages that look Polish are touched.
# ---------------------------------------------------------------------------
import unicodedata

_POLISH_WORDS = """
będę będzie biała białego biebrzańskie bieżący biorę błąd błędach błędów błędu błędy błonnika bóg ból bólu brać budzę
budżet być był była byłby było były bywają
cała całe całkowity całoroczne całorocznych całość całości cały ciągle ciągłość ciągły ciepło cofając cofnąć coś ćwicz
ćwiczenia ćwiczenie część często człowiek człowieka czuję czekać czynników czytać
dała dało decydował demontaż dług długiej długo długość długości dłużej dłuższy dobę dochodów doczepiać dodać dodałem
dodawać dokąd dokładnie dokończy dokupić dołożę dopisać doprowadzić dorzucę dość dostają dostarczają doświadczenia
dotyczącej dotykając dowód drożej drukarkę drżało duża dużej dużo dwóch działa działać działał działalność działalności
dzięki dzień dziesiętny dziś
ekipę ekranów elektronikę etykietę
faktów fakturę Francję
galerię gdańskiej gładka głębi głęboki głębokości głośność głowica głowicę główna głównie górę górna góry górze goście
gotowości Grażyna grzać grzeją
hałas hałaśliwe hałaśliwych histerezę historię hiszpańskiego hiszpańskim
idź iść
jadę jakieś jakiś jednostkę jeść jeśli jeżdżę
kalibrować kąpać kasuję każda każde każdego każdej każdy każdym każę kiełbasa kierować kilkadziesiąt kłamie klasę klientów
klikał kliknięcia klocków kłopot kogoś kolejność kolejności kołowy końca końcem końcowa końcowej końcu kończy kończyć
konfliktów koordynowałam korektę korygująca korygującej korygujący korzyści Kościuszki kosztów kotłownia kotwicę krawędzi
krótka krótki krótkie krótko kryminały krzaczyć krzyczała książce książek książka książkę książki księgowa księgowej
księgowości którą która które który których którym którymś ktoś kubków kupić kupił kupować kupujący kwaśny
ładnie ładowanie ładowarek ładowarkę ładuje łagodna łamiące łańcuch łańcucha łatwo łazience łazienki leżąc leży liczbę
liczyć literówkę listę łódź łóżku ludźmi luźna łyse lżejszy lecą
mają mała małe malinkę metodę metrów miałem mieć między mierzą mieście miesiąc miesiąca miesiące miesiącu migał migruję
młode młody moduł modułu moduły mogą mogę mógł mój montaż mówi mówił mówiła może możesz mózgu możliwość można mylić
myślałem
nagłówki nagłówków najczęściej najprościej najtańszych nakładka napięciowo napisać napisał naprawdę naprawić naprawię
narzędzie następny nauczyłem nazwałem negocjują niechęcią niedokończonych niedzielę Niemcy nietknięte nietknięty niewysłane
niezależność niezależny niż niższa niższe
obecność obejrzeć obietnicę obniżona obniżyć obowiązkowo obowiązuje obsługa obsługiwałam obsługiwane obsługuje oczyść odbiór
odbudować odczytać oddać oddała odłącz odlatują odliczać odliczyć odnowić odpływy odpowiedź odrzucać odrzucał odwołanie
ogarnąć oględziny ogóle ogród około określonym omów opłaca opóźnieniem orientują oryginał oś osób osobników oświadczenia
oświadczenie otworzyć
padł padło pamięci pamiętał państwo patrząc pchły pędzel pękła pełna pół płaska płasko płatki płatności pleców płótnie
płytki pływa pociągnięć pociągnięcia podgrzewać podjechać podróż podsmaż podsmażona podświetleniem podwójnie podwyższony
podzielić pogodzić poinformują pojemność pokaż pokaże pokazują pokazywać polecę północnej północy połowę pomiędzy pomóż
pomoże pomożesz pomysł poniżej popęka poprawiać poprawić poprawię poprosić porównaj porównanie porządkującego porządnego
porządnie postać poszła potrzebuję poważaniem powiększają powieść powieści powód powtórzyć powyżej później pracowałam prądu
pralkę próbowałem próg proponować prośba prostokątne proszę prowadząca prowadzę przeciążona przeciwpchłowa przeczekała
przedłużam przedstawić przegląd przeglądzie przekażę przekaźnik przekaźnika przekształcasz przełącz przełączam przełącznik
przerażony przerwę przerywając przesunąć przesunięciu przeszłości przeznaczeń przyczepność przygotowuję przypłynęła
przypływa przyszedł pyłu pytają
radę ręcznego ręcznie ręczny ręcznym reguła regułach regułę reguły rękopisów rekordów ręku remontują robią robić robię
roczników rodziców rosnące rosnącym również równoległych rozciąganie rozdział rozdziałami rozdziałów rozdziału rozdziały
rozjeżdżają rozliczeń rozstawiają różna różni różnica różnicy rozwiń rurę rzędu rdzeń
są sąsiedniej samochód schodów ścianie serów serwisowalność sformułujmy sieć sierść sierści siła skończę skończona
słaba słońca słów słowo słuchając słupkowy służą służy służyć słyszał słyszałem śniadania śnieg śnieżynki śpię śpimy
spisać spójność spójności sposób spóźnia sprawdź sprawdzić spróbuję sprzątanie sprzedaż sprzedaży sprzęt sprzętowy średnich
średnik średniowiecznych środek środku stację stała stawkę stłuczkę straciłem stronę stukała świat światło świeże sygnału
szczególnie szczotkę szkło szkodę sztukę sztywność szufladę szybkości
tabelę tabliczkę telefonów teściowej też tłumaczy tłuszczów tłuszczu tortownicę treść treści trochę trzymać turystów tydzień
uciążliwość uczą uczę udostępniane udziałem układ ulgę umowę umówieniu utknąłem uwagę uzależnień użycia użyj użytkowej
użytkownikowi używa używać używaj używamy używany urządzenia urządzenie urzędu urzędzie
wannę warstwę wartość wartością warunków wątek wątki wątpliwości ważne wcześniej wcześniejsze wdrożyłam wędlin widoków widzę
widział więc więcej wieczór wiedział wiedzieć większość większości większym wilgotność Wiślany włącz włączać włączony własne
właśnie własny wodę wokół wolę wołowina worków wpłacie wracają wracając wróć wrzątku wrześniu wrzuciłem wskazać wskazówki
wskazujący wskazywać wspólna wspólne wspólny wspólnym wstęp wstępnie wszędzie wybór wyboru wybrałem wycenę wyciągnij wyjątek
wyjątkowym wyjeździe wykryć wykrywać wyłącz wyłączał wyłączenie wyłączony wyłączonym wymagań wymianę wymień wymieniam
wymienić wymienię wypuścić wyrażeniach wyraźniejsza wyraźnych wysłania wysłanie wysokość wystawę wystawiają wystawić
wystawił wyświetlaniu wysyła wysyłaj wysyłają wysyłka wytrzymają wywalić wywołanie
zabrał zacząć zacznę zadziała zadziałać zadzwonić zagęszczające zainstalować zakończenia zakończyć zakręć zaktualizować
załącz załączam załaduj zależą zależnie zależności zależy zaliczkę zamarzł zamarznąć zamawiać zamienił zamknięcia zamknięciu
zamknięte zamów zamówienia zamówienie zaokrąglaj zaokrąglenia zapamiętuje zapewnić zapisałem zaporę zaproponować żarty
zarządcy zasłoniętego zasnąć zatwierdzeń zauważyłem zbóż zdarzeń zdjęcia zdjęcie zdolności że żeby żebym żebyś zepsuć
zeszłego zeszły zgadzały zginąć zgłasza zgłoś zgłosić źle złożoną złożyć zły zmianę zmień zmieniać zmienić zmienił żadnego
zniknąć zobaczyć żona żonie żony zorganizować zostają zostawić zostawię zresetować zrób zrobić zrobię źródłowe zróżnicować
zróżnicowania zsumować żurawi żurek żurawie zużyte zwalniają związku zwiedzającym zwiększając zwłaszcza zwróć zwrócił
zwymiotował życiu życzenia zł zła
chcę chciałbym cenę już opisać opróżnianie pipetę pisać pisał planuję poproś pudełka skręty sobotę znacząca
zwykły zwykłego zwykłych zwykłym
""".split()
DIAC: dict[str, str] = {}
for _w in _POLISH_WORDS:
    _k = unicodedata.normalize("NFKD", _w.replace("ł", "l").replace("Ł", "L"))
    _k = "".join(ch for ch in _k if not unicodedata.combining(ch)).lower()
    DIAC.setdefault(_k, _w)
for _k, _w in {"os": "oś", "slowo": "słowo", "slow": "słów", "sa": "są", "sie": "się", "wiec": "więc",
               "tez": "też", "ze": "że", "moze": "może", "mozna": "można", "zl": "zł", "niz": "niż", "byl": "był",
               "pol": "pół", "maja": "mają", "lezy": "leży", "kaze": "każę"}.items():
    DIAC[_k] = _w
# words that must stay ASCII because two Polish words fold to the same form
for _k in ("lodzie", "umowie", "sum", "koci"):
    DIAC.pop(_k, None)

# ---------------------------------------------------------------------------
# PART 5 - export writers, self-checks, ground truth
# ---------------------------------------------------------------------------
import re
import sys
import unicodedata
import uuid
import zlib

NS = uuid.UUID("5b1c0de5-0000-4000-8000-000000000b02")
ACCOUNT = str(uuid.uuid5(NS, "account"))
ZIP_DATE = (2026, 9, 29, 0, 0, 0)

PERSONA = {
    "name": "Witold Sowa",
    "fictional": True,
    "note": ("Wholly invented for this corpus: a hobbyist tinkerer in Gdynia with a home-automation hub, a "
             "novel-in-progress and a small invoicing tool for his wife's ceramics shop. No real person, "
             "shop, product, statute or data. Any resemblance to real people or to the repo owner's "
             "projects is coincidental."),
}

PROJECTS = [
    {"id": K, "name": "Kotwica", "kind": "home_automation",
     "description": "Raspberry Pi hub in the basement: MQTT sensors (ESP32, DS18B20), boiler relay, heating "
                    "schedule with cheap-tariff pre-heating, presence detection, vacation mode, wall-switch fallback.",
     "aliases": ["Kotwica", "sterownik domu", "hub w piwnicy", "domowy mozg"],
     "inflected_forms": ["Kotwicy", "Kotwice", "Kotwica", "Kotwico", "sterownika domu", "hubie piwnicznym"],
     "alias_stems": ["kotwic", "sterownik dom", "hubie piwnicz", "hub w piwnic", "domowy mozg"]},
    {"id": L, "name": "Latarnik", "kind": "novel_and_writing_tools",
     "description": "A novel about a lighthouse keeper (Anzelm) on a fictional island, plus the writer's toolkit: "
                    "chapter versions as events, continuity checker for characters/timeline, word-count stats, epub export.",
     "aliases": ["Latarnik", "powiesc o latarniku", "ksiazka o latarniku"],
     "inflected_forms": ["Latarnika", "Latarnikowi", "Latarnikiem", "Latarniku", "ksiazce o latarniku", "powiesci o latarniku"],
     "alias_stems": ["latarnik"]},
    {"id": F, "name": "Fakturka", "kind": "small_business_tool",
     "description": "Invoicing tool for his wife Halina's ceramics shop: gapless numbering, VAT per rate, PDF, "
                    "corrections instead of edits, advance invoices, CSV for the accountant.",
     "aliases": ["Fakturka", "programik do faktur", "rozliczenia Haliny"],
     "inflected_forms": ["Fakturki", "Fakturce", "Fakturke", "Fakturka", "Fakturko", "programiku do faktur"],
     "alias_stems": ["fakturk", "fakturc", "programik do faktur", "programiku do faktur"]},
]

FOUNDATIONS = [
    {"id": ST, "name": "storage layer", "aliases": ["Szuflada", "warstwa zapisu", "event store", "dziennik zdarzen"],
     "used_by": [K, L, F],
     "description": "Append-only event store on SQLite, forward-only migrations, upcasting on read."},
    {"id": PL, "name": "plugin system", "aliases": ["system wtyczek", "dodatki", "plugins"],
     "used_by": [K, L, F],
     "description": "Folder-scanned plugins with manifest + declared capabilities; fault isolation."},
    {"id": BUS, "name": "event bus", "aliases": ["Szyna", "szyna zdarzen", "event bus"],
     "used_by": [K, L, F],
     "description": "In-process pub/sub, queue per subscriber, idempotent handlers, outbox."},
]

PRINCIPLES = [
    {"id": "pr.boring_tech", "level": "strategy", "form": "heuristic",
     "statement_pl": "Wybieraj nudne, sprawdzone technologie zamiast nowinek.",
     "statement_en": "Prefer boring, proven technology over novelty."},
    {"id": "pr.append_only", "level": "value", "form": "invariant",
     "statement_pl": "Nigdy nie kasuj ani nie nadpisuj danych - dopisuj; stan to widok na historie.",
     "statement_en": "Never delete or overwrite data - append; state is a view over history."},
    {"id": "pr.manual_fallback", "level": "value", "form": "invariant",
     "statement_pl": "Kazda automatyka musi miec reczny tryb dzialajacy bez niej (test: trzecia w nocy, bez internetu).",
     "statement_en": "Every automation needs a manual mode that works without it (the 3 a.m. offline test)."},
    {"id": "pr.replaceable_pieces", "level": "strategy", "form": "heuristic",
     "statement_pl": "Buduj male klocki z wyraznymi granicami, wymienialne w jeden wieczor.",
     "statement_en": "Build small pieces with clear borders, replaceable in one evening."},
    {"id": "pr.write_the_why", "level": "epistemic", "form": "default",
     "statement_pl": "Zapisuj dlaczego, nie co.",
     "statement_en": "Record the why, not the what."},
    {"id": "pr.own_your_data", "level": "value", "form": "invariant",
     "statement_pl": "Dane w otwartych formatach, do odczytania zwyklym notatnikiem za 20 lat; zadnego vendor locka.",
     "statement_en": "Data in open formats readable by a plain editor in 20 years; no vendor lock-in."},
    {"id": "pr.ugly_first", "level": "strategy", "form": "heuristic",
     "statement_pl": "Brzydka, skonczona pierwsza wersja w ~14 dni; polerka dopiero potem; obcinaj zakres zamiast przedluzac.",
     "statement_en": "Ship an ugly, finished first version in ~14 days; polish later; cut scope instead of extending."},
]

DECISIONS = {
    "dec.manual_override_wall_switch": "Wall switch bypasses all electronics; automation is an add-on, not a pillar.",
    "dec.chapter_versions_as_events": "Every chapter save is an event; current text = latest, history retained.",
    "dec.gapless_numbering_at_commit": "Invoice numbers are assigned at commit inside a transaction, drafts have none.",
    "dec.issued_documents_immutable": "Issued invoices are never edited; a correcting document references the original.",
    "dec.csv_bom_semicolon": "Accountant export: CSV, semicolon, UTF-8 BOM, decimal comma, document number as text.",
    "dec.event_store_sqlite": "One SQLite file per project via a shared event-store library; domain lives in payloads.",
    "dec.forward_only_migrations": "Schema changes via PRAGMA user_version, forward-only, file copy before migrating.",
    "dec.plugin_manifest_capabilities": "Plugins live in a folder with manifest.toml and declared capabilities + core API version.",
    "dec.plugin_fault_isolation": "Plugin calls are wrapped with timeout; N consecutive failures disable the plugin.",
    "dec.inproc_bus_queue_per_subscriber": "In-process bus with hierarchical topics; one queue per subscriber.",
    "dec.idempotent_handlers": "Handlers are idempotent; subscriber offsets are stored in the same transaction as effects.",
    "dec.outbox_pattern": "Event + to-send marker written in one transaction; a separate worker publishes.",
    "dec.core_versioned_package": "Shared core is a versioned package with contract tests run in each project's CI.",
    "dec.advance_invoices_as_negative_lines": "Final invoice carries full value and a negative line for advances.",
    "dec.upcast_on_read": "Old event versions are upcast in memory on read; stored events are never rewritten.",
    "dec.snapshots_are_derived": "Snapshots are a rebuildable cache carrying the last event hash; the old log is archived, never deleted.",
    "dec.core_has_no_domain_deps": "Core never imports from domain code; CI enforces the dependency direction.",
}

HARDNESS = {"explicit_name": "easy", "inflected_alias": "medium", "architecture_unnamed": "hard",
            "paraphrase_only": "hard", "shared_foundation": "hard", "philosophy": "hard",
            "multi_topic_late": "hard"}

FORK_CONVS = {"a03", "p02", "s06", "m03", "n04", "n11", "t05", "e04"}   # ChatGPT regenerate-forks (off the current path)

CLAUDE_PROJECTS = [
    {"code": "warsztat", "uuid": str(uuid.uuid5(NS, "cproj-warsztat")), "name": "Warsztat",
     "description": "Jak buduje rzeczy: styl pracy i zasady.",
     "prompt_template": ("Pomagasz Witoldowi. Lubi nudne, sprawdzone narzedzia, male wymienialne klocki i dane w "
                         "otwartych plikach. Kazda automatyka ma miec reczny tryb. Odpowiadaj krotko."),
     "docs": [{"filename": "zasady.md", "content": (
         "# Zasady\n- nigdy nie kasuj, dopisuj\n- pierwsza wersja brzydka, 14 dni\n- zapisuj dlaczego\n"
         "- test trzeciej w nocy: co gdy internet lezy?\n")}],
     "relevance": {"kind": "philosophy", "principles": ["pr.boring_tech", "pr.append_only", "pr.manual_fallback",
                                                        "pr.replaceable_pieces", "pr.write_the_why", "pr.ugly_first"]}},
    {"code": "sklep", "uuid": str(uuid.uuid5(NS, "cproj-sklep")), "name": "Sklep Haliny",
     "description": "Kontekst sklepu z ceramika mojej zony.",
     "prompt_template": "Pomagasz z drobnymi sprawami sklepu z ceramika (kubki, miski, jarmarki).",
     "docs": [{"filename": "cennik.md", "content": "# Cennik\n- kubek maly 45 zl\n- miska 60 zl\n- komplet 12 kubkow z nadrukiem 480 zl\n"}],
     "relevance": {"kind": "project_context", "projects": [F]}},
]
CLAUDE_MEMORIES = {
    "conversations_memory": ("Witold (Gdynia) buduje dla siebie kilka narzedzi po godzinach. Woli nudne technologie, "
                             "dopisywanie zamiast kasowania i reczny tryb dla kazdej automatyki."),
    "project_memories": {CLAUDE_PROJECTS[0]["uuid"]: "Zasady budowania: male klocki, otwarte formaty, brzydka pierwsza wersja.",
                         CLAUDE_PROJECTS[1]["uuid"]: "Halina prowadzi sklep z ceramika; ksiegowa to pani Grazyna."},
}


def fold(s: str) -> str:
    s = s.replace("ł", "l").replace("Ł", "L")
    return "".join(ch for ch in unicodedata.normalize("NFKD", s) if not unicodedata.combining(ch)).lower()


def uid(*parts: str) -> str:
    return str(uuid.uuid5(NS, "|".join(parts)))


def crc(s: str) -> int:
    return zlib.crc32(s.encode("utf-8"))


PROTECTED = ("kotwic", "latarnik", "fakturk", "fakturc", "szuflad", "szyn", "wtyczk", "sterownik", "hub", "programik",
             "storage", "migracj", "manuskrypt", "zdarzeni", "czujnik", "rozdzia", "faktur", "harmonogram", "przekaznik",
             "watchdog", "zaliczk")
_WORD = re.compile(r"[A-Za-zĄ-ż]+")


def _typo_word(w: str, h: int) -> str:
    if len(w) < 6 or any(p in fold(w) for p in PROTECTED):
        return w
    i = 1 + h % (len(w) - 3)
    return w[:i] + w[i + 1] + w[i] + w[i + 2:]


def add_typos(text: str, seed: str) -> str:
    """Swap two adjacent inner letters in ~1 word of a message (never in protected alias stems)."""
    words = [m for m in _WORD.finditer(text) if len(m.group()) >= 6]
    if not words:
        return text
    m = words[crc(seed + text[:20]) % len(words)]
    return text[:m.start()] + _typo_word(m.group(), crc(seed + m.group())) + text[m.end():]


def restore_diacritics(text: str) -> str:
    def rep(m):
        w = m.group()
        r = DIAC.get(w.lower())
        if r is None:
            return w
        if w.isupper() and len(w) > 1:
            return r.upper()
        if w[0].isupper():
            return r[0].upper() + r[1:]
        return r
    return _WORD.sub(rep, text)


_PL_HINT = {"sie", "nie", "jak", "czy", "jest", "ze", "zeby", "ale", "ktory", "mam", "chce", "dla", "przy", "bez",
            "tak", "juz", "tylko", "teraz", "dobra", "dobrze", "dzieki", "trzeba", "moze", "mozna", "tez", "po", "od",
            "sa", "co", "ok", "na", "do", "w", "z", "i"}
_PL_STRONG = {"sie", "nie", "jak", "czy", "jest", "ze", "zeby", "ale", "ktory", "mam", "chce", "dla", "przy", "bez",
              "tak", "juz", "tylko", "teraz", "dobra", "dobrze", "dzieki", "trzeba", "moze", "mozna", "tez", "po", "od", "sa"}


def looks_polish(text: str) -> bool:
    return any(w.lower() in _PL_STRONG for w in _WORD.findall(text))


_FILLERS = ["eee ", "no wiec ", "yyy ", "halo "]


def dictate(text: str, seed: str) -> str:
    """Speech-to-text artefacts: lower-case, spoken punctuation, a filler word."""
    t = text[:1].lower() + text[1:]
    t = t.replace(", ", " przecinek ", 2).replace(". ", " kropka ", 1).replace("?", " znak zapytania", 1)
    return _FILLERS[crc(seed) % len(_FILLERS)] + t


def render_messages(c: dict) -> list[str]:
    """Deterministic surface noise: ~12% of convs dictated, ~30% typos in user turns, ~55% Polish diacritics."""
    h = crc("surface|" + c["id"])
    typos = (h % 100) < 30
    diac = ((h >> 8) % 100) < 55
    dictated = ((h >> 16) % 100) < 12
    out = []
    for i, t in enumerate(c["msgs"]):
        if dictated and i % 2 == 0 and looks_polish(t):
            t = dictate(t, c["id"] + str(i))
        if typos and i % 2 == 0:
            t = add_typos(t, c["id"] + str(i))
        if diac and looks_polish(t):
            t = restore_diacritics(t)
        out.append(t)
    return out


def msg_times(c: dict) -> list[datetime.datetime]:
    d = datetime.date.fromisoformat(c["date"])
    h = crc("time|" + c["id"])
    t0 = datetime.datetime(d.year, d.month, d.day, 7 + h % 14, (h >> 4) % 50, (h >> 9) % 60,
                           tzinfo=datetime.timezone.utc)
    out, t = [], t0
    for i in range(len(c["msgs"])):
        t = t + datetime.timedelta(seconds=90 + (h >> (i % 7)) % 400 + (i % 2) * 60)
        out.append(t)
    return out


def chatgpt_conv(c: dict) -> dict:
    cid = uid("chatgpt", c["id"])
    texts, times = render_messages(c), msg_times(c)
    root = uid(cid, "root")
    sysn = uid(cid, "sys")
    mapping = {root: {"id": root, "parent": None, "children": [sysn], "message": None},
               sysn: {"id": sysn, "parent": root, "children": [],
                      "message": {"id": sysn, "author": {"role": "system", "name": None},
                                  "content": {"content_type": "text", "parts": [""]},
                                  "create_time": None, "status": "finished_successfully",
                                  "metadata": {"is_visually_hidden_from_conversation": True}}}}
    prev, last = sysn, sysn
    for i, text in enumerate(texts):
        nid = uid(cid, str(i))
        role = "user" if i % 2 == 0 else "assistant"
        meta = {"model_slug": "gpt-4o"} if role == "assistant" else {}
        mapping[nid] = {"id": nid, "parent": prev, "children": [],
                        "message": {"id": nid, "author": {"role": role, "name": None},
                                    "content": {"content_type": "text", "parts": [text]},
                                    "create_time": times[i].timestamp(), "status": "finished_successfully",
                                    "metadata": meta}}
        mapping[prev]["children"].append(nid)
        if c["id"] in FORK_CONVS and i == 1:
            alt = uid(cid, "alt1")
            mapping[alt] = {"id": alt, "parent": prev, "children": [],
                            "message": {"id": alt, "author": {"role": "assistant", "name": None},
                                        "content": {"content_type": "text", "parts": [
                                            "Zalezy od szczegolow - opisz prosze wiecej, wtedy podpowiem konkretniej."]},
                                        "create_time": times[i].timestamp() - 5, "status": "finished_successfully",
                                        "metadata": {"model_slug": "gpt-4o"}}}
            mapping[prev]["children"].insert(0, alt)   # abandoned regenerate sibling, off the current path
        prev = last = nid
    return {"id": cid, "conversation_id": cid, "title": c["title"], "create_time": times[0].timestamp(),
            "update_time": times[-1].timestamp(), "current_node": last, "mapping": mapping,
            "default_model_slug": "gpt-4o"}


def iso(t: datetime.datetime, h: int) -> str:
    return t.replace(microsecond=(h % 900000) + 1000).isoformat().replace("+00:00", "Z")


def claude_conv(c: dict) -> dict:
    cid = uid("claude", c["id"])
    texts, times = render_messages(c), msg_times(c)
    h = crc("iso|" + c["id"])
    msgs = []
    for i, text in enumerate(texts):
        role = "human" if i % 2 == 0 else "assistant"
        msgs.append({"uuid": uid(cid, str(i)), "text": text,
                     "content": [{"type": "text", "text": text}], "sender": role,
                     "created_at": iso(times[i], h + i), "updated_at": iso(times[i], h + i),
                     "attachments": [], "files": []})
    return {"uuid": cid, "name": c["title"], "created_at": iso(times[0], h), "updated_at": iso(times[-1], h),
            "account": {"uuid": ACCOUNT}, "chat_messages": msgs}


def export_id(c: dict) -> str:
    return uid(c["provider"], c["id"])


def zip_bytes(files: dict[str, bytes]) -> bytes:
    import io
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as z:
        for name in sorted(files):
            zi = zipfile.ZipInfo(name, ZIP_DATE)
            zi.compress_type = zipfile.ZIP_STORED
            zi.external_attr = 0o644 << 16
            z.writestr(zi, files[name])
    return buf.getvalue()


def dumps(o) -> bytes:
    return (json.dumps(o, ensure_ascii=False, indent=1) + "\n").encode("utf-8")


# ---- self-checks ------------------------------------------------------------
def stems_hit(c: dict) -> list[str]:
    txt = fold(" ".join(c["msgs"]))
    return [p["id"] for p in PROJECTS if any(s in txt for s in p["alias_stems"])]


def self_check() -> None:
    by_id = {c["id"]: c for c in CONVS}
    for c in CONVS:
        for i, m in enumerate(c["msgs"]):
            assert isinstance(m, str) and m.strip(), (c["id"], i)
        if c["kind"] == "relevant":
            cat = c["category"]
            base = by_id[c["duplicate_of"]]["category"] if cat == "near_duplicate" else cat
            hits = stems_hit(c)
            if base in ("explicit_name", "inflected_alias"):
                assert set(c["projects"]) <= set(hits) and hits, ("must name project", c["id"], hits)
            else:
                assert not hits, ("must NOT name project", c["id"], hits)
            assert c["projects"] or base == "philosophy", c["id"]
            for d in c["decisions"]:
                assert d in DECISIONS, d
            for p in c["principles"]:
                assert any(x["id"] == p for x in PRINCIPLES), p
        elif c["kind"] == "noise":
            assert not stems_hit(c), ("noise names a project", c["id"])
        elif c["kind"] == "trap":
            assert fold(c["term"]) in fold(" ".join(c["msgs"])), ("trap term absent", c["id"])
            assert c["collides_with"], c["id"]
    ids = {export_id(c) for c in CONVS}
    assert len(ids) == len(CONVS)
    # surface noise (typos/dictation/diacritics) must not destroy the truth-defining tokens
    for c in CONVS:
        rt = fold(" ".join(render_messages(c)))
        if c["kind"] == "trap":
            assert fold(c["term"]) in rt, ("trap term lost in rendering", c["id"])
        if c["kind"] == "relevant":
            base = by_id[c["duplicate_of"]]["category"] if c["category"] == "near_duplicate" else c["category"]
            hits = [p["id"] for p in PROJECTS if any(s in rt for s in p["alias_stems"])]
            if base in ("explicit_name", "inflected_alias"):
                assert set(c["projects"]) <= set(hits), ("alias lost in rendering", c["id"])
            else:
                assert not hits, ("alias appeared in rendering", c["id"])


def build_ground_truth() -> dict:
    order = sorted(CONVS, key=lambda c: (c["date"], c["provider"], c["id"]))
    label = {c["id"]: f"U{n + 1:03d}" for n, c in enumerate(order)}
    by_id = {c["id"]: c for c in CONVS}

    def base_row(c):
        return {"label": label[c["id"]], "conv_id": export_id(c), "provider": c["provider"], "title": c["title"],
                "date": c["date"], "message_count": len(c["msgs"])}

    relevant, traps, noise = [], [], []
    for c in order:
        r = base_row(c)
        if c["kind"] == "relevant":
            cat = c["category"]
            basecat = by_id[c["duplicate_of"]]["category"] if cat == "near_duplicate" else cat
            r.update({"project": c["projects"][0] if c["projects"] else None, "projects": c["projects"],
                      "mentions_projects": [], "foundations": c["foundations"], "category": cat,
                      "hardness": HARDNESS[basecat], "names_project_explicitly": bool(stems_hit(c)),
                      "principles": c["principles"], "decisions": c["decisions"]})
            if "duplicate_of" in c:
                r["duplicate_of"] = export_id(by_id[c["duplicate_of"]])
                r["duplicate_of_label"] = label[c["duplicate_of"]]
                r["base_category"] = basecat
            if c["note"]:
                r["note"] = c["note"]
            relevant.append(r)
        elif c["kind"] == "trap":
            r.update({"term": c["term"], "real_sense": c["real_sense"], "collides_with": c["collides_with"],
                      "why_not_relevant": "uses a project/module vocabulary word in an unrelated real-world sense"})
            traps.append(r)
        else:
            r.update({"categories": [c["topic"]]})
            noise.append(r)

    dup_groups = [[export_id(by_id[c["duplicate_of"]]), export_id(c)] for c in order if "duplicate_of" in c]

    principles = []
    for p in PRINCIPLES:
        units = [x["conv_id"] for x in relevant if p["id"] in x["principles"]]
        principles.append(dict(p, phrasing_units=units,
                               philosophy_only_units=[x["conv_id"] for x in relevant
                                                      if p["id"] in x["principles"] and x["category"] == "philosophy"]))
    decisions = []
    for did, stmt in DECISIONS.items():
        us = [x for x in relevant if did in x["decisions"]]
        decisions.append({"id": did, "statement": stmt, "units": [x["conv_id"] for x in us],
                          "projects": sorted({p for x in us for p in x["projects"]})})

    projects = []
    for p in PROJECTS:
        keep = {k: v for k, v in p.items() if k != "alias_stems"}
        keep["relevant_units"] = [x["conv_id"] for x in relevant if p["id"] in x["projects"]]
        keep["units_not_naming_project"] = [x["conv_id"] for x in relevant
                                            if p["id"] in x["projects"] and not x["names_project_explicitly"]]
        projects.append(keep)
    foundations = [dict(f, units=[x["conv_id"] for x in relevant if f["id"] in x["foundations"]]) for f in FOUNDATIONS]

    cat_counts = {k: sum(1 for x in relevant if x["category"] == k) for k in CATS}
    hard_counts = {}
    for x in relevant:
        hard_counts[x["hardness"]] = hard_counts.get(x["hardness"], 0) + 1
    multi = [x["conv_id"] for x in relevant if len(x["projects"]) > 1]
    artifacts = [{"kind": "claude_project", "uuid": p["uuid"], "name": p["name"], "relevance": p["relevance"]}
                 for p in CLAUDE_PROJECTS]
    counts = {
        "conversations_total": len(CONVS), "relevant": len(relevant), "noise_traps": len(traps),
        "noise_generic": len(noise),
        "conversations_chatgpt": sum(1 for c in CONVS if c["provider"] == "chatgpt"),
        "conversations_claude": sum(1 for c in CONVS if c["provider"] == "claude"),
        "messages_total": sum(len(c["msgs"]) for c in CONVS),
        "relevant_by_category": cat_counts, "relevant_by_hardness": hard_counts,
        "relevant_multi_label": len(multi), "relevant_not_naming_project": sum(1 for x in relevant if not x["names_project_explicitly"]),
        "relevant_by_project": {p["id"]: sum(1 for x in relevant if p["id"] in x["projects"]) for p in PROJECTS},
        "relevant_philosophy_only": cat_counts["philosophy"], "near_duplicate_pairs": len(dup_groups),
        "principles": len(PRINCIPLES), "decisions": len(DECISIONS), "foundations": len(FOUNDATIONS),
    }
    return {
        "schema": "loom.eval.ground_truth/1", "corpus_id": "blind_catalog_v2",
        "generated_by": "loom/tools/gen_blind_catalog_v2.py", "persona": dict(PERSONA, git_author="Witold Sowa <witold.sowa@example.invalid>"),
        "purpose": ("Blind validation set for selective-import catalog scoring: find every relevant conversation "
                    "(project work, shared foundations, philosophy) even when no project is named."),
        "units": {"relevant": relevant, "noise_traps": traps, "noise_generic": noise},
        "multi_label_units": multi, "duplicate_groups": dup_groups,
        "projects": projects, "foundations": foundations, "principles": principles, "decisions": decisions,
        "non_conversation_artifacts": artifacts, "counts": counts,
    }


def build_all() -> dict[str, bytes]:
    self_check()
    order = sorted(CONVS, key=lambda c: (c["date"], c["id"]))
    gpt = [chatgpt_conv(c) for c in order if c["provider"] == "chatgpt"]
    cla = [claude_conv(c) for c in order if c["provider"] == "claude"]
    projects_json = [{"uuid": p["uuid"], "name": p["name"], "description": p["description"],
                      "prompt_template": p["prompt_template"], "is_private": True,
                      "docs": [{"uuid": uid(p["uuid"], d["filename"]), "filename": d["filename"], "content": d["content"],
                                "created_at": "2025-01-05T10:00:00Z"} for d in p["docs"]],
                      "created_at": "2025-01-05T10:00:00Z"} for p in CLAUDE_PROJECTS]
    return {
        "chatgpt_export.zip": zip_bytes({"conversations.json": dumps(gpt)}),
        "claude_export.zip": zip_bytes({"conversations.json": dumps(cla), "projects.json": dumps(projects_json),
                                        "memories.json": dumps(CLAUDE_MEMORIES)}),
        "ground_truth.json": dumps(build_ground_truth()),
    }


def main(argv: list[str]) -> int:
    files = build_all()
    if "--check" in argv:
        bad = [n for n, b in files.items() if not (OUT / n).exists() or (OUT / n).read_bytes() != b]
        print("MISMATCH: " + ", ".join(bad) if bad else "OK: committed corpus matches generator")
        return 1 if bad else 0
    OUT.mkdir(parents=True, exist_ok=True)
    for n, b in files.items():
        (OUT / n).write_bytes(b)
    c = json.loads(files["ground_truth.json"])["counts"]
    print(json.dumps(c, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
