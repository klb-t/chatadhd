# Wydzielone badanie: tanie Jev, 192 pierwsze wywołania

Przekazanie offline, 2026-10-05. W tej rozmowie nie uruchomiono tego badania i nie
przeczytano rzeczywistych odpowiedzi. Gotowe są źródła, złoto, 192 dokładne requesty,
preparacja i zamrożony scorer. Nie ma tu klucza ani prywatnego ledgeru. Wątek główny
kontynuuje osobno panel frontier; nie powtarzać jego wywołań.

## Zakres i koszt

Porównanie dwóch wcześniej wybranych przepisów `j_active` i `j_directed`, oba przez
`typesafe/jev-1.13` / `typesafe`: po 96 zapytań, razem 192, na 24 nowych fikcyjnych
rodzinach źródeł (12 PL i 12 EN). Mierzymy zgodność z jawnym zobowiązaniem źródła,
kierunkiem relacji, mówcą i czasem. To dane autorskie, nie ślepy holdout ani
prawdziwość świata. Pierwsza odpowiedź pozostaje jedyną odpowiedzią ocenianą.

Zapisany plan `../new-label-preparation-v1/cost-plan-v1.json` rezerwuje **0,192 USD**;
koszt rzeczywisty tego badania jest jeszcze nieznany. Cennik zapisano
2026-10-05T13:19:54Z i ma preset świeżości 24 godziny. Limit właściciela całego
programu to 5 EUR, a osobnego klucza 5 USD. Znany koszt programu przed nowym panelem
frontier wynosił 0,873216500 USD / 720 unikalnych prób. To historyczny punkt, nie
aktualne saldo: przed uruchomieniem trzeba odczytać wspólny aktualny ledger i licznik
klucza. Nie tworzyć niezależnego pustego ledgeru dla tej samej puli pieniędzy i nie
uruchamiać dwóch płatników równocześnie na kopiach jego stanu.

## Od czego zacząć

1. Pobrać opublikowaną gałąź `gpt/model-research-2026-10-04`, przeczytać raport wątku
   7 i `README.md` w tym folderze. Sprawdzić `SOURCE_FREEZE.json` oraz SHA-256
   kapsuły przekazania, jeśli dane dostarczono w ZIP.
2. Odtworzyć lub zweryfikować istniejące requesty, używając **gotowego** producenta:

   ```sh
   PYTHONPATH="$PWD" python docs/research/model_research_2026-10-04/stage5/context-preparation-v1/programme_context_preparation_v1.py verify --config docs/research/model_research_2026-10-04/stage5/new-label-preparation-v1/configuration.json --output docs/research/model_research_2026-10-04/stage5/new-label-preparation-v1/prepared
   python docs/research/model_research_2026-10-04/stage5/new-label-scoring-v1/test_score_first_only.py
   ```

   Producent nie otwiera złota, rubryk, odpowiedzi ani klucza. Scorer pozostawia
   oryginalny ścisły próg > 0,5 i wszystkie planowane zapytania w mianowniku.
3. Użyć istniejącego `loom/tools/structure/research_programme_runner.py` oraz
   `../new-label-preparation-v1/operator-policy.json` i
   `../new-label-preparation-v1/prepared/manifest.json`. `plan` jest offline;
   `run` wymaga prywatnego klucza, aktualnych dowodów cen/FX i **aktualnego wspólnego
   ledgeru**. Konkretne prywatne ścieżki przekazuje właściciel/koordynator poza
   publicznym repo. Zachować operacje/ID/requesty; nie dopisywać retry.
4. Po rozliczeniu eksportować bezpieczny publiczny bundle funkcją
   `loom.tools.structure.programme_results_v1.normalize(manifest_path, ledger_directory)`.
   Zapisuje się wynik jako nowy `NORMALIZED.json`, jego SHA-256 oraz dowód
   rozliczenia. Eksporter czyta tylko wskazane próby i publiczne projekcje, nie
   publikuje klucza ani bindingu konta. Nie kopiować do repo prywatnych capture'ów.
5. Użyć dokładnego CLI scorera z `README.md`. Frozen config SHA-256:
   `ebc02a4cef3a5c02189815d69ab09217a73d2d240a638f3dd127ca920004bd41`.
   Helper SHA-256:
   `51a68636c569362e6777865467e6eace09ff9d79c7f7613a53a3132d7a782d13`.
   Ten freeze musi być opublikowany **przed pierwszym odczytem rzeczywistych wyników**.
6. Zostawić osobny wynik dla każdego przepisu: trafienia / 96, dostępność / 96,
   24 rodziny, koszt rzeczywisty i nieznane rozliczenia. Widoki rodzin, języka i
   relacji nakładają się; nie sumować ich kosztów ponownie. Wyniki negatywne
   zachować w całości na gałęzi lub w archiwum. Wyniki mają być twierdzeniami o
   konkretnych metodach, z wersją/hashami/parametrami i datą, w dotychczasowym formacie
   profili metod; nie promować automatycznie do presetów.

## Stan bramek i niedokończone

Statyczna walidacja wszystkich źródeł i 192 operacji: PASS. Osiem kontrolnych
przypadków scorera: PASS; dokładny dowód `FAKE_VERIFICATION.json`. Pełne negatywne
fixture'y są w `fake-protocol-controls-full-20261005.zip`; opis i hash w README.
To kontrola protokołu offline, nie wynik modeli. Native CTest dotyczy gałęzi
koordynatora; ta preparacja nie zmienia kodu silnika.

Niedokończone: wywołania 192/192, rzeczywisty koszt, scoring, twierdzenia o metodach
i końcowe wnioski. Pierwszy krok zależny od uruchomienia to uzgodnienie aktualnego
ledgeru z główną rozmową, potem preflight aktualnych cen i budżetu. Nie wykonywać
nowych prób frontier ani etapu odpowiedzi grafowej w tym wydzielonym zakresie.

## Do wątku N

Do wątku 7: opublikować freeze przed oceną, uruchomić wskazaną nową populację i
zachować pierwsze odpowiedzi oraz pełne rozliczenie. Do wątku 1: dopiero po wynikach
odebrać porównanie dwóch przepisów; nie uznawać tego przekazania za wybrany preset.
Do wątku 9: preparacja jest danymi badawczymi; żadnego wyniku modelu jeszcze nie ma.
