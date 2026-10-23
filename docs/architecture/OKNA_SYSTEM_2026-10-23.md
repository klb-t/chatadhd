# System okien aplikacji — projekt (G4, 2026-10-23)

Pakiet G4-windows-system-design (Fable, tylko projekt; żadnego kodu produktu). Dotyczy web UI w `loom/web`
(przeglądarka i powłoka Android WebView). Słowa właściciela cytowane **wyłącznie dosłownie** z `python3 tools/spec.py show <id>`
(użyte id: `spec:R64`, `spec:R56`, `spec:UI27`, `spec:UI12`, `spec:UI16`, `spec:UI17`, `spec:UI08`, `spec:ON10`, `spec:UI14`,
`spec:R58`, `spec:R59`, `spec:R60`, dodatkowo `spec:R65`, `spec:R66`). Wszystko poza cytatami to mój odczyt i moje decyzje,
oznaczone jako takie. Każda konkretna wartość, lista, nazwa i kolejność w tym dokumencie jest **przykładem** mechanizmu albo
presetem w danych (R58/R59), nigdy regułą.

Kod czytany tylko-do-odczytu w `/home/user/wt/m1d0` (stan gałęzi `claude/m1-w0` z 2026-10-23; zliczenia plików e2e sprawdzone
ponownie 2026-10-23 na commicie `270a065f`); odwołania plik:linia odnoszą się do niego. Każde „dziś” w tym dokumencie jest
**obserwacją z tej daty** (z plikiem:linią albo oznaczone „niezweryfikowane”), nigdy ograniczeniem.

---

## 0. Streszczenie dla właściciela (prostymi słowami)

1. Okno to jednostka interfejsu: czat, graf, lista rozmów, pamięć, import, ustawienia, „Ty”, artefakt — każde jest oknem
   (decyzja projektu, §12 D1 i D4; wymienione rodzaje to przykłady, lista jest otwarta).
2. Okno można zadokować przy krawędzi, odpiąć jako pływające, zmaksymalizować, zwinąć albo włożyć do grupy zakładek.
3. „Zwykła aplikacja czatu” to **nie** osobny tryb: to zapisany układ „Czat” = jedno zmaksymalizowane okno czatu (lista rozmów
   jako zwinięta szuflada). Dzisiejszy „Warsztat” to drugi zapisany układ tego samego systemu (decyzja projektu, §12 D3, D5 i D9;
   „Czat” i „Warsztat” są presetami w danych, nie niezmiennikami).
4. Na telefonie preset to stos zmaksymalizowanych okien z przełącznikiem; Wstecz wraca do poprzedniego okna, na końcu do czatu.
   Pływanie i dokowanie są dostępne także na telefonie — jeśli mają mało sensu (np. dwa okna po 180 px), aplikacja tylko
   zapyta „czy celowo?” i pozwoli odpowiedzieć „nie pytaj więcej”.
5. Układy to dane: zapisujesz je pod własną nazwą, masz je na każdym urządzeniu (per osoba), ostatni układ pamiętany per
   urządzenie; każdą zmianę układu da się cofnąć.
6. Okna można sprzęgać: ta sama rozmowa w dwóch oknach, albo niezależna, albo powiązana wybraną zależnością (np. „ten graf
   pokazuje poprzedni dzień tamtego”). Pięć grafów to przykład, nie granica.
7. Wszystko robi się zdaniem w czacie („otwórz graf obok”, „zmaksymalizuj”, „zamknij wszystko poza czatem”), z menu ⋮, gestem
   i klawiaturą — ta sama akcja, wiele dróg.
8. Okno czatu ma sumę funkcji popularnych aplikacji (macierz w `REPLIKACJA_PRZEPLYWOW_DOSTAWCOW_2026-10-23.md` §5), a styl
   (profil aplikacji) nakłada się na okno albo na cały układ; zmiana stylu nigdy nie zmienia po cichu modelu, uprawnień,
   prywatności ani kontekstu.
9. Dziś istnieje: dwa układy jako dane, stos warstw dla Wstecz, sprzężenie tylko „ta sama rozmowa / inna”. Nie istnieje:
   okna pływające/dokowane, przełącznik, nazwane układy, cofanie, sprzężenia innych parametrów.
10. Pierwsze pakiety dają coś do dotknięcia na telefonie: czat jako zmaksymalizowane okno (bez widocznej zmiany), potem
    przełącznik okien i „otwórz graf obok” z zapisanym układem „ostatni”.

---

## 1. Wymaganie jako ponumerowane twierdzenia

Oznaczenia: **[cytat]** = dosłownie z `spec.py show`; **[wtórny]** = cytat, który sam spec oznacza jako relację agenta/zapis
asystenta; **[odczyt]** = moja interpretacja.

**T1 (R64).** [cytat `spec:R64` cytat 1, OWNER_REQUIREMENTS_2026-09-26.md:751]
> Ja mówiłem o panelach czy oknach Jak zwał tak zwał, dokowalnych, pływających, maksymalizowalnych, standardowy interfejs to zmaksymalizowany, inspirowany i zawierającymi sumę funkcjonalności oraz alternatywy stylistyczne popularnych apek

[odczyt] Cztery rzeczy w jednym zdaniu: (a) okno = panel = jednostka; (b) trzy stany: dokowalne, pływające, maksymalizowalne;
(c) „standardowy interfejs to zmaksymalizowany” — stan domyślny jest szczególnym przypadkiem tego samego mechanizmu;
(d) treść okna czatu: suma funkcjonalności + alternatywy stylistyczne popularnych aplikacji. Paragraf §2 odpowiada na (a)–(b),
§3 na (c), §7 na (d).

**T2 (R56/UI27).** [cytat `spec:R56` cytat 1, OWNER_REQUIREMENTS_2026-09-26.md:706]
> "było dla mnie oczywiście w tym punkcie wyjścia, że jak uruchamiasz aplikację, to dostajesz widok albo ostatni zapisany, albo ostatniej rozmowy w okienku czatu, albo nowej rozmowy. Na Androidzie okienka (pływające, dokowalne, obsługujące wszystko) co do zasady powinny być możliwe do skonfigurowania tak jak na desktopie Czy w web apce, tyle że domyślny, wyjściowy widok, to pełnoekranowy interfejs standardowej apki chat bota, z flow, harnesem, stylem wyświetlania, itp itd, będąca fuzją wszystkich apek od kilku (5-10) najpopularniejszych llm-ów. Obowiązkowe jak ktoś się przesiada na naszą apkę, to można się spodziewać że najpierw chciałby przetestować czat, zanim swędzi godziny na konfiguracji"

[odczyt] (a) Start = uporządkowana lista strategii (już jest: `start_view`, `start-view.ts`); (b) okna na Androidzie
„obsługujące wszystko” i konfigurowalne tak jak na desktopie — więc **żadna operacja na oknie nie jest wyłączona na telefonie**;
(c) widok domyślny = pełnoekranowy czat będący fuzją; (d) „Obowiązkowe”: pierwsza rozmowa przed jakąkolwiek konfiguracją (ON10).
To samo zdanie spec trzyma jako `spec:ON10` cytat 1 (identyczna końcówka).

**T3 (UI12).** [wtórny, `spec:UI12` cytat 1]
> The owner's mockup is the reference for the workbench (dockable/floating panels, project structure with dimmed inferable nodes, active context window, artifact views for video/music, execution environment).

[odczyt z obejrzanej makiety `docs/architecture/workbench_mockup_2026-09-26.webp`] Trzy kolumny: lista rozmów (z własnym
paskiem ikon po lewej), aktywna rozmowa z composerem, widok artefaktu (muzyka: JSON, oś czasu wideo, storyboard). Nad rozmową
**pływa** „Knowledge graph” z „Active context window”; pod rozmową zadokowane „Project structure” (drzewo z przyciemnionymi,
„auto-completable” węzłami) i „Execution environment” (terminal); osobne okienko „Video player”. Każde okienko ma własny pasek
tytułu z przyciskami (maksymalizuj ⤢, zamknij ×, menu …). Podpis na makiecie: „Full configurability with shadows and docked
edges”. Z makiety biorę: okno ma pasek tytułowy z operacjami; okna pływające leżą nad zadokowanymi; obszar roboczy ma kolumny
zadokowane o różnych szerokościach; artefakt jest osobnym oknem; środowisko wykonania jest oknem. Makieta jest **wzorcem układu
desktopowego**, nie jedynym układem.

**T4 (UI16).** [wtórny, `spec:UI16` cytat 2]
> Osobne elementy interfejsu oraz elementy nadrzędne: widoki pełnoekranowe, okna, okna zadokowane, karty, kontenery i układy automatycznie/adaptacyjnie dostosowywane. Użytkownik ma móc składać je w potrzebną całość.

[wtórny, cytat 3]
> Dowolna liczba równoczesnych widoków, w tym kilka grafów ze sprzęganymi parametrami wyboru i prezentacji, do poruszania się po wielowymiarowej przestrzeni. Nie wymuszać wyboru graf albo tabela.

[wtórny, cytat 4]
> Parametry mogą być współdzielone, niezależne albo połączone wybraną zależnością. Wspólny selected_entity to dopiero początek, nie pełna realizacja.

[cytat `spec:UI16` cytat 6, OWNER_REQUIREMENTS_2026-09-26.md:359]
> five graphs was an example, not a maximum

[odczyt] Kontenery (pełny ekran, okno, dok, karta/zakładka, kontener adaptacyjny) to **stany i grupowania tego samego okna**,
nie osobne klasy; liczba okien nieograniczona; sprzężenie parametrów to trójka {wspólne, niezależne, powiązane zależnością}
i dotyczy każdego parametru, nie tylko wybranej encji. §5.

**T5 (UI17).** [cytat `spec:UI17` cytat 3]
> changing a UI profile must not implicitly change model, permissions, privacy or context selection

[wtórny, cytat 1]
> Interfejs i pipeline wykonania są składane z niezależnych elementów. Profile dostawców to rozłączalne presety, nie osobne aplikacje ani ograniczenia kombinacji.

[odczyt] Stan okna i układu nie zawiera modelu, uprawnień, prywatności ani doboru kontekstu; profil (styl) jest nakładką, która
tych pól nie dotyka. Test tej własności jest częścią pakietu W9 (§9).

**T6 (UI08).** [cytat `spec:UI08` cytat 4]
> Napraw obsługę artefaktów, bo wyświetla jako zwykły tekst, a ma wpływającej ramce z przyciskami. Zwijalne wiadomości. Quick API panel. Katalog do pamięci - pełna treść plików.

[odczyt] Artefakt = okno rodzaju `artifact` (pływające jako preset otwarcia, z przyciskami: kopiuj/otwórz/zapisz/zadokuj);
„Quick API panel” = okno rodzaju `request_inspector` (dziś tylko w warsztacie: „Expert · inspect or edit one request”).

**T7 (UI14).** [cytat `spec:UI14` cytat 1, fragmenty]
> całe profile interfejsów inspirowane oryginalnymi dostawcami jako możliwe widoki
> w kodzie tak uogólnione żeby te interfejsy to były tylko dane

[odczyt] Styl = profil aplikacji (już dane: `loom.application_profile/1`), nakładany per okno lub per układ (§7).

**T8 (R58).** [cytat `spec:R58` cytat 1, fragmenty]
> unikaj zapisywania przykładów bez wyraźnego zaznaczenia że to są przykłady które silnik i abstrakcje mają obsłużyć we wszelkich możliwych konfiguracjach
> zwracając uwagę na niewielką użyteczność i pytając czy to jest celowe i czy następnym razem nie pytać

[odczyt] Silnik okien **nigdy nie odmawia** konfiguracji (okno 1 px, dwadzieścia okien na telefonie, pętla zależności);
mało użyteczna konfiguracja → niewiążąca podpowiedź z „nie pytaj następnym razem”, zapamiętana jako dane.

**T9 (R59).** [cytat `spec:R59` cytat 1, fragment]
> to mają to rozumieć jako przykłady funkcjonalności silnika i abstrakcji. konieczne i niewystarczające, w pełni wystarczający jest tylko pełne wyczerpanie listy możliwości.

[odczyt] Stąd §10: testy generatywne po przestrzeni stanów okien, a nie lista ręcznych przypadków; luki nazwane jako luki.

**T10 (R60).** [cytat `spec:R60` cytat 1, fragment]
> brak czegoś lub kogoś teraz nie oznacza że będzie tak zawsze. w ekosystemie mamy innych odbiorców, A my budujemy nowy paradigmat.

[odczyt] Rodzaje okien, których dziś nie ma (środowisko wykonania, odtwarzacz wideo, struktura projektu z makiety), są
w rejestrze ze statusem `planned` i powodem — jak dziś `layout_mode=windows` — nie są pomijane.

**T11 (R66).** [cytat `spec:R66` cytat 1]
> o ile dobrze pamiętam work band to był dla ciebie do testów tak? przedstaw plan z perspektywami, kiedy będą funkcjonalności, kiedy interfejs

[odczyt] Kolizja nazw „Warsztat” (dziś etykieta `menu_workshop` = „Warsztat (narzędzia zaawansowane)”, `shell.test.mjs:138`)
z warsztatem z makiety. Rozstrzygnięcie w §11 pkt 3.

**T12 (R65).** [cytat `spec:R65` cytat 1, fragment]
> co w ogóle ten interfejs grafu robi? w ogóle wszystkie teksty to nic nie mówią, mi - autorowi niby.

[odczyt] Każde okno musi mieć tytuł i opis w jego słowach (ze słownika węzła grafu), nie z identyfikatora rodzaju.

---

## 2. Model okna

### 2.1 Okno = instancja widoku ze stanem

[odczyt + decyzja] **Okno** to instancja *rodzaju widoku* (`kind`) pokazująca *węzeł grafu* (`node`) w jednym ze stanów ułożenia.
Wszystkie nazwy pól poniżej są angielskie (identyfikatory), etykiety dla człowieka są polskie i biorą się z danych.

```jsonc
// schemat loom.window/1 (propozycja; miejsce: loom/web/src/shell/data/ → docelowo loom/data/runtime/windows.pack, patrz §2.6)
{
  "id": "w_3fa9c0d1e2b4",            // jak inne id: prefiks + 12 hex
  "kind": "navigator",               // rodzaj z rejestru (§2.3)
  "node": "conv:3fa9c0d1e2b4",       // adres w grafie; tytuł i ikona okna pochodzą z węzła (R65), rodzaj daje tylko zapas
  "mode": "docked",                  // maximized | docked | floating | minimized | tabbed
  "dock":  { "leaf": "L2" },         // mode=docked|tabbed: liść drzewa doków (§2.2), w liściu = grupa zakładek
  "rect":  { "x": 0.12, "y": 0.08, "w": 0.46, "h": 0.40, "unit": "fraction" },  // mode=floating; fraction|px (§2.5)
  "z": 3,                            // kolejność pływających (wyższy = bliżej)
  "params": { ... },                 // parametry rodzaju z deklaracją sprzężenia (§5)
  "profile": null,                   // styl per okno (§7) — null = dziedzicz z układu
  "title_override": null,            // człowiek może nazwać okno po swojemu; null = tytuł węzła
  "collapsed": false,                // zadokowane okno zwinięte do uchwytu (szuflada ☰ listy rozmów w układzie „Czat”)
  "pinned": false                    // „zamknij wszystko poza…” omija okna przypięte — przykład, flaga w danych
}
```

Stany ułożenia (`mode`) — każdy dostępny w każdej klasie urządzenia (T2b):

| mode | znaczenie [decyzja] | odpowiednik dziś |
|---|---|---|
| `maximized` | zajmuje cały obszar roboczy; pozostałe okna są *odstawione* (dostępne przez przełącznik i uchwyty) | układ „Czat” (`ChatShell`), panel w `.drawer` |
| `docked` | liść drzewa doków; rozmiar z proporcji podziału | kolumny warsztatu (`App.tsx:308-328`), lista w `aside.sidebar` |
| `tabbed` | jak `docked`, ale dzieli liść z innymi oknami jako zakładki; jedno aktywne | brak |
| `floating` | prostokąt nad dokami, własne `z` | brak (`.drawer`/`.shell-drawer` to nakładki modalne, nie okna) |
| `minimized` | niewidoczne, na półce/przełączniku, stan zachowany | brak |

**Fokus** — jedno okno w układzie ma fokus (`layout.focus`); to do niego trafiają skróty i zdanie „zmaksymalizuj” bez
dopełnienia. **Z-order** dotyczy tylko `floating`; zadokowane mają porządek drzewa.

### 2.2 Drzewo doków (kontener)

[decyzja] Doki to **binarne drzewo podziałów** z grupami zakładek w liściach (model jak w edytorach kodu: podział poziomy/pionowy
z proporcją, liść = grupa 1..n okien). Uzasadnienie: (a) obejmuje jedno zmaksymalizowane okno (drzewo = jeden liść), pasek boczny
+ czat (jeden podział), siatkę warsztatu (drzewo równych proporcji) i makietę (kolumny o różnych szerokościach, kolumna
środkowa podzielona pionowo); (b) stałe strefy „lewo/prawo/dół” (model Eclipse/Blender) są jego szczególnym przypadkiem;
(c) siatka n×m też (drzewo o równych proporcjach). Odwrót: §12 D2.

```jsonc
"tree": { "split": "h", "ratio": 0.28, "a": { "leaf": "L1" }, "b": { "split": "v", "ratio": 0.7, "a": { "leaf": "L2" }, "b": { "leaf": "L3" } } }
```

Degeneracje obsługiwane, nie odrzucane (R58): `ratio` 0 lub 1 (jedna strona niewidoczna → podpowiedź „okno X ma 0 px, czy celowo?”),
liść bez okien (zwijanie pustego liścia to preset `collapse_empty_leaves` ∈ {`emptied` — [preset] przy zapisie znika liść, który
opustoszał po zamknięciu ostatniego okna; pusty liść utworzony jawnie przez osobę (pole liścia `born: explicit`, np. „nowa grupa
zakładek”) zostaje | `all` | `none`}; zwinięcie nie jest ciche — niewiążąca podpowiedź `empty_leaf_collapsed` [przykład tekstu]:
„Pusty panel został usunięty przy zapisie. [Przywróć] [Zostaw] [Nie pytaj więcej]”; w pamięci pusty liść może trwać),
40 liści na telefonie (silnik ma je przyjąć bez odrzucania — sprawdzi to P1 od W0, dziś niezbudowane; podpowiedź `dock_narrow`).

### 2.3 Rejestr rodzajów okien — dane

[decyzja] Każdy widok aplikacji to rodzaj okna zarejestrowany w danych; lista poniżej jest **zalążkiem** (reguła „listy są
przykładami”, CLAUDE.md 2026-10-16) wypełnionym tym, co już jest w aplikacji, plus to, co makieta i wymagania implikują.
Dziś lista paneli to kod (`shell/panels.ts:3-4`, 15 id), menu to dane (`layouts.json`), arkusze to kod (`SHEETS` w `menu.ts`).

```jsonc
// schemat loom.window_kinds/1
{ "kinds": [
  { "id": "chat",              "label": {"pl": "Czat"},                  "component": "ChatView",          "params": ["conversation", "profile"], "status": "present" },
  { "id": "conversation_list", "label": {"pl": "Rozmowy"},               "component": "ConversationList",  "params": ["conversation", "query"],   "status": "present" },
  { "id": "navigator",         "label": {"pl": "Graf"},                  "component": "GraphNavigator",    "params": ["focus", "perspectives", "depth", "budget", "navigator_layout"], "status": "present" },
  { "id": "settings",          "label": {"pl": "Ustawienia"},            "component": "SettingsHuman",     "params": ["group", "view"],           "status": "present" },
  { "id": "settings_fields",   "label": {"pl": "Ustawienia (wszystkie pola)"}, "component": "SettingsPanel", "params": [],                      "status": "present", "note": "dziś tylko w warsztacie (App.tsx:239)" },
  { "id": "me",                "label": {"pl": "Ty"},                    "component": "GraphNavigator",    "params": ["focus"], "preset_params": {"focus": "party:self"}, "status": "present", "note": "nazwane otwarcie nawigatora, nie osobny komponent" },
  { "id": "memory",            "label": {"pl": "Pamięć"},                "component": "MemoryPanel",       "params": ["node"],                    "status": "present" },
  { "id": "import",            "label": {"pl": "Import"},                "component": "ImportPanel",       "params": [],                          "status": "present" },
  { "id": "context",           "label": {"pl": "Kontekst następnej wiadomości"}, "component": "ContextPanel", "params": ["conversation"],       "status": "present" },
  { "id": "receipt",           "label": {"pl": "Co wysłaliśmy"},         "component": "ContextReceipt",    "params": ["conversation", "turn"],    "status": "present", "note": "dziś arkusz z menu ⋮" },
  { "id": "semantic",          "label": {"pl": "Analiza w tle"},         "component": "SemanticStatus",    "params": [],                          "status": "present", "note": "dziś sheet:semantic" },
  { "id": "knowledge",         "label": {"pl": "Wiedza"},                "component": "KnowledgeWorkbench","params": [],                          "status": "present" },
  { "id": "node_card",         "label": {"pl": "Karta węzła"},           "component": "NodeCard",          "params": ["node"],                    "status": "present", "note": "dziś warstwa w nawigatorze; jako okno = opcja" },
  { "id": "artifact",          "label": {"pl": "Artefakt"},              "component": "ArtifactFrame",     "params": ["message", "artifact", "format"], "status": "planned", "reason": {"pl": "UI08: ramka z przyciskami; dziś artefakty importu to zwykła sekcja (ImportedMessageContent.tsx:84-93)"} },
  { "id": "request_inspector", "label": {"pl": "Żądanie do modelu"},     "component": "RequestInspector",  "params": ["conversation", "turn"],    "status": "present", "note": "dziś „Expert” tylko w warsztacie (ChatView.tsx ~1345)" },
  { "id": "logs",              "label": {"pl": "Dziennik"},              "component": "LogsPanel",         "params": ["level"],                   "status": "present" },
  { "id": "operations",        "label": {"pl": "Operacje"},              "component": "OperationsPanel",   "params": [],                          "status": "present" },
  { "id": "methods",           "label": {"pl": "Metody"},                "component": "MethodsPanel",      "params": [],                          "status": "present" },
  { "id": "analysis",          "label": {"pl": "Analiza"},               "component": "AnalysisPanel",     "params": ["conversation"],            "status": "present" },
  { "id": "onboarding",        "label": {"pl": "Wywiad o Tobie"},        "component": "OnboardingPanel",   "params": ["section"],                 "status": "present" },
  { "id": "user_knowledge",    "label": {"pl": "Co o Tobie wiemy"},      "component": "UserKnowledge",     "params": [],                          "status": "present" },
  { "id": "graph_entry",       "label": {"pl": "Graf (stary widok)"},    "component": "GraphEntry",        "params": [],                          "status": "present" },
  { "id": "perspectives",      "label": {"pl": "Perspektywy"},           "component": "PerspectivesPanel", "params": ["perspective"],             "status": "present" },
  { "id": "view_engine",       "label": {"pl": "Silnik widoku"},         "component": "ViewEnginePanel",   "params": ["view"],                    "status": "present" },
  { "id": "project_structure", "label": {"pl": "Struktura projektu"},    "component": null,                "params": ["root"],                    "status": "planned", "reason": {"pl": "z makiety UI12: drzewo z przyciemnionymi węzłami do uzupełnienia (UI11)"} },
  { "id": "execution_env",     "label": {"pl": "Środowisko wykonania"},  "component": null,                "params": ["environment"],             "status": "planned", "reason": {"pl": "z makiety UI12; środowiska są węzłami (environments.h); obserwacja 2026-10-23: w `loom/web/src` nie ma okna terminala (grep `terminal|xterm` — tylko zmienna logiczna w `ChatView.tsx:681-756`)"} },
  { "id": "media_player",      "label": {"pl": "Odtwarzacz"},            "component": null,                "params": ["artifact", "position"],    "status": "planned", "reason": {"pl": "z makiety UI12 (wideo/muzyka); P39 multimodal to projekt"} },
  { "id": "external",          "label": {"pl": "Strona zewnętrzna"},     "component": "ExternalFrame",     "params": ["url", "sandbox"],          "status": "planned", "reason": {"pl": "R60: inni odbiorcy; iframe z polityką — decyzja bezpieczeństwa osobno"} },
  { "id": "connection",        "label": {"pl": "Połączenie"},           "component": null,                "params": [],                          "status": "planned", "reason": {"pl": "dostępne, gdy istnieje odpowiednik na stronie; obserwacja 2026-10-23: ekran jest natywną aktywnością Androida (ConnectionActivity.kt, MainActivity.kt:105,190, AndroidManifest.xml:47), nie stroną; z menu ⋮ przez LoomBridge.openConnection"} }
],
  "not_window": [
  { "id": "composer_menu", "reason": {"pl": "obserwacja 2026-10-23: menu „+” jest częścią okna czatu (composer-menu.ts), nie osobnym oknem; nakładka może zarejestrować je jako rodzaj"} }
]}
```

Zasady rejestru [decyzja]: (1) każdy komponent montowany dziś przez `App.tsx` lub `ChatShell.tsx` występuje w rejestrze dokładnie
raz — test danych w stylu `test_chat_coverage` (wszystko na liście albo w `not_window` z powodem, nic pominięte po cichu); `not_window` jest dla części innych okien —
brak odpowiednika na stronie dziś to `planned` z powodem „dostępne, gdy istnieje odpowiednik na stronie”, nie `not_window`;
(2) `status: planned` z `reason` jest widoczny w przełączniku i menu jako pozycja wyszarzona z powodem (tak jak dziś
`layout_mode=windows` w arkuszu „Widoki”, `ChatShell.tsx:74-76`), nigdy ukryty (R60); (3) nakładka (overlay pack) może dodać rodzaj.

### 2.4 Układ (layout) — dane

```jsonc
// schemat loom.window_layout/2 (następca loom.shell_layouts/1 z layouts.json)
{
  "id": "czat", "label": {"pl": "Czat"}, "origin": "preset",     // preset | saved | derived (zapisany automatycznie „ostatni”)
  "device_class": "any",                                           // podpowiedź, nie ograniczenie: any | phone | tablet | desktop (§4.3)
  "tree": { "leaf": "L1" },
  "leaves": { "L1": { "windows": ["w_chat"], "active": "w_chat" } },
  "windows": [
    { "id": "w_list", "kind": "conversation_list", "node": "conversations:", "mode": "docked", "dock": {"leaf": "L0"}, "collapsed": true,  "params": {"conversation": {"binding": "shared", "channel": "conversation"}} },
    { "id": "w_chat", "kind": "chat",              "node": "conv:current",   "mode": "maximized",                                        "params": {"conversation": {"binding": "shared", "channel": "conversation"}} }
  ],
  "focus": "w_chat",
  "channels": { "conversation": null },                            // wartości wspólne (§5)
  "switcher": { "style": "bar", "position": "bottom" },            // §4.1; przykład
  "shelf": { "handles": ["w_list"] },                              // odstawione okna z uchwytem na ekranie (☰)
  "profile": null,                                                 // styl per układ (§7)
  "chat": { "topbar": [...], "menu": [...], "message_actions": [...], "composer": [...], "starters": 4 }  // jak dziś w layouts.json
}
```

[odczyt] Uwaga do `w_list` w układzie „Czat”: lista jest **osobnym oknem** zadokowanym po lewej i zwiniętym do uchwytu (☰);
rozwinięcie uchwytu na telefonie to dzisiejsza szuflada (`.shell-drawer`, `shell.css:39`), a na ≥721 px — statyczna kolumna
(`shell.css:59`). To jest decyzja D5 z §11 pkt 1.

### 2.5 Trwałość: per osoba, per urządzenie, nazwane, cofanie

| co | gdzie [decyzja] | dziś |
|---|---|---|
| nazwane układy (`origin: saved`) | ustawienie poziomu `person` (`window_layouts`, lista dokumentów `loom.window_layout/2`) — synchronizuje się z osobą między urządzeniami | brak |
| presety | dane w repo (`layouts.json` v2), nadpisywalne nakładką | `layouts.json` v1 (2 układy) |
| ostatni układ („ostatni własny”, `origin: derived`) | ustawienie poziomu `installation` (`window_layout_last`) + kopia w `localStorage` `loom.shell.last.v2` jako szybki start/offline | `loom.shell.last.v1` tylko w `localStorage` (`start-view.ts:44-55`) |
| aktywny układ per klasa urządzenia | `window_layout_active` z kluczem klasy: `{ "phone": "czat", "desktop": "warsztat" }` [przykład wartości] (poziom `person`), nadpisanie per urządzenie na poziomie `installation` (decyzja projektu, §12 D19) | brak (jeden `layout_mode`) |
| dziennik zmian układu (cofnij/ponów) | `window_layout_journal`: pierścień operacji odwracalnych (preset długości 50 — liczba w danych), poziom `installation` (decyzja projektu, §12 D19); „cofnij układ” = odwrócenie ostatniej | brak |
| potwierdzenia podpowiedzi R58 („nie pytaj następnym razem”) | `advisory_ack` (poziom `person`), klucz = id podpowiedzi; mechanizm wspólny z G3 (silnik widoku) — dziś nie istnieje (spec R58: „no remembered acknowledgement … exists”) | brak |

Jednostki prostokątów: `fraction` obszaru roboczego (preset) albo `px`; zapis w `fraction` przenosi układ między ekranami,
`px` jest dla kogoś, kto chce dokładnie 320 px — oba obsługiwane; przy zmianie rozmiaru obszaru (klawiatura ekranowa:
`android:windowSoftInputMode="adjustResize"`, `AndroidManifest.xml:61`; obrót) prostokąty są **dociskane** do obszaru, nigdy
gubione (okno poza ekranem = podpowiedź + przycisk „przyciągnij”).

Prywatność przy przywracaniu [odczyt UI17]: przywrócenie układu przywraca *które okna i jakie węzły* były widoczne, nigdy
sekretów ani zmiany uprawnień; strategia `start_view` już pozwala nie przywracać (`new_conversation` na pierwszym miejscu).

### 2.6 Gdzie leżą dane: web czy jądro

[decyzja] Etap 1 (pakiety W0–W4): dane w `loom/web/src/shell/data/` (jak dziś `layouts.json`) — zero zmian w C++, test
jednostkowy czyta dane bez przeglądarki (wzór: `tests/shell/menu.test.mjs`). Etap 2 (W5+): nazwane układy i dziennik
przez istniejące `settings.set` na poziomach `person`/`installation` (pola dopisane do `settings_fields.pack`, grupa
„Interfejs i wyświetlanie”), a rejestr rodzajów okien przenosi się do `loom/data/runtime/windows.pack`, żeby
`chat_coverage.pack` mógł sprawdzać, że każdy rodzaj okna jest osiągalny zdaniem (§6). Odwrót: §12 D8.

---

## 3. Jeden mechanizm: „zwykła aplikacja czatu” to układ

### 3.1 Dwa dzisiejsze układy jako dwa zapisane układy

| dziś | w systemie okien [decyzja] |
|---|---|
| `layout: "chat"` (`start-view.ts:3`) → `ChatShell` | układ `czat`: `w_list` (docked, collapsed) + `w_chat` (maximized); pasek, menu ⋮, akcje wiadomości, composer, startery — bez zmian z `layouts.json` |
| `layout: "workshop"` → blok `workshop` w `App.tsx:262-331` | układ `siatka` (dawny „Warsztat”, §11 pkt 3): drzewo doków: lista (0.22) \| [czat \| wiedza] + panele jako zakładki jednego liścia, nagłówek warsztatu znika (jego przyciski = pozycje przełącznika) |
| `layout_mode = full_screen_chat` | alias układu `czat` |
| `layout_mode = views_grid` | alias układu `siatka` |
| `layout_mode = windows` (planned) | przestaje być „planned”: oznacza „ostatni własny układ” (`window_layout_last`); `reason` w `settings_fields.pack:866-876` usuwany w pakiecie W3, nie wcześniej |

**Migracja `layout_mode`** [decyzja]: klucz zostaje (zgodność: `settings_fields.pack:850`, test `shell.test.mjs:54-59`), dostaje
czwartą wartość `layout:<id>` dla układów nazwanych; `layoutOfMode()` zwraca id układu zamiast `"chat"|"workshop"`. Odwrót: §12 D3.

**Deep linki** [decyzja]: `#czat[/<conv>]` i `#warsztat` zostają (32 pliki e2e używają `#warsztat`, §8); nowe:
`#uklad/<id>[/<conv>]` (dowolny układ), `#okno/<kind>[/<node>]` (otwórz jedno okno zmaksymalizowane nad aktualnym układem —
link do „Ustawienia” czy „Ty” z zewnątrz). `parseShellHash` zwraca `{ layout: string, window?: {kind,node}, conversationId }`.
Co znaczy `#warsztat` po zmianie nazwy — §11 pkt 3.

**Reguły startu R56** bez zmian w kolejności (`start_view`): `last_saved` = ostatni układ + fokus + rozmowa kanału;
`last_conversation` = układ aktywny dla klasy urządzenia (preset `czat`) z tą rozmową; `new_conversation` = ten sam układ z
`conversation: null`. `SavedView` zyskuje `layout: string` (id) — zapis v2; czytnik v1 (`"chat"|"workshop"`) mapuje na
`czat`/`siatka` (test migracji w §10).

### 3.2 Co „zmaksymalizowany” znaczy dokładnie

[decyzja] `maximized` to flaga okna, nie tryb układu. Układ z jednym oknem `maximized` i resztą odstawioną **wygląda** jak zwykła
aplikacja; „przywróć” wraca do drzewa doków sprzed maksymalizacji (zapamiętanego w oknie jako `restore: {mode, dock|rect}`).
Odstawione okna mają uchwyty wg `shelf.handles` (lista w danych; w `czat` tylko lista rozmów) i są w przełączniku. Dzięki
temu T1(c) „standardowy interfejs to zmaksymalizowany” jest szczególnym przypadkiem, a nie gałęzią `if (layout === "chat")`
(`App.tsx:333`).

### 3.3 Nakładki (warstwy) a okna

[decyzja] Zostają dwie rzeczy: **okna** (trwałe, w układzie) i **warstwy** (przejściowe: menu ⋮, menu „+”, karta węzła,
krok ścieżki nawigatora, arkusz potwierdzenia). Warstwa ma jeden wpis historii (`LayerStack`, `history.ts`); okno nie — ale
*przełączenie aktywnego okna* na telefonie ma (§4.2). Dzisiejsze arkusze `views`/`semantic` i panele w `.drawer` to w nowym
systemie **okna otwierane w trybie `maximized` nad układem** (jak `#okno/...`): zamknięcie × = `window.close`, Wstecz = to samo.
Odwrót: §12 D6.

---

## 4. Telefon (390 px), tablet, desktop

### 4.1 Preset telefonu: stos zmaksymalizowanych okien z przełącznikiem

[decyzja] `device_class = phone` → układ aktywny `czat`; każde otwarcie okna (`window.open` bez `placement`) domyślnie
`placement: "maximize"` → okno ląduje na stosie jako kolejne zmaksymalizowane (preset klasy `phone` w danych; odwrót: §12 D18);
**przełącznik** (`switcher.style`) to dane:

| styl | wygląd [przykłady] |
|---|---|
| `bar` | pasek dolny z ikonami otwartych okien (jak `gnav-bar` nawigatora, `GraphNavigator.tsx:217`) |
| `tabs` | zakładki pod paskiem tytułowym |
| `sheet` | arkusz „Okna” z menu ⋮ (lista z miniaturami) — preset telefonu, bo nie zabiera miejsca |
| `swipe` | przeciągnięcie krawędzią (gest) |
| `none` | tylko zdania i menu |

Wszystkie style są dostępne na każdej klasie; preset per klasa w danych (`device_classes.json`, §4.3).

### 4.2 Wstecz na Androidzie i w przeglądarce

Dziś (obserwacja 2026-10-23): `LayerStack` (`history.ts`) trzyma jeden wpis historii na warstwę; `window.__loomBack()`
(`MainActivity.kt:198-218`, `BACK_JS` :366) zamyka wierzchnią warstwę, a u korzenia pyta wg `android_back_exit_confirm`.
[decyzja] Okna wpinają się w ten sam `LayerStack`, bez drugiego mechanizmu historii i Wstecz (jeden mechanizm do testowania;
odwrót: §12 D20).

[decyzja] Przełącznik prowadzi **dziennik odwiedzin** (`visits: ["w_chat","w_nav","w_settings"]`). Aktywacja okna B z A pcha na
`LayerStack` wpis `visit:<n>` z `close = aktywuj poprzednie`. Wstecz: najpierw warstwy (menu, karta), potem odwiedziny okien
(wracamy do poprzedniego okna), u korzenia — `android_back_exit_confirm` jak dziś. Zachowanie jest ustawieniem
`window_back` [przykłady]: `visits` (preset telefonu) | `to_chat` (Wstecz zawsze do okna czatu) | `close_window` (Wstecz zamyka
aktywne okno, jak × ) | `none` (Wstecz nigdy nie zmienia okna). Pętla A→B→A→B daje 3 wpisy i 3 Wstecz — to własność testowana
generatywnie (§10 P10). Escape na klawiaturze = Wstecz (bez zmian). Odwrót: §12 D7.

### 4.3 Klasy urządzeń i presety — dane

```jsonc
// device_classes.json (przykład; progi to presety, media query dziś: 721 px w shell.css:59 — 6 wystąpień w CSS, do zastąpienia zmienną)
{ "classes": [
  { "id": "phone",   "max_width": 720,  "layout": "czat",   "open_placement": "maximize", "switcher": "sheet",  "window_back": "visits" },
  { "id": "tablet",  "max_width": 1023, "layout": "czat",   "open_placement": "beside",   "switcher": "tabs",   "window_back": "visits" },
  { "id": "desktop",                    "layout": "czat",   "open_placement": "beside",   "switcher": "bar",    "window_back": "none" }
] }
```

Desktop preset `czat`: to samo co telefon, lecz `w_list` `collapsed: false` (kolumna) — czyli wygląd ChatGPT/Claude web.
Układ „warsztat” (makieta) jako preset desktopu/tabletu: lista 0.22 | [czat 0.7 / struktura projektu + środowisko] |
artefakt 0.3, graf pływający 0.46×0.40 nad czatem. Osoba może na telefonie wybrać `warsztat` — układ jest zaprojektowany tak, by otwierał się na każdej klasie urządzenia (nic go nie
wyłącza); zacznie działać po W3/W4, dziś (2026-10-23) nie jest zbudowany, a jego użyteczność na 390 px jest niezmierzona.

### 4.4 Pływanie i dokowanie na telefonie: dostępne, z podpowiedzią

[decyzja] Żadna operacja nie jest wyłączona na `phone` (T2b). Podpowiedzi R58 (`advisory`, niewiążące, z „nie pytaj następnym
razem” zapisanym w `advisory_ack`) — rodzaje [przykłady, każdy z id i tekstem w danych]:

| id | kiedy | tekst dla właściciela |
|---|---|---|
| `dock_narrow` | po dokowaniu liść ma szerokość < `min_useful_width` (preset 240 px, w danych) | „Dwa zadokowane okna na 390 px mają po 180 px. Czy tak ma zostać? [Zostaw] [Zmaksymalizuj] [Nie pytaj więcej]” |
| `float_offscreen` | > 50 % prostokąta poza obszarem | „Okno «Graf» jest w większości poza ekranem. [Przyciągnij] [Zostaw]” |
| `float_under_keyboard` | klawiatura ekranowa zakrywa pływające okno z fokusem | „Klawiatura zakrywa okno. [Przesuń nad] [Zostaw]” |
| `many_windows` | liczba zamontowanych okien > `mounted_soft` (preset telefonu 6, w danych; §8.3) | „8 okien naraz na telefonie zużywa pamięć. [Odstaw niewidoczne] [Zostaw]” |
| `coupling_cycle` | pętla zależności nie zbiega (§5) | „Zależności okien tworzą pętlę. [Rozłącz ostatnią] [Zostaw]” |

Zasada: podpowiedź **nigdy nie cofa** operacji sama; przyciski są akcjami; kolejność przycisków to [preset w danych `advisory_button_order`]: „Zostaw” jest domyślnie pierwszy,
każdą inną kolejność można ustawić.

---

## 5. Sprzężenie parametrów między oknami (UI16)

### 5.1 Model

[decyzja] Każdy parametr okna (`params.<name>`) ma **wiązanie** (`binding`):

```jsonc
"params": {
  "conversation": { "binding": "shared",      "channel": "conversation" },                    // wspólny kanał układu
  "time_range":   { "binding": "independent", "value": { "from": "2026-10-01", "to": "2026-10-23" } },
  "zoom":         { "binding": "linked", "from": { "window": "w_nav1", "param": "zoom" }, "dependency": { "kind": "scale", "factor": 2 } },
  "perspective":  { "binding": "linked", "from": { "channel": "focus" },                  "dependency": { "kind": "perspective_of_node" } }
}
```

- **shared** — wartość w `layout.channels[channel]`; każde okno z tym wiązaniem czyta i pisze to samo (dziś: `coupling: "coupled"`
  w `view-state.ts:2`, tylko dla rozmowy).
- **independent** — wartość we własnym stanie okna (dziś: `"independent"` + `selectedId`).
- **linked** — wartość obliczana z innego okna/kanału przez **zależność** (dziś: brak; spec UI16 luka: „no 'linked by chosen
  dependency'”).

Kanały są dowolne (nazwy w danych); zalążek: `conversation`, `focus` (węzeł), `time_range`, `perspective`, `zoom`,
`selection` (zbiór węzłów), `turn`. Zależności jako dane [przykłady]: `same`, `offset` (czas: −1 dzień), `scale` (zoom ×2),
`parent_of` / `children_of` (węzeł → węzeł), `perspective_of_node` (węzeł → perspektywa wg rodzaju), `expression` (małe wyrażenie
po polach: `other.zoom * 2`, sandbox bez dostępu do niczego poza wartościami). Nowa zależność = nowa pozycja w danych +
funkcja czysta w `shell/windows/dependencies.ts` zarejestrowana po id; brak funkcji dla id = okno pokazuje „zależność
«x» nieznana” (nie milczy).

### 5.2 Propagacja i pętle

[decyzja] Zmiana wartości → propagacja po grafie wiązań do punktu stałego; limit iteracji `coupling_max_iterations` (preset 8,
w danych); brak zbieżności → stan ostatniej iteracji + podpowiedź `coupling_cycle`. Pętla `same`↔`same` zbiega w 1 kroku i jest
poprawna (dwa okna wzajemnie równe) — R58: obsłużone, nie zabronione.

### 5.3 Interfejs sprzężenia

[przykład, w danych] Przy parametrze w pasku okna ikona łańcucha (przykładowy zestaw: 🔗 wspólny / ⛓ niezależny / ↔ powiązany) —
przykładowy gest: stuknięcie zmienia tryb w kółko (kolejność jest przykładem), długie naciśnięcie otwiera wybór zależności;
przykładowe zdanie: „połącz zakres czasu tego grafu z tamtym, przesunięty o dzień” (§6). Ikony, kolejność, gest i zdanie to
dane (`keymap`, `gestures`, `chat_coverage.pack`), nie reguła; ta sama akcja ma wiele dróg (§6).
Migracja `view-state.ts`: `coupled` → `shared/conversation`, `independent` → `independent/value` (test migracji w §10 P12).

---

## 6. Operacje na oknach: akcje, zdania, gesty, klawiatura, Wstecz, dostępność

### 6.1 Akcje (katalog) wykonywane po stronie klienta

[decyzja] Operacje okien to akcje katalogu z wykonaniem po stronie strony (wzór dziś: zdarzenie `open-settings`, które jądro
emituje, a `App.tsx:140-148` interpretuje; `client-step-events.test.mjs`). Jądro trasuje zdanie → akcja → krok klienta
`{"client": "window", "op": ..., "args": ...}`; strona wykonuje i odpowiada paragonem (co zrobiła, jakim układem). Zalążek
listy (każda z odwrotnością do dziennika):

| akcja | argumenty | odwrotność |
|---|---|---|
| `window.open` | kind, node?, placement ∈ {maximize, beside, below, float, tab, shelf} | `window.close` |
| `window.close` | id \| `all_except:[ids]` \| `kind:<k>` | `window.open` z zapisanym stanem |
| `window.maximize` / `window.restore` | id? (domyślnie fokus) | wzajemnie |
| `window.dock` | id, target: {leaf \| side ∈ {left,right,top,bottom}, ratio?} | poprzedni stan |
| `window.float` | id, rect? | poprzedni stan |
| `window.minimize` | id | `window.restore` |
| `window.focus` / `window.next` / `window.prev` | id / — | poprzedni fokus |
| `window.move` / `window.resize` | id, rect lub delta | poprzedni rect |
| `window.swap` | a, b | sam |
| `window.link` / `window.unlink` | id, param, from, dependency | wzajemnie |
| `layout.apply` | id | poprzedni układ (cały) |
| `layout.save` | label, device_class? | `layout.delete` |
| `layout.undo` / `layout.redo` | — | wzajemnie |
| `layout.reset` | — (preset klasy urządzenia) | poprzedni układ |

### 6.2 Zdania (chat_coverage)

Każda akcja ma frazy w `chat_coverage.pack` (schemat `loom.chat_coverage/1`, test `test_chat_coverage.cpp` sprawdza, że
przykładowe zdanie trafia do akcji przez router). Zalążek [przykłady PL; EN i inne języki paczki jak w K2b-4]:

- „otwórz graf obok” → `window.open {kind: navigator, placement: beside}`; na telefonie preset klasy zamienia `beside` na
  `maximize` **i mówi o tym w paragonie** („na telefonie: jako kolejne okno”), chyba że zdanie mówi „obok” wprost — wtedy dok
  + podpowiedź `dock_narrow`.
- „zmaksymalizuj” → `window.maximize` (fokus); „zmaksymalizuj ustawienia” → id po rodzaju.
- „zamknij wszystko poza czatem” → `window.close {all_except: [kind:chat]}`; przypięte zostają, paragon je wymienia.
- „pokaż ustawienia w pływającym oknie”, „przypnij graf do prawej”, „schowaj listę rozmów”, „zapisz ten układ jako «praca»”,
  „wróć do układu «praca»”, „cofnij układ”, „połącz czas tych grafów”, „pokaż «Ty» obok czatu”.
- Zdanie, którego nie da się wykonać samym zdaniem (np. „przesuń okno” bez celu) → `chat_outcome: unavailable` z powodem i
  propozycją (jak w K2b-2), nie cisza.

### 6.3 Menu, gesty, klawiatura — dane (`keymap`), nie kod

- **Menu ⋮** [przykład, zalążek w danych] (`layouts.json` → `menu`): pozycja „Okna” (arkusz przełącznika) i „Układ ▾” (zapisane,
  zapisz, cofnij, reset). Pasek tytułowy okna [przykład ikon i pozycji]: ⤢ (maksymalizuj/przywróć), ⊟ (zwiń), × (zamknij),
  … (menu okna: dokuj do…, odepnij, zakładka, sprzężenia, styl, nazwij). Zestaw ikon i pozycji to dane, lista jest otwarta.
- **Gesty** [przykłady, w danych `gestures`]: przeciągnięcie paska tytułowego → odpięcie (`float`) i strefy upuszczenia przy
  krawędziach (`dock side`) i nad zakładkami innego liścia (`tab`); długie naciśnięcie paska → menu okna; podwójne stuknięcie
  paska → maksymalizuj/przywróć; przeciągnięcie krawędzi podziału → `ratio`; uchwyt w rogu → `resize`; szczypnięcie w oknie
  grafu to parametr `zoom` okna, nie okno.
- **Klawiatura** (`keymap.json`, zalążek): `Ctrl+Shift+←/→/↑/↓` dokuj, `Ctrl+Shift+F` pływaj, `Ctrl+Shift+M` maksymalizuj,
  `Ctrl+Tab` następne okno, `Ctrl+Shift+Z` cofnij układ, `Escape` = Wstecz (bez zmian). Rozmiar klawiatury pod `prefers-reduced-motion`
  nie ma wpływu; animacje otwarcia/doku z `ui_animation.pack` (element `window`).

### 6.4 Dostępność

- Każde okno: `role="region"` z `aria-label` = tytuł z węzła; okno zmaksymalizowane nad układem: `role="dialog"` + `aria-modal`
  i `inert` na reszcie (jak dziś `.drawer`, `App.tsx:223`, `inertAttr`).
- Kolejność fokusu [preset w danych `focus_order`]: drzewo doków w porządku czytania (a przed b), potem pływające po `z` rosnąco,
  potem uchwyty półki; `Ctrl+Tab` idzie po tej kolejności. Inną kolejność (np. pływające najpierw) ustawia się w danych. Przełącznik = `role="tablist"`, okna = `tabpanel`.
- Cele dotyku [preset w danych `touch_target_px`]: ≥ 44×44 px dla przycisków paska i uchwytów (wartość jak dziś w
  `shell.css:5,32` — obserwacja 2026-10-23); krawędź podziału ma [preset `split_grip_px`] 24 px strefę chwytu (niewidoczną), cel
  44 px na uchwycie. Mniejsze wartości działają i wywołują tylko niewiążącą podpowiedź („cel dotyku poniżej 44 px — czy celowo?”).
- Czytnik ekranu: zmiana aktywnego okna ogłaszana (`aria-live="polite"`: „Okno: Graf”); podpowiedzi R58 w `role="status"`.

---

## 7. Suma funkcjonalności i alternatywy stylistyczne (R64)

### 7.1 Funkcje jako zdolności okna czatu (dane)

[odczyt] „Suma funkcjonalności popularnych apek” mierzy się macierzą unii funkcji z
`docs/architecture/REPLIKACJA_PRZEPLYWOW_DOSTAWCOW_2026-10-23.md` §5 (wiersz = funkcja atomowa testowalna, kolumna = aplikacja
z wersją, komórka = status z dowodami; kolumna `loom` wypełniana testem e2e). Z tej macierzy wiersze **interfejsowe**
(artefakty/canvas — wiersz 12; rozgałęzianie/wersje — 11; głos — 13; wyszukiwanie w sieci — 9; akcje wiadomości, startery,
zwijanie wiadomości z UI08, pasek boczny…) to **zdolności** (`capabilities`) rodzaju `chat`, włączane per układ w sekcji
`chat` (dziś już: `message_actions`, `composer`, `starters` w `layouts.json`). Układ `czat` (preset) włącza unię; układ
„minimalny” może włączyć tylko `text` i `send` — oba są układami tego samego rodzaju. Wiersze nieinterfejsowe (MCP, hooki,
piaskownica) nie należą do okna — to przepływy/narzędzia (REPLIKACJA §2, §6).

Test: kolumna `loom` macierzy dla wierszy interfejsowych jest wypełniana z e2e, które otwierają układ `czat` na 390×844 i
sprawdzają obecność każdej zdolności (scenariusz per wiersz); wiersz bez scenariusza = `unknown`, widoczny w macierzy.

### 7.2 Styl = profil aplikacji, per okno lub per układ

Dziś profil (`loom.application_profile/1`, `runtime.ts:17-19`): `renderer`, 6 tokenów koloru (hex), `sidebar.side/width`,
`content_width`, `message_style ∈ {plain, bubble, user-bubble}`; 10 dokumentów w `profiles/data/` (claude/chatgpt/gemini-inspired
r1/r2, librechat-0.8.8, nextchat-2.16.1, loom-default r1/r2); jeden renderer.

[decyzja] Dziedziczenie: `window.profile` → `layout.profile` → profil osoby → `loom-default`. Profil działa przez zmienne CSS
na korzeniu okna (dziś `profileStyle(primaryProfile)` na `.app-root`, `App.tsx:264`), więc dwa okna czatu mogą mieć dwa style
naraz (porównanie „jak w ChatGPT” / „jak w Claude” obok siebie — przykład użycia, nie wymóg). Pola profilu, które dotyczą
układu (`sidebar.side/width`), są **sugestią**, a to, gdzie się je stosuje, ustawia `profile_layout_fields` [przykłady wartości]:
`layout` ([preset] stosuje je zastosowanie profilu do układu; może to przestawić dok listy rozmów — `layout.apply` wie o tym
i zapisuje odwrotność) | `window` (stosuje je także profil przypisany do okna: dotyczą podziału, w którym okno siedzi) | `off`
(nikt ich nie czyta). Gdy zastosowanie — w trybie `layout` albo `window` — przestawiłoby dok, czyli zmieniło `side` lub
`ratio` podziału ustawionego ręcznie, pokazuje się niewiążąca podpowiedź `profile_moves_dock` [przykład tekstu]: „Styl «jak w
ChatGPT» chce listy rozmów po lewej, a jest po prawej. [Przestaw] [Zostaw] [Nie pytaj więcej]”; wybór ląduje w `advisory_ack`.

UI17: profil nie zawiera i nie dotyka `model`, uprawnień, prywatności ani doboru kontekstu — test (W9): zmiana profilu na oknie
i na układzie nie zmienia żadnego pola żądania (`workflow-snapshot.ts:22` już mówi: „no request/model settings are captured implicitly”).
UI14: profile „archiwalne” (wersja z datą) to kolejne dokumenty tego samego schematu; system okien ich nie rozróżnia.

---

## 8. Co jest dziś, a co projektuje ten dokument — uczciwie

### 8.1 Tabela

| obszar | dane dziś | kod dziś | testy dziś | projekt |
|---|---|---|---|---|
| układy | `layouts.json` (`loom.shell_layouts/1`): `czat` (pasek, menu, akcje, composer, startery) i `warsztat` (tylko `note`) | `App.tsx:99-132` wybór `layout`, dwa bloki renderowania (`:262-331` warsztat, `:333-347` czat) | `shell.test.mjs:42-59`, `menu.test.mjs`; e2e: `#warsztat` w 32 plikach, `shell-menu-workshop` w 6, `toggle-nav` w 5, `nav-chat` w 2 | `loom.window_layout/2`: drzewo doków, okna, kanały, przełącznik; `czat`/`siatka`/`warsztat` jako presety |
| tryb | `layout_mode`: `full_screen_chat` (preset), `views_grid`, `windows` (planned + powód) `settings_fields.pack:850-880` | `layoutOfMode()` `start-view.ts:18-20` | `shell.test.mjs:54-59` (windows planned) | `windows` = ostatni własny; `layout:<id>`; test „planned” znika w W3 |
| panele | lista w kodzie `panels.ts` (15 id), etykiety `PANEL_LABELS` w `App.tsx:58` | `.drawer` modalny (`App.tsx:223-257`), jeden naraz (`activePanel`) | e2e per panel | rejestr rodzajów okien jako dane (§2.3), wiele naraz |
| warstwy/Wstecz | — | `LayerStack` `history.ts`, `useLayer`, `__loomBack` ↔ `MainActivity.kt:198-218,366` | `history.test.mjs` (M1-FLAKE1), `m1-history-native.mjs` 390×844 | bez zmian + odwiedziny okien (§4.2) |
| sprzężenie | — | `view-state.ts`: `coupled\|independent`, tylko rozmowa, w `ApplicationProfiles` | `application-profile-state.mjs` | wiązania per parametr, kanały, zależności (§5) |
| profile | 10 JSON `profiles/data/`, schemat ścisły (`runtime.ts:205-232`) | `profileStyle` na `.app-root` (cały warsztat) | `application-profiles.mjs`, `profile-view-geometry.mjs` | per okno/układ, dziedziczenie (§7.2) |
| przełącznik okien | — | — | — | §4.1 |
| nazwane układy, cofanie | — | `loom.shell.last.v1` w `localStorage` | `shell.test.mjs:48-52` | §2.5 |
| podpowiedzi R58 z pamięcią | — | — (spec R58: brak) | — | `advisory_ack` wspólne z G3 |
| artefakt jako okno | — | `ImportedMessageContent.tsx:84-93` sekcja; brak detekcji w odpowiedziach | `imported-message-content-*.mjs` | rodzaj `artifact` (planned → W8) |
| klawiatura/gesty | `ui_animation.pack` (animacje) | Escape w `history.ts` | — | `keymap`, `gestures` jako dane |

### 8.2 Co się psuje i jak to przyjmujemy

1. **Typ `Layout = "chat" | "workshop"`** (`start-view.ts:3`, `App.tsx:32,99`): staje się `string` (id układu). Łamie: `shell.test.mjs`
   (asercje `'workshop'`/`'chat'`), `readSaved` v1. Przyjęcie: czytnik v1 mapuje na `czat`/`siatka`; testy uaktualnione w W1/W3.
2. **E2E, które wchodzą przez `#warsztat` i klikają `toggle-nav` → `nav-<panel>`** (nagłówek warsztatu, `App.tsx:275-300`): po W3
   nagłówek znika. Przyjęcie: `#warsztat` zachowuje dzisiejsze znaczenie **do końca W3** (patrz §11 pkt 3: alias), a `data-testid`
   `toggle-nav`/`nav-<panel>` pozostają na przełączniku okien (ten sam id, inna rola) — e2e działają bez zmian treści; 2 pliki z
   `nav-chat` dostają `nav-chat` jako „aktywuj okno czatu”.
3. **`ChatView variant="chat"|"workshop"`** (`ChatView.tsx:102-103`, jedno sprawdzenie w 1564 liniach): `variant` zastępuje lista
   zdolności z układu (§7.1); `workshop` = unia + „Graph reply”, „Context for next message”, „Expert” jako osobne okna.
4. **`SettingsPanel` vs `SettingsHuman` wybierane po `layout`** (`App.tsx:239-240`): dwa rodzaje okien (`settings`,
   `settings_fields`), oba w rejestrze, oba dostępne wszędzie.
5. **`behindOverlay`/`inert`** liczone z `activePanel !== null || knowledgeOpen` (`App.tsx:336`): zastępuje je „istnieje okno
   `maximized` nad układem lub warstwa modalna”.
6. **Android smoke S2** (`QUEUE_S1` P20: kliknięcie `shell-menu-toggle` → `shell-menu-connection`): bez zmian (menu ⋮ zostaje).

### 8.3 Wydajność na WebView: wiele okien, pamięć

[odczyt] Każde okno to zamontowane poddrzewo React; `GraphNavigator` z mapą (silnik widoku, canvas) wydaje mi się najcięższy
(odczyt kodu, niezmierzony). Ile pamięci zajmuje wiele okien w WebView telefonu i czy `performance.memory` zwraca tam użyteczne
liczby — **niezweryfikowane (stan 2026-10-23, brak pomiaru), do pomiaru w W10**; z pamięci: pole jest niestandardowe i znane
z przeglądarek na Chromium, ale strona MDN nie dała się pobrać 2026-10-23 (odmowa proxy 403), więc to nie jest źródło.
[decyzja] Polityka montowania to ustawienie `window_keep_mounted` [przykłady]: `visible` (preset telefonu, wybrany z ostrożności,
a nie ze zmierzonego faktu: okna niewidoczne —
odstawione, zwinięte, nieaktywne zakładki — są odmontowane, ich stan serializowany w układzie; powrót = ponowne zamontowanie z
tego stanu) | `all` (preset desktopu) | `recent:<n>`. Nie ma górnej liczby okien; `many_windows` to podpowiedź (§4.4).
Pomiar w pakiecie W10 na emulatorze S2: czas przełączenia okna i RSS WebView dla 1/4/8/16 okien, wynik do `INDEX.md` jako
obserwacja z datą. Ryzyko: okna z własnym stanem niezserializowanym (przewinięcie listy, pozycja mapy) tracą go przy
odmontowaniu — każdy rodzaj deklaruje w rejestrze `state_keys`, które pola trzymać; brak deklaracji = test rejestru czerwony.

### 8.4 Ryzyka (trzy największe najpierw)

1. **Przebudowa `App.tsx` bez regresji na telefonie**: 32 pliki e2e (z 61 w `loom/web/e2e/`; `grep -l '#warsztat'`, 2026-10-23) zakładają dwa układy. Osłona: W1 ma zero widocznych zmian
   (zrzut 390×844 identyczny co do piksela z dzisiejszym, próg w danych), W3 trzyma aliasy testid i `#warsztat`.
2. **Wstecz**: odwiedziny okien wplecione w `LayerStack` mogą wejść w kolizję z M1-FLAKE1 (późne `popstate`). Osłona: ten sam
   model „wpis = warstwa”, test generatywny P10 na czystej logice i `m1-history-native.mjs` rozszerzony o okna.
3. **Pamięć WebView** przy wielu oknach grafu (ryzyko niezmierzone, 2026-10-23). Osłona: `window_keep_mounted=visible` jako
   preset telefonu (ostrożność), pomiar W10, podpowiedź `many_windows`.
4. Rozjazd danych: układ zapisany na desktopie (px) przywracany na telefonie — dociskanie prostokątów + podpowiedzi (P11).
5. Kolizja nazwy „Warsztat” (§11 pkt 3) — decyzja odwracalna jedną zmianą etykiet.
6. Mechanizm `advisory_ack` nie istnieje i jest potrzebny także G3 — kto pierwszy go zbuduje, ten ustala schemat; propozycja
   schematu w §2.5 (klucz = id podpowiedzi, wartość = `{ack_at, choice}` na poziomie `person`).

---

## 9. Pakiety wdrożeniowe (kolejność; każdy ≈ 50 wywołań narzędzi, commit + karta; bez godzin i dat)

| # | pakiet | zakres | akceptacja (e2e 390×844 o ile dotyczy) | właściciel dotyka |
|---|---|---|---|---|
| **W0** | model jako dane + czysta logika | `shell/data/window_kinds.json`, `layouts.json` v2 (`czat`, `siatka` jako drzewa), `shell/windows/model.ts` (typy, operacje totalne, dziennik), czytnik v1→v2; testy jednostkowe + generatywne (§10 P1–P8); żaden komponent nie zmieniony | `node --test tests/shell` zielone; test rejestru: każdy komponent z `App.tsx`/`ChatShell.tsx` w rejestrze lub `not_window` | nic widocznego |
| **W1** | czat jako zmaksymalizowane okno | `WindowHost` renderuje układ `czat` (lista zwinięta + czat maximized); `ChatShell` staje się treścią okna `chat`; panele otwierają się jako okna `maximized` nad układem; `#czat`, `start_view`, Wstecz bez zmian | zrzuty 390×844 z `m1-*-native.mjs` identyczne z dzisiejszymi (próg różnicy w danych); `shell.test.mjs` uaktualniony | **telefon: nic się nie zmienia** — to jest akceptacja |
| **W2** | przełącznik + „otwórz graf obok” + układ „ostatni” | arkusz „Okna” z menu ⋮ (`switcher: sheet`), dziennik odwiedzin w `LayerStack`, `window.open/close/focus/next/prev` jako kroki klienta, zdania „otwórz graf obok”, „zamknij wszystko poza czatem” w `chat_coverage.pack`, `window_layout_last` w `localStorage` v2 | e2e: czat → „otwórz graf obok” → okno Graf → Wstecz → czat; arkusz „Okna” pokazuje 2 pozycje; po przeładowaniu `last_saved` przywraca oba okna | **tak: pierwszy dotykalny pakiet** |
| **W3** | dawny Warsztat jako układ `siatka` | drzewo doków dla 15 paneli (zakładki), nagłówek warsztatu znika, `toggle-nav`/`nav-<panel>` na przełączniku, `layout_mode=windows` bez „planned”, aliasy `#warsztat` (§11 pkt 3), `SettingsPanel` jako rodzaj | wszystkie e2e z `#warsztat` zielone bez zmiany treści; `shell.test.mjs:54-59` zmienia asercję „planned” | desktop: warsztat wygląda jak dziś, z zakładkami |
| **W4** | dokowanie i pływanie | drag paska tytułowego, strefy upuszczenia, `rect`, `z`, krawędzie podziału, podpowiedzi `dock_narrow`/`float_offscreen`/`float_under_keyboard` + `advisory_ack` (schemat wspólny z G3) | e2e: na 390 px zadokuj graf obok czatu → podpowiedź z „Nie pytaj więcej” → drugi raz bez pytania; odepnij ustawienia → pływają nad czatem | tak |
| **W5** | nazwane układy, cofanie, per osoba/urządzenie | `window_layouts`, `window_layout_active` (klasa), `window_layout_journal` w `settings_fields.pack`; „Układ ▾” w menu; `layout.save/apply/undo/redo/reset` | e2e: zapisz „praca” na telefonie, przeładuj, „wróć do układu «praca»” zdaniem; cofnij po zamknięciu okna | tak |
| **W6** | sprzężenia | wiązania per parametr, kanały, zależności `same/offset/scale/parent_of/perspective_of_node/expression`, migracja `view-state.ts`, ikona łańcucha | e2e: dwa okna grafu, zoom powiązany ×2, pętla → podpowiedź `coupling_cycle` | tak (dwa grafy) |
| **W7** | katalog akcji + zdania + klawiatura/gesty jako dane | pełna lista §6.1 w katalogu, `chat_coverage.pack` z frazami PL/EN, `keymap.json`, `gestures.json`; `test_chat_coverage` zielony | każde zdanie z §6.2 trafia do akcji przez router (test C++); e2e: „zmaksymalizuj ustawienia” | tak |
| **W8** | artefakt jako okno (UI08) | detekcja bloków (kod/HTML/markdown) w odpowiedziach, rodzaj `artifact` z przyciskami, otwarcie `float` (preset) lub `beside`; `request_inspector` jako okno z czatu | e2e: odpowiedź z blokiem kodu → ramka z „Kopiuj/Otwórz w oknie/Zapisz” → okno pływające | tak |
| **W9** | style per okno/układ + test UI17 | `window.profile`, `layout.profile`, dziedziczenie, `sidebar` jako sugestia (`profile_layout_fields`: `layout` preset | `window` | `off`; podpowiedź `profile_moves_dock`); e2e: zmiana profilu nie zmienia żadnego pola żądania | e2e UI17 (brakujący wg spec: „no test asserting a profile switch leaves model/privacy/context untouched”) | tak |
| **W10** | wydajność WebView | `window_keep_mounted`, `state_keys` w rejestrze, pomiar 1/4/8/16 okien na emulatorze S2 → `INDEX.md` | liczby w raporcie; podpowiedź `many_windows` | pośrednio |
| **W11** | rodzaje z makiety | `project_structure`, `execution_env`, `media_player`, `external` — każdy osobny pakiet po projekcie swojego obszaru (UI11, P39); do tego czasu `planned` z powodem widoczne w przełączniku | — | później |

Kolejność uzasadniona: W0–W2 dają telefonowi coś do dotknięcia bez ryzyka regresji (W1 = brak zmian jako test); W3 usuwa
rozwidlenie `if (layout === "chat")` zanim W4–W6 dodadzą stany; W7 domyka R54 (wszystko ze zdania); W8–W9 to treść okna
(R64 d); W10 po tym, jak jest co mierzyć.

---

## 10. Testy generatywne modelu okien (R59: przestrzeń możliwości)

[decyzja] Czysta logika (`shell/windows/model.ts`) testowana `node:test` + generator własności. Biblioteka: `fast-check`
(MIT) jako devDependency z wersją przypiętą przy dodaniu (dziś brak w `package.json`); zamiennik bez zależności: własny
generator z ziarnem (`seed` w danych testu, wypisywany przy porażce). Generatory: rodzaj ∈ rejestr; `mode` ∈ 5; drzewo
binarne losowej głębokości (preset 0–6) z `ratio` ∈ [0,1] **włącznie z 0 i 1**; prostokąty z ujemnymi, zerowymi i większymi od
obszaru; obszar ∈ {390×844, 360×640, 768×1024, 1440×900, 1×1} (preset zbioru); sekwencje (preset 0–40) operacji z §6.1 z losowymi
argumentami (także nieistniejące id); grafy wiązań z pętlami; zapisy v1 (`"chat"|"workshop"`) × `layout_mode` (4 wartości +
śmieci) × hash. Głębokość 0–6, 0–40 operacji i pięć obszarów to **presety w danych testu** (`max_depth`, `max_ops`, `areas`),
przykłady z przestrzeni możliwości, nie jej granice; reszta jest wypisana w „Lukach” poniżej.

Własności (każda z nazwą testu):

| # | własność |
|---|---|
| P1 | **Totalność**: każda operacja na każdym układzie zwraca układ poprawny względem schematu; nigdy nie rzuca; nieistniejące id → wynik `noop` z powodem |
| P2 | **Nic nie znika po cichu**: każde okno układu jest widoczne albo w przełączniku albo ma uchwyt na półce (zbiór okien = zbiór osiągalnych) |
| P3 | **Round-trip**: `read(write(L)) ≡ L` (po normalizacji: zwinięte puste liście) |
| P4 | **Odwracalność**: dla każdej operacji `op`, `undo(op(L)) ≡ L`; `redo(undo(op(L))) ≡ op(L)` |
| P5 | **Drzewo**: po operacjach każdy liść ma ≥1 okno albo został zwinięty; `ratio` ∈ [0,1]; każdy `dock.leaf` istnieje |
| P6 | **Z-order**: `z` okien pływających jest permutacją 0..k−1 po `window.focus` |
| P7 | **Fokus**: `focus ∈ windows` lub `null` dokładnie wtedy, gdy układ pusty |
| P8 | **Predykat „zwykła aplikacja”**: układ `czat` spełnia „dokładnie jedno okno `maximized` rodzaju `chat`, lista `collapsed`”; `window.maximize(chat)` na dowolnym układzie daje układ spełniający predykat po `window.close(all_except: chat)` |
| P9 | **Sprzężenia**: propagacja kończy się ≤ `coupling_max_iterations`; po zbieżności wszystkie wiązania `shared` jednego kanału mają równą wartość; pętla `same` zbiega; pętla `offset≠0` → podpowiedź `coupling_cycle` dokładnie raz |
| P10 | **Wstecz**: po k aktywacjach okien k wywołań `back()` wraca do okna początkowego, (k+1)-sze zwraca `false`; przeplot z warstwami (menu, karta) zachowuje porządek LIFO |
| P11 | **Zmiana obszaru**: układ z obszaru A zastosowany w B: każde okno osiągalne (P2), prostokąty dociśnięte do B, liczba podpowiedzi `dock_narrow` = liczba liści o szerokości < `min_useful_width` |
| P12 | **Migracja**: każda para (zapis v1, `layout_mode`) × hash daje układ `czat` albo `siatka` z tą samą rozmową; `coupled`/`independent` z `view-state` → wiązania o tej samej rozmowie widocznej |
| P13 | **Rejestr**: dla każdego rodzaju `present` istnieje komponent; dla `planned` istnieje `reason.pl`; każdy komponent montowany w `App.tsx`/`ChatShell.tsx` (lista przez grep w teście) jest w rejestrze lub w `not_window` |
| P14 | **Profil (UI17)**: zastosowanie dowolnego profilu per okno/układ nie zmienia żadnego pola poza `profile` (porównanie strukturalne) |

**Luki (uczciwie, nie „poza zakresem”)**: przestrzeń poza presetami generatora (drzewa głębsze niż 6, sekwencje dłuższe niż 40
operacji, obszary spoza pięciu — np. okna składane, skalowanie systemowe, 0×0, dwa okna przeglądarki) jest niepokryta; zmniejsza
ją podniesienie presetów w dłuższym przebiegu (parametr, nie zmiana kodu); gesty, strefy upuszczenia i rzeczywiste rozmiary zależą od przeglądarki — tylko e2e
przykładowe (Playwright, 390×844); pamięć WebView — tylko pomiar (W10); zachowanie `popstate` prawdziwego Androida — tylko smoke
S2; czytniki ekranu — brak automatycznego testu (ręcznie TalkBack w smoke, jedna lista kontrolna w danych).

---

## 11. Błędy w rozumowaniu właściciela (i gdzie mój odczyt może być błędny)

1. **„standardowy interfejs to zmaksymalizowany” a pasek boczny.** Standardowy interfejs ChatGPT/Claude na desktopie ma pasek
   boczny z rozmowami — to nie jest „jedno zmaksymalizowane okno”. Rozstrzygam **jako dane** (D5): lista rozmów jest osobnym
   oknem rodzaju `conversation_list`, w układzie `czat` zadokowanym po lewej; na telefonie `collapsed: true` (szuflada ☰), na
   desktopie `collapsed: false` (kolumna). Zdanie właściciela jest więc prawdziwe **dosłownie na telefonie** i **jako preset** na
   desktopie; nie jest niezmiennikiem systemu. Alternatywa (lista wewnątrz okna czatu) tworzyłaby drugi mechanizm listy — odrzucona,
   odwrót w D5.
2. **„suma funkcjonalności” w jednym oknie a „niezależne elementy” (UI16/UI17).** Unia wszystkiego w jednym oknie czatu
   kłóci się ze składalnością, jeśli jest sztywna. Rozstrzygnięcie: funkcje to zdolności włączane per układ (§7.1); preset `czat`
   włącza unię, nic nie wymusza unii. Zdanie o „standardowym interfejsie” odczytuję jako opis presetu, nie niezmiennika — to mój odczyt, nie jego słowa.
3. **„Warsztat” znaczy dwie rzeczy** (R66: „work band to był dla ciebie do testów tak?”). Dziś `Warsztat` = siatka wszystkich
   paneli z etykietą „Warsztat (narzędzia zaawansowane)”; w makiecie i w R64 warsztat = składany obszar roboczy. Decyzja D9:
   id `siatka` (etykieta „Wszystkie panele”) dla dzisiejszej siatki; id `warsztat` (etykieta „Warsztat”) dla układu wg makiety
   (§4.3). `#warsztat` do końca W3 otwiera siatkę (testy), od W4 otwiera warsztat z makiety, a `#siatka` siatkę; e2e podmieniają
   hash jednym `sed` w W4. Jeśli właściciel woli, by „Warsztat” nadal znaczyło siatkę — zamiana dwóch etykiet w `layouts.json`.
4. **„na Androidzie … tak jak na desktopie”** — prawdziwe co do dostępności operacji, nieprawdziwe co do użyteczności: klawiatura
   ekranowa (`adjustResize`, `AndroidManifest.xml:61` — obserwacja 2026-10-23) zmniejsza obszar; o ile — niezweryfikowane,
   do pomiaru na emulatorze S2 (moja ocena „~40 %” nie ma źródła); pływające okno nad composerem może być wtedy zakryte. Nie wyłączam — podpowiedź
   `float_under_keyboard` z „Przesuń nad”. Koszt nazwany, nie ukryty.
5. **„fuzją wszystkich apek od kilku (5-10)”** — „wszystkich” i „5–10” naraz; czytam jako listę otwartą (R59): macierz §5 rośnie,
   a „fuzja” mierzy się odsetkiem komórek `unknown`, nie liczbą aplikacji.
6. **„obsługujące wszystko”** — dziś (obserwacja 2026-10-23: `ConnectionActivity.kt`, `AndroidManifest.xml:47`) ekran „Połączenie” jest natywną aktywnością
   Androida, a nie stroną — to stan dzisiejszy, nie niemożność. Rejestr trzyma go jako rodzaj `connection` ze `status: planned`
   i powodem „dostępne, gdy istnieje odpowiednik na stronie” (widoczny jako pozycja wyszarzona, nie ukryty; jak wpisy z powodem w
   `chat_coverage`); do tego czasu menu ⋮ otwiera ekran natywny jak dziś. Odpowiednik na stronie to osobna decyzja S2.
7. **„widok ostatni zapisany”** przywraca na starcie to, co było widoczne — na wspólnym urządzeniu pokazuje cudze rozmowy przy
   uruchomieniu. To nie błąd wymagania, ale kolejność `start_view` jest już konfigurowalna, a układ nigdy nie niesie sekretów (§2.5).
8. **Gdzie mój odczyt może być błędny**: (a) UI12 i większość UI16 to cytaty **wtórne** (relacja agenta) — model „drzewo doków”
   wyprowadzam z makiety, nie ze słów właściciela; (b) „zmaksymalizowany” może znaczyć u właściciela „pełnoekranowy bez paska
   tytułowego” — wtedy pasek tytułowy okna `chat` w układzie `czat` jest ukryty (`chrome: "none"` jako dane, już przewidziane:
   dziś `ChatShell` ma własny pasek z ☰ i ⋮, który staje się paskiem okna).

---

## 12. Decyzje i odwrót

| id | decyzja | odwrót |
|---|---|---|
| D1 | Okno = instancja rodzaju widoku + węzeł grafu; stany `maximized/docked/tabbed/floating/minimized` jako pole `mode` jednego obiektu | rozbić na klasy kontenerów: zmiana `mode` na `container.kind`; schemat v3 z czytnikiem v2 |
| D2 | Kontener doków = binarne drzewo podziałów z grupami zakładek w liściach | stałe strefy (lewo/prawo/dół/środek): `tree` zastąpione `areas`; drzewo o głębokości ≤2 mapuje się 1:1, konwerter w czytniku |
| D3 | `layout_mode` zostaje jako alias (`full_screen_chat`→`czat`, `views_grid`→`siatka`, `windows`→ostatni własny, `layout:<id>`) | usunąć alias: pole `layout_mode` oznaczone `deprecated` w `settings_fields.pack`, `window_layout_active` jedynym źródłem |
| D4 | Rejestr rodzajów okien jako dane w web (`window_kinds.json`), docelowo `windows.pack` w jądrze | zostać w web na stałe: `chat_coverage` sprawdza rejestr przez plik JSON w teście C++ (ścieżka w danych) |
| D5 | Lista rozmów = osobne okno `conversation_list`, w `czat` zadokowane i zwinięte do ☰ | lista wewnątrz okna `chat`: zdolność `embedded_list` rodzaju `chat`, okno listy usunięte z presetu `czat` |
| D6 | Dzisiejsze panele `.drawer` i arkusze stają się oknami `maximized` nad układem; warstwami zostają tylko menu/karta/krok/potwierdzenie | panele pozostają warstwami: rodzaj okna dostaje flagę `as_layer: true`, `WindowHost` renderuje je w `.drawer` jak dziś |
| D7 | Wstecz na telefonie cofa odwiedziny okien przez `LayerStack` (`window_back=visits` preset telefonu) | `window_back=none` jako preset: Wstecz tylko warstwy; dziennik odwiedzin zostaje do „poprzednie okno” |
| D8 | Etap 1 dane w `loom/web/src/shell/data/`, etap 2 nazwane układy w ustawieniach `person`/`installation` | zostać przy `localStorage` + eksport/import pliku układu (akcja `layout.export`) |
| D9 | Zmiana nazw: dzisiejsza siatka = `siatka` („Wszystkie panele”), `warsztat` = układ wg makiety; `#warsztat` zmienia znaczenie w W4 | zamiana dwóch etykiet i id w `layouts.json`; `#warsztat` zostaje siatką na zawsze, makieta pod `#makieta` |
| D10 | Podpowiedzi R58 z pamięcią `advisory_ack` (poziom `person`, klucz = id podpowiedzi) — schemat wspólny z G3 | per urządzenie (`installation`) albo bez pamięci (zawsze pytaj): zmiana poziomu pola |
| D11 | Sprzężenie per parametr: `shared/independent/linked` + kanały + zależności jako dane; pętle obsługiwane limitem iteracji i podpowiedzią | tylko `shared/independent` (jak dziś) — `linked` oznaczone `planned` z powodem, zależności nie ładowane |
| D12 | Operacje okien = akcje katalogu z krokiem klienta `{"client":"window"}`; zdania w `chat_coverage.pack` | operacje lokalne bez katalogu: wpis `not_from_chat` z powodem dla każdej (łamie R54 — tylko jako tymczasowy odwrót) |
| D13 | Polityka montowania `window_keep_mounted` (preset telefonu `visible`, desktopu `all`), bez górnego limitu okien | `all` wszędzie: jedno pole w `device_classes.json` |
| D14 | Styl per okno/układ przez zmienne CSS na korzeniu okna; pola układowe profilu (`sidebar.side/width`) jako sugestia stosowana wg `profile_layout_fields`: per układ (preset), per okno (opcja) albo wcale; gdy przestawiłaby dok — niewiążąca podpowiedź `profile_moves_dock` | `profile_layout_fields=layout` na stałe (pola układowe profilu okna nieczytane); najdalej: `window.profile` ignorowane z podpowiedzią „styl per okno wyłączony” |
| D15 | Testy generatywne na `fast-check` (devDependency, wersja przypięta) | własny generator z ziarnem w `tests/shell/windows/gen.mjs`, bez zależności |
| D16 | `#okno/<kind>[/<node>]` otwiera jedno okno `maximized` nad aktualnym układem | usunąć: linki tylko do układów (`#uklad/...`), okno z zewnątrz przez zdanie w czacie |
| D17 | Prostokąty w `fraction` jako preset, `px` dostępne; dociskanie do obszaru, nigdy gubienie | tylko `px` + przewijany obszar roboczy (okna poza ekranem osiągalne przewinięciem) |
| D18 | Telefon: `window.open` bez `placement` domyślnie `maximize` (preset `open_placement` klasy `phone`, §4.1, §4.3) | `open_placement: "beside"` (albo `float`/`tab`) dla klasy `phone` w `device_classes.json`; jedno pole, bez zmiany kodu |
| D19 | Aktywny układ per klasa urządzenia: `window_layout_active` na poziomie `person` z nadpisaniem `installation`; dziennik `window_layout_journal` (pierścień, preset 50) na poziomie `installation` (§2.5) | zmiana poziomu pola w `settings_fields.pack`: `window_layout_active` w całości na `installation` (każde urządzenie osobno) i/lub `window_layout_journal` na `person` (cofanie wspólne między urządzeniami) |
| D20 | Okna wpinają się w istniejący `LayerStack` (`history.ts`), bez drugiego mechanizmu historii i Wstecz (§4.2) | osobny stos odwiedzin okien obok `LayerStack`, z własnym wpisem historii; `window_back` wybiera, który stos obsługuje Wstecz (ryzyko podwójnego `popstate`, M1-FLAKE1 — do zmierzenia) |

---

### Co zrobiłem i czego nie

Przeczytałem (tylko-do-odczytu): `spec.py show` dla 14 id; `settings_fields.pack:835-880`; `App.tsx` (grep + `:99-135`, `:255-349`);
`shell/{ChatShell.tsx, start-view.ts, panels.ts, menu.ts, history.ts, use-layer.ts, Disclosure.tsx, data/layouts.json, shell.css}`
(grep); `profiles/view-state.ts`, `runtime.ts:17-19,205-232`, lista `profiles/data`; `GraphNavigator.tsx` (grep); e2e: lista,
viewport 390×844 w `m1-*-native.mjs`, zliczenia `#warsztat`/`shell-menu-workshop`/`toggle-nav`/`nav-chat`; `tests/shell/{shell,history}.test.mjs`
(grep); `MainActivity.kt` (grep Wstecz), `AndroidManifest.xml:61`; `QUEUE_S1.md` (nagłówki, uwagi 22.10), `INDEX.md:935-950`,
`REPLIKACJA_…` §5; makieta `.webp`. Nie czytałem `ChatView.tsx` w całości (1564 linie; grep: jedno sprawdzenie `variant`),
`docs/exports`, `eval/real-holdout-key`. Nie edytowałem `loom/`, nie budowałem, nie uruchamiałem testów. Liczby w tym dokumencie
(240 px, 50 wpisów, 8 iteracji, progi klas) to presety do danych, nie reguły.
