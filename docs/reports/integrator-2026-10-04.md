# Integrator9 — 2026-10-04

Baza kodu: `161cc22dfb84fe863389d6b90323bd44516a68dc` z zachowanym INTERFEJS/PR9.
Gotowych gałęzi kodu przyjęto **0**. Aktualizacja main obejmuje tylko dokumentację
koordynacji, po jej osobnych bramkach. Nie edytowano implementacji innych wątków,
README ani profili aplikacji. [INDEX](INDEX.md) zawiera sprawdzone commity11 wątków.

## Co wykonano

- Świeży fetch, przegląd wszystkich gałęzi/raportów i liniowy rebase kandydata2.
- Niezależne wykonanie19/19 kontraktów2, pełnego CTest z kontrolą rzeczywistych
  liczników i build web; kandydat pozostaje poza main z powodu presetu w C++.
- Odtwarzalne reprodukcje błędów4/5/7, pełne źródła/dane/wyniki/hash w archive.
  Nowa poprawka7 zweryfikowana na trzech oryginalnych kontrolach; historia
  negatywna zachowana. Cały negatywny checkpoint6 także zachowany w archive.
- Inwentarz11 (695 grup/559 plików) przekazano każdemu z11 wątków w INDEX;
  ponowne zadania1/2 i jawne luki zakresów zapisano, bez udawania ich odbioru.
- Przed odbiorem3/4 wymagany wspólny wersjonowany format metod/pochodzenia
  w grafie i regresja przechodząca przez oba API. Same ślady JSON nie wystarczą.

## Liczby i granice dowodów

| Pomiar | Wynik |
|---|---|
| Pierwsza pełna baza161cc22 | 106/108; structure/contracts timeout60s;907.42s |
| Pierwszy pełny kandydat2 (34cc920 rebased) | 106/108; te same timeouty;620.91s |
| Osobny końcowy pełny kandydat2 | 108/108;339.14s;659 native/24465 assertions,1276 Python,0 skips |
| Kontrakty kernel2 poza CTest | 19/19; fake HTTP sentinels0 |
| Build web kandydata2 | 85 modułów; zakończony powodzeniem |
| Czysta linia161cc22 + dokumenty9 | Build/web zielony;108/108 w351.36s;659 native/24465 assertions,1276 Python/0 skips; osobny pomiar |
| Płatne wywołania integratora | 0 |

Guard potwierdził107 wykonanych outer entries kandydata2; istniejący opt-in
catalog_scale ma0/0 i jest jawnie niewykonany. Context i knowledge mają po18
rzeczywistych przypadków. Żadnych testów, progów ani timeoutów nie zmieniono.
Powtórka to nowy pełny przebieg, nie suma wybranych retry. Obciążenie hosta
wpływało na czas: structure po spadku obciążenia29.15s, contracts36.27s.
Wcześniejsze disk-full/OOM/timeout są zachowane jako oddzielne wyniki.
Nowe24 grupy2 z648a4e9 nie są objęte naszym wykonaniem19/19.

Oryginalne dowody czystego main z dokumentami9:
[pełny gate351.36s](integrator-main-verification-2026-10-04/README.md).
Pełne źródło/pokwitowania nieprzyjętego kandydata2:
[archive przed migracją danych](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-usage-policy-before-data/docs/reports/integrator-usage-review-2026-10-04/README.md).
Pierwsza nieudana baza:
[archive timeoutów](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-baseline-timeouts/docs/reports/integrator-baseline-timeouts-2026-10-04.md).
Reprodukcje4/5 i pozytywny replay7 są przypięte w INDEX. Wszystkie wejścia
integratora są publiczne lub syntetyczne; brak kluczy i prywatnych eksportów.

## Czego nie przyjęto i dlaczego

2: autorytatywny preset nadal w kodzie; autor910a1d6 jawnie wstrzymuje odbiór.
3: pierwszy selektor ukończony, rozszerzony rejestr/graf i data defaults niegotowe.
4: b8f29bf zawiera replay fix i packet-side graph-method contract; wspólny
adapter/regresja3 nadal nie istnieją. 5: abade80 usuwa przyczynę błędnego
checkpointu i naprawia stare journale; nowe regresje/finalny raport/bramki czekają.
Poprawki4/5 oceniono źródłowo, nie przypisujemy im starego negatywu ani własnego
niewykonanego pozytywnego rerunu. 6:14 pominięć, autor wstrzymał po jakości.
7: orphan fix zweryfikowany, reszta badań/raport/graf w toku. 8: CI Clang blokuje.
10/11: zakresy/bramki nieukończone; materialize nadal ukrywa błędy szablonów.
1: aed85b8 podłącza production prompt registry; brakuje wspólnego grafu metod,
legacy/public adapterów i końcowej weryfikacji z aktualnym2. Zapisane nowe
receipts autora sprawdzono hashem. Kolejność2→3/4/5→reszta nadal obowiązuje.

## Do wątku N

- **2:** preset z danych/profilu, jeden rzeczywisty loader i nakładka; nowy receipt24 grup.
- **3/4:** wspólny kontrakt method/version/run/result w grafie, pack defaults,
  krawędzie pochodzenia i regresja obu API przed odbiorem.
- **4/5:** wykonać niezależny replay nowych poprawek oraz pełne bramki po rebase;
  5 końcowy raport i instrukcja właściciela.
- **1/7:** prompty/przepisy/oceny jako wersjonowane byty i twierdzenia z dowodami;
  7 końcowy raport, rozliczenie i przygotowanie graph-reply vs tekst+JSON.
- **6:** nowy wynik DEV, zachować negatyw i jednokrotnie wykorzystane pierwsze spojrzenie.
- **8/10:** poprawka dwóch unused[this], świeży CI i końcowe receipts; bez suppressions.
- **11:** błędy materialize stage muszą być jawne; poprawić nieaktualne linki/opisy
  CLI, pełne parity/current-source/fullCTest i runtime injection.
- **3/11:** wspólna walidacja embeddingów (liczność/wymiar/wartości) we wszystkich
  wejściach providera; checked APIs i overlay. Wszystkie zadania inwentarza w INDEX.
