# Podwątek 7C — walidacja Jev na nowych źródłach

Stan 2026-10-05: odtworzono zamrożoną populację i żądania; płatny dispatch prowadzi
wyłącznie 7A na istniejącym wspólnym rejestrze. Gałąź
`gpt/research-jev-validation-2026-10-05`, przygotowanie bazowe `66cd016` po fetch
opublikowanego `edcb32f` i pobraniu późniejszego commitu z checkoutu prowadzącego.
Nie ponawiano wcześniejszych 720 wywołań. Nie czytano `eval/real-holdout-key`,
prywatnych danych, klucza ani prywatnego ledgeru.

Przyrost odtworzenia: lokalny `c13cea5`. Dispatch jest przygotowany w
`stage5/new-label-dispatch-binding-v1`; commit opublikowany przez connector i
końcowe wyniki zostaną przypięte poniżej. Zwykły `git push` nie miał dostępnego
uwierzytelnienia; publikacja korzysta z połączonego GitHub, bez czytania credentiali.

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
   następnie wyeksportować datowane twierdzenia o wersjach przepisów w istniejącym
   formacie profili metod. Zachować requested/observed modele, prompt/version hash,
   parametry, dowody, produced_by i wszystkie mianowniki.
5. Porównać dostępność, protokół, etykiety, PL/EN, koszt, błędy i pary rozbieżności.
   Zachować pełne negatywy na gałęzi archiwalnej, bez automatycznej adopcji presetu.

## Do wątku N

- **7A:** Odbierz preparację 7C i wykonaj dispatch na jedynym istniejącym rejestrze.
  Oczekujemy publicznych pierwszych odpowiedzi i rachunków generacji, nie klucza.
- **1, 3/4:** Nowych wyników jeszcze nie ma; historyczne 80/96 i 89/96 nie opisują
  nowych rodzin. Twierdzenia i produced_by będą osobnym przyrostem po collection.
- **9:** Nie oznaczaj 7C jako zakończonej walidacji modeli na podstawie bramek offline.
