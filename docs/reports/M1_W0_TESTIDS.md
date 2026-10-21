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
