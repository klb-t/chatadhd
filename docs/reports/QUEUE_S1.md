# Kolejka S1 — stan 2026-10-21 02:00 UTC

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
- **P39 projekt** (multimodalny dupleks / JEV / głos / awatar) — gałąź `claude/p39-design`, worktree
  `/home/user/wt/p39`, etap syntezy; wynik: `docs/architecture/P39_INTERAKCJA_MULTIMODALNA_2026-10-21.md`
  + `docs/reports/P39_WORK_ITEMS.json`; po zakończeniu scalić dokumenty na `main`.

## Czeka na miejsce w oknie (start, gdy skończy się któryś z trzech)
1. **X-Port Android — pakiet poprawek** (gałąź `claude/xport-android`, worktree `/home/user/wt/xport`,
   build `xport-apk.yml` na push, numer przebiegu do S2): (a) odmiana „1 rozmowa / 3 rozmowy / 5 rozmów”;
   (b) „0 KB” → bajty lub „<1 KB”; (c) w Przeglądarce bez wewnętrznych prefiksów czasu i bez
   angielskich „sources | markdown | archive | format=”; (d) polskie nazwy pól w Operacjach
   (of, all, mime, min_size, missing); (e) zakładka Eksport widoczna bez zgadywania (np. zawijany
   pasek albo strzałka); (f) gotowe przepisy „zostaw w metadanych tylko znaczniki czasu” i
   „wyciągnij spod zagnieżdżenia do `/[timestamp]-[tytuł]`”; (g) silnik: wykrywanie danych
   kodowanych/hex jako kryterium zaznaczania. Źródło: S2 na emulatorze (przebieg 39524948848) i słowa
   właściciela.
2. **R38 — dwie uwagi S2** (pliki S1): (1) wiadomość wyniku `agent.run` niesie cały JSON pętli
   w treści obok bloku Akceptuj/Odrzuć — `body_of` ma pokazać tekst/propozycje; (2) po „Akceptuj”
   końcowa odpowiedź agenta (tekst wznowionego przebiegu) nigdzie nie trafia do rozmowy. Poprawka
   po stronie web już u S2 (`50239c94` na `claude/s2-parallel`).
3. Zadanie #24 `memory.attach_folder` (po retargetowaniu R39 na graf), #19 P37 „jeden silnik”,
   M1: D2, K1, W1, K2, W2, W3, A1 (S2), T1, DOC; etap replikacji dostawców po M1.

## Czeka na właściciela (tak/nie)
- Hak automatycznego wypychania gałęzi po commicie: `tools/install_checkpoint_hook.sh` (nieaktywny).
- Bezpośrednie połączenie z Google/Lyria.
- Publikacja GitHub Release.

## Odnośniki
Zasady kontroli marnotrawstwa: `docs/reports/INDEX.md` („S1 — kontrola marnotrawstwa”), dziennik
okna: `docs/reports/LIMITS_LEDGER.md`, skrypty: `tools/workflows/`, spec M1:
`docs/architecture/M1_USTAWIENIA_JAKO_GRAF_2026-10-16.md`.
