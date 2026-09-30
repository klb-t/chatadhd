# Pierwszy lokalny przebieg na prawdziwych eksportach

Sprawdzone 2026-09-30 względem CLI Looma z linii rozwojowej. Dotychczasowe
pomiary importerów dotyczą danych syntetycznych; prawdziwy ZIP jest nowym testem.
Dziewięć poniższych wywołań CLI przeszło na dwóch fixture’ach ZIP
(`docs/research/owner_export_guide_smoke.json`); nie sprawdza to Twoich danych.

## Pobierz oryginały

ChatGPT: profil → Settings → Data controls → Export data → Confirm export;
alternatywnie [Privacy Portal](https://privacy.openai.com/). Pobierz ZIP, gdy
otrzymasz powiadomienie. Instrukcja i dostępność zależą od typu konta:
[oficjalna pomoc OpenAI](https://help.openai.com/en/articles/7260999-exporting-your-chatgpt-history-and-data).

Claude: w wersji web lub Desktop Settings → Privacy → Export data, następnie
pobierz plik z otrzymanego linku. [Oficjalna instrukcja Claude](https://support.claude.com/en/articles/9450526-export-your-claude-data).
Obaj dostawcy podają ważność linku 24 godziny; szczegóły sprawdzono w tych
stronach 2026-09-30. Zachowaj wszystkie otrzymane części eksportu, bez przerabiania
oryginalnych nazw i treści. Zapis strony/MHT nie zastępuje pełnego eksportu.

## Import i raport kompletności

Poniższe polecenia wykonaj z katalogu repo. Podstaw lokalne ścieżki. Oryginały
pozostają poza Git; nowy katalog danych oddziela ten test od codziennej bazy.
Nie konfiguruj kluczy ani modeli w tym katalogu.

```sh
LOOM_BIN="$PWD/loom/build/dev/cli/loom"
RUN_DIR="$PWD/../real-export-first-run"
INPUT_DIR="/lokalna/sciezka/do/eksportow"
mkdir -p "$RUN_DIR"
"$LOOM_BIN" --data-dir "$RUN_DIR/data" import "$INPUT_DIR/chatgpt.zip" --audit > "$RUN_DIR/chatgpt-audit.txt"
"$LOOM_BIN" --data-dir "$RUN_DIR/data" import "$INPUT_DIR/claude.zip" --audit > "$RUN_DIR/claude-audit.txt"
```

Dla samego JSON użyj `import PLIK.json --export-mode on --audit`. Dla dodatkowych
ZIP-ów powtórz import każdej części. Nie używaj `--force` w pierwszym przebiegu.
Wersję raportu do analizy maszynowej zapisuje `--json import PLIK.zip`; w tym
trybie `--audit` nie drukuje osobnego podsumowania tekstowego.

Sprawdź: conversations/messages/branches/fork points, `leaves_preserved` względem
`json_leaves`, linked/unresolved attachment pointers, unknown/unreferenced members,
`partial`, errors/warnings i repairs. Równe liczniki nie dowodzą poprawnej
interpretacji wszystkich danych. Porównaj kilka znanych rozmów i odgałęzień
oraz obecność plików; nie oceniaj kompletności po samym tytule rozmowy.

## Katalog i dossier — osobny pomiar jakości

Najpierw podejrzyj katalog bez importu do warstwy wiedzy:

```sh
"$LOOM_BIN" --data-dir "$RUN_DIR/data" catalog scan --source "$INPUT_DIR"
"$LOOM_BIN" --data-dir "$RUN_DIR/data" catalog profile
"$LOOM_BIN" --data-dir "$RUN_DIR/data" catalog score
"$LOOM_BIN" --data-dir "$RUN_DIR/data" catalog list --label relevant --limit 100 --offset 0
"$LOOM_BIN" --data-dir "$RUN_DIR/data" catalog list --label irrelevant --limit 100 --offset 0
```

Zwiększaj `--offset`, żeby przejrzeć dalsze strony. `catalog show ID` pokazuje
powody i fragmenty; `catalog include ID --reason "..."` pozwala jawnie poprawić
pominięcie. Wynik katalogu może zgubić ważną rozmowę — istniejący wynik 31/45
dotyczy wyłącznie syntetycznego development.

Następnie opcjonalnie uruchom potok wiedzy bez modeli:

```sh
"$LOOM_BIN" --data-dir "$RUN_DIR/data" knowledge run --source "$INPUT_DIR" --out "$RUN_DIR/products" --llm off
"$LOOM_BIN" --data-dir "$RUN_DIR/data" artifacts list
```

To potok domyślnie selektywny. Do kontrolowanego porównania pełnego importu
użyj świeżego katalogu danych i konfiguracji `stage_params.catalog.import.mode`
ustawionej na `full` (opis: `docs/selfhost/v2/README.md`). Porównuj pokrycie
decyzji/zasad i źródła dossier, a nie samą liczbę produktów. Inferred/proposed/
absent pozostają hipotezami; wersje i dopasowania paradygmatów mogą być błędne.

## Zgłoszenie problemu

Zachowaj lokalnie: commit/build, polecenie bez sekretów, hash ZIP-a, rozmiar,
liczniki, kod błędu i identyfikator brakującego elementu. Nazwy członków, tytuły,
fragmenty, JSON, baza, raporty i produkty mogą zawierać prywatne informacje.
Do zgłoszenia wystarczy zanonimizowany minimalny fixture i wynik oczekiwany;
pełnych eksportów ani kluczy nie commituj. Ten przewodnik nie wymaga wysyłania
ZIP-a na serwer ani uruchomienia płatnego API.
