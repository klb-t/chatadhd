# M1-W0 — testid-y ekranów dla smoke Androida (kontrakt z S2)

Stan: WYPEŁNIONY (P7, 2026-10-21). Blok `gate0` niżej i tabele testid-ów pochodzą z prawdziwych ekranów; każdy testid z bloku
jest sprawdzany w `loom/web/e2e/m1-navigator-native.mjs` albo `m1-settings-native.mjs` (390×844, dotyk, prawdziwy `loom-server`).
Zrzuty tych ekranów: `docs/reports/M1_CHECKPOINT_0/`, opis: `docs/reports/M1_CHECKPOINT_0.md`.

Kontrakt z S2 (wiadomość z 2026-10-21): sterownik `drive.mjs` czyta **pierwszy blok kodu `json`
w tym pliku**, o kształcie `{"gate0": [kroki]}`. Krok: `{"name", "do": [akcje], "expect": [oczekiwania],
"reset": false (opcjonalnie)}`.
- Akcje: `{"click": tid}`, `{"clickText": [tid, regex]}`, `{"wait": ms lub tid}`, `{"back": N}` (systemowe Wstecz).
- Oczekiwania: `{"tid", "count": N}`, `{"tid", "min": N}`, `{"tid", "text": regex}`, `{"tid", "absent": true}`,
  `{"exitQuestion": true}`.
- Po każdym kroku sterownik woła `window.__loomBack()` aż zwróci false; `"reset": false` to wyłącza.
- Bez bloku lub z pustą listą kroki są „pominięte” z powodem — S2 nie czeka na nas.
- Klucz `preconditions` obok `gate0` (dodany po przeglądzie r1:b4, 2026-10-21): `{"config": {klucz: wartość}, "android_shell": true}` —
  co sterownik ma zapewnić **przed** pierwszym krokiem. Oczekiwanie z `"android_only": true` obowiązuje tylko w powłoce Androida
  (w przeglądarce bez `LoomBridge` ten testid ma liczność 0 i sterownik desktopowy może je pominąć).

**Warunki wstępne bloku `gate0`** (sprawdzone sondą 390×844 na prawdziwym serwerze, 2026-10-21; bez nich kroki 3 i 4 padają):
1. `ui_language = pl` w config **przed** blokiem (`PUT /api/config/ui_language {"value": "pl"}`, tak jak robią to
   `m1-navigator-native.mjs:47` i `m1-settings-native.mjs:51`, albo `settings.set`). Preset to `auto` = język przeglądarki,
   a WebView emulatora ma `en-US`, więc bez tego ekrany mówią po angielsku („In all: 109 fields · 0 yours”, „Background
   analysis”). Wzorce `text` niżej są dwujęzyczne (pl|en) jako siatka bezpieczeństwa, ale Gate 0 ocenia ekrany **po polsku**.
2. Blok jest dla powłoki Androida: `shell-menu-connection` istnieje tylko z `LoomBridge` (`layouts.json`: `when: android_shell`),
   dlatego to oczekiwanie ma `"android_only": true`; w Chromium bez mostka ma liczność 0 (to stan oczekiwany, nie błąd).

Kroki, które blok ma pokryć (testid-y i liczby z prawdziwych ekranów i danych, nie z tego opisu):
1. Menu ⋮ → Graf → wiersz ROM: liczba wierszy ROM z danych (spec: 13), chipy pochodzenia, brak „Trybu ratunkowego”.
2. Menu ⋮ nie ma pozycji „Pamięć” ani „Wiedza” (`absent`).
3. Ustawienia: 8 grup, liczniki „N pól · M Twoje”.
4. Modele › Role: karta roli z łańcuchem polityki i flowami czytającymi klucz.
5. Wstecz: arkusz → karta → nawigator → czat → pytanie o wyjście (`android_back_exit_confirm`), `reset: false` tam, gdzie krok ma zostać na stosie.

Blok kroków (P7). Liczby: ROM ma 13 pozycji w `loom/data/graph/rom.pack` (każda z chipem `nav-chip`); grup ustawień jest 8
(`shum-group-<id>`); liczniki pól są sprawdzane wzorcem, bo ich wartość zależy od packa w danej wersji (dziś 109). Uwaga do
kroku 1 z listy wyżej: „Tryb ratunkowy” to **pozycja ROM** (wiersz jak każdy inny), nie ekran powłoki — w webie nie ma
ekranu o tej nazwie, więc blok nie sprawdza jego braku. Krok 5 zostaje na stosie (`reset: false`) i kończy się
systemowym Wstecz na czacie, gdzie powłoka Androida ma zapytać o wyjście (M1-A0).

```json
{"preconditions": {"config": {"ui_language": "pl"}, "android_shell": true},
 "gate0": [
  {"name": "Menu ⋮ → Graf → ROM: 13 pozycji po polsku, chip pochodzenia na każdej",
   "do": [{"click": "shell-menu-toggle"}, {"click": "shell-menu-graph"}, {"wait": "nav-rows"}, {"click": "nav-row-area:rom"}, {"wait": "nav-focus-why"}],
   "expect": [{"tid": "nav-crumb-1", "text": "ROM"}, {"tid": "nav-chip", "count": 13}, {"tid": "nav-status", "min": 1}, {"tid": "nav-bar-view", "count": 1}]},
  {"name": "Menu ⋮: osiem pozycji z layouts.json, bez Pamięci i Wiedzy",
   "do": [{"click": "shell-menu-toggle"}, {"wait": "shell-menu"}],
   "expect": [{"tid": "shell-menu-graph", "count": 1}, {"tid": "shell-menu-settings", "count": 1}, {"tid": "shell-menu-me", "count": 1},
              {"tid": "shell-menu-this_conversation", "count": 1}, {"tid": "shell-menu-views", "count": 1}, {"tid": "shell-menu-semantic", "count": 1},
              {"tid": "shell-menu-connection", "count": 1, "android_only": true}, {"tid": "shell-menu-workshop", "count": 1},
              {"tid": "shell-menu-memory", "absent": true}, {"tid": "shell-menu-knowledge", "absent": true}]},
  {"name": "Ustawienia: 8 grup z „N pól · M Twoje” i suma",
   "do": [{"click": "shell-menu-toggle"}, {"click": "shell-menu-settings"}, {"wait": "shum-groups"}],
   "expect": [{"tid": "shum-total", "text": "^(Razem: \\d+ p(ole|ól|ola)|In all: \\d+ fields?) · \\d+ (Twoje|yours)"},
              {"tid": "shum-group-ty", "count": 1}, {"tid": "shum-group-rozmowa", "count": 1}, {"tid": "shum-group-modele", "count": 1}, {"tid": "shum-group-kontekst", "count": 1},
              {"tid": "shum-group-interfejs", "count": 1}, {"tid": "shum-group-prywatnosc", "count": 1}, {"tid": "shum-group-koszty", "count": 1}, {"tid": "shum-group-silnik", "count": 1},
              {"tid": "shum-group-count-rozmowa", "text": "^\\d+ (p(ole|ól|ola)|fields?) · \\d+ (Twoje|yours)"}, {"tid": "shum-filter", "count": 1}]},
  {"name": "Modele › Role: karta Analiza w tle z łańcuchem polityki i przepływami czytającymi klucz",
   "do": [{"click": "shell-menu-toggle"}, {"click": "shell-menu-settings"}, {"wait": "shum-groups"}, {"click": "shum-group-open-modele"}, {"wait": "roles-table"},
          {"click": "role-open-analysis.fast"}, {"wait": "role-card"}],
   "expect": [{"tid": "role-card-title", "text": "^(Analiza w tle|Background analysis)$"}, {"tid": "role-chain", "count": 1}, {"tid": "role-chain-step-1", "count": 1},
              {"tid": "role-chain-meaning-1", "text": "semantic_model"}, {"tid": "role-tier-reason", "count": 1},
              {"tid": "role-readers", "count": 1}, {"tid": "role-reader-actions.preview", "count": 1}, {"tid": "role-readers-total", "text": "\\d+"}]},
  {"name": "Wstecz: karta roli → grupa → panel → czat → pytanie o wyjście", "reset": false,
   "do": [{"click": "shell-menu-toggle"}, {"click": "shell-menu-settings"}, {"wait": "shum-groups"}, {"click": "shum-group-open-modele"}, {"wait": "roles-table"},
          {"click": "role-open-analysis.fast"}, {"wait": "role-card"},
          {"back": 1}, {"wait": "roles-table"}, {"back": 1}, {"wait": "shum-groups"}, {"back": 1}, {"wait": "chat-shell"}, {"back": 1}],
   "expect": [{"tid": "role-card", "absent": true}, {"tid": "panel-settings", "absent": true}, {"exitQuestion": true}]}
]}
```

## Powłoka czatu — menu ⋮ i panel (paczka P1, `loom/web/src/shell/ChatShell.tsx`, `shell/history.ts`; e2e `chat-first.mjs`, `m1-settings-native.mjs`)

| ekran | testid | znaczenie |
|---|---|---|
| czat | `chat-shell` | korzeń układu czatu (zawsze obecny) |
| menu ⋮ | `shell-menu-toggle` (`aria-expanded`), `shell-menu` (`role=menu`), `shell-menu-<id>` (`role=menuitem`) | `<id>` = id pozycji z `layouts.json`: `graph`, `settings`, `me`, `this_conversation`, `views`, `semantic`, `connection` (tylko `when: android_shell`), `workshop`; **nie ma** `shell-menu-memory` ani `shell-menu-knowledge` |
| panel | `panel-<id>` (szuflada; `panel-settings`, `panel-navigator`, `panel-user-knowledge`, `panel-graph`; `role="dialog"`, `aria-modal="true"`, `aria-label` = tytuł), `close-panel` (×) | jeden wpis historii na otwarty panel; `×` = to samo co Wstecz; po otwarciu fokus jest w szufladzie (`document.activeElement` w środku), po zamknięciu wraca na `shell-menu-toggle` |
| wstecz | `window.__loomBack()` → `true` gdy zamknął warstwę (menu, szufladę, arkusz, kartę, krok ścieżki, panel), `false` na dnie stosu | synchroniczne; Escape na komputerze robi to samo |

## Nawigator — lista (paczka P2, `loom/web/src/components/GraphNavigator.tsx`; e2e `loom/web/e2e/m1-navigator-native.mjs`)

Otwarcie: `shell-menu-toggle` → `shell-menu-graph` → panel `panel-navigator`. Adresy węzłów są częścią testid-u (np. `nav-row-area:rom`, `nav-row-card-rom:boot`).

| ekran | testid | znaczenie |
|---|---|---|
| nawigator | `graph-navigator` | korzeń ekranu; atrybut `data-focus` = adres ogniska (`""` = korzeń grafu) |
| ścieżka | `nav-path`, `nav-crumb-<i>` (0 = Graf), `nav-back` | wiersz ścieżki; człon cofa do kroku `i`; `‹` cofa o jeden (tylko poza korzeniem) |
| szukanie | `nav-search-toggle`, `nav-search-input` | lupa otwiera/zamyka pole; pole = `graph.navigate query` na tym poziomie |
| ognisko | `nav-focus-why` | jedno zdanie „po co” ogniska (poza korzeniem) |
| lista | `nav-rows` (`data-shown`, `data-total`), `nav-loading`, `nav-empty`, `nav-error` | lista `li` wierszy; stany |
| wiersz | `nav-row-<adres>` (`data-has-children`), `nav-count` (`data-known`: `true` = liczba w linii etykiety, `false` = „nieznane (powód)” we własnej linii pod etykietą), `nav-chip`, `nav-evidence`, `nav-status`, `nav-row-card-<adres>` | wiersz (dotknięcie: wejście albo karta liścia), licznik, chip pochodzenia, chip dowodu, chip stanu, `⋯` (otwiera/zamyka kartę węzła, `aria-expanded`) |
| uczciwość | `nav-collapsed`, `nav-hidden`, `nav-unknown-<źródło>` | zwinięte przez budżet, ukryte przez Ciebie, źródło bez odpowiedzi (powód) |
| pasek | `nav-bar`, `nav-bar-perspective`, `nav-bar-wider`, `nav-bar-narrower`, `nav-bar-deeper`, `nav-bar-shallower`, `nav-bar-actions`, `nav-bar-view`, `nav-bar-planned`, `nav-budget` | pasek dolny; `planned` = linia po dotknięciu przycisku bez obsługi; `budget` = „budżet N węzłów na poziom · głębia H · pokazane z wszystkich” |

## Karta węzła (paczka P3, `loom/web/src/components/NodeCard.tsx`; e2e `loom/web/e2e/m1-navigator-native.mjs`, kroki karty)

Otwarcie: dotknięcie wiersza-liścia `nav-row-<adres>` albo `nav-row-card-<adres>`; karta rysuje się pod listą, w `gnav-body` (przewija się razem z listą). Pole ustawienia dla `gate0`: `nav-row-area:profiles_settings` → `nav-row-group:rozmowa` → `nav-row-setting:temperature`.

| ekran | testid | znaczenie |
|---|---|---|
| karta | `node-card` (`data-address`, `data-step` = `card`/`edit`/`level`, `aria-busy`), `card-close`, `card-loading`, `card-error` | korzeń karty; `×`; stany |
| nagłówek | `card-label`, `card-chip`, `card-evidence`, `card-layers`, `card-value`, `card-why`, `card-effect`, `card-edit-rule`, `card-flows` | etykieta, chip pochodzenia, chip dowodu, „Warstwy: …”, „Wartość: …”, „Po co”, „Co zmienia”, zasada edycji, „Czyta to: …” |
| łańcuch | `card-chain` (Disclosure), `card-chain-rows`, `card-chain-<poziom>` (`data-role`, `data-winning`) | „Skąd jest wartość”: jeden `li` na poziom łańcucha jądra; `data-winning="true"` = poziom, który daje wartość; **tylko gdy jądro narysowało łańcuch** (r2:b1) |
| bez łańcucha | `card-no-chain` | r2:b1: węzeł bez łańcucha poziomów (pozycja ROM, pole profilu): jedno zdanie `card.no_value` zamiast nagłówka `card-chain` |
| pole profilu | `card-profile-interview` | r2:b1: karta `kind: profile_slot` — „Odpowiesz w wywiadzie: ⋮ › Warsztat › Onboarding” (stan `known`: „Twoja odpowiedź z wywiadu — zmienisz ją tam”), przed działaniami |
| powiązania | `card-relations` (Disclosure), `card-relations-none`, `card-relation-group-<rodzaj>`, `card-relation-<adres>` | grupy rodzaju krawędzi; dotknięcie węzła = krok ścieżki nawigatora (karta się zamyka) |
| działania | `card-actions` (Disclosure), `card-action-rows`, `card-change`, `card-unset-<poziom>`, `card-action-<id>` | „Zmień”; „Zdejmij wartość: <warstwa> (<wartość>) — wróci: …” (wyłączony, gdy nie ma reguły albo warstwa Ty: powód w tekście); akcje jądra słowami |
| edytor | `card-editor`, `card-editor-input` (liczba/tekst/JSON), `card-editor-true`/`card-editor-false` (przełącznik), `card-editor-error`, `card-next`, `card-cancel` | krok `edit`; `card-next` = „Dla kogo?” |
| Dla kogo? | `card-levels`, `card-level-<poziom>` (`data-writable`), `card-more-levels`, `card-levels-back` | krok `level`: najpierw poziomy pola, po „więcej poziomów” cały łańcuch; niezapisywalny poziom jest wyłączony i mówi dlaczego |
| bramka | `card-gate`, `card-gate-reason`, `card-gate-confirm`, `card-gate-deny` | pytanie jądra przed zapisem (`confirm:rules.set`, `confirm:rules.disable`, `require_confirmation`) |
| wynik | `card-notice` (`role=status`) | „Zapisano…” albo „Nie zapisano: <powód jądra>” |

Wstecz w karcie: zamyka najpierw wybór poziomu (`navigator-card-level`), potem edytor (`navigator-card-edit`), potem kartę (`navigator-card`), dopiero potem krok ścieżki. Krok karty dla `gate0` (po kroku ROM): `{"click": "nav-crumb-0"}, {"click": "nav-row-area:profiles_settings"}, {"click": "nav-row-group:rozmowa"}, {"click": "nav-row-setting:temperature"}, {"expect": "node-card"}, {"expect": "card-chain-person"}, {"click": "card-change"}, {"expect": "card-editor"}, {"click": "card-next"}, {"expect": "card-levels"}, {"back": true}, {"expect": "card-editor"}, {"back": true}, {"expect": "node-card"}, {"back": true}, {"absent": "node-card"}`.

Wstecz (`window.__loomBack()`): zamyka najpierw pole szukania (jeśli otwarte), potem ostatni krok ścieżki (`navigator-step-<i>`), potem panel (`panel`); w korzeniu czatu zwraca `false`. Krok ROM dla `gate0`: `{"click": "shell-menu-toggle"}, {"click": "shell-menu-graph"}, {"wait": "nav-rows"}, {"click": "nav-row-area:rom"}, {"wait": "nav-focus-why"}` → `{"tid": "nav-status", "min": 1}`, liczba `nav-row-rom:*` = liczba wpisów `rom.pack` (nie literał).

## Nawigator — mapa i Widok ▾ (paczka P4, `loom/web/src/components/NavigatorMap.tsx`; e2e `loom/web/e2e/m1-navigator-native.mjs`, krok mapy)

Lista i mapa są jedna pod drugą w `gnav-body`, każda w `Disclosure` (`data-state` = `collapsed`/`expanded`, nagłówek `.disclosure-head` 44 px). Które są otwarte na start mówi `navigator_layout` (`data-layout` nawigatora: `both`/`list`/`map`). Adres węzła jest w testid-zie znaku (korzeń grafu = `root`).

| ekran | testid | znaczenie |
|---|---|---|
| zwijanie | `nav-list-disclosure`, `nav-map-disclosure` (`data-state`) | „Pokaż/Ukryj listę”, „Pokaż/Ukryj mapę”; dotknięcie `.disclosure-head` przełącza |
| mapa | `nav-map-wrap`, `nav-map` (`data-zoom` np. `1.000`, `data-marks`, `data-system` = `sector`, `data-exclusion` = JSON prostokątów w CSS px) | płótno 300 px; `data-exclusion` = to, co poszło do `LoomBridge.setGestureExclusion` (`[]` gdy zwinięta albo poza ekranem) |
| znak | `nav-map-mark` (`data-id`, `data-focus`, `data-selected`) w SVG; przycisk `nav-map-node-<adres>` (`aria-pressed`) | kółko węzła i jego niewidzialny cel 44 px; dotknięcie = linia pod mapą + karta `node-card` (`data-address` = adres) |
| przyciski | `nav-map-zoom-in`, `nav-map-zoom-out`, `nav-map-home`, `nav-map-zoom-value` | rząd pod płótnem: `+` ×1,25, `−` ÷1,25, `⌂` środek i 1:1, powiększenie w % |
| pod mapą | `nav-map-selected` (`data-address`), `nav-map-enter`, `nav-map-hint`, `nav-map-unplaced` | etykieta + licznik + chip dotkniętego węzła; „Wejdź ›” (gdy ma dzieci); podpowiedź gestów (gdy nic nie wybrano); węzły bez położenia (nie zdarza się na układzie `sector`) |
| Widok ▾ | `nav-bar-view` (`aria-expanded`), `nav-view-menu`, `nav-view-both` / `nav-view-list` / `nav-view-map` (`aria-checked`), `nav-view-note` | menu z trzema wartościami pola `navigator_layout`; wybór działa od razu i zapisuje się (`settings.set`); linia „Zapisane dla Ciebie: …” albo powód |

Wstecz: otwarte menu Widok ▾ jest warstwą `navigator-view` (zamyka się pierwsze), potem karta, potem kroki ścieżki. Krok mapy dla `gate0` (propozycja): `shell-menu-toggle` → `shell-menu-graph` → `nav-row-area:rom` → `nav-map-node-rom:boot` → `expect node-card`, `nav-map-selected text /./`, `nav-map min 1`.

## Ustawienia po ludzku (paczka P5, `loom/web/src/components/SettingsHuman.tsx`; menu ⋮ › Ustawienia w układzie czatu)

| ekran | testid | znaczenie |
|---|---|---|
| korzeń | `settings-human`, `shum-loading`, `shum-error` + `shum-retry` | korzeń ekranu; stany wczytywania i błędu jądra |
| lista grup | `shum-intro`, `shum-total` (`data-fields`, `data-yours`), `shum-yours-help`, `shum-filter` (`role=switch`, `aria-checked`), `shum-groups`, `shum-hidden`, `shum-proposals` | opis, suma „N pól · M Twoje”, filtr „Pokaż tylko zmienione”, lista ośmiu grup; `shum-hidden` = linia „N kluczy ukrytych” (tylko gdy `hidden_keys` z `settings.resolved` nie jest puste), `shum-proposals` = linia „Propozycje: <dziś> <powód>” z `proposals_status` |
| grupa (wiersz) | `shum-group-<id>` (`data-fields`, `data-yours`), `shum-group-open-<id>` (przycisk), `shum-group-count-<id>` (tekst „N pól · M Twoje”) | `<id>` = id grupy z jądra: `ty`, `rozmowa`, `modele`, `kontekst`, `interfejs`, `prywatnosc`, `koszty`, `silnik`; suma `data-fields` = `counts.fields` z `settings.resolved` |
| widok grupy | `shum-group-view-<id>` (`data-fields`, `data-shown`), `shum-back`, `shum-group-why`, `shum-group-view-count`, `shum-empty`, `shum-sub-<podgrupa>`, `shum-fields-<id>` | po otwarciu grupy; `shum-back` = „Wróć do grup ustawień” |
| pole | `shum-field-<klucz>` (`data-address`, `data-planned`), `shum-field-main-<klucz>` (rozwija szczegóły, `aria-expanded`), `shum-label-<klucz>`, `shum-value-<klucz>`, `shum-why-<klucz>`, `shum-chip-provenance-<klucz>`, `shum-chip-evidence-<klucz>`, `shum-chip-status-<klucz>`, `shum-chip-profile-<klucz>`, `shum-planned-<klucz>`, `shum-card-btn-<klucz>` (⋯ = karta) | wiersz pola; np. `shum-field-temperature` (Rozmowa), `shum-chip-provenance-temperature` |
| szczegóły | `shum-detail-<klucz>`, `shum-effect-<klucz>`, `shum-flows-<klucz>`, `shum-edit-<klucz>` | po rozwinięciu: Co zmienia, Czytają to przepływy, Zmiana |
| karta pola | `node-card` i `card-*` z bloku karty (ta sama karta co w nawigatorze) | „⋯” przy polu; po zapisie liczniki grup się przeładowują |
| Wszystkie pola | `shum-all-fields` (Disclosure, `.disclosure-head`), `shum-all-fields-why`, `shum-legacy`, w środku `settings-panel` i dawne `cfg-*`, `secret-*` | w grupie `silnik`; prośba o wpisanie sekretu otwiera Ustawienia od razu tu |

Wstecz (`window.__loomBack()`): zamyka najpierw kartę pola (`navigator-card*`), potem widok grupy (`settings-group`, wraca do listy grup), potem panel; w korzeniu czatu zwraca `false`. Krok Ustawień dla `gate0` (propozycja dla S2): `shell-menu-toggle` → `shell-menu-settings` → `shum-groups` ma 8 wierszy, suma `data-fields` = `counts.fields` → `shum-group-open-modele` → `shum-field-main-default_model` pokazuje etykietę i chip → `shum-back` → lista grup.


## Role modeli „Kto wykonuje którą pracę” (paczka P6, `loom/web/src/components/RoleCard.tsx`; menu ⋮ › Ustawienia › Modele)

Tabela ról stoi na górze widoku grupy `modele` (stała `ROLES_GROUP_PRESET` w `role-card-state.ts`; gdy żadna grupa nie ma tego id, tabela jest pod listą grup).

| ekran | testid | znaczenie |
|---|---|---|
| tabela | `roles` (sekcja), `roles-title`, `roles-intro`, `roles-table` (lista), `roles-loading`, `roles-error` + `roles-retry`, `roles-issues` | korzeń tabeli; stany wczytywania i błędu jądra; `roles-issues` = to, czego jądro nie umiało przeczytać |
| wiersz roli | `role-row-<id roli>` (`data-tier`, `data-state`), `role-open-<id>` (przycisk, otwiera kartę), `role-label-<id>`, `role-model-<id>`, `role-tier-<id>`, `role-state-<id>` | `<id>` = id roli z jądra (`chat.default`, `analysis.fast`, `guardian`, …); wierszy tyle, ile ról w `ports.pack` |
| karta | `role-card` (`data-role`, `data-tier`), `role-back` (do tabeli), `role-card-title`, `role-card-tier`, `role-card-state`, `role-why`, `role-today`, `role-status-reason` | „Po co”, „Dziś”, „Dlaczego tak jest” słowo w słowo z danych roli |
| model i klasa | `role-model`, `role-tier-line`, `role-tier-chip`, `role-tier-reason`, `role-class-planned` (przypisz klasę ręcznie / zmień progi — „planowane”), `role-class-planned-reason` | klasa z powodem to zdanie jądra |
| łańcuch | `role-chain` (`data-read` = 1 gdy czytnik czyta łańcuch, 0 gdy tylko opisuje), `role-chain-step-<n>` (`data-type`, `data-decides`), `role-chain-title-<n>`, `role-chain-value-<n>`, `role-chain-meaning-<n>`, `role-chain-decides-<n>`, `role-chain-note-<n>`, `role-chain-not-read`, `role-today-chain`, `role-today-step-<n>` | `meaning` = zdanie jądra słowo w słowo; krok, który wybiera, ma `role-chain-decides-<n>` |
| przepływ per klasa | `role-flow-by-class`, `role-class-<klasa>` (`data-current`), `role-class-label-<klasa>`, `role-flow-current`, `role-flow-<klasa>` + `-title` / `-status` (`data-status="calibration"` → chip „kalibracja”) / `-calls` / `-does` / `-planned`, `role-flow` (gdy rola ma jeden przepływ), `role-flow-none` | `<klasa>` = `cheap`/`mid`/`frontier`/`unknown`; zaznaczona klasa Twojego modelu |
| czytają rolę | `role-readers` (`data-count`), `role-reader-<id przepływu>` (`data-also="1"` dla przepływów, które czytają ustawienie bez deklarowania roli), `role-readers-total`, `role-readers-none` | wszystkie przepływy, które czytają klucz roli (lista z katalogu działań) |
| tryb i koszt | `role-mode`, `role-mode-threshold`, `role-mode-no-key`, `role-cost-price`, `role-cost-week` | tylko rola Analiza w tle ma tryb; koszt albo powód „nieznana” z jądra |
| nazwa techniczna | `role-settings`, `role-setting-<klucz>`, `role-setting-key-<klucz>`, `role-change-<klucz>` (rozwija kartę pola `node-card` w tym samym miejscu) | zmiana modelu roli = ta sama karta pola co w nawigatorze |

Wstecz (`window.__loomBack()`): zamyka najpierw kartę pola otwartą z roli, potem kartę roli (`role-card`, wraca do tabeli), potem widok grupy, potem panel. Krok dla Androida (propozycja): `shell-menu-toggle` → `shell-menu-settings` → `shum-group-open-modele` → `role-open-analysis.fast` → `role-flow-by-class`, `role-readers`.
