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

DIAC: dict[str, str] = {}

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


PROTECTED = ("kotwic", "latarnik", "fakturk", "fakturc", "szuflad", "szyn", "wtyczk", "sterownik", "hub", "programik")
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


def render_messages(c: dict) -> list[str]:
    """Deterministic surface noise: ~30% of convs get typos in user turns, ~55% get Polish diacritics."""
    h = crc("surface|" + c["id"])
    typos = (h % 100) < 30
    diac = ((h >> 8) % 100) < 55
    out = []
    for i, t in enumerate(c["msgs"]):
        if typos and i % 2 == 0:
            t = add_typos(t, c["id"] + str(i))
        if diac:
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
