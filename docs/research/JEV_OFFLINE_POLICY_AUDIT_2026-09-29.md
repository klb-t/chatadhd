# Jev — analiza offline polityk i profilu błędów, 2026-09-29

**Status: analiza opisowa i post hoc istniejących agregatów. Nie wykonano nowych wywołań modelu. Nowy koszt API: 0 USD.** Nie zmieniono kodu, progów produkcyjnych, stanu wiedzy, STATE ani organizacji istniejących dokumentów.

## Źródła

Stan odniesienia: `f4bd58b5be9c5a35a10156302b3349af96f5239b`.

- [Agregaty pilota](inputs/jev-structure-analysis-2026-09-28.json), blob `7f935790844adfaf0b27684a480dcc80ec237441`.
- [Opis wyników i ograniczeń rubryki](JEV_RESULTS_2026-09-28.md), blob `2951757b257c4e7560629b3aa765f734641c6a58`.
- [Zamrożony protokół pierwotnego pilota](JEV_LIVE_PROTOCOL_2026-09-28.md).

Odczytano zapisane statystyki przez konektor GitHub. Obliczenia wykonano lokalnie w Pythonie na jawnym wyciągu pól; nie pobrano pełnego archiwum surowych odpowiedzi. To nie jest ponowienie inferencji, arbitralny sweep progów ani niezależna walidacja modelu.

## Porównanie polityk na dostępnych agregatach

| Polityka | Pozostawione decyzje | Do weryfikacji | Błędy w pozostawionych | Pokrycie |
|---|---:|---:|---:|---:|
| Próg 0,5, bez weryfikacji | 768/768 | 0 | 26/768 | 100% |
| Pozostaw p<=0,2 albo p>=0,8 | 673/768 | 95 | 5/673 | 87,63% |
| To samo pasmo, ale całe q01 kieruj do weryfikacji — **post hoc** | 627/768 | 141 | 0/627 | 81,64% |

Trzeci wariant wybrano po obejrzeniu błędów. Odkłada pytania do sprawdzenia, **nie poprawia odpowiedzi Jev**. Zero w tej próbce nie oznacza bezbłędności w produkcji. Nie zmieniać na tej podstawie istniejących reguł routingu ani kryteriów akceptacji.

Dodatkowa kwarantanna q01 oznacza 46 dodatkowych weryfikacji na pięć usuniętych z automatycznej akceptacji błędów: 9,2 weryfikacji na jeden taki błąd w tej próbce. Nie wiadomo z tej analizy, ile kosztuje rzeczywiste poprawne rozstrzygnięcie odłożonych pytań.

## Co to mówi o modelu modeli

Globalna accuracy 96,61% maskuje mocno różniące się profile konkretnych pytań. Dla q01 precyzja dodatnich odpowiedzi wynosi 30%: sześć trafnych z dwudziestu dodatnich (14 FP, raportowana precyzja 0,3 i recall 1). Dla q07 wynosi 70%, a dla q11 około 54,55%.

Wszystkie pięć błędów pozostających przy pasmie 0,2/0,8 dotyczy q01 i ma p(YES) 0,81–0,86. Nie należy utożsamiać tego score'u ze sprawdzoną empirycznie pewnością poprawności na tym zadaniu.

**Ważne ograniczenie interpretacji:** według raportu q01 wymaga lokalnie zdefiniowanych ról P/Q, a model czasem dostrzega relację warunkową w zdaniu przyczynowym bez jawnych ról. To rozbieżność z wąską rubryką; nie uzasadnia wniosku „Jev nie rozumie implikacji”. Rubryka sama może częściowo rozmijać się z celem odkrywania niewypowiedzianych struktur. W profilu instrumentu zapisać zadanie, rubrykę, reprezentację wejścia i rodzaj rozbieżności, nie globalną negatywną ocenę modelu.

## Nie wykazano poprawy na niezależnym sprawdzianie

Wszystkie pięć błędów wysokiego score'u było w części development: 5/329 pozostawionych ocen. Część validation już przy starej polityce miała 0/344. Z tej analizy nie wynika więc poprawa na validation po dodaniu kwarantanny q01.

Nowa reguła musi zostać zamrożona i sprawdzona na świeżych danych; wykorzystanie obejrzanych przypadków do projektowania pytania pozostaje eksploracją.

## Stabilność nie jest poprawnością

Wśród 12 par parafraz dających identyczne wektory tylko osiem miało oba wektory poprawne; cztery powielały błąd. Dla transferu domen identyczne wektory wystąpiły w 14 parach, ale oba poprawne tylko w siedmiu. Mierzyć stabilność razem z poprawnością i reakcją na rzeczywistą zmianę relacji.

## Następny informacyjny test

Rozdzielić dwa pytania dotyczące tych samych kandydackich ról i materiału: (1) czy relacja jest wyrażona w źródle, (2) czy jest sensowną propozycją wywnioskowanej abstrakcji. Nie promować wyniku drugiego pytania do pierwszej klasy dowodu. Zamrozić warianty sformułowań przed nowym zestawem przykładów i mierzyć oba cele osobno.

To propozycja, nie wykonany live test ani polecenie zmiany kanonicznej ontologii.

## Odtwarzanie podstawowego obliczenia z repo

Uruchomić z katalogu repo; skrypt nie korzysta z sieci ani kluczy:

```python
import json
from pathlib import Path
j = json.loads(Path('docs/research/inputs/jev-structure-analysis-2026-09-28.json').read_text())
c = j['confusion']
n = sum(c[k] for k in ('tp', 'tn', 'fp', 'fn'))
s = j['high_confidence']
q = j['score']['groups']['question']['q01']
q_retained = round(q['available'] * q['selective_coverage_planned'])
q_errors = round(q_retained * q['selective_error'])
assert (n, s['answered'], s['correct'], q_retained, q_errors) == (768, 673, 668, 46, 5)
for name, accepted, errors in [
    ('threshold_0.5', n, c['fp'] + c['fn']),
    ('band_0.2_0.8', s['answered'], s['answered'] - s['correct']),
    ('POST_HOC_q01_quarantine', s['answered'] - q_retained, s['answered'] - s['correct'] - q_errors),
]:
    print(name, 'accepted=', accepted, 'review=', n-accepted,
          'errors=', errors, 'coverage=', accepted/n)
```

Lokalny skrypt analizy dodatkowo sprawdził zgodność mianowników per pytanie/split, liczb błędów oraz oznaczenie polityki post hoc; osiem testów obliczeń przeszło. **To testy obliczeń raportu, nie osiem nowych eksperymentów Jev.** Pakiet kodu, wyciąg wejścia, wyniki i testy przekazano jako załącznik w rozmowie; powyższe obliczenie jest odtwarzalne z już istniejącego repo.

## Ograniczenia środowiska tej sesji

Zaszyfrowany plik OpenRouter został odnaleziony, ale w bieżącym środowisku brak dostępnego odpowiadającego klucza prywatnego. Próba publicznego połączenia z `openrouter.ai` ze środowiska wykonawczego zakończyła się błędem rozwiązywania nazwy. Konektor GitHub działa niezależnie. Nie wykonano płatnego requestu, nie odczytywano klucza odpowiedzi ukrytego holdoutu i nie umieszczono sekretów w repo.

Pozostałe ograniczenia badawcze: 64 autorskie teksty, po 12 ocen, warianty językowe i rodziny nie są niezależnymi obserwacjami. Z agregatów nie da się uczciwie przeliczyć dowolnego nowego progu ani całego rankingu per-przypadek. Nie dopisywać gwarancji statystycznych na podstawie samej liczby 768.
