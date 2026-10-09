# Uruchamianie PASS4

Z checkoutu gałęzi audytu:

```bash
python tools/ecosystem-audit-2026-10-09/pass4/run_index.py --list
python tools/ecosystem-audit-2026-10-09/pass4/run_index.py watchdog \
  --repo /path/to/Watchdog-JH16 \
  --sha 58a0c93bd0135e3715dcbc4d92fb80e61bd31215 \
  --output /tmp/pass4-watchdog-new.json
```

Runner wymaga zgodnego HEAD i czystych śledzonych plików. Zależności są jawnymi wartościami `--set NAME=PATH`; brakująca wartość daje błąd, a `--show` pokazuje dokładny argv bez wykonania. Polecenia nie przechodzą przez shell. Nie uruchamia płatnego CI ani modeli. Każdy konkretny moduł ma dodatkowy opis zależności w swoim REPORT/REPRODUCE albo README narzędzi.

Przykład E (wymaga rzeczywistego wcześniej odczytanego packetu D z native, nie atrapy):

```bash
python tools/ecosystem-audit-2026-10-09/pass4/run_index.py E-acceptance \
  --repo /path/to/E-checkout --sha 3eaac2953c3ee0d01d085e595d68edc091c284f2 \
  --output /tmp/pass4-E-new.json \
  --set node=/path/to/node --set node_modules=/path/to/node_modules \
  --set scratch=/tmp/pass4-E-build --set native_packet=/path/to/d-native-packet.json
```

B-native przyjmuje zweryfikowany selective object manifest; B-native-full używa świeżego pełnego buildu i attestation. `B-native/record_full_build.py` wykonuje rzeczywisty build i zapisuje proof, nie podpisuje arbitralnego starego archiwum. W pass4 wykonano wariant selective; nowy full-build wrapper sprawdzono składniowo/CLI, nie zaliczono go jako pełnego buildu produktu. Nie uruchamiać selektywnej receptury na niepokrytych zmianach zależności.

Kategorie wyników: `reproduction/bug_reproduction/A` odtwarzają wadę; `acceptance/repair/B` wymagają poprawnego produktu; `contract` sprawdza niezmienność; `integration` dotyczy wskazanych rzeczywistych modułów; `control/environment` ma osobny zakres. PASS reprodukcji nigdy nie kompensuje FAIL akceptacji. JS może zwrócić kod 2 przy braku kontraktu i braku FAIL; szczegóły w module. W AGEDS/LEM/CKP można wybrać `--phase acceptance`. Bramka Watchdog obejmuje wszystkie przypadki z oddzielnymi kategoriami.

Autorytatywny spis końcowych wykonań: results-index.json. D-main i D-B4 są dwoma warunkami tych samych kryteriów. Wczesnych kalibracji harnessu oraz minireprodukcji nie dodawać jako nowych niezależnych sukcesów. Finalne E4-results.json zawiera 20 prób po peer QA. Stary brak E jest zastąpiony odbiorem opublikowanego SHA.

Testy runnera:

```bash
python -m unittest discover -s tools/ecosystem-audit-2026-10-09/pass4 -p test_run_index.py -v
```

W sesji: 3 PASS; rzeczywiste uruchomienie Watchdog przez indeks zachowało kod 1 i 3 FAIL produktu. Pełny wykaz bajtów opublikowanego pakietu zapisuje manifest.json/PUBLICATION.json. Ich weryfikacja nie jest bramką funkcjonalną produktu.
