# B-LEM-03 — LEM-Workbench: UI, motyw i twierdzenia historyczne jako dane; opis grafowy konfiguracji, modeli, metod i wyników

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/LEM-Workbench` @ `1755e1df`; potwierdzić na bieżącym `main` |
| Fala / priorytet | W3 / P3 |
| Ustalenia | LEM-006, LEM-007 |
| Niezmienniki EITG | G1, G5, G6, G10 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | B-LEM-01, B-LEM-02 (obiekty do opisania istnieją) |
| Worktree | `t9-lem-03` |

## Zakres
- Teksty ekranów, paleta motywu i „twierdzenia historyczne” (checkpointy badawcze pokazywane
  w UI) w danych: katalog komunikatów, profil motywu, odniesienie do źródła i daty checkpointu
  z pochodzeniem; parametry motywu faktycznie podłączone do renderera albo pozorna obsługa
  usunięta ze stabilnego API (LEM-006).
- Opis grafowy istniejących obiektów (config, profil, metoda, model, run, dowód, dostawca,
  trigger, kolejka) z odniesieniami do zewnętrznych magazynów artefaktów i sekretów, przy
  zachowaniu Room; dopiero po kontrakcie podłączenie resolvera danych i UI (LEM-007).

## Pliki
- `app/src/main/java/com/example/ui/theme/Theme.kt:30–36`; `ui/screens/DashboardScreen.kt:74–94`;
  `ui/screens/ExperimentsScreen.kt:65–79`; `ui/navigation/Screen.kt:3–7`.
- `data/local/AppDatabase.kt:11–15`; `data/model/ExperimentResult.kt:9–36`; `data/model/LedgerEntry.kt:9–18`;
  `api/ApiKeyStore.kt:16–38`; `viewmodel/ResearchViewModel.kt`.

## Mechanizm docelowy (istniejący)
Zasoby Androida (`res/values` z językami) jako katalog komunikatów — konwencja platformy, nie
nowy magazyn; profil motywu w `assets/`; opis grafowy jako plik danych w repo (`assets/graph/
self.json` albo równoważny) z kopertą `loom.ecosystem_ref/1`, czytelny bez Loom; sekrety wyłącznie
jako odniesienia do `ApiKeyStore`.

## Kroki
**Rozważenie.** Potwierdzić lokatory; wyliczyć obiekty do opisania z `flow-map.json` audytu dla
LEM; rekord decyzji: obecny wygląd = domyślny preset; istniejącym twierdzeniom nie nadawać
fikcyjnego źródła akceptacji (ryzyko audytu) — brak źródła = `source: unknown` jawnie.

**Implementacja.** (a) test LEM-006: zmiana danych profilu zmienia motyw i etykiety; status
maszynowy renderowany przez katalog; klik checkpointu prowadzi do źródła i daty, nie tekstu
w kodzie; (b) opis grafowy: test LEM-007: od wyniku do config, modelu, promptu i surowego dowodu;
zmiana warstwy presetu zmienia żądanie i ma wyjaśnienie; trwałe wykluczenie nie wraca po
aktualizacji packa.

**Optymalizacja.** Jeden katalog komunikatów dla wszystkich ekranów; pomiar: literały UI przed/po.

**Iteracja.** Wpis Room/`SharedPreferences`/plików do `storages.pack` repo (B-ECO-02); opis
grafowy jako wejście mostu M4 (brak w tym cyklu).

## Kryterium zamknięcia (LEM-006, LEM-007)
- Dwa testy akceptacyjne z audytu PASS na hoście; build JVM zielony; Android nieuruchamiany — jawnie.
- Opis grafowy nie zawiera sekretów ani ontologii ChatADHD narzuconej na semantykę badawczą
  (ryzyko audytu): typy mapowane jawnie, nieznane pola zachowane.

## Czego nie robić
- Nie nadawać istniejącym twierdzeniom źródła, którego nie mają.
- Nie zastępować Room nowym magazynem; opis grafowy jest indeksem nad Room (EITG §0).

## Co zapisać
Rekord decyzji; liczba obiektów opisanych / nieopisanych; literały UI przed/po.
