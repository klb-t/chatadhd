# B-AG-01 — AGEDS: receptura ASR przypięta do zadania, limity skanu przez każdy interfejs

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/AGEDS` @ `9c1d513b` (`main` 2026-10-09); potwierdzić na bieżącym `main` |
| Fala / priorytet | W1 / P1 |
| Ustalenia | EA-AGEDS-001, EA-AGEDS-002, EA-AGEDS-003 |
| Niezmienniki EITG | G2, G6, G10 |
| Rozmiar / model | M / Sonnet 5.5 high (Python serwer + Kotlin core/Android) |
| Zależności | brak; `pytest` i `anyio` do zainstalowania w środowisku (audyt) |
| Worktree | `t9-ag-01` |

## Zakres
Dowód w postępowaniu (R9) musi być odtwarzalny: transkrypcja bez przypiętej receptury ASR nie jest
cytowalna. Dlatego:
1. Receptura ASR (model, urządzenie, typ obliczeń, VAD, znaczniki czasu słów) jest profilem danych
   z hashem; `run` przypina pełny efektywny profil (EA-AGEDS-001).
2. Zadanie w kolejce przypina żądaną wersję receptury; dwa zgłoszenia tego samego artefaktu
   z dwoma hashami receptury to dwa jawne zadania; identyczny hash podlega wybranej polityce
   dedup; zmiana domyślnego nie zmienia zadania już przypiętego (EA-AGEDS-002).
3. Limity skanu (`maxFiles` itd.) docierają do silnika z każdego interfejsu (Android, HTTP, CLI)
   i do pokwitowania; przekroczenia nadal dają jawne częściowe pokrycie (EA-AGEDS-003).

## Pliki (lokatory audytu @ `9c1d513b`)
- `server/app/config.py:11–14`; `server/app/worker.py` (`FasterWhisperAdapter.describe/transcribe`,
  `run_transcribe`).
- `server/app/transcription.py:49–65` (`queue_transcription`); `server/app/jobs.py` (`claim_job`);
  `server/app/db.py` (`jobs.payload_json` — dziś nieużywany przez enqueue/worker dla receptury).
- `core/src/commonMain/kotlin/dev/klbt/ageds/core/SourceScanModels.kt:5–24`;
  `androidApp/src/main/java/dev/klbt/ageds/SourceScanner.kt`, `CorpusVm.kt` (`scanSources`);
  `server/app/main.py` (`api_source_scan`); `cli.main`.
- Sondy: `docs/reports/ecosystem-audit-2026-10-09/AGEDS/audit-tools/semantic_probes.py`
  (na gałęzi audytu) — 6 sond, w tym wymuszone opcje adaptera, rzeczywiste limity, dedup kolejki.

## Mechanizm docelowy (istniejący)
Ustawienia env już docierają do adaptera (model/device/compute — działa); `jobs.payload_json`
istnieje i jest pusty; typowane limity + serializowany snapshot istnieją (`SourceScanModels`).
Receptura = ten sam mechanizm env/Settings rozszerzony o VAD/timestamps + hash; przypięcie =
wypełnienie `payload_json`. Żadnego nowego magazynu; seed grup korpusu (EA-AGEDS-011) nie ruszać.

## Kroki
**Rozważenie.** Potwierdzić lokatory; uruchomić 6 sond i `pytest` (po instalacji zależności);
rekord decyzji: obecne wymuszone VAD/timestamps = preset `legacy` z hashem; istniejące puste
`payload_json` migrują jako `recipe: legacy/unknown` (ryzyko audytu: nie wymyślać historycznej
receptury).

**Implementacja.** (a) atrapa `faster-whisper` przechwytująca parametry; (b) profil receptury
w danych serwera (ten sam plik/katalog co `Settings`), test: dwa profile VAD on/off → różne
przechwycone parametry bez zmiany źródeł; `run` przypina profil i hash; (c) kolejka: hash
receptury w `payload_json`; test: dwa hashe → dwa zadania; ten sam hash → polityka
(`reuse` / `duplicate` jako ustawienie); zmiana defaultu nie zmienia przypiętego; (d) limity:
Android (`CorpusVm`) i HTTP (`api_source_scan`) przekazują profil do silnika; test: `maxFiles`
1 → 2 przez każdy interfejs trafia do silnika i pokwitowania.

**Optymalizacja.** Jedna funkcja `effective_recipe()` używana przez worker, enqueue i eksport
metadanych; pomiar: pola receptury w `run` przed/po.

**Iteracja.** Profil słowny (word timestamps) jako warunek selektora cytatów — jeśli profil bez
timings, selektor jawnie ograniczony (ryzyko audytu).

## Kryterium zamknięcia (EA-AGEDS-001/002/003)
- Trzy testy akceptacyjne z audytu PASS; 6 sond audytu uruchomione (te, które reprodukowały wadę,
  teraz pokazują naprawę — zapisać przed/po).
- `pytest` zielony; brak płatnych wywołań; Android nieuruchamiany — jawnie.
- Eksport metadanych (`export_metadata_package`) zawiera hash receptury per `run`, bez sekretów.

## Czego nie robić
- Nie osłabiać ochrony pamięci i limitów odczytu; polityka zasobów osobno od walidacji poprawności.
- Nie przypisywać historycznym `run` receptury, której nie było.
- Nie otwierać EA-AGEDS-011 (defaulty grup już dane).

## Co zapisać
Rekord decyzji `docs/reports/decisions/B-AG-01.jsonl` w repo AGEDS; hash presetu `legacy`;
wynik sond przed/po.
