# Python AST — odbiór parsera, odłożona integracja natywna

Data: 2026-11-01. Baza: `e2f6873c7ca24b41957b36b0127439b926068f41`.
Task źródłowy: `fleet-python-structure-20261101`; ten sam sole integrator
`01a192f5-2814-7e52-bf0c-32f3428151ed`. Kanon §0, rewizja 2.

**Przyjęty zakres: lokalny parser stdlib i narzędzia weryfikacji. Python AST
w grafie natywnym, HTTP, wybranym kontekście i UI nie jest odebrany.**
Nie zmieniono runtime, ABI, wspólnego `source_parts.cpp`, profili, helperów
pakietowych ani web. Stary pakiet pozostaje niezmieniony.

## Pochodzenie i zakres

Autor zakończył pracę na tej samej bazie. Świeże odczyty HEAD/status/diff,
checkpointu i wszystkich 14 plików zgadzały się przed i po przeglądzie
z audytem administratora. Obejrzano całe dziewięć nowych plików produktu
oraz osobny hook. Treści chronione były wyłączone z pathspecu; prywatnego
archiwum nie otwierano.

- Snapshot: `1793513632522785756`.
- Archive attestation: `c2a4ef2e661467bb8ba9b7ffc841b78f91884792082f04caeaae85a3d49c7f34`.
- Product patch: `69db3996747d6704f885c1c0fcd2d42ed4abbad1ad6f1a3ee85cadba7d978098` — `apply --check --whitespace=error-all` PASS, nałożony RAZ, 9/9 hashy zgodnych po nałożeniu.
- Shared hook: `1745986596b3593796d1627f2b2e341fc70ddf8f4580378c6cab081bbd5a87ed` — apply-check PASS, **nienałożony** ze względu na granicę zaufania.

Zmiany integratora ponad product patch: jawne `.get<std::string>()` w dwóch
porównaniach nagłówka C++ (naprawa rzeczywistego błędu kompilacji), dwa małe
testy granicy receipt oraz doprecyzowanie README. Parser/profile/fixtures
pozostały identyczne z odebranymi. Transportowe checkpointy, manifest autora,
patche i logi nie weszły do produktu.

## Wykonany dowód granicy zaufania

Własny focused executable `check_receipt.cpp` linkował istniejący własny
`libloom_core.a`; nie przebudowano biblioteki ani serwera. Własny test użył
wyłącznie nowego syntetycznego pliku PL, z BOM i CRLF, bez wykonania jego kodu.

| Przypadek | Checker bajtów C++ | Ponowne parsowanie pinned Python |
|---|---|---|
| Prawdziwy receipt | przyjęty | przyjęty |
| Spójnie podmieniona nazwa deklaracji | przyjęty | odmowa |
| Spójnie podmieniony rodzaj function → class | przyjęty | odmowa |
| Podmieniony hash parsera i przeliczone odwołania metody | przyjęty | odmowa |

Cztery przypadki diagnostyki spełniły asercje. Sprawdzono również dokładny
zakres funkcji w oryginalnych bajtach BOM/CRLF i zachowanie całego oryginału.
**To dowód niewystarczalności checkera, nie PASS bezpiecznego importu AST.**
Hash źródła, poprawne Unit/Observation ID i zgodność cytatu nie dowodzą,
że deklaracje lub metoda pochodzą z parsera. Inwariant musi być egzekwowany
przez zaufanego hosta; wywołanie `validate` przez klienta nie zabezpiecza API.

Pierwsza kompilacja ujawniła błąd `std::string != ordered_json`; powtórzenie
z rzeczywistą flagą repo `JSON_USE_IMPLICIT_CONVERSIONS=1` potwierdziło ten sam
błąd. Dopiero jawna ekstrakcja dwóch stringów go naprawiła. Zachowano logi
obu błędów i udanej kompilacji. Brak próby zmiany asercji w celu uzyskania PASS.

Dostarczony `verify_native` rzeczywiście uruchomiono raz na istniejącej
przyjętej bibliotece, we własnym nowym scratch. Upload przeszedł, materializacja
zakończyła się `not_found: text pack: no level "python_declaration"` (exit 1).
Nie osiągnięto dalszych asercji grafu, idempotencji ani reopen. **Native gate
nie przeszedł.** Nie przedstawiamy tego uruchomienia jako testu biblioteki
ze scalonym hookiem — hook celowo nie został nałożony.

Nie ponawiano 19 testów stdlib autora ani jego 5 kontroli składni. Ich PASS
pozostaje dowodem autora. Brak zmian wspólnego source_parts nie uzasadniał
ponowienia G1/DOCX/P0/accounts/backup. Nie uruchamiano HTTP, providera, GUI,
pełnej bramki repo ani testów innych autorów floty.

## Polecenia i powiązanie artefaktów

```sh
c++ -std=c++20 -O0 -DJSON_USE_IMPLICIT_CONVERSIONS=1 \
  -I. -Iloom/include -Iloom/third_party/nlohmann \
  loom/tests/python_ast_basic/check_receipt.cpp \
  .integration-release-20261031/build/libloom_core.a \
  .integration-release-20261031/build/libloom_miniz.a \
  .integration-release-20261031/build/libloom_pqc.a \
  -lsqlite3 -lssl -lcrypto -lpthread -ldl \
  -o .integration-fleet-python-20261101/check_receipt
PYTHONDONTWRITEBYTECODE=1 python3 -B -m loom.tests.python_ast_basic.verify_trust_boundary \
  --byte-checker .integration-fleet-python-20261101/check_receipt \
  --scratch-root .integration-fleet-python-20261101/native-scratch
```

Pełna komenda `verify_native`, wynik oraz sumy executable/źródeł/bibliotek
są w lokalnym `.integration-fleet-python-20261101/` (`binding.json`,
`provided-native-result.json`, `trust-boundary-result.json`, `manifest.json`).

- Istniejący core: `6da467854334437d784bfb6a45462afc4709f2f92b054301d808bce3609a03d0`.
- Istniejąca przyjęta lib: `2f582cf7594f95b1249e3ca8548cccc57864310268886fbd64a45f286cc0f928`.
- Niezmieniony pakiet `ccada5e5-release-docx-ui-20261101`: `b786b7e654e6660e6382cd7d8ac8434d9ef1517f2e0326ff9003a3abc372ac10`.
- Niezmieniony source_parts: `c830ac77c9aeaf46615839ecda86c11c75ba126c5d4c458f5c53ba0c36ef341a`.

Preflight: ponad 1 GiB wolnego miejsca, około 14,46 GB dostępnej pamięci,
brak widocznych aktywnych kompilatorów; poprzedni właściciel slotu miał świeży
receipt `completed`. Jego CHECKPOINT był niedostępny (EACCES), bez obchodzenia
ACL. Aktualne zlecenie przyznawało jedyny heavy slot integratorowi. Kompilowano
wyłącznie jeden mały executable, jednym procesem; nie kopiowano cache.

## Konkretny następny krok i ograniczenia

Zaufany host musi sam wytworzyć/zweryfikować receipt z niezmiennych bajtów
bloba przez pinned parser. Istniejące `docx_basic/adapter.cpp`, `package.cpp`
i `docx_projection.cpp` dostarczają wzorzec: jawny interpreter/helper,
bez shella i odziedziczonych hooków Python, ograniczony stdin/stdout, watchdog,
cleanup i sprawdzenie zmiany helpera. Obecny adapter AST nie zawiera takiego
transportu ani dostarczenia helpera w pakiecie. Nie należy zastępować tego
flagą zaufania lub ścieżką wykonywalną przyjmowaną z żądania.

Po dopięciu tego mechanizmu: osobno shared hook, przyrostowy build, pełny
provided native harness, właściwe sentinele G1/DOCX, bezpieczne odmowy
sfałszowanych receipt/hash/source i rzeczywisty selected context po reopen.
Parser rozpoznaje składnię, nie semantykę wykonania; limity liczby węzłów
po `ast.parse` nie stanowią sandboxa pamięci. Brak odbioru domyślnego pakietu
AST, UI, hierarchii wszystkich formatów i wdrożenia. Pozostała flota/tail
nie została podjęta w tej turze.
