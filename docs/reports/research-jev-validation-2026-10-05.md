# Podwątek 7C — walidacja Jev na nowych źródłach

**AKTUALIZACJA WŁAŚCICIELA: 2026-10-05 18:16 Europe/Amsterdam — STOP syntetyków.
Dotychczasowy dispatch został WYCOFANY. Wszystkie nowe eksperymenty wyłącznie
na rzeczywistych eksportach ChatADHD, ze wspólnego wycinka przygotowywanego przez
7A. Nie wykonano żadnego płatnego wywołania przez 7C. Dane i preparację syntetyczną
zachowano bez zmian; nie są nowym eksperymentem ani wynikiem walidacji.**

Zaktualizowano [PR13](https://github.com/klb-t/chatadhd/pull/13), status przekazania
i dokument dispatchu; opublikowany commit STOP: `abd2f33ae8f41171841b539dc4c27a096a8362d6`.
Pobranie `gpt/model-research-2026-10-04` po tej wiadomości
nadal zwróciło `edcb32f` bez nowego pliku koordynacji; czekamy na publikację 7A.
Nowe źródła/gold/żądania dostaną osobny freeze, bez podmieniania starych hashy.
Raw rzeczywistych rozmów i request body nie wolno kopiować do publicznego repo;
dotychczasowy pełny publiczny eksporter jest właściwy dla fikcyjnych danych i
przed nową collection potrzebuje jawnego kontraktu publicznej projekcji.
Dokładna instrukcja nowego source/gold/request freeze oraz granicy publikacji:
`stage5/jev-validation-20261005/REAL_SOURCE_READINESS.md`.

Poniższe sekcje opisują wcześniejszy, wycofany etap offline.

Stan 2026-10-05: odtworzono zamrożoną populację i żądania; płatny dispatch prowadzi
wyłącznie 7A na istniejącym wspólnym rejestrze. Gałąź
`gpt/research-jev-validation-2026-10-05`, przygotowanie bazowe `66cd016` po fetch
opublikowanego `edcb32f` i pobraniu późniejszego commitu z checkoutu prowadzącego.
Nie ponawiano wcześniejszych 720 wywołań. Nie czytano `eval/real-holdout-key`,
prywatnych danych, klucza ani prywatnego ledgeru.

**Commit gotowy do dispatch: `957819e9e17da29603a6587cc9d543a455ccef8e`**.
**Commit do odbioru eksportera/bramek: `dee0923a593b9db3c6bb8340fd666a69c9330354`**
(lokalny `4bf73e6`; dokładne drzewo `0e7b10df1cbccfe3c77ea83f1e7586e8527a2540`).
Przekazanie do 7A: [draft PR13](https://github.com/klb-t/chatadhd/pull/13).
Archiwum pełnych własnych kontroli:
`archive/gpt/research-jev-validation-2026-10-05`, tip
`254c2ec29146ddc309d8d8a9a37bdbdbf8e01d7a`; ZIP SHA256
`11bf5411560c30a52bf1f3b7a6dadae7769706530196272ce63bfb8837413b4e`.

Commit dispatch jest
na `gpt/research-jev-validation-2026-10-05`. Drzewo SHA
`beaf13c420c530f2a4085144a25500de3044af0a` jest identyczne z lokalnym `7c5cb10`.
Przyrost odtworzenia: opublikowany `37a65efab8927f5fa3901b1db4237a7bbb69e141`
(lokalny `c13cea5`). Bazowe przygotowanie `66cd016` opublikowano pod
`9e1a8bb1334d061dd07703dfaedde568f9550373`, również z identycznym drzewem.
Wszystkie przyrosty wypchnięto kolejno, bez force. API GitHub utworzyło nowe SHA
commitów ze względu na własne metadane; treści i drzewa nie uległy zmianie.

## Populacja i stan wyników

24 nowe autorskie, recenzowane rodziny, 12 PL i 12 EN, cztery pytania na rodzinę.
Po 96 ocen dla `j_active` i `j_directed`; razem 192 operacje fizyczne.
To źródłowo oceniany zbiór autorski, widoczny dla recenzentów; nie zewnętrzny ślepy
holdout. Mierzona jest zgodność z aktywnym jawnym zobowiązaniem źródła dla relacji,
kierunku, mówcy i czasu, bez twierdzenia o prawdziwości świata.

| Populacja | j_active | j_directed | Status |
|---|---:|---:|---|
| Znane rodziny: dwie wcześniejsze powtórki | 80/96 | 89/96 | Wynik historyczny; osobna populacja |
| Nowe 24 rodziny | null/96 | null/96 | Oczekuje na publiczne pierwsze odpowiedzi od 7A |

Mianowniki nowych danych pozostają 96/arm i 48/arm/język. Braki, niepoprawny
protokół i konflikt nie są usuwane. Nie zmieniamy gold, etykiet, progu >0,5,
przygotowanych body, ID ani odpowiedzi modeli. Nie naprawiamy JSON.

Audyt konstrukcji przed odczytem wyników: gold ma 34 supported, 27 refuted,
35 unknown; PL 17/13/18, EN 17/14/17. Kolejność pytań jest skorelowana z etykietą:
modalna reguła pozycji `[supported, refuted, supported, unknown]` trafia 61/96.
To **dopasowanie opisowe do tego samego gold**, nie ślepa predykcja ani wynik
nowego konkurencyjnego przepisu. Nie dowodzi, że Jev korzysta z tej korelacji.
Pokazuje ograniczenie szerokiej interpretacji wyniku; porównanie dwóch przepisów
na identycznych źródłach pozostaje osobnym pomiarem. 21 rodzin ma pięć tur,
trzy mają sześć; zbiór nie pokrywa pełnej różnorodności rozmów rzeczywistych.
Wszystkie dane i instrument pozostają niezmienione. Reprodukcja:
`python docs/research/model_research_2026-10-04/stage5/jev-validation-20261005/audit_corpus.py`;
wynik `CORPUS_AUDIT.json` ma datę, hashe wejść i produced_by do skryptu.

Planowana rezerwa: **0,192 USD**. Rzeczywisty koszt nowej populacji: **null** do
otrzymania rachunków generacji. Własne płatne wywołania 7C: **0**. Historyczne
0,873216500 USD/720 prób to wcześniejszy checkpoint całego programu, nie aktualne
saldo ani koszt tego badania. Wspólny limit klucza: 5 USD; bez drugiego wykonawcy.

## Odtworzenie i bramki

Kapsuła `stage5/UNEXECUTED_PREPARATIONS_20261005.zip`:
SHA256 `3885798cdc1769a8de1b29e7a1ef8b1633edc8cf33502baa947497c19c91ec06`,
CRC PASS, 568 unikalnych bezpiecznych ścieżek, 566/566 payloadów zgodnych z sidecar.
Rozpakowano do pustego katalogu; odtworzono tylko potrzebne źródła i preparację.
Wersja scorera w ZIP jest historyczna; aktywny, domknięty scorer z `66cd016`
zachowano osobno i nie zastąpiono go zawartością kapsuły.

- Gold SHA256: `2f6e041024658543ffacc20f123ee9ef047ac32858638b8eecb49ff3c9d4c6e2` — PASS.
- Oryginalny manifest SHA256: `14cbb6bb88f0397554815c06ac63d0e19121bd662ceb985f301a2a4770d092b2` — PASS.
- Scorer SHA256: `51a68636c569362e6777865467e6eace09ff9d79c7f7613a53a3132d7a782d13`.
- Konfiguracja SHA256: `ebc02a4cef3a5c02189815d69ab09217a73d2d240a638f3dd127ca920004bd41`.
- Osiem oryginalnych kontroli scorera — PASS na nowym checkout.
- Producent `programme_context_preparation_v1.py verify` — PASS, dokładnie 192 operacje.
- Powiązanie programu: 9/9 testów PASS; wszystkie 192 body i ID identyczne.
- Eksport wyników i porównanie par: 8/8 testów PASS, w tym actual `produced_by`
  w zakodowanym grafie, rozdzielenie poprawnego protokołu i konfliktu etykiet,
  stałe 96/48, hash binding i brak wyniku przy całkowicie niewykonanym badaniu.
- Istniejący normalizer i manifest: 25/25 testów kompatybilności PASS.
- Integracja pełnej zamrożonej populacji z pustym bundle: 192 brakujące pierwsze
  odpowiedzi pozostają w mianowniku; eksport grafu zachowuje null jakości.

Dowody nowych bramek: `stage5/jev-validation-20261005/GATES.json`, dwa pełne logi
oraz `EMPTY_CONTROL.json` / `EMPTY_EXPORT_CONTROL.json`. To wyłącznie kontrola
mechanizmu na atrapach, nie wynik modeli. 42 unittest cases + 8 oryginalnych
kontroli protokołu + dwa integration controls; pełny CTest nie był uruchamiany.

Manifest wykonawcy SHA256 `e0dea1c45f0306cd4e7253fa4989bd9d1007a8c4878989bb80a98d7c850bea26`;
config scorera SHA256 `4cfc08a2afbe4e98204232eda45d4cafd81f6a12f738fd4d7c5456bbc9f328ce`.
Zmiana wyłącznie programme_id i trzech pól powiązania manifestu w osobnym configu.
Oryginalne hashe, gold, próg i helper pozostają niezmienione. Receipt nie twierdzi,
że offline sprawdził stan prywatnego rejestru; tę bramkę wykonuje 7A.

Pierwszy replay preparacji wykazał brak historycznych template'ów w checkout.
Odtworzono oryginalny manifest i dwa wymagane body z istniejącej kapsuły
`label-repetition-v1/repetition-request-replay-v1.zip`; nie zmieniono preparacji.
Dowody: `stage5/jev-validation-20261005/RESTORE.json` i `PRE_COLLECTION.json`.
Pełnych historycznych bramek C++ nie przenosimy na ten przyrost Python/danych.

## Wznowienie

1. Fetch tej gałęzi i gałęzi prowadzącego. Sprawdzić powyższe hashe oraz oryginalne
   osiem testów: `python docs/research/model_research_2026-10-04/stage5/new-label-scoring-v1/test_score_first_only.py`.
2. Odbiór gotowej transformacji i dokładne polecenia:
   `stage5/jev-validation-20261005/DISPATCH_7A.md` oraz `DISPATCH_READY.json`.
   Nie generować nowej preparacji ani nie zmieniać opublikowanych requestów.
3. 7A wykonuje preflight świeżych cen i wspólnego stanu kosztów, wysyła raz 192
   operacje, rozlicza generacje i publikuje wyłącznie bezpieczny NORMALIZED bundle
   z niezależnie zapisanym SHA. 7C nie otrzymuje klucza ani kopii prywatnego ledgeru.
4. Uruchomić oryginalny scorer na publicznym bundle z hashami config/bundle;
   następnie użyć gotowego `python -m loom.tools.structure.jev_validation_results_v1`
   z `--score`, `--bundle`, `--manifest`, `--config`, `--output`,
   `--observed-on 2026-10-05` i osobnymi `--score-sha256`, `--bundle-sha256`,
   `--manifest-sha256`, `--config-sha256`. Inputami są wynik zamrożonego scorera,
   publiczny NORMALIZED i powyższy manifest/config wykonawcy. Output musi być nowym
   katalogiem w repo, np. `stage5/jev-validation-20261005/measured-v1`.
   Eksporter zapisuje datowane twierdzenia o wersjach przepisów w istniejącym
   formacie profili metod: requested/observed modele, prompt/version hash,
   parametry, dowody, produced_by i wszystkie mianowniki. Zachowuje oryginalne
   bajty czterech wejść oraz osobny `VALIDATION.json` z 96 porównaniami par.
5. Porównać dostępność, protokół, etykiety, PL/EN, koszt, błędy i pary rozbieżności.
   Zachować pełne negatywy na gałęzi archiwalnej, bez automatycznej adopcji presetu.

Pozostaje zależność: 7A musi wykonać i opublikować collection oraz potwierdzone
rachunki. Offline nie można ustalić dostępności, jakości i kosztu nowych modeli.
Komplet własnych pozytywnych i negatywnych fixture'ów oraz pusty kontrolny bundle
zachowujemy w archiwum wskazanym przez `NEGATIVE_ARCHIVE.json`; archiwum nie jest
wynikiem nowej populacji. Po wynikach nie zmieniać instrumentu ani wejść.

## Do wątku N

- **7A:** NIE wykonuj wycofanego syntetycznego dispatchu `957819e9`.
  Opublikuj aktualny plik koordynacji i wspólny wycinek rzeczywistych eksportów,
  z kontraktem prywatnego źródła oraz bezpiecznych publicznych projekcji.
  7C przygotuje osobny source/gold/request freeze przed collection na jedynym
  wspólnym rejestrze. Oczekujemy odpowiedzi i rachunków, nie klucza ani ledgeru.
- **1, 3/4:** Nowych wyników jeszcze nie ma; historyczne 80/96 i 89/96 nie opisują
  nowych rodzin. Twierdzenia i produced_by będą osobnym przyrostem po collection.
- **9:** Nie oznaczaj 7C jako zakończonej walidacji modeli na podstawie bramek offline.
