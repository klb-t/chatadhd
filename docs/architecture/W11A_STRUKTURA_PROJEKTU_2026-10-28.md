# W11a — okno „Struktura projektu” (`project_structure`) — projekt

Data: 2026-10-28. Pakiet W11a (OKNA_SYSTEM §9, wiersz W11). Dokument projektowy, tylko odczyt kodu
(`wt/w2b2`, HEAD `1bcc4c02`). Stan: **propozycja do sprawdzenia przed użyciem** (kontrola 4 antywzorców, CLAUDE.md).
Zasada: wszystko konfigurowalne, listy to przykłady, wartości domyślne to presety.

## 0. Streszczenie dla właściciela

- Okno „Struktura projektu” to **drugi widok tych samych danych grafu**, nie nowy magazyn: drzewo wokół wybranego węzła
  (takiego jak projekt, jednostka, import), podobne do tego, co dziś pokazuje okno „Graf” (obserwacja 2026-10-28: rodzaj `navigator`), ale z nałożoną „siatką oczekiwań”.
- „Przyciemnione węzły do uzupełnienia” to miejsca, w których według **wzorca (danych, nie kodu)** coś powinno być, a
  jeszcze nie ma — albo jest tylko wywnioskowane. Rysuje je się tak jak dziś wpisy wywnioskowane na ekranie „Ty”
  (obserwacja 2026-10-28: ustawienie `me_inferred_opacity`, preset), ale sposób oznaczenia jest jednym z wielu możliwych i da się go zmienić.
- Wzorców jest otwarta lista (dane); w kodzie nie ma ich na sztywno. Przykłady (jedne z wielu): jednostka → rozmowy, dokumenty, decyzje;
  import → rozmowy, załączniki. Stan 2026-10-28: w `loom/data/project_kinds/` leży pięć plików rodzajów projektów (takich jak
  aplikacja, film, muzyka) i mogą być jednym ze źródeł wzorców.
- „Uzupełnienie” uruchamia akcję z katalogu (np. `conversation.set_unit`, `units.define`), domyślnie z potwierdzeniem (preset w danych; tryb zmienia się w ustawieniu).
- Decyzje D1–D7 w §5 to propozycje, sprawdzone 2026-10-28 (sekcja na końcu).
- Parametr okna: `params.root` (adres węzła-korzenia); jeśli pusty, okno pyta, co pokazać.
- Dwa pakiety: W11a-1 (silnik wzorców + odczyt, bez nowego UI poza oknem), W11a-2 (uzupełnianie i ustawienia).
- Pytań do Ciebie: **zero** (w §6 uzasadnienie).

## 1. Co mówią słowa właściciela, a co jest moim odczytem

**Źródło wtórne UI12** (relacja agenta, nie słowa właściciela; `SLOWA_WLASCICIELA_2026-10-16.md:1479-1480`, `:1627`):
> „The owner's mockup is the reference for the workbench (dockable/floating panels, project structure with dimmed inferable
> nodes, active context window, artifact views for video/music, execution environment).”

oraz opis makiety (`OWNER_REQUIREMENTS_2026-09-26.md:723-728`, cytowany tamże): „struktura projektu z przyciemnionymi
węzłami do automatycznego uzupełnienia”. [wtórny — OKNA_SYSTEM §11 pkt 8a to sam zastrzega]

**Słowa dosłowne UI11** (R5, `OWNER_REQUIREMENTS_2026-09-26.md:30`; `SLOWA_WLASCICIELA…:1475`):
> „te które z dużą pewnością można uzupełnić to zaznaczone na przykład opacity jako jedna z wielu opcji może sygnalizować
> dane inferowalne z określoną oczekiwaną własnością, nawet dokładnie to określone. Ja ci podaję takie podstawowe parametry
> ale ty mądrzejsze wymyślisz.”

**Odczyt (mój, do poprawienia):**
1. Zdanie UI11 mówi o *oznaczaniu danych inferowalnych* i ich *oczekiwanej własności* — to wymaganie ogólne, niezależne od
   okna; „opacity” jest przykładem („jedna z wielu opcji”). Dlatego oznaczenie jest danymi/ustawieniem, nie stałą.
2. Z UI12 (wtórnego) wynika tylko, że w makiecie *istnieje panel z drzewem struktury*, w którym takie węzły się pojawiają.
   Że to „drzewo plików projektu”, „drzewo jednostki” czy „drzewo importu” — nie wiem; **model „drzewo wokół dowolnego korzenia”
   obejmuje wszystkie trzy** i nie rozstrzyga (zasada R59: projekt pokrywa wszystkie przypadki).
3. „Automatyczne uzupełnianie” rozumiem jako: system **proponuje** uzupełnienie, a skutek zapisu zatwierdza człowiek (tak
   działają dziś wszystkie akcje piszące, `requires`+potwierdzenie), chyba że ustawienie powie inaczej (zasada właściciela
   2026-09-30: automatyczne przyjmowanie wyników jest konfigurowalne, nie decyzją za niego).

## 2. Model jako abstrakcje

**2.1 Widok = perspektywa/projekcja istniejących danych.** Okno nie ma własnej tabeli. Wejście: `params.root` (adres węzła
w jednym grafie nawigowalnym, np. `unit:project/x`, `area:imports`, `conv:<id>`) oraz parametry wspólne z rodzajem `navigator`
(`depth`, `budget`, `perspectives`). Dzieci z `graph.navigate` (jeden poziom, z licznikami, pochodzeniem, dowodem); drzewo
rozwija się leniwie, poziom po poziomie. Okno można związać z innym oknem (sprzężenie parametrów, OKNA §5): np. `root`
podąża za węzłem wskazanym w oknie „Graf”.

**2.2 Wzorzec struktury (template) = dane.** Wzorzec odpowiada na pytanie „czego po węźle o cesze C spodziewamy się pod nim”:

```
{ "schema": "loom.structure_template/1", "id": "...", "match": <predykat na węźle: kind/class/prefix/atrybut/…>,
  "expects": [ { "id", "label": {pl,en}, "relation": "part_of|…", "child_match": <predykat>, "card": {min,max} (otwarta; presety takie jak one={1,1}, many={0,∞}),
                 "fill": [ { "action": "<id z katalogu>", "args": {…odwołania do atrybutów węzła…}, "ask": […] } ],
                 "confidence": <0..1 lub metoda>, "source": "wzorzec|wnioskowanie|…" } ] }
```

- Lista wzorców jest **otwarta** (pliki `*.pack` w danych + warstwa osoby; dodanie nowego nie wymaga zmiany kodu).
  Przykłady (nie wymagania): jednostka → rozmowy, dokumenty, decyzje; import → rozmowy, załączniki; „sprawa prawna” z
  `project_kinds/legal_case.json` → strony, terminy, dokumenty. Silnik musi obsłużyć także wzorce mało użyteczne (puste
  `expects`, `expects` wskazujące to samo dziecko wielokrotnie, predykaty niedopasowane do niczego): każdy taki przypadek
  daje **ostrzeżenie niewiążące** z „nie pytaj więcej” (CLAUDE.md, „Examples are examples”), nigdy odrzucenie.
- Predykat `match` ma tę samą składnię co predykaty już używane w danych (reguły wnioskowania, kotwice `project_kinds`
  — `anchors` w `software_app.json:~45`), żeby nie mnożyć języków. Gdzie dziś jej brak w danym miejscu — patrz §7.

**2.3 „Przyciemniony węzeł” = oczekiwanie bez spełnienia.** Dla węzła N i wzorca W silnik liczy dla każdego `expects[i]`
dzieci spełniające `child_match`. Wynik na pozycję to stan z listy otwartej (dane), takie jak: `present` (są), `missing` (mniej niż `card.min`), `inferred`
(jest tylko dziecko o klasie dowodu `inferred`/`derived`), `declined` (osoba odmówiła; jak `declined` na ekranie „Ty”). Lista
nie jest zamknięta; zamknięcie wymagałoby powodu i daty.
`missing` rysowany jest jako **węzeł-widmo** (adres syntetyczny `…#expect/<id>`, nie zapisany nigdzie), niosący: etykietę,
oczekiwaną własność (typ, kardynalność), pewność i źródło oczekiwania — to jest „oczekiwana własność” z UI11.
Wygląd = mapowanie danych (klasa dowodu/stan → znak wizualny), **lista znaków otwarta**: nieprzezroczystość, kreskowana
ramka, ikona, kolor, rozmiar, kolejność, ukrycie; kilka naraz i kierunek zależny od innej zmiennej też dozwolone. Preset:
istniejące `me_inferred_opacity` jako jeden z mapowanych znaków (reuse).

**2.4 Jak działa uzupełnianie.** Węzeł-widmo i każdy węzeł dostępny w drzewie niosą listę **działań** — z `fill` wzorca albo
z działań źródła grafu (`actions` w `user.pack` dla grupy `units`, np. `units.define`, `conversation.set_unit` z `ask:[conv_id]`).
Wybór działania wywołuje istniejący przepływ propozycji akcji (`ActionProposalBlock`, potwierdzenie, ślad). Nowe akcje
tylko tam, gdzie katalog nie ma odpowiednika (kandydaci, **przykłady**: „utwórz rozmowę w jednostce”, „dołącz plik jako
dokument jednostki”, „zapisz decyzję”); każda z deklarowanymi `effects` jak reszta katalogu. Tryb wyzwalania —
wartość otwarta z danych; presety, na przykład: `ask`, `propose` (system przygotowuje, człowiek zatwierdza), `auto` (system
wykonuje, jeśli osoba tak ustawi) — ustawienie o dowolnym zakresie (takim jak wzorzec, węzeł, okno); preset: `ask`. Oszacowanie kosztu i wysyłki poza urządzenie pokazują zwykłe
`effects` akcji (`sends_off_device`, `costs_money`); ×10 nadal wymaga potwierdzenia (reguła kosztowa właściciela).

**2.5 `params.root`.** Adres węzła (string) lub pusty. Pusty = widok „wybierz korzeń” (lista jednostek + obszarów
importu + otwarta rozmowa jako podpowiedzi — przykłady, nie zamknięta lista). Zmiana `root` przez osobę, przez
sprzężenie lub zdanie („pokaż strukturę projektu X”) przechodzi przez ten sam kanał `window.open`/parametr co inne rodzaje.
Parametry dodatkowe okna (propozycja, w danych rodzaju; takie jak): `templates` (które wzorce stosować; domyślnie wszystkie
pasujące), `show_expected` (włącz/wyłącz nakładkę), `mapping` (wygląd), `depth`, `budget`.

## 3. Co jest dziś w kodzie (obserwacje z 2026-10-28, HEAD `1bcc4c02`, mogą się zmienić)

| Obserwacja | Miejsce |
|---|---|
| Wpis rejestru rodzaju: `project_structure`, `component: null`, `params:["root"]`, `status:"planned"`, powód „z makiety UI12…” | `loom/web/src/shell/data/window_kinds.json:378-391` |
| Rodzaj `navigator` (komponent `GraphNavigator`, parametry `focus, perspectives, depth, budget, navigator_layout`) — gotowy szkielet drzewa | `window_kinds.json:39-57`; `web/src/components/GraphNavigator.tsx` |
| Ładowanie rejestru; `Kind.component:string|null` | `web/src/shell/windows/registry.ts:5` |
| `graph.navigate`: jeden poziom grafu, dzieci z licznikami, pochodzeniem, dowodem; tylko odczyt | `loom/data/runtime/actions.pack:13548` |
| Źródło `unit` (prefiks `unit:`, czytnik `db.units`, krawędź `part_of`, obszar `context_projects`) — jednostki z `conversations.metadata.units` | `loom/data/runtime/graph_sources.pack` (źródło `unit`) |
| Źródło importu: węzły z `metadata.import_graph`, drzewo po `import_graph.nav_parent` (eksport → plik → wpisy; eksport → rozmowy → załączniki) | `graph_sources.pack:~1255-1284` |
| `party_self` i „sloty”: wpis bez wartości ma `state` (unanswered/declined/never) zamiast dowodu — **istniejący odpowiednik „puste miejsce do uzupełnienia”** | `graph_sources.pack:324`; `web/src/api/graph-projection.ts:53-55` |
| Klasy dowodu: `declared/observed/said/derived/inferred/extrapolated` | `web/src/api/graph-projection.ts:8` |
| Grupa „Jednostki” na ekranie „Ty”, działania `units.forget`, `conversation.set_unit` (`ask:[conv_id]`), `units.define`, `units.list` | `loom/data/profiles/user.pack:~284-330` |
| `units.define` (jednostka = „projekt, zespół, organizacja…”, węzeł `unit:<kind>/<id>`) i `conversation.set_unit` | `actions.pack:15146`, `:15442` |
| „Projekt” w jądrze: poziom zakresu reguł `["person","project","conversation"]`, jednostki = `project` | `graph_sources.pack:~1465-1473` |
| Wygląd wpisu wywnioskowanego: ustawienie `me_inferred_opacity` (0..1, preset), czytane w `MeView` | `settings_fields.pack:5126`; `web/src/components/me-view-state.ts:13-15`; `App.tsx:674` |
| Rodzaje projektów jako dane (`loom.kb.project_kind/1`): `domain_kinds` z `card`, `relation`, kotwicami; stan 2026-10-28: pięć plików (takich jak software_app, film, music) | `loom/data/project_kinds/*.json` (np. `software_app.json:108,137,162` — `"card":"many"`) |
| Zasada „fakt ≠ inferencja… z oczekiwaną własnością” w danych filozofii | `loom/data/philosophy/principles.json:~279-282` |
| Wzorzec komponentu-okna bez nowej infrastruktury: `RequestInspector` (rodzaj `request_inspector`, `present`) | `window_kinds.json:361-376`; `web/src/shell/RequestInspector.tsx`; e2e `windows-gaps-native.mjs` |
| Nie znalazłem w kodzie czytnika „wzorców oczekiwań” ani pola „oczekiwana własność” po stronie web | grep `expected`/`inferable` w `web/src`, `data/runtime` — brak trafień (2026-10-28) |

## 4. Plan pakietów

**W11a-1 — wzorce i odczyt (≈60–80 wywołań narzędzi).**
- Dane: `loom/data/runtime/structure_templates.pack` (schemat `loom.structure_template/1`, wzorce-przykłady, na przykład:
  jednostka, import, wzorzec wyprowadzony z jednego `project_kinds`), wpis w rejestrze danych/kontroli, że każdy wzorzec
  ma czytelnika (reguła repo: pole bez czytelnika = błąd).
- Jądro: nowa akcja tylko-do-odczytu `graph.structure` (korzeń + wzorce → drzewo jednego poziomu z pozycjami
  stanami takimi jak `present/missing/inferred/declined`), implementowana jako nakładka na `graph.navigate` (bez nowego magazynu); cache
  jak nawigator. Wpis w `chat_coverage`/`interfaces`, testy doctest (przypadki zwykłe + właściwościowe: dowolny wzorzec,
  puste/podwójne `expects`, cykle wzorców, korzeń nieistniejący → odpowiedź z powodem, nie wyjątek).
- Web: komponent `ProjectStructure` (reuse `GraphNavigator`/`NodeCard`), rodzaj → `status:"present"`, `component` ustawiony,
  `params: root, templates, show_expected, mapping, depth, budget`; mapowanie wyglądu przez istniejące `me_inferred_opacity`
  (preset) + nowe pole mapowania jako dane.
- Akceptacja (e2e `windows-w11a-native.mjs`), ogólnie: dla dowolnego wzorca i korzenia drzewo pokazuje stany zgodnie z
  danymi, a oglądanie niczego nie zapisuje. Przykłady przypadków (jedne z wielu): (1) otwarcie okna ze zdania i z menu
  przełącznika; (2) `root` = jednostka z dwiema rozmowami → rozmowy `present`, a oczekiwania bez źródła w grafie
  (jeśli wzorzec takie zawiera; dziś nie ma źródła dokumentów ani decyzji) jako widma; (3) zmiana `mapping` zmienia wygląd bez
  przeładowania; (4) wzorzec mało użyteczny → niewiążące ostrzeżenie z „nie pytaj więcej”; (5) układ okna zapisuje/odtwarza
  `params`; (6) brak zapisu do danych podczas samego oglądania (stan danych identyczny przed/po).
- Czego dotyka właściciel: nic poza obejrzeniem okna (przełącznik → „Struktura projektu”).

**W11a-2 — uzupełnianie i ustawienia (≈50–70 wywołań).**
- `fill`: wywołanie akcji z węzła-widma przez przepływ propozycji; tryb wyzwalania jako ustawienie z danych (presety takie jak `ask`, `propose`, `auto`; zakres otwarty, na przykład wzorzec,
  okno, osoba); ślad w historii; po zatwierdzeniu widmo zamienia się w węzeł (odświeżenie).
- Nowe akcje z katalogu **tylko** gdy brak odpowiednika (kandydaci w §2.4), z `effects`.
- Ustawienia w „Co aplikacja wie”: wzorce (lista otwarta, edycja jako dane), mapowanie wyglądu, tryb wyzwalania, „nie pytaj więcej”.
- Akceptacja e2e, ogólnie: uzupełnienie przechodzi tym trybem, jaki ustawiono, i zostawia ślad. Przykłady przypadków: (1) „dodaj do jednostki” z widma zapisuje `conversation.set_unit` po potwierdzeniu, widmo znika;
  (2) odmowa → stan `declined` i brak ponownych pytań do czasu zmiany ustawienia; (3) tryb `auto` wykonuje bez pytania i
  zostawia ślad; (4) cofnięcie przez istniejące mechanizmy akcji.
- Czego dotyka właściciel: opcjonalnie — przejrzeć/poprawić wzorce w ustawieniach.

Kolejność (preset z reguły S1 w CLAUDE.md, „Working with subagents”/„Waste control”: pojedyncza ścieżka, jeden worktree, punkt
kontrolny po każdym kroku); koordynator może zmienić.

## 5. Decyzje (koordynatora, z odwróceniem)

| # | Decyzja | Odwrócenie |
|---|---|---|
| D1 | Widok = nakładka na `graph.navigate`, bez nowego magazynu | Dodać źródło w `graph_sources.pack` (dane) |
| D2 | Wzorce jako dane `loom.structure_template/1`; startowe wzorce to **przykłady**, nie wymagania | Usunąć/wymienić pliki danych; kod bez zmian |
| D3 | Oznaczenie „do uzupełnienia” jako mapowanie danych; preset = `me_inferred_opacity` | Zmiana mapowania w ustawieniach |
| D4 | Uzupełnianie przez istniejący przepływ propozycji akcji; tryb domyślny `ask` | Ustawienie trybu `propose`/`auto` |
| D5 | Dwa pakiety (odczyt, potem zapis) — odczyt nic nie zmienia w danych osoby | Scalić w jeden, jeśli koszt niski |
| D6 | Odczyt UI12 jako „drzewo wokół dowolnego korzenia” — może obejmować, jako jeden z możliwych przykładów, drzewo plików projektu oraz korzenie takie jak jednostka, import | Zawęzić domyślny `root`; model bez zmian |
| D7 | Rodzaje `project_kinds/*.json` mogą dać wzorce, ale nie są jedynym źródłem | Wyłączyć źródło w ustawieniu `templates` |

Wpis do `docs/reports/INDEX.md` robi koordynator przy przyjęciu (ten dokument tego nie zmienia).

## 6. Pytania do właściciela

Brak. Sprawdzone wobec filozofii: nie ma tu jego pieniędzy ponad uzgodniony budżet (tylko odczyt grafu lokalnie; wnioskowanie
modelem, jeśli kiedyś włączone, idzie istniejącą ścieżką z szacunkiem kosztu), ani wysyłki jego danych do nowego dostawcy
(wzorce i drzewo liczone lokalnie), ani sprzeczności z wymaganiem (UI11/UI12 nie określają niczego, co model łamie). Różne
możliwe odczytu makiety (§1 pkt 2) obsługuje projekt, więc nie są pytaniem (R59).

## 7. Luki i niepewności

- UI12 jest źródłem **wtórnym**; makieta (`workbench_mockup_2026-09-26.webp`) nie została tu ponownie obejrzana — układ
  panelu (dokowany pod rozmową) pochodzi z OKNA_SYSTEM §1 T3 (odczyt z makiety) i **nie został zweryfikowany** w tym dokumencie.
- Nie sprawdziłem, czy istnieje wspólny język predykatów (`match`) użyteczny od razu dla węzłów grafu; reguły
  wnioskowania (`data/rules/inference_rules.json`) i kotwice `project_kinds` mają własne formy — do zbadania na początku W11a-1
  (jeśli brak wspólnego, wzorzec dostaje minimalny predykat jako dane, bez zamykania listy operatorów).
- Dokumenty i decyzje jako „dzieci jednostki” nie mają dziś źródła w grafie (jednostka ma tylko rozmowy-członków,
  `graph_sources.pack` (źródło `unit`, pole `edges: []`)); wzorzec je wyświetli jako widma, a ich dodanie wymaga nowych akcji/źródeł — obserwacja
  na dziś, nie ograniczenie.
- Kosztu wydajności przy dużych drzewach nie mierzyłem; budżet i leniwe rozwijanie jak w nawigatorze (ustawienie `navigator_children_limit`, `settings_fields.pack:4606`, sprawdzone 2026-10-28).
- Pokrycie przestrzeni możliwości: testy właściwościowe obiecane w §4 dotyczą silnika wzorców; wygląd (kombinacje
  znaków wizualnych) pokryje się częściowo — reszta zgłoszona jako luka po pakiecie.

## Kontrola przed użyciem (2026-10-28)

Kontrola Haiku (4 antywzorce) została wykonana; poniższe poprawki naniesiono jako dodatek do tego dokumentu. Sprawdzenie
względem filozofii (listy to przykłady, nic nie rozstrzygamy za osobę, silnik pokrywa każdą konfigurację) wykonano teraz;
wcześniejsze zdanie w §6 o „sprawdzeniu wobec filozofii” dotyczyło tylko pytań do właściciela.

- Blokujące: „zawsze z potwierdzeniem” → domyślnie, preset w danych; stany wyniku, tryby wyzwalania i ich zakres oraz
  kardynalność `card` zamienione na wartości otwarte z presetami (zamknięcie wymaga powodu i daty).
- Sformułowania: przypadki akceptacyjne oznaczone jako przykłady przy ogólnym sformułowaniu; przypadek z dokumentami i
  decyzjami uzależniony od istnienia źródła; „jeden agent naraz” jako preset z reguły S1; D6 jako jeden z możliwych
  przykładów; fakty o stanie obecnym opatrzone datą; D1–D7 nazwane propozycjami; twierdzenie o układzie z OKNA §1 T3
  oznaczone jako niezweryfikowane; `navigator_children_limit` zweryfikowane (`settings_fields.pack:4606`); przybliżone
  numery linii przy źródle `unit` usunięte; listy i liczby zamienione na „takie jak …”.
