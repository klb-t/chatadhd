# Niezależne testy granicy C

Wymaga Python 3.12+ i `jsonschema==4.26.0`. Narzędzie importuje rzeczywiste moduły wskazanego checkoutu. Nie kopiuje ich implementacji. Przechwytuje transport i blokuje Python socket connect; natywny kontekst ma `start_workers=false`. Nie odczytuje kluczy, archiwów prywatnych ani środowiskowych credentiali.

```bash
python3 run.py --repo /path/to/C --sha 69880859802267f1b38b82eeaecd6fdc5522a47a \
  --phase projection --output /tmp/projection-receipt.json
python3 run.py --repo /path/to/C --sha 69880859802267f1b38b82eeaecd6fdc5522a47a \
  --phase native --native-lib /path/to/libloom.so \
  --native-sha 9e20f99ab27e7cd45e1892f83bf60fbe60db3de9 --output /tmp/native-receipt.json
```

`--phase all` wykonuje oba zakresy. `--sha` musi odpowiadać HEAD badanego checkoutu. `--native-sha` jest przypięciem provenance buildu dostarczonym przez wykonawcę; hash biblioteki zostaje zapisany, runner nie udaje, że sam dowiódł źródłowego SHA binarium. Rerun przeciw poprawce wymaga aktualnego SHA tej poprawki. Bazowy main bez plików C nie jest poprawnym wejściem dla tej baterii; native runtime main badany jest poprzez `--native-lib`.

Exit: 0 = wszystkie wykonane bramki akceptacji PASS; 1 = co najmniej jedna akceptacja FAIL; 2 = brak wymaganej biblioteki. `reproduction: PASS` nigdy nie zmienia porażki akceptacji na sukces produktu. Pierwszopoziomowa blokada eksportu jest negatywną kontrolą, nie reprodukcją błędu.

Canary istnieją wyłącznie w pamięci i usuwanych katalogach tymczasowych. Receipt zapisuje hash/status, nigdy ujawnione bajty. Base64 źródeł jest dekodowane do kontroli, bo szukanie tylko w outer JSON przeoczyłoby wyciek. Mutacje nie nadpisują historycznych artefaktów.

Kategorie przypadków: `C-PUBLIC-*` publiczna projekcja; `C-WIRE-*` rzeczywisty transport z intercept; `C-BIND-*` tożsamość powtórzenia na granicy wyniku; `C-NATIVE-*` zapis, nowy kontekst, read/replay; `C-GRAPH-*` bezpośrednie walidacje native, CAS i zachowanie starej receptury. Cała batería jest mechaniczna/offline: nie mierzy jakości modeli.
