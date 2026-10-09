# PASS4 — zlecenie i granice, 2026-10-09

Bieżące zlecenie właściciela: niezależny odbiór B/C/D/E, testy niezmienności znaczenia, granic modułów, otwartości nowych struktur i perspektyw oraz dalsze ryzykowne ścieżki pięciu aktywnych repo. A pisze tylko w docs/reports/ecosystem-audit-2026-10-09/pass4/ i tools/ecosystem-audit-2026-10-09/pass4/. Żadnych zmian produktu, wspólnych schematów, main, gałęzi B/C/D/E ani dowodów pass1–3. Nie wolno odpalać płatnych modeli/CI; syntetyczne fixtures i przechwycone transporty.

## Dokładny cytat korekty właściciela

> Excel był przypadkowym przykładem, nie priorytetem produktowym.
> Nie zamieniaj tego etapu w certyfikację formatów Office.
>
> Wymaganie ogólne:
> dołączenie źródła oraz korzystanie z jego struktury przez graf bez
> obowiązkowego kopiowania całej treści i materializowania wszystkich węzłów.
> Referencja + parsowanie/projekcja na żądanie jest pełnoprawną ścieżką.
> Cache, indeks, osadzenie i snapshot są niezależnymi możliwościami.
>
> Nieznane źródło nadal można dołączyć. W razie potrzeby uruchamia się
> rozpoznawanie, dobór/złożenie adapterów, analizę struktury lub discovery.
> Nie oznacza to obowiązku bezbłędnego zrozumienia każdego pliku.
> Nieznane, częściowe, niedostępne i puste pozostają odróżnialne.
>
> Perspektywy są niezależne od konkretnego formatu.
> Widoczne w UI, wybrane do analizy i dostępne zgodnie z uprawnieniami
> to trzy różne zakresy.

## Warunki audytu (streszczenie pozostałego zlecenia)

Przeczytaj pass2 REPORT/RESUME i pass3-resources REPORT/RESUME/CONTRACT/backlog/coverage-index; lokalne AGENTS/CLAUDE i aktualne wymagania R15/R20/R21/R33/R39–42. Główny REPORT jest historyczny. Poprzedni head e291098, payloadpass3 1c77e16. Root jawnie przypina B–E i publikuje moduły. Mianownik bazowy267/1027 zakresy,760 bez zakresów. Odśwież denominator tylko przy zmianie main. Standalone loom jest historyczny.

Nie powtarzaj udanych testów bez zmiany kodu/warunków/nowej hipotezy. Odbiór: naprawione/częściowo/nadal wadliwe/regresja/niezweryfikowane + SHA + actual consumer/test. Szczególnie CH-RES-N001/N002, A3-WEB001, A3-DISC001/002, A3-IMP-CH001/003/004/006 oraz C test_credential_handoff_v2: katalog testów naprawiony bez usunięcia odrzucania niedozwolonych lokalizacji.

Metamorficzne próby: kolejność kluczy, równoważna serializacja, próg parsera, import/ref, eager/lazy, cold/warmcache, file/container, relocation pinnedversion, interrupted read/index resume. Oddziel składnię/strukturę/domenę. Kolejność list/wiadomości nie jest obojętna. 1 vs1.0 zależy od kontraktu. Oracle ze źródła/wymagania; nie z aktualnego wyniku produktu. Minimalizuj błąd, jedno ID na przyczynę.

Granice: D→istniejący graph/store; graphprofile→Bresolver→consumer; C→MethodRegistry→executoravailability; projekcja/selector→Eheadless/UI. Można izolowany worktree integracji tylko lokalnie; zapisz dokładny skład/konflikty, bez publikacji product merge. Mock osobno; brak publicznego kontraktu BLOCKED. Nie tworzyć repliki w harnessie i nie nazywać jej integracją. Sprawdź czy D/E nie tworzą drugiego store/resolvera/workflow/parsera tylko rendererowego.

Otwartość: nowy układ pól, dwie interpretacje, częściowy dokument, unknownfields, brokenref, source/adapterversion. Rozpoznana struktura dostępna bez specjalnego renderera; nowe mapping/adapter bez zmiany core. Declaration≠implementation≠evidence≠permission; executorunavailable poprawne jeśli rzeczywiściebrak. E: selectbezrenderu, identityacrossprojection, detaildoesnotmutatecanonical, visibility≠permission/context, historicalfocus, partialindexnotcomplete.

Poza odbiorem B–E domknij kolejne wysokiego ryzyka ścieżki Chat/WD/CKP/AGEDS/LEM: lostparams, implicitstrategy/fallback, migrations/overwrite/exclusionresurrection, failedworkmarkedcomplete, provenance, privacy/cost/auth. Zakresy funkcji, rzeczywiste consumers, untracedcalls. Nie masowy grep/literalhunt.

Po zamkniętym module: raport/findings/receipts/test-index/coverage/handoff i checkpoint push przez root. ReproductionPASS≠productPASS. Wszystkie testy przyjmującheckout/SHA. Brakbranch/testenv nie kończy reszty pakietu; nie idlefetch, nie czekać na inne sesje. Komunikacja: publikacja pliku nie jest wysłaniem wiadomości B–E.
