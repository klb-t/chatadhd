# Podwątek 7B — prompty, parametry i preferencje użytkownika

Stan 2026-10-05 po aktualizacji właściciela 16:16:21 UTC: **wywołania na syntetykach wstrzymane; nowe badanie wyłącznie na rzeczywistych eksportach ChatADHD ze wspólnego wycinka A**.

Gałąź `gpt/research-prompt-preferences-2026-10-05`, baza `edcb32fb87116ac497f8eb3d59d53ecea0cfee3f`. Bieżący kod/raport zostanie wskazany po publikacji tego checkpointu. Commit oryginalnych odzyskanych danych: `da3eb54f5dac455a2bd78660eabafbadb20589f3`. Nie ma commitu gotowego do płatnego uruchomienia na rzeczywistych eksportach.

## Gotowy przyrost i bramki

Kontynuowano istniejący panel `express20261005/prompt-parameters-v1`; nie utworzono drugiego płatnego wykonawcy. W istniejącym `prepare.py` dodano strict first-response scorer oraz `shared_metrics_by_configuration` w formacie metryk ModelProfile. Oryginalny materializer, prompty, schemat i 128 przygotowanych żądań pozostały byte-for-byte bez zmian.

**22/22 testy PASS**: siedem istniejących i 15 kontrolowanych testów scorera. Weryfikują dokładne źródłowe bindingi/gold/czas, JSON bez duplikatów i nonfinite, identyczny schemat, pierwsze odpowiedzi i duplikaty, cytaty/ucięcie, jawne braki i metryki profili. Nieuruchomione sloty nie są nieznanymi wydatkami: pusty panel daje 0 USD, 0 nieznanych kosztów i null dla jakości. Nie rozluźniono progów. To testy kodu na atrapach, nie modelowe eksperymenty na syntetykach.

[SCORER_CHECKPOINT_7B.json](../research/model_research_2026-10-04/express20261005/prompt-parameters-v1/SCORER_CHECKPOINT_7B.json) wiąże kod i log bramki. SHA-256 prepare.py: `357b7d3e1cf7b96538bc195560c42d280c69378bed910c78eb1e249e4ec84c1d`; test_prepare.py: `12beca93c28edf40ba384b5dae3c3ac2fdc0149d8bdf797ae2e3905cec37584a`.

Bramka nowego badania: **NOT READY**. Ostatni pobrany stan `gpt/model-research-2026-10-04` nadal wskazywał `edcb32f`; `express20261005/COORDINATION.json` zwrócił 404. Nie opublikowano tam jeszcze koordynacji nowego rzeczywistego wycinka. Nie zamrożono pozornych real sources/gold/żądań, nie wyceniono nieotrzymanych wejść i nie uznano syntetycznych bindingów za rzeczywiste.

Nie uruchamiano build/CTest dla tego przyrostu badawczego. Nie zmieniano STATE, głównego README, UI ani kodu natywnego. Nie odczytywano kluczy ani `eval/real-holdout-key`.

## Zachowane niewykonane przygotowanie

Źródła zostały odzyskane z publikacji `gpt/research-jev-validation-2026-10-05`, commit `37a65efab8927f5fa3901b1db4237a7bbb69e141`. ZIP ma SHA-256 `3885798cdc1769a8de1b29e7a1ef8b1633edc8cf33502baa947497c19c91ec06`. Rozpakowanie do pustego katalogu, CRC 568 wpisów i SHA/rozmiary 566 payloadów: PASS. Zweryfikowano 137 oryginalnych wpisów SOURCE_FREEZE. Siedem pierwotnych testów oraz istniejący loader manifestu 128 operacji: PASS.

Zachowano całe źródła, aktualny gold, cztery pytania new_label_001 i cztery new_label_013 oraz pełne 16-config crossing. Gold nie trafia do promptów. Te dane są syntetyczne i **nie wolno ich teraz wykonywać**. SOURCE_FREEZE wiąże oryginalny producent sprzed rozszerzenia scorera. Wcześniejszy etap odkrywania źródeł zachowano na `archive/gpt/research-prompt-preferences-source-discovery-2026-10-05`, commit `7b35923e1e74809bdc81353ba7dd833ffd65f72e`; nie jest aktywnym protokołem.

## Koszt i wyniki

- Rzeczywiste płatne wywołania / koszt / rezerwacje **7B: 0 / 0,000000000 USD / 0 USD**.
- Nowy panel rzeczywistych eksportów: **planowany koszt null**, do czasu bindingu wspólnego wycinka i aktualnej oferty wszystkich komponentów.
- Niewykonany panel syntetyczny: dokładna konserwatywna wycena referencyjna **0,3580992 USD** = 567568 górnych jednostek wejścia ×0,40 USD/M +81920 limitowanych tokenów wyjścia ×1,60 USD/M. Jednostki wejścia są allowance bytes+1088, nie tokenizacją. [Wyliczenie każdego wariantu](../research/model_research_2026-10-04/express20261005/prompt-parameters-v1/reference-cost-plan-20261005.json). Nie jest to wycena rzeczywistych eksportów ani koszt wykonania.
- Wspólny klucz ma limit **5 USD**; prowadzący 7A posiada jeden prywatny rejestr i wykonawca. Wywołań samodzielnych nie było.

Wyniki wszystkich 16 konfiguracji: **niezmierzone**, zero pierwszych odpowiedzi; nie zapisano zer jako jakości. Dotyczy obu promptów, obu temperatur, obu limitów i obu preferencji. Wnioski empiryczne zależne od preferencji: **brak**. Mechaniczny scorer raportuje długości/liczności, ale nie przyznaje punktów za krótszy tekst i nie myli literalnego cytatu z adekwatnością. Ręczne metryki pozostają null. Przy rzeczywistych odpowiedziach trzeba zachować źródłową zależność obserwacji; nie deklarować ślepego holdoutu ani jakości produkcyjnej.

## Dokładne wznowienie

[7B_HANDOFF.md](../research/model_research_2026-10-04/express20261005/prompt-parameters-v1/7B_HANDOFF.md), **punkt 1: pobranie aktualnej opublikowanej koordynacji A i wspólnego wycinka rzeczywistych eksportów**. Potem nowa wersja source/gold/design, dostosowanie istniejącego binder/scorera, pełny freeze przed odpowiedziami oraz nowa wycena. Dopiero taki commit przekazać 7A do jego centralnego wykonania. Nie uruchamiać znajdującego się tu archiwalnego prepared/manifest.json.

## Do wątku N

- **7A / A:** 7B respektuje wyłącznie rzeczywiste eksporty. Opublikuj aktualne COORDINATION.json/md oraz autorytatywny wspólny wycinek/gold/manifest na gpt/model-research-2026-10-04. Archiwalnych 128 żądań nie wykonuj. Scorer i jego 22 testy są gotowe do adaptacji; rzeczywisty panel wymaga nowego freeze i wyceny.
- **7B:** zacznij od punktu 1 handoffu. Nie twórz osobnego wycinka, nie zastępuj źródłowych etykiet ani nie czytaj odpowiedzi przed zamrożeniem aktualnych kryteriów. Oceniaj opublikowane pierwsze odpowiedzi zgodnymi metrykami ModelProfile i datowanym produced_by; negatywne odpowiedzi w pełni zachować w archiwum.
- **3/4:** brak odpowiedzi nie jest pomiarem jakości. Export metryk/produced_by nie udaje zapisu native.
- **9:** przyrost jest sprawdzonym scorerem i handoffem, nie zakończonym badaniem modeli. Nie przenoś archiwalnego panelu syntetycznego do aktywnej kolejki.
