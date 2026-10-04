# Katalog: pierwsze spojrzenie, 2026-10-04

Jedna końcowa ocena publicznego, fikcyjnego `blind_catalog_v2` z commitu
`5d85034e2353a3a6e2b2beacd1549b8cd0566584` gałęzi
`wip/worktree-agent-a342fccb481c4116c`. Kod produkcyjny i parametry zamrożono
przed otwarciem korpusu, na `7a0d3ed6a05aa45d4cf9098cfd1ea4e9a44adf01`.
Biblioteka: SHA-256
`5ded94c8771d79105c32d8b7de38c80a7500ec7cb475198e49148a9bb49a1fe5`.

| Populacja | Wybrane | Razem |
| --- | ---: | ---: |
| Relevant | 35 | 55 |
| Noise traps | 5 | 15 |
| Noise generic | 5 | 24 |
| Dokumenty pomocnicze bez etykiety | 2 | 3 |

TP=35, FN=20, FP=10, TN=29. Precision=35/45=0.777778;
recall=35/55=0.636364. Udział wybranych traps=5/15=0.333333:
dotychczasowy warunek ≤0.05 nie jest spełniony. Wynik nie daje pełnego
zielonego sprawdzianu jakości. Dokumenty pomocnicze nie wchodzą do
precision/recall rozmów.

Nie było osobnego protokołu ani zadeklarowanego przed oceną profilu właściciela
dla tego katalogu. Użyto dokładnie aliasów z zamrożonego DEV, bez zmian. Dopiero
po zapisaniu przewidywań odczytano publiczny `ground_truth.json`; jego zbiór
zadeklarowanych aliasów różni się od DEV. To transfer zamrożonego profilu,
z podaną granicą interpretacji. Etykiety nie trafiły do selektora. Nie czytano
generatora, `.blindwork`, prywatnych archiwów ani `eval/real-holdout-key`.

Szew zewnętrznego kanału semantycznego był wyłączony. LLM był wyłączony,
wywołań dostawców było 0, import wiadomości wyłączony, `dry_run=true`.
Nie dobierano aliasów, wektorów, wag ani progów do tego wyniku. Nie wykonano
ponownej oceny.

`predictions_before_gold.json` utrwala pełne wyniki natywne bez etykiet przed
otwarciem publicznego gold. `inputs_receipt.json` zawiera dokładną konfigurację,
hashy korpusu, profilu, packa i źródeł. `report.json` zawiera wszystkie decyzje
z dołączonymi później etykietami i liczby. `artifact_manifest.json` wiąże te
artefakty z ich SHA-256. Oryginalne ZIP-y i gold są w przypiętym commicie oraz
w lokalnym katalogu `inputs/`; nie są ponownie otwierane do analizy.

Dokładne polecenie pojedynczego wykonania:

```sh
python3 evaluate_first_look.py \
  --repository /workspace/scratch/73a479acdbc0/chatadhd \
  --library /workspace/scratch/73a479acdbc0/chatadhd/loom/build/dev/libloom.so.0.1.0 \
  --baseline-inputs /workspace/scratch/73a479acdbc0/catalog-baseline.inputs.json \
  --output /workspace/scratch/73a479acdbc0/catalog-blind-first-look
```

Marker `ATTEMPT_ONCE` blokuje powtórzenie, również po błędzie. Zachowany runner
opisuje reprodukowalną procedurę audytu; kolejne uruchomienie na tym korpusie
nie będzie już „pierwszym spojrzeniem”. W ramach tego zadania nie uruchamiaj go
ponownie. Liczby można przeliczyć z zapisanych decyzji bez nowej oceny silnika.
