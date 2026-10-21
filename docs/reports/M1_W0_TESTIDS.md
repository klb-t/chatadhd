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
| wiersz | `nav-row-<adres>` (`data-has-children`), `nav-count`, `nav-chip`, `nav-evidence`, `nav-status`, `nav-row-card-<adres>`, `nav-card-stub` | wiersz (dotknięcie: wejście albo zaślepka karty), licznik, chip pochodzenia, chip dowodu, chip stanu, `⋯`, zaślepka karty (adres) |
| uczciwość | `nav-collapsed`, `nav-hidden`, `nav-unknown-<źródło>` | zwinięte przez budżet, ukryte przez Ciebie, źródło bez odpowiedzi (powód) |
| pasek | `nav-bar`, `nav-bar-perspective`, `nav-bar-wider`, `nav-bar-narrower`, `nav-bar-deeper`, `nav-bar-shallower`, `nav-bar-actions`, `nav-bar-view`, `nav-bar-planned`, `nav-budget` | pasek dolny; `planned` = linia po dotknięciu przycisku bez obsługi; `budget` = „budżet N na poziom · głębia H · pokazane z wszystkich” |

Wstecz (`window.__loomBack()`): zamyka najpierw pole szukania (jeśli otwarte), potem ostatni krok ścieżki (`navigator-step-<i>`), potem panel (`panel`); w korzeniu czatu zwraca `false`. Krok ROM dla `gate0`: `{"click": "shell-menu-toggle"}, {"click": "shell-menu-graph"}, {"wait": "nav-rows"}, {"click": "nav-row-area:rom"}, {"wait": "nav-focus-why"}` → `{"tid": "nav-status", "min": 1}`, liczba `nav-row-rom:*` = liczba wpisów `rom.pack` (nie literał).
