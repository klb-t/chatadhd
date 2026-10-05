# 7B — wznowienie wyłącznie na rzeczywistych eksportach ChatADHD

**Aktualna dyspozycja właściciela, 2026-10-05 16:16:21 UTC: wszystkie nowe eksperymenty na rzeczywistych eksportach; wywołania syntetyczne wstrzymane.**

Nie uruchamiać znajdującego się tu syntetycznego `prepared/manifest.json`. Jego 128 żądań, oryginalne prompty/kolejność i źródłowy gold pozostają jako niewykonane archiwum. `reference-cost-plan-20261005.json` wycenia wyłącznie ten dawny panel. Nie wycenia przyszłego panelu eksportów.

Kontynuacja istniejącego `prepare.py` zawiera offline scorer pierwszych odpowiedzi i metryki zgodne z ModelProfile. Nie jest płatnym wykonawcą. Oryginalny materializer i istniejący root `research_programme_runner.py` pozostają drogą przygotowania/wykonania. Kod testowano kontrolowanymi atrapami, bez wywołań modeli.

1. Pobierz `gpt/model-research-2026-10-04` i aktualne `express20261005/COORDINATION.json` oraz `COORDINATION.md`. Przy ostatnim odczycie commit był nadal `edcb32fb87116ac497f8eb3d59d53ecea0cfee3f`, a te pliki nie były opublikowane. Nie korzystać z wcześniejszej dyspozycji o wywołaniach syntetycznych.
2. Wątek A jest właścicielem wspólnego rzeczywistego wycinka. Użyj wyłącznie jego zadeklarowanych źródeł, query_id, gold, reguł przekazywania danych i manifestu. Nie twórz odrębnego wycinka ani zastępczych etykiet.
3. W nowej jawnej wersji danych zwiąż tę samą macierz 16 wariantów z właściwą liczbą pytań ustaloną w aktualnej koordynacji. Zachowaj pełne źródła i tożsamość pytań; gold pozostaje poza promptem. Jeśli koordynacja utrzyma osiem pytań, plan pozostaje 128 operacji.
4. Dostosuj źródłowy kontrakt materializera/scorera do rzeczywistego wycinka, jeżeli jest taka potrzeba. Aktualny binder sprawdza archiwalny format source/gold i widoczność known_at≤as_of; nie udaje automatycznej zgodności z nieotrzymanymi eksportami. Epistemiczny opis obecnego scorera także odnosi się do dawnych źródeł i wymaga nowej wersji.
5. Zamroź przed pierwszą odpowiedzią: rzeczywiste sources/gold, schemat, dokładne prompty i wszystkie żądania, kolejność, parametry, aktywny scorer/rubrykę i hashe. Nie zmieniaj kryteriów po odpowiedziach. Stary SOURCE_FREEZE jest historycznym dowodem oryginalnego przygotowania, nie hashem rozszerzonego scorera ani prerejestracją rzeczywistego badania.
6. Policzyć nową pełną górną wycenę i aktualną ofertę endpointu. Prowadzący 7A jest jedynym płatnikiem i właścicielem prywatnego rejestru wspólnego klucza 5 USD. Przekazać mu gotowy commit; nie wysyłać wywołań samodzielnie, nie kopiować/resetować ledgeru.
7. Po publikacji przez 7A niezmienionych pierwszych odpowiedzi i kosztowych atestacji wywołać `score_first_responses(...)` z jawnie sprawdzonymi hashami design/manifest/bundle i datą. Odrzucane są duplikaty, uszkodzony JSON, rozbieżności bindingu i próby naprawy.
8. `shared_metrics_by_configuration(score, source_id=...)` daje metryki zgodne z istniejącym formatem ModelProfile. Wyniki trzeba wiązać z datą, dowodem, dokładnym prompt hash/parametrami oraz produced_by przez istniejący eksport metod. Nie deklarować zapisu native.
9. Osobno ocenić adekwatność cytatów i obserwowalne wykonanie preferencji według zamrożonej rubryki. Długość, liczba cytatów i kontrargumentów nie są automatycznym wynikiem preferencji. Zachować wszystkie negatywne pierwsze odpowiedzi na archiwalnej gałęzi.

Bramka bieżącego kroku: **22/22 testy PASS** (siedem istniejących i 15 kontrolowanych testów scorera). Bramka badania rzeczywistego: **NOT READY — oczekiwanie na wspólny wycinek A**. Wywołania/koszt/rezerwacje 7B: **0/0 USD/0 USD**. Nie odczytano `eval/real-holdout-key`, kluczy ani rzeczywistych odpowiedzi modelowych tego panelu.
