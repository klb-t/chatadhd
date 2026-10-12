# B-CKP-01 — IO Matrix: najpierw hipoteza spóźnionej odpowiedzi, potem receptury zadań i katalog dostawców

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/Custom-Keyboard-Pro` @ `192f820f` (`main` 2026-10-09); potwierdzić na bieżącym `main` |
| Fala / priorytet | W1 / P0 (hipoteza CKP-A-012), P1 (CKP-A-001/002) |
| Ustalenia | CKP-A-012 (hipoteza, rozszerzona w pass4 o opóźnione wklejanie i fallback do zmienionego celu), CKP-A-001, CKP-A-002 |
| Niezmienniki EITG | G6, G10; dla CKP-A-012: integralność tekstu użytkownika (I1 w sensie „bajty użytkownika niezmienione bez jego działania”) |
| Rozmiar / model | M / Sonnet 5.5 high (Kotlin); kompilator Kotlin/JVM na hoście wymagany dla testów — w audycie zablokowany, sprawdzić |
| Zależności | brak |
| Worktree | `t9-ckp-01` |

## Zakres
1. **Test hipotezy CKP-A-012** (zanim cokolwiek się zmieni): fake `AiClient` z opóźnieniem;
   żądanie w polu A, zmiana fokusu do B, spóźniona odpowiedź → B musi pozostać bez zmian; zmiana
   treści A w trakcie → wynik do podglądu albo ponowne zatwierdzenie; stan `sensitive` po wysłaniu
   blokuje późniejszą automatyczną edycję. Jeśli hipoteza potwierdzona → naprawa w tym briefie
   (wiązanie wyniku z identyfikatorem pola i wersją treści); jeśli nie → status `rejected` z dowodem.
2. Wbudowane zadania AI jako wersjonowany `TaskRecipe` + `PromptTemplate` w danych; jeden
   resolver warstw default/user z tombstone wykluczenia podłączony do `byId` i UI; personalne
   zadanie o tym samym ID przesłania wbudowane; błędny JSON → jawny błąd (CKP-A-001).
3. Katalog dostawców: preset zgodności LEGACY jako dane albo pack osadzony generowany; jawna
   obsługa uszkodzonego katalogu; wykluczenia docierają do wszystkich fallbacków; wiązanie klucza
   do dostawcy zachowane (CKP-A-002).

## Pliki (lokatory audytu @ `192f820f`)
- CKP-A-012: `app/src/main/java/com/example/ime/CustomKeyboardIme.kt:2048–2082` (`runAiTask`),
  `:2129–2169` (`pasteOrConvert`, `pasteNativeClip`); `app/src/main/java/com/example/ime/EditorController.kt:190–247`
  (`replaceSelectionOrAll`, commit).
- CKP-A-001: `app/src/main/java/com/example/core/ai/AiTasks.kt:28–177` (`all/byId`,
  `parseCustom`, `writeCustom`); `CustomKeyboardIme.kt` (`runAiTask`); `app/src/main/java/com/example/ui/kb/Panels.kt`
  (`AiPanel`).
- CKP-A-002: `app/src/main/java/com/example/core/discovery/ProviderCatalog.kt:246–285`
  (`parseList`, `all`, LEGACY); `app/src/main/java/com/example/core/ai/AiClient.kt`
  (`AiConfig.from`, `effectiveBaseUrl`); dane: `assets/providers.json` (bootstrap — nazwa pliku
  to kategoria 5 R42, lista LEGACY nie).
- Istniejące dane referencyjne: `profiles/*.json` (CKP-A-011 — czy mają konsumenta, rozstrzyga B-CKP-03).

## Mechanizm docelowy (istniejący)
Kaskada ustawień z pochodzeniem lokalnych nadpisań (CKP-A-010: działa) jako resolver warstw;
`assets/providers.json` jako katalog; dane zadań w tym samym katalogu zasobów. Żadnego nowego
magazynu; sekrety pozostają w keystore urządzenia (odniesienie, EITG D-G2).

## Kroki
**Rozważenie.** Potwierdzić lokatory; sprawdzić dostępność kompilatora Kotlin na hoście
(w audycie brak — bez niego testy JVM nie są możliwe i brief kończy się na raporcie
„niezweryfikowane” z dokładną instrukcją); rekord decyzji z wynikiem testu hipotezy **przed**
jakąkolwiek zmianą.

**Implementacja.** (a) test hipotezy (trzy przypadki z audytu) z fake opóźnionym klientem;
naprawa, jeśli FAIL: wynik niesie `{fieldId, contentVersion}` i jest odrzucany/kierowany do
podglądu przy niezgodności; (b) zadania wbudowane → `assets/ai_tasks.json` (ten sam katalog
zasobów) jako preset z obecnymi promptami bajt w bajt; resolver warstw; test: custom `id=fix`
o innym promptcie → ciało żądania używa wybranej warstwy; wykluczenie `fix` + aktualizacja packa →
brak przywrócenia; błędny JSON → błąd, nie pusty sukces; (c) LEGACY dostawców → preset zgodności
w danych; test: uszkodzony katalog → bez jawnego presetu nie powstaje konfiguracja do sieci;
zmiana endpointu/modelu w danych zmienia żądanie bez Kotlina; wykluczony dostawca nie wraca.

**Optymalizacja.** Jedna ścieżka rozwiązywania ID (zadania, dostawcy) z tym samym resolverem
warstw; usunięcie duplikatu fallbacku modelu między `ProviderCatalog` i `AiProviders`.

**Iteracja.** Pozostałe listy w kodzie (agent liczy `grep` na adresy/modele) tym samym wzorem.

## Kryterium zamknięcia (CKP-A-012, CKP-A-001, CKP-A-002)
- Trzy przypadki hipotezy PASS (albo udokumentowane `rejected` przed zmianą).
- Trzy testy akceptacyjne CKP-A-001 i trzy CKP-A-002 z audytu PASS.
- `validate_profiles.py` PASS; testy JVM zielone (albo jawnie „kompilator niedostępny”).
- Istniejące ID zadań i kolejność UI zachowane (ryzyko audytu).

## Czego nie robić
- Nie usuwać wsparcia legacy dostawców ani wiązania klucza do dostawcy; stare instalacje dostają
  jawny preset zgodności.
- Nie uznawać nazw `HTTP/HTTPS` za zakazane dane domenowe (kategoria 2).
- Nie zaliczać testu hosta za test urządzenia.

## Co zapisać
Rekord decyzji z wynikiem hipotezy; hashe presetów `legacy`; status kompilatora.
