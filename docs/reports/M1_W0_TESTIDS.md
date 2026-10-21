# M1-W0 — testid-y ekranów dla smoke Androida (kontrakt z S2)

Stan: SZKIELET, wypełnia go ostatni pakiet W0 (P7) prawdziwymi testid-ami po zbudowaniu ekranów.

Kontrakt z S2 (wiadomość z 2026-10-21): sterownik `drive.mjs` czyta **pierwszy blok kodu `json`
w tym pliku**, o kształcie `{"gate0": [kroki]}`. Krok: `{"name", "do": [akcje], "expect": [oczekiwania],
"reset": false (opcjonalnie)}`.
- Akcje: `{"click": tid}`, `{"clickText": [tid, regex]}`, `{"wait": ms lub tid}`, `{"back": N}` (systemowe Wstecz).
- Oczekiwania: `{"tid", "count": N}`, `{"tid", "min": N}`, `{"tid", "text": regex}`, `{"tid", "absent": true}`,
  `{"exitQuestion": true}`.
- Po każdym kroku sterownik woła `window.__loomBack()` aż zwróci false; `"reset": false` to wyłącza.
- Bez bloku lub z pustą listą kroki są „pominięte” z powodem — S2 nie czeka na nas.

Kroki, które blok ma pokryć (testid-y i liczby z prawdziwych ekranów i danych, nie z tego opisu):
1. Menu ⋮ → Graf → wiersz ROM: liczba wierszy ROM z danych (spec: 13), chipy pochodzenia, brak „Trybu ratunkowego”.
2. Menu ⋮ nie ma pozycji „Pamięć” ani „Wiedza” (`absent`).
3. Ustawienia: 8 grup, liczniki „N pól · M Twoje”.
4. Modele › Role: karta roli z łańcuchem polityki i flowami czytającymi klucz.
5. Wstecz: arkusz → karta → nawigator → czat → pytanie o wyjście (`android_back_exit_confirm`), `reset: false` tam, gdzie krok ma zostać na stosie.

Poniżej pusty blok; P7 go zastępuje, pod nim wypisuje wszystkie testid-y ekranów w tabeli (ekran, testid, znaczenie).

```json
{"gate0": []}
```

## Nawigator — lista (paczka P2, `loom/web/src/components/GraphNavigator.tsx`; e2e `loom/web/e2e/m1-navigator-native.mjs`)

Otwarcie: `shell-menu-toggle` → `shell-menu-graph` → panel `panel-navigator`. Adresy węzłów są częścią testid-u (np. `nav-row-area:rom`, `nav-row-card-rom:boot`).

| ekran | testid | znaczenie |
|---|---|---|
| nawigator | `graph-navigator` | korzeń ekranu; atrybut `data-focus` = adres ogniska (`""` = korzeń grafu) |
| ścieżka | `nav-path`, `nav-crumb-<i>` (0 = Graf), `nav-back` | wiersz ścieżki; człon cofa do kroku `i`; `‹` cofa o jeden (tylko poza korzeniem) |
| szukanie | `nav-search-toggle`, `nav-search-input` | lupa otwiera/zamyka pole; pole = `graph.navigate query` na tym poziomie |
| ognisko | `nav-focus-why` | jedno zdanie „po co” ogniska (poza korzeniem) |
| lista | `nav-rows` (`data-shown`, `data-total`), `nav-loading`, `nav-empty`, `nav-error` | lista `li` wierszy; stany |
| wiersz | `nav-row-<adres>` (`data-has-children`), `nav-count`, `nav-chip`, `nav-evidence`, `nav-status`, `nav-row-card-<adres>` | wiersz (dotknięcie: wejście albo karta liścia), licznik, chip pochodzenia, chip dowodu, chip stanu, `⋯` (otwiera/zamyka kartę węzła, `aria-expanded`) |
| uczciwość | `nav-collapsed`, `nav-hidden`, `nav-unknown-<źródło>` | zwinięte przez budżet, ukryte przez Ciebie, źródło bez odpowiedzi (powód) |
| pasek | `nav-bar`, `nav-bar-perspective`, `nav-bar-wider`, `nav-bar-narrower`, `nav-bar-deeper`, `nav-bar-shallower`, `nav-bar-actions`, `nav-bar-view`, `nav-bar-planned`, `nav-budget` | pasek dolny; `planned` = linia po dotknięciu przycisku bez obsługi; `budget` = „budżet N na poziom · głębia H · pokazane z wszystkich” |

## Karta węzła (paczka P3, `loom/web/src/components/NodeCard.tsx`; e2e `loom/web/e2e/m1-navigator-native.mjs`, kroki karty)

Otwarcie: dotknięcie wiersza-liścia `nav-row-<adres>` albo `nav-row-card-<adres>`; karta rysuje się pod listą, w `gnav-body` (przewija się razem z listą). Pole ustawienia dla `gate0`: `nav-row-area:profiles_settings` → `nav-row-group:rozmowa` → `nav-row-setting:temperature`.

| ekran | testid | znaczenie |
|---|---|---|
| karta | `node-card` (`data-address`, `data-step` = `card`/`edit`/`level`, `aria-busy`), `card-close`, `card-loading`, `card-error` | korzeń karty; `×`; stany |
| nagłówek | `card-label`, `card-chip`, `card-evidence`, `card-layers`, `card-value`, `card-why`, `card-effect`, `card-edit-rule`, `card-flows` | etykieta, chip pochodzenia, chip dowodu, „Warstwy: …”, „Wartość: …”, „Po co”, „Co zmienia”, zasada edycji, „Czyta to: …” |
| łańcuch | `card-chain` (Disclosure), `card-chain-rows`, `card-chain-<poziom>` (`data-role`, `data-winning`) | „Skąd jest wartość”: jeden `li` na poziom łańcucha jądra; `data-winning="true"` = poziom, który daje wartość |
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
| lista grup | `shum-intro`, `shum-total` (`data-fields`, `data-yours`), `shum-yours-help`, `shum-filter` (`role=switch`, `aria-checked`), `shum-groups` | opis, suma „N pól · M Twoje”, filtr „Pokaż tylko zmienione”, lista ośmiu grup |
| grupa (wiersz) | `shum-group-<id>` (`data-fields`, `data-yours`), `shum-group-open-<id>` (przycisk), `shum-group-count-<id>` (tekst „N pól · M Twoje”) | `<id>` = id grupy z jądra: `ty`, `rozmowa`, `modele`, `kontekst`, `interfejs`, `prywatnosc`, `koszty`, `silnik`; suma `data-fields` = `counts.fields` z `settings.resolved` |
| widok grupy | `shum-group-view-<id>` (`data-fields`, `data-shown`), `shum-back`, `shum-group-why`, `shum-group-view-count`, `shum-empty`, `shum-sub-<podgrupa>`, `shum-fields-<id>` | po otwarciu grupy; `shum-back` = „Wróć do grup ustawień” |
| pole | `shum-field-<klucz>` (`data-address`, `data-planned`), `shum-field-main-<klucz>` (rozwija szczegóły, `aria-expanded`), `shum-label-<klucz>`, `shum-value-<klucz>`, `shum-why-<klucz>`, `shum-chip-provenance-<klucz>`, `shum-chip-evidence-<klucz>`, `shum-chip-status-<klucz>`, `shum-chip-profile-<klucz>`, `shum-planned-<klucz>`, `shum-card-btn-<klucz>` (⋯ = karta) | wiersz pola; np. `shum-field-temperature` (Rozmowa), `shum-chip-provenance-temperature` |
| szczegóły | `shum-detail-<klucz>`, `shum-effect-<klucz>`, `shum-flows-<klucz>`, `shum-edit-<klucz>` | po rozwinięciu: Co zmienia, Czytają to przepływy, Zmiana |
| karta pola | `node-card` i `card-*` z bloku karty (ta sama karta co w nawigatorze) | „⋯” przy polu; po zapisie liczniki grup się przeładowują |
| Wszystkie pola | `shum-all-fields` (Disclosure, `.disclosure-head`), `shum-all-fields-why`, `shum-legacy`, w środku `settings-panel` i dawne `cfg-*`, `secret-*` | w grupie `silnik`; prośba o wpisanie sekretu otwiera Ustawienia od razu tu |

Wstecz (`window.__loomBack()`): zamyka najpierw kartę pola (`navigator-card*`), potem widok grupy (`settings-group`, wraca do listy grup), potem panel; w korzeniu czatu zwraca `false`. Krok Ustawień dla `gate0` (propozycja dla S2): `shell-menu-toggle` → `shell-menu-settings` → `shum-groups` ma 8 wierszy, suma `data-fields` = `counts.fields` → `shum-group-open-modele` → `shum-field-main-default_model` pokazuje etykietę i chip → `shum-back` → lista grup.

