# B-LEM-01 — LEM-Workbench: nic nie ginie — migracja Room, surowe dowody, spójne poświadczenia

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/LEM-Workbench` @ `1755e1df` (`main` 2026-10-09); potwierdzić na bieżącym `main` |
| Fala / priorytet | W1 / P0 |
| Ustalenia | LEM-005, LEM-003, A4-LEM-001, A4-LEM-002 |
| Niezmienniki EITG | G2, G3, G6 |
| Rozmiar / model | S / Sonnet 5.5 high (Kotlin); testy JVM na hoście z fake transportem; Gradle jeśli dostępny |
| Zależności | brak |
| Worktree | `t9-lem-01` |

## Zakres
1. Brak migracji Room **nie** kasuje bazy: jawna migracja albo bezpieczny błąd (odmowa otwarcia
   nieznanej wersji bez modyfikacji znacznika); reset jako osobna świadoma operacja (LEM-005).
2. Każdy krok eksperymentu utrwala żądanie i wyjście **przed** agregacją; przy błędzie drugiego
   kroku pierwszy i jego wynik zostają, drugi jest `failed`, koszt nie jest zmyślany; utrata
   przechowywania jest jawną polityką, nie domyślnym zachowaniem (LEM-003).
3. Dwa żądania jednego eksperymentu używają tych samych poświadczeń: tożsamość operacji przypięta
   do poświadczeń w chwili startu; zmiana po pierwszym żądaniu → albo dokończenie z pierwotnymi,
   albo jawne zatrzymanie z zapisem (A4-LEM-001). Spóźniona odpowiedź starego rejestru nie
   odtwarza stanu po `clear`/rekey (A4-LEM-002).

## Pliki (lokatory audytu @ `1755e1df`)
- `app/src/main/java/com/example/data/local/AppDatabase.kt:11–29` (`getDatabase`, Room DAO).
- `app/src/main/java/com/example/research/ExperimentRunner.kt:22–58`, `:77–121`, `:134–170`;
  `app/src/main/java/com/example/api/ResearchAgent.kt:28–36` (dekoder odpowiedzi).
- `app/src/main/java/com/example/api/ApiKeyStore.kt:16–38`; `viewmodel/ResearchViewModel.kt`
  (`refreshModels`, instrument); sondy hosta pass4 (`pass4/ageds-lem/REPORT.md`).

## Mechanizm docelowy (istniejący)
Room z jawną listą migracji (zamiast `fallbackToDestructiveMigration`); encje `ExperimentResult`/
`LedgerEntry` rozszerzone o odniesienie do surowego artefaktu i hashe żądania/konfiguracji;
`ApiKeyStore` jako jedyne źródło poświadczeń z wersją (sekret nigdy w encji — EITG D-G2).
Ręczne uruchomienie i jawna porażka (LEM-008) to mechanizm — zachować.

## Kroki
**Rozważenie.** Potwierdzić lokatory; sprawdzić, czy `exportSchema` jest włączone (w audycie: brak
— to utrudnia fixture starych schematów; włączyć od teraz, stare wersje odtworzyć ręcznie jako
fixture v1); rekord decyzji: polityka retencji surowych = `keep` domyślnie, `drop` jako jawne
ustawienie.

**Implementacja.** (a) fixture bazy v1 z rekordami → otwarcie v2: dane zachowane; wersja nieznana
(999): odmowa bez modyfikacji; jawny reset testowany osobno; (b) utrwalenie żądania/wyjścia per
krok z hashem i odniesieniem do artefaktu; test: transport testowy zwraca znane wektory; wynik
wskazuje hash/artefakt i dokładne żądanie; drugi embedding rzuca → pierwszy krok zostaje, drugi
`failed`, koszt `unknown`; (c) poświadczenia: snapshot w chwili startu operacji; test z pass4:
podmiana po pierwszym przechwyceniu → dwa żądania A albo jawne zatrzymanie z zapisem, nigdy
mieszanka; (d) spóźniona odpowiedź: epoch/cursor na rejestrze; test: opóźnij A, `clear` → brak
starych modeli i statusu live-success; opóźnij A, zapisz B → stara odpowiedź nie staje się
rejestrem B.

**Optymalizacja.** Jedna ścieżka zapisu artefaktów używana przez embedding i generację.

**Iteracja.** Polityka retencji jako preset (W3, B-LEM-03).

## Kryterium zamknięcia (LEM-005, LEM-003, A4-LEM-001/002)
- Cztery testy akceptacyjne z audytu/pass4 PASS na hoście; stare raw nieodtwarzalne są oznaczone
  `missing`, nie rekonstruowane (ryzyko audytu).
- Build JVM zielony; Android nieuruchamiany — jawnie; brak płatnych wywołań.

## Czego nie robić
- Nie rekonstruować starych surowych danych z interpretacji.
- Nie mierzyć realnych kont ani kosztów (pass4 tego nie robił i tu też nie).
- Nie zapisywać sekretów w encjach ani eksportach.

## Co zapisać
Rekord decyzji `docs/reports/decisions/B-LEM-01.jsonl` w repo LEM; wersja schematu przed/po;
wynik czterech testów.
