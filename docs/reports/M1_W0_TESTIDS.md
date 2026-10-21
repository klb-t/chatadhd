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
