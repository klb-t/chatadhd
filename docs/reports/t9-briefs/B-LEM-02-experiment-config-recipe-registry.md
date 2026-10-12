# B-LEM-02 — LEM-Workbench: ExperimentConfig steruje wykonaniem, receptura i transport jako profil, rejestr modeli kompletny

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/LEM-Workbench` @ `1755e1df`; potwierdzić na bieżącym `main` |
| Fala / priorytet | W2 / P2 |
| Ustalenia | LEM-001, LEM-002, LEM-004 |
| Niezmienniki EITG | G2, G6, G10 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | B-LEM-01 (hash żądania/konfiguracji w wyniku) |
| Worktree | `t9-lem-02` |

## Zakres
- Encja `ExperimentConfig` (29 pól) staje się `preset`/`method_version` z listą pól **obsługiwanych**
  i statusem zdolności per pole; obsługiwane pola docierają do runnera i transportu; nieobsługiwane
  są odrzucane/oznaczane, nigdy cicho ignorowane; README mówi prawdę o tym, co konfigurowalne
  (LEM-001).
- Receptura pomiaru (teksty embedding, wymiar 768, instrukcja generacji, `temperature=0`, pierwszy
  kandydat) i profil transportu (`BASE_URL`, timeouty) jako dane; implementacja Gemini jako adapter
  mechanizmu (LEM-002).
- Rejestr modeli: paginacja (`nextPageToken`) do końca; kompletność rejestru oddzielona od filtra
  i sortowania widoku; nowy model nie jest wybierany automatycznie (LEM-004).

## Pliki
- `app/src/main/java/com/example/data/model/ExperimentConfig.kt:9–41`;
  `viewmodel/ResearchViewModel.kt:70–95`; `data/repository/ResearchRepository.kt:16–18`;
  `api/ResearchAgent.kt:39–51`.
- `app/src/main/java/com/example/research/ExperimentRunner.kt:13–20`, `:71–121`;
  `api/ResearchAgent.kt:23–36`; `api/GeminiApiService.kt:94–113` (RetrofitClient).
- `api/ResearchAgent.kt:56–58`; `api/GeminiApiService.kt:67–77`; `ui/screens/ModelsScreen.kt:26–31`,
  `:106–107`; `viewmodel/ResearchViewModel.kt` (`refreshModels`).

## Mechanizm docelowy (istniejący)
`ConfigDao`/`ResearchRepository` (zapis/odczyt istnieją, brak konsumenta); runner przyjmujący
dziś `modelName` i wymiar — rozszerzyć o preset; dane receptury w `assets/` (brak katalogu danych
w repo — utworzyć minimalny, zgodnie z konwencją Androida); hash z B-LEM-01.

## Kroki
**Rozważenie.** Potwierdzić lokatory; przejrzeć 29 pól i podzielić na obsługiwane dziś / możliwe /
nieobsługiwane (status per pole jako dane); rekord decyzji: obecna receptura = preset `legacy`
z hashem obejmującym teksty i wszystkie parametry (ryzyko audytu).

**Implementacja.** (a) fake transport przechwytujący żądania; (b) test LEM-001: dwa presety
różniące się wymiarem i `taskType` → różne żądania; wynik zawiera ID/hash presetu; pole
nieobsługiwane odrzucone/oznaczone; (c) test LEM-002: wariant promptu, wymiaru i timeoutu z danych
bez zmiany kodu; brak konfiguracji → jawny błąd albo pack wygenerowany; (d) test LEM-004:
dwustronicowy fake rejestr → obie strony pobrane, snapshot zachowany; pusta/nieznana zdolność
nie znika bez informacji; filtr widoku nie wpływa na kompletność.

**Optymalizacja.** Jeden profil (receptura + transport) używany przez embedding i generację;
pomiar: liczba literałów w `ExperimentRunner`/`ResearchAgent` przed/po.

**Iteracja.** Presety z badań ChatADHD (wątek 7) jako kolejne wersje — tylko gdy właściciel
zdecyduje o mostach M4 (brak w tym cyklu).

## Kryterium zamknięcia (LEM-001, LEM-002, LEM-004)
- Trzy testy akceptacyjne z audytu PASS na hoście; stare rekordy konfiguracji oznaczone
  „niewykonane”, bez retrospektywnych pomiarów (ryzyko audytu).
- README doprecyzowane; build JVM zielony; brak płatnych wywołań.

## Czego nie robić
- Nie wybierać automatycznie nowego modelu po rozszerzeniu rejestru.
- Nie udawać treningu: pola treningowe mają status `unsupported`, dopóki runner ich nie wykonuje.

## Co zapisać
Rekord decyzji; tabela 29 pól ze statusem; hash presetu `legacy`.
