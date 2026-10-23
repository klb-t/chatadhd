# Interfejs współpracy i ekonomia tokenów — synteza R89 (Fable, 2026-10-23)

Stan na 2026-10-23 ≈ 11:00 UTC. Wejście: pięć torów badania (`docs/research/TOKENY_2026-10-23/*.md`, 115 ustaleń), ocena
relewantności, R88/R89 (słowa właściciela, stan `proposed`), R88 v1 (skrzynka decyzji + sekretarz co godzinę, `docs/spec/README.md`
§„Skrzynka decyzji”). Zasady: wszystko konfigurowalne, domyślne to presety, listy otwarte; każda konkretna wartość niżej jest
**przykładem** albo **presetem** (oznaczenia uzupełnione w kontroli przed użyciem, §9; wartość bez oznaczenia to błąd zapisu, nie reguła); każdy fakt o teraźniejszości ma datę i ocenę źródła (R59/R60); nic nie jest przypisane
właścicielowi bez jego cytatu. Numery `nasze-NN`, `prac-NN`, `nauk-NN`, `interf-NN`, `role-NN` odsyłają do notatek torów (tam
pełne źródła); §7 zbiera je w jeden ledger.

## §0 Streszczenie dla zmęczonego właściciela

1. Twój pomysł (R88/R89) jest zgodny z tym, co mierzymy: 95 % zużycia to agenci, nie rozmowa z Tobą (nasze-04); koszt agenta rośnie
   z **kwadratem** jego długości (nasze-01); 24 % wydatku agentów to nie praca — 7 % prefiks startu, 17 % konteksty przepisane po
   pauzie > 5 min (nasze-02); odczyt z cache to 95–97 % wolumenu tokenów (nasze-03). Oszczędzamy więc **długość i trwałość
   kontekstu**, a Twój czas czytania to osobna waluta (R62).
2. W tym tygodniu robimy pięć małych pakietów (§5, każdy o budżecie ~50 wywołań — preset, każdy z pomiarem „po”): pomiar sekretarza (P1; częstość — przykład: co godzinę, preset w danych),
   kolejka „czeka na Ciebie” na stronie telefonu (P2), brief z hipotezą + metryka „wywołania vs plan” (P3), układ briefu „część
   wspólna → własna” z pomiarem salwy równoległej (P4), karta wznowienia wg szablonu + kompakt przed pauzą (P5).
3. Ile to da (górne granice z pomiarów, do potwierdzenia pakietami): przepisane konteksty ≤ 17 % wydatku agentów (≈ 2–3 pkt
   tygodnia na dobę przy tempie z 23.10: 16 pkt/dobę, nasze-18; przeliczenie na pkt zależy od wag założonych — §7, L1); pakiety biegną 2–5× dłużej od planu (nasze-07) — skrócenie
   agenta o 1/3 to przy kwadracie ≈ połowa jego kosztu (model); trzy zatrzymania i restarty jednego workflow ≈ 0,9 pkt łącznie (nasze-08, 22.10); jeden
   uniknięty zryw 5 agentów = 24 % okna 5 h (nasze-09).
4. Z sieci bierzemy wprost tylko to, co ma pomiar lub jest faktem producenta (§1); resztę przerabiamy po naszemu (§2): nic nie
   znika po cichu, decyzje to dane, listy otwarte. Oryginalne (§3): korekta jako diff do spec z routingiem po cechach, ustalenie jako
   węzeł grafu z odwrotem, waluty na jednym paragonie (np. tokeny i Twój czas czytania; lista otwarta), skryba kalibrujący własną wiarygodność.
5. Interfejs (§4) domyślnie (preset) nie jest nowym produktem: to **perspektywa grafu aplikacji** (spec, decyzje, pomiary, pakiety jako węzły z
   proweniencją) z cienkim widokiem na telefon (dziś strona-artefakt R88 v1); osobny, dedykowany interfejs zostaje opcją z kosztem i odwrotem (§6). Nic z tego nie wymaga Twojej decyzji teraz; każda
   decyzja ma odwrót (§6). Luki (§8) są pomiarami, nie problemami.

## §1 Co użyć wprost (use_now)

Oceny `own` dla nasze-08…30 pochodzą z syntezy, bez osobnego etapu oceny (L14); do czasu kontroli nie są ustalonym faktem.

| Technika | Źródło (ocena, data) | Nasz zmierzony zlew | Oczekiwana oszczędność | Pakiet |
|---|---|---|---|---|
| Pakiet ≈ 50 wywołań z twardym wyjściem (karta); spec w pliku → świeży agent czyta tylko spec | nasze-01, nasze-07 (own, high/medium, 21–23.10); prac-13, interf-03 (docs, high, 23.10) | koszt ∝ długość²; pakiety 2–5× planu | przy skróceniu o 1/3 ≈ −50 % kosztu agenta (model) | P3 |
| Długość wywołania poniżej progu (preset ~4 min, z TTL cache 5 min dostawcy — prac-03); kompakt/karta PRZED pauzą; porzucona ścieżka → ucięcie do checkpointu, nie streszczenie | prac-03, prac-07 (docs, high, 23.10); nasze-02 | 17,1 % przepisanych kontekstów | do −17 % wydatku agentów | P5 |
| Szablon karty wznowienia: postęp i decyzje, ograniczenia, pozostałe kroki, dane krytyczne, lista plików (ścieżki), **ostatnie słowa właściciela dosłownie** | prac-21 (Codex, medium, 2025-12); prac-09 (docs, high) | agent kontynuacji czyta kartę + `git log` | mniej ponownego czytania; nic do zmierzenia poza tokenami startu | P5 |
| Stała część zapytania bajt w bajt na początku (hierarchia cache dostawcy narzędzia → system → wiadomości — fakt datowany 23.10, prac-02), zmienna na końcu (układ — preset); deterministyczna serializacja JSON; brak znaczników czasu w prefiksie (preset) | prac-02, prac-06 (docs, high, 23.10); prac-23 (Manus, medium) | prefiks 39–60 tys. na start, N zapisów w salwie | N zapisów → 1 zapis + N−1 odczytów (hipoteza) | P4 |
| Model i effort ustalane na starcie agenta, per etap i klasa zadania; poprawki/mechaniczne/logi domyślnie na tańszym modelu (preset, §4.2) | prac-11, prac-06 (docs, high); nasze-15 (own, high) | 13 pakietów poprawek na Fable = 19,6 % | zmierzyć udział po 22.10 | istnieje; pomiar w P1 |
| Budzić koordynatora rzadko i hurtem; rutyny w świeżej małej sesji z własnym kontekstem | prac-15 (docs, high); nasze-20, nasze-21 (own, high) | kontekst koordynatora 377–587 tys. na ruch; ≈ 0,25 pkt na pakiet | każda uniknięta tura ≈ 0,5 mln równoważników | P1 |
| Wynik agenta/podagenta jako rekord strukturalny (format domyślny — preset; proza dozwolona), budżet — preset (przykład 1–2 tys. tokenów) | prac-16 (blog Anthropic, high, 2025-09); nauk-23 (PACT, preprint, średnia) | karty i raporty czytane przez koordynatora | PACT: −50 % wejścia (ich pomiar) | P5 |
| Filtr wyjścia narzędzi przed modelem (log → wycinek), pełny log na dysku, przełącznik „pokaż całość” | prac-12 (docs, high); nasze-16 (own, medium) | czytacze logów: 14 × 1,5 mln = 43 % S2 | większość z 20,5 mln (hipoteza) | P9 |
| Stawki cache per model jako datowane dane (otwarty zbiór kategorii; dziś u dostawcy np. wejście, zapis, odczyt, wyjście — prac-01, 23.10); advisory gdy `cache_read` = 0 przy prefiksie > minimum | prac-01, prac-04 (docs, high, 23.10) | `cache_write: null` w cenach repo | poprawny rachunek, nie oszczędność | P10 |
| Pola `usage` dostawcy jako prawda pierwotna; cena jako nakładka z datą; kubełki rozłączne; koszt roli nadzorczej liczony do zadania | role-19, role-21, role-22 (docs/repo, high); role-12, role-13 | estymata vs koszt | kalibracja | P10 |
| Miara wybieralna; domyślnie (preset) koszt przyjętego wyniku (cost-of-pass) i tokeny na jednostkę wyniku; per wywołanie i per commit dostępne, przy małej użyteczności advisory | nauk-21 (preprint, średnia); role-15 (MetaGPT, high); nasze-06, nasze-14 (own) | 2,94–3,32 USD/commit jako jedyna miara mówi mało (advisory, nie wykluczenie) | jednostka planowania | P3 |
| Liczba recenzentów proporcjonalna do zmiany (preset: 1 dla poprawki); tania kontrola (Haiku) PRZED użyciem dokumentu | nasze-24, nasze-30 (own, high/medium); interf-05 (docs, high) | pętla przeglądów 54 % wydatku agentów | zmierzyć „po” | istnieje; pomiar w P1 |
| Reguły skalowania wysiłku jako dane w briefie koordynatora (1 agent 3–10 wywołań … >10 podagentów — przykład Anthropic); budżet domyślnie bez limitu | prac-17, role-08, role-03 (Anthropic, high) | zryw 5 agentów = 24 % okna | kalibracja progu ×10 | P4 |
| Próbka `rate_limit_event` do ledgera automatycznie, nie ręcznie | nasze-11 (own, high) | ledger 145 linii ręcznie | czas koordynatora | P1 |
| Niepewność widoczna w artefakcie (znacznik niejasności; przykład formatu: `[NEEDS CLARIFICATION: …]`, Spec Kit), nie ukryta w założeniu | interf-09 (Spec Kit, high) | parafrazy `proposed` bez listy niejasności | mniej tur korekt (niemierzone) | P2 |
| Pytanie poboczne jako gałąź z flagą „nie wysyłaj dalej”; licznik korekt tej samej sprawy jako sygnał (próg — preset) | prac-08 (docs, high); interf-19, interf-26 (medium) | brak flagi w jądrze | jakość rozmowy | jądro, małe |
| Kolejność bloków w prompcie jako zmienna; „mieści się” ≠ „użyte” | nauk-12 (TACL, wysoka); nauk-14 (Chroma, średnia) | selektor K3 | jakość | K3 |
| Wagi oceny pamięci (czynniki np. świeżość, ważność, trafność — lista otwarta) jako presety; drogie oceny raz przy zapisie | nauk-07 (UIST 2023, wysoka); nauk-09 (AutoMEM, średnia) | selektor K3 | koszt stały | K3 |
| Punkt odniesienia: ≈ 13 USD/dev/dzień; zespoły agentów ≈ 7× (datowane) | prac-05 (docs, high, 23.10) | nasze konto liczy okna | porównanie | — |

## §2 Co usprawnić po naszemu (improve_on)

| Źródło | Co robi | Jak filozofia to zmienia | Pakiet |
|---|---|---|---|
| role-25, role-11, role-14 (sekretarz = osobne wywołanie, zapis poza kontekstem, format, filtr) | streszczenie z własnym promptem jedzie dalej zamiast rozmowy | **skryba jako akcja przepływu w danych**: model i prompt presety, wejście = wycinek przebiegu, wyjście = rekord o schemacie (ustalenia, otwarte, odrzucone, koszt) jako węzeł grafu z datą i źródłem; filtr przekazania widoczny i edytowalny | P6 |
| interf-02, interf-07, interf-14 (AskUserQuestion, „Needs input”, 30 s Devina) | pytanie z opcjami; brak odpowiedzi → kontynuacja po czasie | decyzja = opcja + „własnymi słowami”; **czas oczekiwania to otwarte pole, nie lista 60s/5m/10m**; działanie bez odpowiedzi zapisane jako `proposed` z odwrotem (R75); kolejka sortowana „blokujące najpierw” to preset | P2 |
| interf-08 (Spec Kit clarify: ≤ 5 pytań, ukryta kolejka) | tabela opcji z rekomendacją, zapis Q→A z datą | zapis Q→A do spec przejmujemy; **limit 5 to preset, kolejka pytań widoczna w całości**; priorytet wpływ × niepewność jako jawne pola | P2 |
| interf-22 (Horvitz, Fleming, Dong — wartość informacji) | pytać, gdy wartość odpowiedzi > koszt uwagi | analogiczna podstawa gałki „jak często pytać, do nigdy” (domena inna — §7): koszt pytania (Twoja uwaga) i stawka decyzji (koszt odwrotu) jako dane przy każdej pozycji; uzasadnienie pytania zawsze widoczne | P2, P8 |
| nauk-20 (180 konfiguracji: kiedy więcej agentów szkodzi) | architektura dobrana do zadania | cechy zadania (np. dekomponowalność, liczba narzędzi, sekwencyjność — lista otwarta) jako **dane briefu**, architektura jako wynik, progi presety; „podatek koordynacyjny” = nasze karty i ponowne czytanie — mierzony | P4 |
| nauk-06 (A-Mem: 2 520 vs ~17 tys. tokenów na pytanie) | notatki + linki + ewolucja węzłów | nasz graf + krok ewolucji przy zapisie i top-k zamiast całej historii; każda zmiana węzła zapisana z datą (nic nie znika) | K3 |
| prac-16, nauk-24, nauk-01..03 (czyszczenie wyników narzędzi, kompresja) | usuwa/kompresuje bez śladu | domyślnie (preset) **jawna, odwracalna, z podglądem usuniętego**; tryb bez potwierdzania dostępny jako opcja z advisory i obowiązkowym śladem; wytyczna kompresji jako edytowalny tekst; każde kompaktowanie zostawia czytelny ślad (co, ile, dlaczego) | P5, P9 |
| prac-14 (stagger 5 s dla wspólnego cache) | opóźnia start równoległych agentów | nasze briefy różnią się od pierwszej linii, więc zysk ze staggera jest prawdopodobnie mały (hipoteza, do zmierzenia w P4; preset 0, advisory) — **układ „część wspólna → część własna”**; liczby (5 s, 25 agentów) to presety produktu | P4 |
| prac-10 (CLAUDE.md < 200 linii) | reguła długości | **advisory z pomiarem** (`/usage` per żądanie), co jest zasadą, a co historią do `docs/` — decyduje właściciel; żadnego limitu | P1 (pomiar) |
| interf-01, interf-12 (Plan Mode: trzy odpowiedzi; sugestia trybu dla złożonych) | zamknięta lista odpowiedzi | lista odpowiedzi otwarta (przykłady: „tak, dalej bez pytań o edycje”, „tak, każdą zmianę osobno”, „planuj dalej”); system **proponuje** droższy tryb z etykietą kosztu, osoba decyduje (nasza zasada ×10) | P2 |
| interf-06 (zmęczenie zgodami: klasyfikator, allowlisty) | klasyfikator decyduje za osobę | tylko z **dziennikiem decyzji i odwrotem**; allowlisty jako dane; przegląd „diff po fakcie” jako tryb dla telefonu | P8 |
| interf-16 (Linear: delegat ≠ właściciel, `awaitingInput`) | stan sesji pierwszoklasowy | w grafie: rodzaj krawędzi „deleguje” i stan węzła „czeka na osobę” — kolejka buduje się bez czytania transkryptu; lista stanów otwarta | P7 |
| interf-13 (ręczne edycje planu ignorowane) | plan nie jest źródłem prawdy | edycja parafrazy/planu przez osobę **wygrywa**: plan = węzeł z wersjami czytany tuż przed krokiem, diff po akceptacji jawnie do kontekstu | P6, P7 |
| nauk-13 (Self-Route: RAG → pełny kontekst) | eskalacja po samoocenie modelu | eskalacja jawna i zliczana (odsetek eskalacji jako miara profilu); u nas „pełny kontekst” = całe archiwum, 10–100× | K3 |
| prac-20 (mapa repo Aidera) | sygnatury + ranking + budżet 1k | „mapa grafu” (etykiety, rodzaje, sygnatury) przed treścią; budżet to cel, ranking konfigurowalny; model zamawia węzły | K3 |
| nauk-15, nauk-17, nauk-16 (kaskady, routery) | próg z kalibracji | próg jako datowany parametr; etykiety z ocen właściciela jako otwarte pole (przykłady: zaakceptowane bez zmian / ze zmianami / odrzucone), zbierane offline; wymiary wyboru (przykład: koszt↔jakość; lista otwarta) w rękach osoby | P8 |
| interf-25 (modele generują zamiast pytać; >60 %) | brak wykrywania niejasności | tani klasyfikator niejasności → znaczniki w parafrazie; przy wielu niejasnościach dzielić pozycję spec na mniejsze węzły (próg — preset) | P8 |
| prac-25 (Max Mode z dopłatą) | tryb „szerzej” z etykietą kosztu | mamy „szerzej/głębiej” (K1c), brakuje etykiety kosztu obok — dodać (jądro ma szacunek PRZED wysyłką, nasze-28) | jądro, małe |
| interf-28, interf-27 (który komentarz prowadzi do zmiany; 58 % interwencji to prowadzenie) | metryka skuteczności recenzji | wskaźnik „uwaga → zmiana” jako kalibracja wiarygodności recenzenta (precedens `calibration.json`); konwencje z adnotacji → stałe reguły w danych | P3 |
| interf-17 (plan deklaruje zasięg) | lista plików przed zmianą | rozjazd zasięg deklarowany vs faktyczny jako advisory w karcie, nie blokada | P3 |

## §3 Oryginalne pomysły (z pryzmatem filozofii i tanim eksperymentem)

| # | Pomysł | Pryzmat filozofii | Pierwszy eksperyment i pomiar |
|---|---|---|---|
| O1 | **Korekta jako diff do spec, nie wiadomość.** Twoja uwaga (przycisk/tekst/głos) trafia do skrzynki; skryba liczy jej **cechy** (otwarta lista, przykłady: ile pozycji spec dotyka, czy sprzeczna z zatwierdzoną parafrazą, czy wprowadza słowo nieobecne w słowniku spec, czy dotyczy pieniędzy/danych/dostawcy, długość) i kieruje: zastosuj (tani model) / przeprojektuj (drogi) / zapytaj; progi to presety; rekord „skierowano do X, bo …” jest węzłem widocznym dla Ciebie z przyciskiem „jednak do Fable” | nic ukryte, decyzja o routingu jest danymi z odwrotem; R88 „żonglerka modelami w zależności od stopnia złożonościi [sic] nietypowości mojej korekty” | offline: 30 ostatnich Twoich korekt z `OWNER_REQUIREMENTS` sklasyfikować Haiku wg cech; porównać z tym, co koordynator faktycznie zrobił; miara: zgodność i koszt klasyfikacji (P8) |
| O2 | **Ustalenie jako węzeł grafu z odwrotem.** Rodzaj węzła `decision`: cytat, parafraza, stan, kto, data, koszt dotąd, pole `reversal`; odwrót = nowy węzeł `supersedes`, nie usunięcie (preset); usunięcie tylko jako jawna opcja osoby z podglądem i śladem; strona pokazuje obie wersje | KOD ≠ DANE; nic nie ginie; spec już jest danymi (487 węzłów, 1 090 cytatów — nasze-23) | zaimportować `spec.json` do grafu aplikacji (importer: nowy format), policzyć węzły = `spec.py check`; miara: czas do odpowiedzi „co czeka na mnie” w perspektywie vs na stronie (P7) |
| O3 | **Waluty na jednym paragonie** (np. tokeny i Twój czas czytania; lista otwarta — R83). Każda pozycja (karta, status, pytanie) niesie tokeny **i** szacunek Twojego czasu czytania (słowa × tempo — preset); koordynator widzi oba przed wysłaniem | R62: „mój czas … jest więcej wart”; paragon jądra (nasze-28) przeniesiony na naszą pracę | przez dobę logować długość linii statusu i pytań do Ciebie; miara: ile słów dostałeś do przeczytania na 1 pkt tygodnia; luka 12 (brak tej miary) zamknięta |
| O4 | **Kolejka „czeka na Ciebie” sortowana wartością informacji × stawką.** Pierwsze są decyzje, w których moja decyzja wstępna (R75) ma drogi odwrót; każda z najkrótszą odpowiedzią i etykietą „co się stanie, jeśli nic nie klikniesz” | interf-22 bez progów: koszt pytania i stawka jako dane; R75 (decyzja wstępna) widoczna, nie domyślna | parafrazy `proposed` (R67–R89) oznaczyć klasą kosztu odwrotu ręcznie; po Twoim pierwszym rzucie zmierzyć: ile pozycji odpowiedziałeś, w jakiej kolejności, ile przez skrzynkę vs czat (luka 7; P2) |
| O5 | **Pomiar „po” jako część każdej reguły.** Reguła w `CLAUDE.md` bez metryki i terminu pomiaru dostaje advisory; `usage_report.py` porównuje przed/po przy następnym biegu | „necessary, insufficient”: przykłady przechodzą, przestrzeń pokryta — tu: reguła wprowadzona, skutek zmierzony | uruchomić `usage_report.py` na transkryptach 22–24.10: udział przepisanych kontekstów po regule 4 min (baza 17,1 %), udział przeglądów (54 %), udział Fable (42,5 %) — luka 1 (P1/P5) |
| O6 | **Skryba kalibruje własną wiarygodność.** Każda jego decyzja (zastosował / eskalował / zapytał) dostaje później Twój werdykt (dobrze / źle); rozkład Beta per kategoria (jak `calibration.json`); gałka „jak często pytać” **proponuje** nową wartość, Ty ją przyjmujesz | pytanie „czy następnym razem nie pytać” z CLAUDE.md jako dane uczące; nie decydujemy za Ciebie — proponujemy | zapis decyzji sekretarza z wynikiem w `spec_inbox` (`state`, `verdict`); po 20 pozycjach policzyć trafność; koszt: zero dodatkowych tokenów (P1) |
| O7 | **Wspólny prefiks jako artefakt z hashem.** Stała część promptu każdego agenta (reguły, odsyłacze, narzędzia) jako plik z wersją; agent wpisuje hash do karty; trafienia cache liczone per hash; brief = hash + część własna | prac-02/06 jako dane, nie wiedza plemienna; różnica briefów od pierwszej linii (nasze-09) widoczna | w następnym workflow z ≥ 3 torami porównać sumę `cache_creation` agentów z N × prefiks; baza: 5 torów R89 (P4) |
| O8 | **Koszt na pozycję spec.** Złączenie `usage_report` (per workflow) z `spec:<id>` cytowanymi w briefach → „ile kosztowała pozycja do stanu approved/main”, widoczne w drzewie | koszt przyjętego wyniku (nasze-06/14) na jednostce, którą Ty czytasz; nic ukryte | 20 pakietów 21–22.10 z tabeli ledgera: ręczne złączenie id ↔ koszt; miara: rozrzut kosztu per pozycja (P3) |
| O9 | **Głos → cytat dosłowny** (później). Notatka głosowa z telefonu → transkrypcja (tani model/ASR, jak `providers.py`) → skryba zapisuje jako cytat spec z oznaczeniem „transkrypcja, nie tekst własny” | Twoje słowa dosłownie (R59) także wtedy, gdy nie piszesz; kanał jako preset | jedna notatka end-to-end: koszt, czas, błędy transkrypcji; dopiero potem decyzja o kanale |

## §4 Projekt dedykowanego interfejsu współpracy — abstrakcje, nie układ

### 4.1 Wspólne artefakty jako dane z proweniencją

| Artefakt | Dziś (2026-10-23) | Jako węzeł grafu (rodzaj — przykład) | Proweniencja (pola — lista otwarta) |
|---|---|---|---|
| pozycja spec | `docs/spec/spec.json` (487 węzłów), strona-artefakt | `requirement` | cytat `plik:linia`, parafraza, stan rozumienia z datą, kto, aliasy |
| dokument projektowy | `docs/architecture/*.md` | `design` z wersjami | autor (model), data, wynik kontroli Haiku, pozycje spec cytowane |
| schemat | packi, DDL, formaty kart | `schema` | wersja, od kiedy, testy |
| decyzja / ustalenie | INDEX.md (proza), `spec_inbox` | `decision` | cytat, kto, data, koszt odwrotu (klasa), `reversal`, `supersedes` |
| pomiar | `LIMITS_LEDGER.md`, karty | `measurement` | wartość, jednostka, metoda, data, wiarygodność, czego dotyczy |
| pakiet | brief JSON + karta | `package` | brief → `spec:<id>`, model, wywołania plan/faktyczne, koszt, stan, karta |
| źródło zewnętrzne | notatki torów | `source` | URL, data, typ, ocena, do czego użyte (R59) |

Krawędzie (przykłady): `cites`, `implements`, `supersedes`, `measured_by`, `waits_for` (osoba), `proposed_by`, `reversed_by`,
`routed_to`. Każdy węzeł ma „kto i kiedy” — tak samo jak dziś cytaty spec sprawdzane `check`.

### 4.2 Role jako presety i routing korekty jako dane

| Rola (nazwa — przykład) | Model (preset, datowany 23.10) | Wejście | Wyjście | Zmierzona baza kosztu |
|---|---|---|---|---|
| koordynator (przykład: Fable kieruje) | Fable | karty, eskalacje hurtem, kolejka, tempo | briefy, projekty, decyzje z odwrotem | ≈ 0,25 pkt na pakiet (20 tur × 0,28 mln; jeden pomiar, luka 5); projekt 1,2–1,6 pkt (nasze-06) |
| rozmówca (przykład: Sonnet rozmawia) | Sonnet | Twoja wiadomość + top-k spec | propozycja diffu spec + odpowiedź (długość — preset) | brak (nowa rola) |
| sekretarz / skryba | Haiku lub Sonnet | skrzynka, wycinek przebiegu | rekord `decision`, commit, eskalacja | **niezmierzony** (P1); analog: czytacz logu 0,07 pkt (nasze-06), ale 1,5 mln przy dużym wsadzie (nasze-16) |
| wykonawca pakietu | Sonnet | brief (`spec:<id>`), karty | kod, testy, karta | 0,43–0,5 pkt (nasze-06); 2–5× planu wywołań (nasze-07) |
| kontroler przed użyciem | Haiku | dokument, 4 antywzorce | lista znalezisk | 14–17 wywołań (nasze-24) |
| recenzent / sędzia | proporcjonalny do zmiany | diff, rubryka | ocena 0–1 + pass/fail (role-04) | pętla przeglądów 54 % (nasze-30) — do zmierzenia „po” |

Routing korekty właściciela (R88: „żonglerka modelami w zależności od stopnia złożonościi [sic] nietypowości mojej korekty”) to
**dane**: wektor cech korekty (O1, lista otwarta) → wynik → progi (presety) → rola. Rekord routingu jest węzłem `decision`
widocznym dla właściciela („zastosowane przez sekretarza, bo dotyka 1 pozycji i nie sprzecza z parafrazą”) z przyciskiem odwrotu.
Przypisanie rola → model jest rekordem z datą i terminem odświeżenia (role-06: ta sama rola Explore na Haiku w 2025, na modelu
głównym w 2026).

### 4.3 Kanały i graf aplikacji jako węzeł centralny

| Kanał | Co niesie | Dziś (23.10) | Dalej |
|---|---|---|---|
| strona na telefonie | drzewo spec, kolejka „czeka na Ciebie”, przyciski, koszt | artefakt z bazą (`spec_inbox`), R88 v1, żadna pozycja jeszcze nie przeszła (nasze-19) | ta sama strona generowana z grafu aplikacji (perspektywa), nie z `spec.json` |
| czat sesji | dyskusja, linie statusu | sesja S1 (Fable) | rozmówca na tańszym modelu w osobnej sesji (nasze-21: dwie sesje działają od 13.10); eskalacja do S1 wiadomością |
| głos | notatka → cytat dosłowny | brak | O9, po pomiarze |
| graf aplikacji | wszystko powyżej jako węzły | węzły/krawędzie, importer, perspektywy (M1), propozycje (W2b-1), szacunek/paragon (nasze-28), router (nasze-26) | hub |

**Jak interfejs może BYĆ widokiem grafu aplikacji, nie osobnym produktem:** (1) importer dostaje format `spec.json` (jak każdy
inny: wykrywanie + handler → `_import_message_list`-odpowiednik dla węzłów) i tworzy węzły `requirement`/`decision`/`measurement`
z krawędziami; (2) „współpraca” to **perspektywa**: filtr „czeka na mnie” / „zmienione od mojej ostatniej wizyty” / „kosztowało
najwięcej”, z tym samym mechanizmem zwijania i podświetlenia, co R73; (3) przyciski (przykłady akcji: „Zatwierdzam / Popraw / Uwaga”; zestaw otwarty, R84) to **akcje
grafu** = propozycje (W2b-1 ma już „Popraw/Zapomnij przez propozycje”): zapis dosłowny, stan, odwrót; (4) koszt przy pozycji to
ten sam paragon, który jądro liczy dla wiadomości (nasze-28), tylko z innej księgi (`usage_report` / `rate_limit_event`);
(5) dzisiejsza strona-artefakt (`tools/spec_html.py`) jest cienkim rendererem tych samych danych — zostaje, dopóki serwer
aplikacji nie jest osiągalny z jego telefonu (datowana obserwacja 23.10: `loom-server` biegnie w kontenerze chmury i na VM, bez
publicznego adresu; artefakt jest osiągalny). **Poza grafem zostają dziś (obserwacja 23.10; ponowna ocena, gdy zmieni się dostęp do kontekstu sesji, telemetrii konta albo hosting):** kontekst sesji Claude (niewidoczny, nieprzenośny), limity
konta (telemetria tylko z sesji, nasze-11), git jako prawda o kodzie, cenniki dostawców (datowane dane, nie nasze), mechanizm
publikacji artefaktu.

### 4.4 Co widzi właściciel (przykłady zawartości; układ — preset)

- **Żywe drzewo** (R73): zwijane, zmienione ostatnio i edytowane rozwinięte; znaczniki niejasności w parafrazach (interf-09).
- **Czeka na Ciebie**: pozycje sortowane „blokujące / drogi odwrót najpierw” (O4), każda z najkrótszą odpowiedzią (np. cyfra, przycisk,
  „własnymi słowami” — lista otwarta), etykietą „co zrobię, jeśli nic nie klikniesz, i kiedy” (R75, czas — otwarte pole) i uzasadnieniem pytania.
- **Co się zmieniło** od ostatniej wizyty (znacznik wizyty per przeglądarka — wygoda; decyzje — w bazie strony).
- **Koszt dotąd**: tydzień (próbka `rate_limit_event` z datą), per pakiet, per pozycja spec (O8); waluty wg O3 (tokeny, czas czytania, …).
- **Advisories**: nieblokujące („to wygląda na mało użyteczne, bo … — celowe? nie pytaj następnym razem”).

### 4.5 Co widzi koordynator

Tylko dane: eskalacje ze skrzynki (hurtem, rzadko — prac-15), karty pakietów, tempo (`pace.py`), kolejka. Domyślnie (preset) **nie** czyta transkryptu
rozmowy rozmówcy z właścicielem — dostaje rekord skryby (ustalenia / otwarte / odrzucone), chyba że eskalacja; pełny transkrypt jako opcja z etykietą kosztu, zapisana w `input_filter`; filtr przekazania to
dane (role-09 `input_filter` jawnie, nie cicho).

### 4.6 Droga korekty i odwrót decyzji

```
właściciel (przycisk / tekst / głos / … — kanały to przykłady)
  → skrzynka (spec_inbox: dosłownie, data, kto)
  → sekretarz/skryba (świeża sesja, tani model): cechy korekty → zastosuj | eskaluj | zapytaj; rekord decision
  → spec (nowa wersja pozycji; stan rozumienia corrected; cytat; nic nie nadpisane)
  → pakiety: brief cytuje spec:<id>; biegnący agent domyślnie nie jest restartowany (preset; wyjątek jak CLAUDE.md, kontrola marnotrawstwa 6c: pierwsze minuty i korekta zmieniająca cały kierunek; nasze-08: trzy restarty ≈ 0,9 pkt łącznie) — korekta = addendum po karcie
  → karta → odświeżenie strony → „co się zmieniło”
```
Odwrót decyzji: każdy węzeł `decision` ma `reversal` (co zrobić, koszt — klasa); odwrót = nowy węzeł `supersedes` wskazujący
stary; oba widoczne; pakiet dostaje addendum, nie restart; `understanding` wraca do `proposed` z notą. Odwrót nie usuwa (preset: `supersedes` + archiwizacja); usunięcie to jawna, świadoma opcja osoby z podglądem i śladem; nic nie znika po cichu.

### 4.7 Budżet tokenów per rola (bazy zmierzone, presety do kalibracji)

| Rola | Baza (pomiar, data) | Preset budżetu na jednostkę | Jednostka | Jak mierzyć „po” |
|---|---|---|---|---|
| wykonawca | 0,43–0,5 pkt / pakiet; 2–5× planu wywołań (21–22.10) | 50 wywołań plan; karta przy przekroczeniu | pakiet | `calls_used/plan_calls` w karcie (P3) |
| projekt Fable | 1,2–1,6 pkt (22–23.10) | 1,5 pkt | dokument | `usage_report` per workflow |
| koordynator | ≈ 0,25 pkt / pakiet (1 pomiar) | — (mierzyć) | pakiet | tury koordynatora na zdarzenie (luka 5) |
| sekretarz | brak | — (mierzyć) | obudzenie | P1: koszt pustego i niepustego biegu |
| kontroler Haiku | 14–17 wywołań | 20 | dokument | karta |
| czytacz logu | 1,5 mln przy pełnym logu (S2, 21.10) | wycinek logu | bieg CI | P9 |

## §5 Pakiety (kolejność — preset; budżet ~50 wywołań każdy — preset; akceptacja + pomiar)

| # | Pakiet | Zakres | Akceptacja | Pomiar, który dowodzi oszczędności |
|---|---|---|---|---|
| P1 | **Sekretarz: rekord biegu i pomiar** (najmniejszy krok po R88 v1) | prompt zadania `Sekretarz spec` dopisuje po każdym biegu wiersz do `docs/reports/sekretarz_runs.jsonl` (czas, pozycje new/applied/escalated, komendy, id sesji); koordynator liczy koszt biegu `usage_report` po id; próbka `rate_limit_event` dopisywana tym samym biegiem; pole `verdict` w `spec_inbox` (O6) | 3 kolejne biegi zapisane; jeden bieg z niepustą skrzynką | koszt biegu pustego vs niepustego (luka 6) → preset częstości (co godzinę to przykład); udział Fable/przeglądów „po” z `usage_report` 22–24.10 (luka 1) |
| P2 | **Kolejka „czeka na Ciebie” na stronie** | `spec_html.py`: sekcja na górze, sortowanie (preset: drogi odwrót najpierw), najkrótsza odpowiedź, „co zrobię bez odpowiedzi i kiedy” (R75, pole czasu), znaczniki niejasności w parafrazach, „zmienione od ostatniej wizyty” (localStorage w try/catch) | render na szerokości telefonu; e2e: klik zapisuje wpis; lista odpowiedzi otwarta (opcje + własne słowa) | liczba decyzji przez skrzynkę vs czat w 48 h (luka 7); liczba słów do przeczytania na decyzję (O3) |
| P3 | **Brief z hipotezą + metryka wywołań** | `single-package.js`: pola `hypothesis`, `repro_plan`, `plan_calls`; karta: `calls_used`, `scope_declared/actual` (interf-17); `tools/pace.py` czyta karty → koszt na pozycję spec (O8) | następne 5 pakietów mają metrykę; zero zmian w zachowaniu agenta poza briefem | rozkład `calls_used/plan_calls` vs baza 2–5× (nasze-07); koszt per `spec:<id>` dla 20 pakietów 21–22.10 |
| P4 | **Prefiks wspólny → własny + pomiar salwy** | skrypty workflow emitują część wspólną bajt w bajt (hash w karcie, O7), część własną na końcu; `parallel()` ze staggerem jako preset (domyślnie 0) | brief dwóch agentów równoległych ma identyczny prefiks (diff = 0 do znacznika) | suma `cache_creation` N agentów vs N × prefiks; baza: 5 torów R89 (nasze-09, luka 4) |
| P5 | **Karta wznowienia wg szablonu + kompakt przed pauzą** | szablon karty (prac-21 + pliki jako ścieżki, słowa właściciela dosłownie); próg długości operacji jako preset w danych (~4 min) z zapisem „kompakt/karta przed operacją dłuższą niż próg” w `DISCIPLINE`; przekroczenie → karta i advisory, nie zakaz; ślad kompaktowania czytelny (co ucięto) | 3 karty wg szablonu; agent kontynuacji startuje z karty bez pytań | udział kontekstów przepisanych po pauzie (baza 17,1 %, nasze-02) z `usage_report` |
| P6 | **Skryba jako akcja przepływu w jądrze** | pack: akcja `scribe` (model preset, wycinek wejścia, filtr jako dane) → węzeł `decision` (pola np. ustalenia, otwarte, odrzucone, koszt, źródło, `reversal` — lista otwarta jak w §4.1); testy jednostkowe + 1 e2e | rekord powstaje z rozmowy testowej; odwrót tworzy `supersedes`; nic nie usunięte | tokeny rekordu vs tokeny wycinka (baza PACT −50 % to ich liczba, nie nasza) |
| P7 | **Spec jako węzły grafu aplikacji** | importer: format `spec.json` → `requirement`/`decision`/`measurement` + krawędzie; perspektywa „czeka na mnie” / „zmienione” | liczba zaimportowanych węzłów = liczba z `spec.py check` w dniu testu (23.10: 487); perspektywa renderuje się w web UI | czas do „co czeka na mnie” vs strona; liczba węzłów/krawędzi |
| P8 | **Router korekt jako dane** | cechy korekty (O1) + progi presety + rekord routingu widoczny z odwrotem; klasyfikacja na tanim modelu; ocena offline | 30 korekt z `OWNER_REQUIREMENTS` sklasyfikowane; zgodność z faktycznym postępowaniem raportowana | zgodność, koszt klasyfikacji na korektę; kalibracja progu z werdyktów (O6) |
| P9 | **Filtr wyjścia narzędzi (wrapper)** | `tools/workflows`: wrapper build/test/CI → wycinek wg reguł (dane, per komenda), pełny log na dysku, „pokaż całość” | 3 komendy objęte; log pełny dostępny; wycinek zawiera blok REPORT i błędy | tokeny czytaczy logów przed/po (baza 1,5 mln/agent S2, nasze-16) |
| P10 | **Profil ekonomii: stawki z datą i źródłem, usage dostawcy, cost-of-pass** | jądro (P38 slice 4): stawki per model jako otwarty zbiór kategorii z datą i źródłem (przykłady: wejście, zapis, odczyt, wyjście), `cache_write` ≠ null, `usage` dostawcy jako prawda, advisory „cache nie zadziałał”, koszt przyjętego wyniku | testy; estymata nazywa bazę; cena nieznana zostaje nieznana | różnica estymata vs koszt dostawcy na 20 wywołaniach |
| P11 | **Ping cache jako opcja przepływu** (opcjonalny) | `max_tokens: 0` (rozgrzewanie cache — prac-03, 23.10; ping — prac-19) co < TTL, gdy P(powrót) × zapis > ping (rachunek jawny) | opcja wyłączona domyślnie; włączenie zapisane | zapisy cache uniknięte vs koszt pingów |

Kolejność to preset: P1–P5 są tanie (dokumenty, skrypty, strona), P6–P8 dotykają jądra, P9–P11 ekonomii. Każdy pakiet kończy się
kartą z liczbami; pakiet bez pomiaru „po” dostaje advisory w karcie (O5; preset: dla pakietów ekonomii P9–P11 pomiar wymagany — konfigurowalne), przyjęcie nie jest blokowane.

## §6 Decyzje z odwrotem

| Decyzja (koordynator, 23.10) | Dlaczego | Odwrót |
|---|---|---|
| Interfejs współpracy = perspektywa grafu aplikacji + cienki renderer na telefon, nie osobny produkt | KOD ≠ DANE; graf już ma importer, perspektywy, propozycje, paragon | strona-artefakt zostaje samodzielnym rendererem `spec.json` (działa dziś); osobny dedykowany interfejs jako opcja (koszt: własny model danych i synchronizacja z grafem) |
| Skryba/sekretarz jako akcja przepływu w danych (model, prompt, filtr — presety), nie jako stała rola w kodzie | filozofia: role to presety w danych (obserwacja 23.10, role-25 — synteza bez pomiaru: w przeczytanych źródłach brak nazwanej roli; fakt o teraźniejszości, nie argument) | zadanie zaplanowane R88 v1 bez jądra (działa dziś) |
| Decyzja = węzeł z `reversal` i `supersedes`; nic nie jest usuwane | nic ukryte; R75 (decyzja wstępna, tania do wycofania) | usunięcie jako jawna opcja osoby z podglądem i śladem (preset: archiwizacja jako perspektywa) |
| Czas oczekiwania na odpowiedź właściciela to otwarte pole z presetem per klasa decyzji, nie lista wartości | R83/R84: liczba w liście to nie lista; interf-02 wartości producenta to przykłady | preset „nigdy nie kontynuuj bez odpowiedzi” dla klasy |
| Routing korekty po cechach z rekordem widocznym; progi presety; przycisk „jednak do Fable” | R88 cytat o żonglerce; interf-06 klasyfikator tylko z dziennikiem | próg = 0 → wszystko do koordynatora (dzisiejszy stan) |
| Kompresja/czyszczenie kontekstu domyślnie jawne, odwracalne, z podglądem usuniętego (preset); tryb bez potwierdzania jako opcja z advisory i obowiązkowym śladem (co, ile, dlaczego) | konflikt z filozofią w nauk-01..03, prac-16, prac-22 (cicha kompresja) | opcja wyłączona = brak kompresji poza producenta |
| Każda reguła dostaje metrykę i pomiar „po” (O5); pakiety P1–P5 najpierw | luka 1: żadna reguła z 21.10 nie ma pomiaru „po” | kolejność pakietów to preset w `QUEUE_S1.md` |
| Limit 5 pytań, 200 linii CLAUDE.md, 30 s, 25 agentów, stagger 5 s — zapisane jako presety/przykłady, nigdy jako reguły | R58, prac-10 konflikt jako reguła | gdy rzecz sama wymusza wartość stałą (R84): zapis z powodem i datą |
| Not_for_us (gist, RadixAttention, TokenDance, Swarm) = datowane obserwacje, nie zamknięte drzwi | R60: „brak czegoś lub kogoś teraz nie oznacza że będzie tak zawsze” | ponowna ocena przy własnym hostingu |
| Liczby z sieci (−65 %, −50 %, ×15, 1 310:1) cytowane z oceną i nigdy jako nasza oszczędność; nasze oszczędności tylko z naszych pomiarów | R59 | własny pomiar potwierdzający liczbę → liczba staje się naszą, z datą |

## §7 Ledger źródeł (R59) — to, czego użyłem

Dostęp do wszystkich: 2026-10-23 (tory R89). „Data” = publikacja, gdy znana, inaczej dostęp. Ocena = ocena toru.

| Id | Źródło | Data | Typ | Ocena | Użyte do |
|---|---|---|---|---|---|
| nasze-01…30 | `docs/reports/LIMITS_LEDGER.md`, `INDEX.md`, `QUEUE_S1.md`, `EKONOMIA_TOKENOW_2026-10-16.md`, `tools/usage_report.py`, `tools/pace*.json`, karty agentów | 2026-10-10…23 | pomiar własny | high/medium (wagi równoważników założone; wyjście agentów = dolna granica; nasze-08…30: ocena przez syntezę, bez etapu oceny — L14) | wszystkie liczby zlewów i baz (§0, §1, §4.7, §5) |
| prac-01, -02, -03, -04 | https://platform.claude.com/docs/en/build-with-claude/prompt-caching | 2026-10-23 | dokumentacja oficjalna | high | stawki cache, hierarchia unieważniania, TTL, minima (§1, P10, P11) |
| prac-05, -08, -10, -11, -12, -15 | https://code.claude.com/docs/en/costs ; …/best-practices ; …/model-config | 2026-10-23 | dokumentacja oficjalna | high | koszt odniesienia, higiena kontekstu, CLAUDE.md, effort, filtr narzędzi, pełny kontekst przy obudzeniu |
| prac-06, -07, -09 | https://code.claude.com/docs/en/prompt-caching ; …/context-window | 2026-10-23 | dokumentacja oficjalna | high | warstwy zapytania, /compact, /rewind, co przeżywa kompaktowanie |
| prac-13, -14 | https://code.claude.com/docs/en/best-practices ; https://code.claude.com/docs/en/workflows | 2026-10-23 | dokumentacja oficjalna | high | tryb bezgłowy, fan-out, stagger (§1, §2, P4) |
| prac-16 | https://www.anthropic.com/engineering/effective-context-engineering-for-ai-agents | 2025-09-29 | blog producenta | high | kompaktowanie, notatki, 1–2k tokenów podagenta |
| prac-17, role-02, -03, -04, nauk-19 | https://www.anthropic.com/engineering/multi-agent-research-system | 2025-06-13 | blog producenta (ewaluacja wewnętrzna) | high (nauk: średnia) | ×4/×15, reguły wysiłku, sędzia, tryby awarii |
| prac-18 | https://github.com/anthropics/claude-code/issues/24147 | 2026-02-08 | zgłoszenie użytkownika z metodą | medium | 1 310:1 jako metoda do powtórzenia, nie liczba |
| prac-19, -20 | https://raw.githubusercontent.com/Aider-AI/aider/main/aider/website/docs/usage/caching.md ; …/repomap.md | 2026-10-23 | dokumentacja projektu | high | ping cache, mapa repo (§2, P11, K3) |
| prac-21 | https://gist.github.com/badlogic/cd2ef65b0697c4dbe2d13fbecb0a0a5f ; https://developers.openai.com/codex/config-reference | 2025-12-02 | gist + docs | medium | szablon karty wznowienia (P5) |
| prac-22 | https://docs.cline.bot/getting-started/understanding-context-management ; …/features/checkpoints | 2026-10-23 | dokumentacja projektu (nieprzeczytana w całości) | medium | checkpointy, przekazanie; konflikt cichej kompresji (§6) |
| prac-23 | https://agentic-ai.readthedocs.io/en/latest/ContextEngineering/manus/ | 2026-10-23 | wtórne streszczenie wpisu Manus | medium | KV hit rate, brak znaczników czasu w prefiksie |
| prac-24 | https://ghuntley.com/ralph/ | 2026-10-23 | blog | low | świeży kontekst per iteracja — eksperyment, nie reguła |
| prac-25 | https://docs.cursor.com/context/max-mode | 2026-10-23 | dokumentacja (nieprzeczytana u źródła) | medium | etykieta kosztu obok trybu |
| prac-26 | https://docs.devin.ai/pt-BR/use-cases/gallery/analyze-session-acu-efficiency | 2026-10-23 | dokumentacja | medium | jednostka rozliczenia jako dane; koszt eksploracji |
| nauk-01, -02, -03 | https://arxiv.org/abs/2310.05736 ; https://arxiv.org/abs/2310.06839 ; https://arxiv.org/abs/2310.06201 | 2023 | academic (EMNLP/ACL) | wysoka | kompresja — tylko jawna (§2, §6) |
| nauk-04, -10, -11 | NeurIPS 2023 (gist tokens); https://arxiv.org/abs/2312.07104 ; https://arxiv.org/abs/2604.03143 | 2023–2026 | academic | wysoka / średnia | not_for_us jako datowane obserwacje (§6) |
| nauk-05, -06, -07, -08, -09 | https://arxiv.org/abs/2310.08560 ; https://arxiv.org/abs/2502.12110 ; https://arxiv.org/abs/2304.03442 ; NeurIPS 2023 (Reflexion); https://arxiv.org/abs/2606.04315 | 2023–2026 | academic (preprinty, UIST, NeurIPS) | średnio-wysoka / wysoka / średnia | pamięć agentów: A-Mem, wagi, refleksja, AutoMEM (§1, §2, K3) |
| nauk-12, -13, -14 | https://arxiv.org/abs/2307.03172 ; https://aclanthology.org/2024.emnlp-industry.66/ ; https://research.trychroma.com/context-rot | 2023–2025 | academic (TACL, EMNLP) / raport dostawcy | wysoka / wysoka / średnia | kolejność bloków, Self-Route, Context Rot |
| nauk-15, -16, -17, -18, role-16, -17 | https://arxiv.org/abs/2305.05176 ; https://arxiv.org/abs/2404.14618 ; https://arxiv.org/abs/2406.18665 ; https://arxiv.org/abs/2310.03094 ; https://raw.githubusercontent.com/lm-sys/RouteLLM/main/README.md | 2023–2024 | academic (ICLR, preprinty, kod) | wysoka / średnio-wysoka | kaskady i routery jako dane z datowanym progiem (§2, P8) |
| nauk-20, -21, -22, -23, -24, -25, -26 | https://arxiv.org/abs/2512.08296 ; https://arxiv.org/abs/2508.02694 ; https://arxiv.org/abs/2503.13657 ; https://arxiv.org/abs/2606.05304 ; https://arxiv.org/abs/2510.00615 ; https://arxiv.org/abs/2608.06503 ; https://mlanthology.org/iclrw/2025/wang2025iclrw-agenttaxo | 2025–2026 | academic (preprinty, warsztat) | średnia / średnio-wysoka / nisko-średnia | skalowanie agentów, cost-of-pass, MAST, PACT, ACON, TRACE, podatek komunikacyjny |
| interf-01, -02, -03, -04, -05, -06, -07 | https://code.claude.com/docs/en/permission-modes ; …/tools-reference ; …/best-practices ; …/agent-view | 2026-10-23 | dokumentacja oficjalna | high | Plan Mode, AskUserQuestion, wywiad → spec, weryfikacja, zgody, kolejka decyzji (§2, P2) |
| interf-08, -09 | https://raw.githubusercontent.com/github/spec-kit/main/templates/commands/clarify.md ; …/spec-driven.md | 2026-10-23 | repozytorium projektu | high | Q→A do spec, znaczniki niejasności; konflikt ukrytej kolejki i limitu 5 (§2, §6) |
| interf-10 | https://kiro.dev/blog/from-chat-to-specs-deep-dive/ | 2026-08-25 | blog producenta | medium | fazy spec jako presety |
| interf-11, -15 | https://docs.github.com/en/copilot/how-tos/use-copilot-agents/cloud-agent/review-copilot-prs ; https://raw.githubusercontent.com/agentsmd/agents.md/main/README.md | 2026 | dokumentacja / repozytorium | medium / high | tryby pytam/zmieniaj; sesja jako jednostka kosztu |
| interf-12, -13 | https://cursor.com/es/blog/plan-mode ; https://forum.cursor.com/t/models-ignore-manual-plan-edits/139129 | 2026-10-23 | blog producenta / forum | medium / low | sugestia trybu; edycja planu przez osobę wygrywa |
| interf-14 | https://docs.devin.ai/work-with-devin/interactive-planning | 2026-10-23 | dokumentacja | medium | 30 s jako preset z zapisem `proposed` |
| interf-16, -17, -18 | https://linear.app/developers/agent-interaction ; https://www.atlassian.com/blog/announcements/rovo-dev-in-jira ; https://www.notion.com/releases/2026-08-28 | 2026 | dokumentacja / blogi producentów | medium | delegat, `awaitingInput`, zasięg planu, propozycje zmian |
| interf-19, -20 | https://lovable.dev/faq/ai-agent/errors/wasted-credits-bugs ; https://survey.stackoverflow.co/2025/ai | 2025–2026 | FAQ / ankieta | medium | licznik powtórzeń, „prawie dobre” |
| interf-21 | https://huggingface.co/papers/2507.09089 (METR RCT) | 2025-07-12 | preprint z RCT | high (mała próba) | poczucie ≠ pomiar |
| interf-22 | Horvitz 1999, Fleming 2001, Dong 2026 (Consensus: https://consensus.app/papers/details/46927d48d0cc5857b4138cb2a884487c/ i nn.) | 1999–2026 | academic (abstrakty) | high (domeny inne — analogia) | wartość informacji jako podstawa gałki pytania (§2, O4) |
| interf-23, -24, -25, -26 | CIKM 2021 (https://arxiv.org/pdf/2109.05955); TiCoder TSE 2024; HumanEvalComm TOSEM 2024 (https://huggingface.co/papers/2406.00215); SWE-Together / CodeChat (Consensus) | 2021–2025 | academic | high / medium | kiedy pytać, testy jako doprecyzowanie, wykrywanie niejasności, tury korygujące |
| interf-27, -28, -29 | MSR 2026, TSE 2025, DIS 2026 (Consensus) | 2022–2026 | academic (abstrakty) | high | interwencje w PR, komentarz skuteczny, inicjatywa mieszana |
| role-01 | https://www.anthropic.com/engineering/building-effective-agents | 2026-10-23 | blog producenta | high | słownik wzorców przepływów |
| role-05, -06, -07, -08 | https://code.claude.com/docs/en/sub-agents ; https://code.claude.com/docs/en/agent-sdk/subagents ; https://simonwillison.net/2025/Oct/15/catherine-wu/ | 2025–2026 | dokumentacja / wpis | high / low (role-06 wpis) | rola = pola danych; model per rola jako datowany rekord; budżet bez limitu |
| role-09, -10 | https://raw.githubusercontent.com/openai/openai-agents-python/main/docs/handoffs.md ; https://raw.githubusercontent.com/openai/swarm/main/README.md | 2026-10-23 | repozytoria | high | filtr przekazania jawny; Swarm historyczny |
| role-11, -12 | https://raw.githubusercontent.com/microsoft/autogen/0.2/website/docs/tutorial/conversation-patterns.ipynb ; …/notebook/agentchat_cost_token_tracking.ipynb | 2026-10-23 | repozytorium (wersja 0.2, historyczna) | high | skryba jako osobne wywołanie; dwie księgi kosztu |
| role-13, -14, -15 | https://raw.githubusercontent.com/crewAIInc/crewAI/main/docs/edge/en/learn/hierarchical-process.mdx ; https://raw.githubusercontent.com/geekan/MetaGPT/main/README.md (ICLR 2024) | 2024–2026 | repozytoria / academic | high | tokeny menedżera w sumie; role jako dokumenty; 2× mniej na linię |
| role-18 | Zheng i in. 2023 (LLM-as-judge, NeurIPS) | 2023 | academic | high | zabezpieczenia sędziego jako presety |
| role-19, -20, -21, -22, -23 | https://github.com/OpenRouterTeam/typescript-sdk ; https://platform.claude.com/docs/en/manage-claude/usage-cost-api ; Langfuse/LangSmith/Helicone docs (raw GitHub) | 2026-10-23 | repozytoria / dokumentacja | high | pola usage, cenniki z datą, etykiety atrybucji (P10) |
| role-24 | https://getunblocked.com/blog/cost-per-merged-pr/ | 2026-10-23 | blog (nieprzeczytany, streszczenie) | low | definicja metryki na wynik; liczby nieużyte |
| role-25 | synteza toru „role” | 2026-10-23 | synteza źródeł pierwotnych | high jako synteza; brak pomiaru | skryba jako akcja przepływu (§2, P6) |
| R88, R89, R62, R63, R73, R75 | `python3 tools/spec.py show …` → `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` | 2026-10-23 | słowa właściciela (cytaty sprawdzone `check`) | high | zakres, „mój czas … jest więcej wart” (R62), „nie bierz tego schematu dosłownie” (R88), „wolna ręka” (R89) |

## §8 Luki (pomiary do zrobienia, nie problemy)

| # | Luka | Dlaczego ważna | Kto zamyka |
|---|---|---|---|
| L1 | Waga odczytu z cache w limicie abonamentu (5 h / tydzień) — nieznana; w USD 0,1×/0,025× | cała ekonomia „oszczędzamy odczyty” to hipoteza w punktach | kolejne próbki `rate_limit_event` przy agentach o znanym podziale usage (P1) |
| L2 | Mnożnik Fable w limicie; koszt pakietu 50 wywołań na Haiku (`"haiku": null`) | routing pakietów po koszcie | P1 + jeden pakiet Haiku |
| L3 | Zmierzona oszczędność skryby/sekretarza — brak w jakimkolwiek źródle | podstawa P6 | P1, P6 |
| L4 | Weryfikacja modelu „restart co N ruchów z kartą” na pełnym przebiegu | model 35–50 % to model | P3, P5 |
| L5 | Czy świeże sesje z zadań zaplanowanych dzielą cache promptu w chmurze | koszt sekretarza co godzinę | P1 (porównać `cache_creation` kolejnych biegów) |
| L6 | Ile tur właściciela oszczędza plan/pytanie z opcjami/clarify — nikt nie mierzy | wartość P2 | P2 (liczba decyzji skrzynką vs czat) |
| L7 | Czy ręczna edycja planu/parafrazy dociera do wykonawcy — tylko anegdoty | interf-13 | P6/P7 (test: edycja → brief czyta nową wersję) |
| L8 | Interfejs decyzji na telefonie — żadne źródło | właściciel czyta w rzutach | P2 (e2e na szerokości telefonu) |
| L9 | Google Docs / Gemini w trybie sugestii z agentami — stan 23.10: brak wiarygodnego źródła (obserwacja; ocena: brak) | kanał alternatywny; zostaje opcją | ponowna ocena przy następnym badaniu kanałów albo na życzenie (R60) |
| L10 | Effort medium vs high w tokenach myślenia — producent nie podaje | gałka per etap | pomiar na 2 pakietach tej samej klasy |
| L11 | Dwie daty resetu tygodnia (27.10 08:00 vs 00:00 UTC) | plan tempa | następna próbka |
| L12 | Waluta czasu właściciela (słowa przeczytane) — brak miary | R62 | O3 w P2 |
| L13 | Pomiar „po” żadnej reguły z 21.10 (4 min, jeden recenzent, poprawki na Sonnecie, czytacze logów) | czy reguły działają | P1/P5 (`usage_report` 22–24.10) |
| L14 | Lista ustaleń oceny ucięta po nasze-07 — nasze-08…30 ocenione tu bezpośrednio przez syntezę (tabele wyżej), bez etapu oceny | spójność | ten dokument; kontrola Haiku po nim |

## §9 Kontrola przed użyciem (chatadhd, 2026-10-23)

Kontrola Haiku (4 antywzorce + pytanie R60) dała 36 znalezisk (13 blokujących, 23 słowne). Każde sprawdzone w tekście i w źródłach
(`tools/spec.py show R60/R62/R83/R84/R88/R89`, `docs/research/TOKENY_2026-10-23/nasze.md` nasze-08, `prac.md` prac-03); poprawki
jako addendum Sonnet, bez zmiany treści decyzji. **Zastosowane: 36** (34 w całości, 2 częściowo). Rodzaje poprawek: liczba
przestawiona jako reguła → preset (budżet ~50 wywołań, próg ~4 min, 1 recenzent, 1–2 tys. tokenów, tańszy model dla poprawek,
brak restartu z wyjątkiem 6c z CLAUDE.md); listy zamknięte → przykłady z listą otwartą (waluty paragonu — wcześniej „dwie”,
przyciski, kanały korekty, etykiety ocen, wymiary wyboru, czynniki pamięci, cechy zadania, pola rekordu skryby, kategorie
stawek — wcześniej „cztery”); usunięcie i tryb cichej kompresji przywrócone jako jawne opcje z advisory i śladem (domyślne bez
zmian); fakty o teraźniejszości z datą i warunkiem ponownej oceny (granica grafu, L9, role-25, 487 węzłów, 16 pkt/dobę);
cytaty właściciela dosłownie (R60 po polsku, R88 „nie bierz tego schematu dosłownie”, R62 „jest więcej wart”, typo „złożonościi”
oznaczone [sic]); błędna liczba: 0,9 pkt to koszt trzech restartów łącznie (nasze-08), nie jednego; R58 → R83 przy „liczba w liście
to nie lista”; brakujące odwroty w §6 (3 wiersze); miara kosztu, pomiar „po” i lektura transkryptu jako presety z dostępną
alternatywą; §1 i §7 z notą, że oceny nasze-08…30 pochodzą z syntezy (L14).

**Częściowo odrzucone (2):**
- w. 37 — zarzut sprzeczności z wierszem o kolejności bloków (nauk-12) odrzucony: hierarchia cache dostawcy (narzędzia → system →
  wiadomości) i kolejność treści w prompcie to różne osie; zastosowano tylko oznaczenia (fakt datowany, preset).
- w. 201 — zarzut braku źródła w §7 odrzucony: `max_tokens: 0` ma źródło prac-03 (wiersz prac-01…04 w §7); dopisano cytowanie
  w wierszu P11.

**Poza A–F, bez zmiany (uwaga kontrolera):** §0 pkt 2 i §5 planują pakiety bez zatwierdzonej parafrazy (R61). Parafrazy R88/R89 są
`proposed`; właściciel dał „wolną rękę” (R89) i działanie bez czekania w czasie nieobecności (R62) — kolejka P1–P5 zostaje
propozycją do jego przeglądu, nie jest tu zmieniana.
