# Natywny konsument wspólnego artefaktu W3 → W4

Biblioteka została zbudowana z rzeczywistego CABI W4, GraphPacketStore i polityki W2.
Linkowanie używa `--whole-archive` i `--no-undefined`; nie ma atrap ABI.
`build.json` zawiera piny, hashe źródeł/obiektów/biblioteki i polecenia z przenośnymi
nazwami katalogów. Oryginalne archiwum CMake jest czytane i kopiowane, bez zmiany.

Konsument sprawdza deklarowane relacje grafu i prawdziwe natywne
validate/accept/restart/read/replay/retry. Sam ten test nie dowodzi wykonania
producenta W3; dowód jego rzeczywistego wykonania jest osobnym artefaktem producenta.
Konsument wykonuje zero wywołań dostawców. Akceptacja nie ustanawia prawdziwości treści.

## Odtworzenie

W checkout W3 zbuduj zwykłą konfigurację CMake z `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.
Potrzebne są statyczne archiwa rdzenia, vendored SQLite i miniz oraz Linux,
C++20, GNU ar/nm, gold, OpenSSL, Python 3.11+ i zależności
`loom/tools/contracts/requirements.txt`. Przypięte commity W2/W4 muszą istnieć lokalnie.
Artefakt wejściowy musi pochodzić z rzeczywistego eksportu producenta W3.

```bash
python3 docs/reports/chat-selector-2026-10-04-evidence/golden-consumer/reproduce.py \
  --repo "$PWD" \
  --build-dir /tmp/chatadhd-w3-build \
  --artifact /path/to/actual-w3-method-graph-artifact.json \
  --output-dir /tmp/chatadhd-w3-consumer-reproduction
```

Skrypt kompiluje właściwe źródła W3/W2/W4 pojedynczo, składa kopię archiwum,
linkuje rzeczywistą bibliotekę CABI i uruchamia przypięty konsument W4.
Nowy katalog wyjściowy zachowuje wszystkie polecenia, źródła, hashe i logi,
również przy wyniku negatywnym. `verification/receipt.json` zachowuje pełny
pokwitowany pakiet; `verification/verification.json` podaje zakres dowodu.
Każde odtworzenie korzysta z osobnej tymczasowej bazy.

Wynik: **PASS**, kod wyjścia 0. Rzeczywisty eksport W3 zweryfikowany przez
niezależny konsument W4 1377e20: 3 wyniki modelu, 19 encji, 30 twierdzeń i 17 źródeł.
`input.json` jest dokładnym eksportem producenta, bez skracania historii;
`receipt.json` zawiera pełny natywny pokwitowany pakiet.
`verification.json` podaje hashe i zakres dowodu; `invocation.json` wiąże pliki.

Producent osobno przeszedł 1 przypadek / 97 asercji rzeczywistego wykonania.
Konsument uczciwie raportuje `producer_execution_verified=false`: sprawdził
pełny kontrakt i natywną trwałość, a nie przebieg wywołania producenta.
W obu fazach nie było płatnych wywołań. Pokwitowanie zachowuje
`acceptance_establishes_content_truth=false`.

Do odtworzenia opublikowanego wejścia użyj w poleceniu `--artifact`
`docs/reports/chat-selector-2026-10-04-evidence/golden-consumer/input.json`.
Dokładne pliki JSON są publicznymi danymi syntetycznymi. Jedyną zmianą logu jest
zastąpienie losowego katalogu tymczasowej bazy znacznikiem `<TEMP_DATABASE>`.
