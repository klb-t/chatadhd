# A: niezależny przegląd przyrostu C2, 2026-10-09

Przypięty przyrost `69880859802267f1b38b82eeaecd6fdc5522a47a` → `b9b503f62bb8e8e00c94ab7401e1cd9579129af3` w `klb-t/chatadhd`: 6 commitów, 64 zmienione pliki, +6287/−42 linii tekstu. To przegląd granic payer → wynik → publiczna projekcja → native store. Nie jest pełnym audytem nowych narzędzi do korpusu, kontekstu i ocen.

Oba poprzednie probe'y wykonano bez zmiany ich kodu: **projection 3 PASS / 6 FAIL**, **native 9 PASS / 0 FAIL**. Nowy niezależny zestaw: **7 PASS / 4 FAIL**, w tym cztery udane reprodukcje naruszeń i cztery nadal nieprzechodzące kryteria akceptacji. Nie sumujemy ich jako odsetka poprawności produktu; obejmują kontrole pozytywne, powtórzenia i jeden nieaktualny warunek starego harnessu. Nie wykonano płatnych ani rzeczywistych sieciowych wywołań. Przeczytano wyłącznie publiczne dane i własne syntetyczne fixtures; żadnych prywatnych archiwów, kluczy ani historycznych ledgerów właściciela.

## Co zostało potwierdzone

| Ustalenie / bramka | Dowód i wynik | Klasyfikacja przyrostu |
|---|---|---|
| **A2-C-003**: `sync_verified` nie sprawdza zgodności requestu kolejki z ledgerem | Prawidłowo zakończony i ponownie zweryfikowany rzeczywisty `PrivateLedger` z atrapą transportu; osobna publicznym API utworzona kolejka offline o tym samym operation ID i innym request SHA. Sync nadaje `captured`, `billing_verified=true` i request SHA z niewłaściwej kolejki. Kontrola zgodnej kolejki przechodzi. | **Częściowa naprawa granicy payera**: nowe admission rzeczywiście weryfikuje rezerwację, ale odtworzenie wyniku ma odrębną lukę. Nie jest to dowód ominięcia admission przez normalnie związany nowy dispatcher. |
| **A2-C-001**, nowe dowody C2 | Rzeczywisty `evaluation-build.py` przepuszcza nieznane pola pomocnicze protokołu i profilu projekcji do publicznego artifactu i odzyskanych źródeł. Syntetyczny canary wykryty odpowiednio w `policy` i `projection-profile-bytes`. | **Problem nadal istnieje** w nowym publicznym entrypoincie. Generic `method_graph` słusznie zachowuje bajty; odpowiedzialność za granicę publiczną spoczywa na callerze. |
| **A2-C-004**: odrzucone dane pojawiają się w błędzie | Nieznane pole prezentacji jest poprawnie odrzucone przez JSON Schema; `ValidationError` zawiera canary. Standardowy `sys.excepthook`, do którego trafia nieprzechwycony wyjątek CLI, wypisuje go do przechwyconego bufora stderr. | **Częściowa naprawa**: zamknięto pole w samym artefakcie, lecz pozostaje wyciek diagnostyczny. Nie stwierdzono publikacji rzeczywistej prywatnej treści. |
| **A2-C-002**, wcześniejszy wynik | Niezmieniony probe nadal przyjmuje wynik innego operation ID przy wspólnym request SHA przez `capture_first`. | **Nadal nierozwiązane**; nie twierdzimy, że nowy `sync_verified` tworzy taki inny operation ID. |
| `CacheObservedTransport` | Rzeczywisty transport, przechwycony opener: caller `cache=true` daje faktyczne `false`; obserwowane `HIT` pochodzi z odpowiedzi; obcy header nie jest projektowany. | **Naprawa wcześniej brakującego połączenia na tej granicy**. Brak dowodu integracji z produkcyjnym dispatcherem/UI lub bieżącej zgodności usługi z nagłówkiem. |
| Nowy handoff v2 → native store | 13 entities, 22 claims, 12 sources, 4 odzyskane rekordy; zamknięcie kontekstu i nowy Runtime, read/replay, wszystkie bajty źródeł i wyniki identyczne. | **Niezależnie zweryfikowana kompatybilność i trwałość**. Zapis natywny nie dowodzi wykonania zapisanej metody ani prawdziwości wyniku. |
| Nowy bridge → rzeczywisty payer → wynik → GraphPacket → native store | Jeden syntetyczny request, rzeczywiste rezerwowanie i walidacja, reference/evidence/order, wynik billing verified w ledgerze fixture, zapis i odtworzenie natywne. 11 entities, 16 claims, 12 sources, 2 rekordy. | **Naprawa zweryfikowana w składanym harnessie**; brak deklaracji kompletnego flow aplikacji. |

## Historyczny packet nie powinien udawać nowej receptury

Dodatkowy FAIL starego `C-PUBLIC-00` dotyczy równości osadzonego programu ze współczesnym plikiem. Oddzielny `C2-HISTORY-01` potwierdził, że archiwalny artifact pozostał bajtowo identyczny, a program jest dokładnie wersją z `6988085`. To dopuszczalne zachowanie R41 i nieaktualne założenie starego testu, **nie regresja C**. Stary receipt pozostaje nienaruszony; nowa bramka bada właściwe przypięcie historycznej receptury. Pozostałe pięć FAIL starego zestawu to wcześniejsze cztery testy publicznej projekcji i test przypisania wyniku.

## Źródła, zakres i ograniczenia

Czytano nowy `handoff-B.md`, `payer-boundary.md`, kontrakt prezentacji oraz wcześniejsze instrukcje repo i R15/R33/R39–R42. Wnioski o wiązaniu wyników wynikają z R15/R41 i wymaganej ścieżki użytkownik → decyzja → zapis → odtworzenie, a wymóg ochrony publicznych artefaktów i logów pochodzi wprost z instrukcji zadania A. Nie przypisujemy właścicielowi dodatkowych zamiarów.

Mianownik przyrostu: **64 pliki**, w tym **11 zmienionych wykonawczych plików Python bez testów autora** i 5 plików testowych Python. Ten pakiet analizuje zakresy **4 z 11 wykonawczych plików**: payer boundary, fragmenty workflow, fragmenty analysis oraz publiczny CLI. Pozostałe 7 nie ma tutaj przeglądu semantycznego. Wsparcie `method_graph`, native wrapper i payer użyto jako rzeczywistych konsumentów; wcześniejsze ich badanie nie jest ponownie naliczane. `coverage.json` zawiera cały wykaz 64 plików, sklasyfikowany mianownik, funkcje i ograniczenia każdego zakresu.

W szczególności nadal nieprześledzone: rzeczywisty korpus i jego prywatne locatory, pełny context compiler, implementacja semantycznych ocen, paired comparison, Jev admission, UI Basic/Advanced/Expert, live dispatch i odzyskanie niejednoznacznych rzeczywistych płatności. Nie uruchamiano cudzych zestawów testów kolejki; użyto wyłącznie setupu syntetycznych danych i mock transportu autora, a obserwacje/assertions są niezależne.

Native library pochodzi z main `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`, SHA-256 `4ce4d06f2348c82f063893882a85ebc1c2efdec584e28f52dd90d87f64842771`. Diff między tym main a C2 nie obejmuje `loom/src`, `loom/include`, native wrappera ani codec. Powtórzenie nie wymaga modelu, klucza ani emulatora. Sieć Python jest zablokowana, a natywny Runtime nie uruchamia workers.

R42 nie jest podstawą do usuwania wszystkich stringów. Nazwy kontraktów i lifecycle/diagnostic codes mają wyjątki 1/3, protokół HTTP i SHA-256 wyjątek 2, serializacja wyjątek 4, bootstrap wyjątek 5, diagnostyka wyjątek 6. Wyjątek diagnostyczny nie stanowi uprawnienia do ujawnienia prywatnych wejść. Syntetyczne klucze, treści i modele w harnessie to fixtures, a nie dane produktu. Nie sklasyfikowano wymuszenia braku response cache w metodzie badania niezależnych prób jako ogólnego zakazu innych strategii ani nie sformułowano nowej architektury obok R15/R33/R40/R41.

## Odbiór i punkt wznowienia

`findings.jsonl` i `backlog.json` zawierają reprodukcję → expected → bramkę → konsumenta → migrację → ryzyko → zależności. `test-index.json` wskazuje wykonywalne komendy i rzeczywiste receipts. Początkowy receipt delta zachowano oddzielnie: później dodano natywny roundtrip bridge i rzeczywiste renderowanie błędu, bez osłabienia kryteriów.

Następna konkretna czynność po poprawce C/B: uruchomić niezmieniony `C2/run.py` z checkoutem poprawki i jego SHA, a następnie sprawdzić cztery acceptance FAIL. Payer powinien odmówić niezgodnej projekcji przed zapisem wyniku/evidence, zachowując prawdziwy ledger; wrapper publiczny powinien objąć walidacją także auxiliary sources i generować bezpieczny błąd. Zachować sukcesy obu natywnych roundtripów i historyczne przypięcie programu. Nie wymaga to oczekiwania na nowy płatny eksperyment.
