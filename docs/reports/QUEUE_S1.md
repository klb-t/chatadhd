# Kolejka S1 — stan 2026-10-21 02:25 UTC

Po co ten plik: zasada 1 (trwałe checkpointy) — kto przejmie pracę po przerwie sesji, ma tu
wszystko, czego kontekst nie przeniesie. Aktualizowany przy każdym zakończeniu pakietu.

## W toku (3 agenci naraz — tyle mieści budżet okna, `LIMITS_LEDGER.md`)
- **M1-W0, pakiety** — gałąź `claude/m1-w0`, worktree `/home/user/wt/m1d0`, skrypt
  `tools/workflows/m1-w0-packages.js`. P1 (menu, Wstecz), P2 (lista nawigatora), P3 (karta węzła),
  P4 (mapa) gotowe; P5 (ustawienia po ludzku) w toku; dalej P6 (karta roli), P7 (e2e, zrzuty,
  raport, testid-y dla S2), potem recenzje po kolei. **Po zakończeniu:** scalić `main`, bramka
  `gate.sh /home/user/wt/m1d0 m1w0` (≈ 40 min), przywrócić `loom/web/e2e/screenshots`, FF `main`,
  powiedzieć S2, że `docs/reports/M1_W0_TESTIDS.md` ma blok `gate0`, zbudować wydanie
  (`package.yml`), dać właścicielowi link do APK + `docs/reports/M1_CHECKPOINT_0.md`.
- **Poprawka przerywanego zrywania logowania** — gałąź `claude/fix-login-drop`, worktree
  `/home/user/wt/extract`, 2 commity; po zakończeniu bramka i FF `main` **przed** bramką W0
  (inaczej bramki bywają losowo czerwone).
## Zrobione od ostatniej wersji
- **X-Port Android, pakiety A i B** (gałąź `claude/xport-android`, head `db469050`, build `xport-apk.yml`
  przebieg 39532408090 = success, artefakt `xport-apk` 12315942830): pięć poprawek UX, polskie etykiety,
  przepisy „zostaw tylko znaczniki czasu” (makro) i „katalog rozmowy” (profil eksportu), wybór po danych
  kodowanych w zakładce Wybór, `main` scalone do gałęzi. Niesprawdzone na emulatorze — S2 dostał numer
  przebiegu i ma opisać układ. **Do zrobienia:** wynik S2; scalenie `claude/xport-android` do `main`
  przez bramkę (testy `tools.extract`) — dopiero po wyniku S2.
- **P39 projekt gotowy i na `main`** (`bbdaca7f`): `docs/architecture/P39_INTERAKCJA_MULTIMODALNA_2026-10-21.md`
  (specyfikacja po weryfikacji handoffu, trzy tory T/D/E, bramki T0–E2, 14 ryzyk, hipotezy H1–H9) i
  `docs/reports/P39_WORK_ITEMS.json` (26 pozycji z zależnościami). Pierwsza dotykalna rzecz: **T0** (cztery
  pozycje: T-A1 → T-K1 → T-K2 → T-K3, „Dokończyć przerwaną odpowiedź?”), ale dopiero po M1-W0. Następne
  pozycje P39 startują po scaleniu W0, nie wcześniej.

## Czeka na wolny worktree z buildem jądra (dysk 7 GB: drugi build jądra się nie mieści)
Kolejność po zakończeniu poprawki logowania i jej bramce: worktree `/home/user/wt/extract` (build 2,1 GB)
przestawić na nowy `main` i użyć go kolejno do: R38, potem M1-D2 → M1-K1.
1. **R38 — dwie uwagi S2** (pliki S1): (1) wiadomość wyniku `agent.run` niesie cały JSON pętli
   w treści obok bloku Akceptuj/Odrzuć — `body_of` ma pokazać tekst/propozycje; (2) po „Akceptuj”
   końcowa odpowiedź agenta (tekst wznowionego przebiegu) nigdzie nie trafia do rozmowy. Poprawka
   po stronie web już u S2 (`50239c94` na `claude/s2-parallel`).
2. Zadanie #24 `memory.attach_folder` (po retargetowaniu R39 na graf), #19 P37 „jeden silnik”,
   M1: D2, K1, W1, K2, W2, W3, A1 (S2), T1, DOC; etap replikacji dostawców po M1.

## Czeka na właściciela (tak/nie)
- Hak automatycznego wypychania gałęzi po commicie: `tools/install_checkpoint_hook.sh` (nieaktywny).
- Bezpośrednie połączenie z Google/Lyria.
- Publikacja GitHub Release.
- P39 §9: (1) wgląd w Twoje eksporty lokalnie dla E-A1 i T-R2 (do repozytorium tylko cytaty do Twojej
  akceptacji i liczby zbiorcze) — tak/nie; (2) trzecia sesja S3 dla torów D/E — tak/nie; (3) plik
  `face_embedding_research_master_prompt_2026-10-19.md` do `docs/architecture/`; (4) model estymatora
  `turn_policy_estimator_model`, jeśli chcesz trzecią politykę (koszt pokazany przed wywołaniem).

## Odnośniki
Zasady kontroli marnotrawstwa: `docs/reports/INDEX.md` („S1 — kontrola marnotrawstwa”), dziennik
okna: `docs/reports/LIMITS_LEDGER.md`, skrypty: `tools/workflows/`, spec M1:
`docs/architecture/M1_USTAWIENIA_JAKO_GRAF_2026-10-16.md`.
