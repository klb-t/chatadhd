# Jev: pełne intencje, oddzielne bramki

Etap kontynuacji; poprzednie freeze, requesty i wyniki są niezmienione.
`jev-plan-v2.json` wiąże checkpoint SHA256
`137c49ab13681d036b955039a580aff3cb86bc342b62981c484abcfe9363c28e`,
poprzedni kod `69880859802267f1b38b82eeaecd6fdc5522a47a` oraz własny freeze
Jev `18d5bcda5fea674b092ffa5e6176d3cb2001d629d766d0ea80baa35fe12d2c2d`.

## Co ograniczał stary profil

`jev_live_pilot.validate_body` ma dwa niezależne warunki połączone tym samym
kodem błędu `jev_body_exceeds_reservation_allowance`:

| Warunek | Znaczenie |
|---|---|
| kanoniczny JSON ≤ 16 384 bajty | historyczny limit instrumentu |
| (bajty + 1024) × 0,000000042 USD ≤ 0,001 USD | historyczny konserwatywny model kosztu i jednostkowa rezerwacja |

Limit jest sprawdzany przez przygotowanie starego pilota, jego walidację
manifestu i adapter `jev_context_pilot`. Stary runner dodatkowo sprawdza
rezerwację w bramce klucza, parserze odpowiedzi oraz odczycie generation.
Samo podniesienie `BODY_LIMIT` nie usuwa tych pozostałych ograniczeń. Kod,
konfiguracja i testy starego pilota pozostają niezmienione.

## Wykonana preparacja

`loom/tools/structure/jev_admission_v2.py` odtwarza wszystkie **16/16**
zapisanych intencji: dwa warianty × osiem zależnych pytań na trzech rodzinach.
Weryfikuje wszystkie pliki poprzedniego freeze, wersję starego walidatora
i tożsamość istniejącej kampanii `thread7-new-key-2026-10-04-eur5`.
Nie utożsamia nazwy kampanii zawierającej EUR z nowym budżetem: historyczny
nieodnawialny limit klucza nadal wynosi 5 USD.

Każda intencja ma osobny wynik starej i nowej bramki. Stara odtwarza **4/16**
dopuszczeń oraz **12/16** odrzuceń. Nowa polityka badania z limitem
65 536 bajtów i planowaną rezerwacją 0,003 USD dopuszcza lokalnie **16/16**.
Rzeczywiste requesty mają 9 829–50 167 bajtów. Dokładne bajty, włącznie
z końcowym newline, oraz wszystkie pola przechodzą bez zmian do istniejącego
`loom.research_programme_manifest/1`. Żadnej treści nie obcięto; scope nie
przemianowano. Wąski wariant polityki nadal zachowuje wszystkie 16 intencji,
także 12 odrzuconych, i nie wystawia ich jako wykonanych.

Nowy adapter ma świadomie wersjonowany zakres obsługi odziedziczony po
starym: wskazany model/provider, `state.text`, 1–32 pytania `noul` i dokładny
zestaw pól. To zakres obsługiwany przez **adapter v2**, nie dowód bieżącego
limitu dostawcy. Inny model, parametr, typ pytania lub pole daje jawne
odrzucenie; nie jest usuwane ani udawane jako wykonane. Rozszerzenie zakresu
wymaga nowej wersji kontraktu adaptera i dowodu możliwości dostawcy.

## Cztery różne granice

| Granica | Status po preparacji |
|---|---|
| historyczny instrument | odtworzony; 4/16 dopuszczone |
| jawna polityka badania v2 | 16/16 dopuszczone lokalnie |
| realny model/endpoint i parametry | unknown; wymagane świeże sprawdzenie przed dispatch |
| budżet kampanii / aktualny klucz | unknown; wymagany preflight istniejącego payera |

Suma planowanych minimalnych rezerwacji to **0,048 USD**; niczego jeszcze
nie zarezerwowano. Model rachunkowy według historycznej stawki daje
**0,025759020 USD**, co nie jest świeżą wyceną ani gwarantowanym górnym
kosztem dostawcy. Założenie bajty + 1024 jest jawne, nie jest pomiarem
tokenizera. Zachowano historyczny `provider.max_price`; nie podnosi on
autoryzowanego limitu. Przed faktycznym wysłaniem potrzebne są aktualne
komponenty ceny i ich bounds, routing, ograniczenia i obsługa parametrów,
tożsamość klucza, zużycie, nierozstrzygnięte rezerwacje oraz rezerwacja
`research_programme_runner.PrivateLedger`. Nie powstał drugi ledger.

Każdy rekord ma `provider_eligibility=null`, `campaign_admission=null`,
`execution_status=not_executed` i `dispatch_ready=false`. Brak klucza nie
przeszkodził preparacji. Nowe wywołania: **0**. Nowy koszt: **0 USD**.
Nie powstały nowe oceny jakości ani deklaracja dopuszczenia przez dostawcę.

## Weryfikacja i wznowienie

Testy obejmują oddzielne bramki, niefinitywne/ujemne limity, zachowanie
parametrów, brak ukrytego scope mapping, nieobsługiwane warianty, bezpieczne
stałe kody błędów, hashe każdego prywatnego pliku, niezmienność wszystkich
16 requestów oraz 4/16 i 16/16 na rzeczywistych zamrożonych intencjach.
Testy dawnego pilota uruchomiono jako regresję kontrolowanego transportu.
Wyniki liczbowe testów zapisuje `jev-tests-v2.json`; pełny log jest obok.

Przykład reprodukcji bez sieci i bez płatnych wywołań:

```sh
python3 -m loom.tools.structure.jev_admission_v2 \
  --prior /private/prior/jev-real-final-v1 \
  --output /private/new/jev-admission-v2 \
  --plan docs/research/thread7_real_2026-10-09/continuation_01/jev-plan-v2.json
```

Ścieżka output musi być nowa i znajdować się poza repozytorium.
`ADMISSIONS.json` zachowuje osobne intencje i uzasadnienia; `payer/manifest.json`
i dokładne requesty są wejściami istniejącego payera po jego preflight.
`FREEZE.json` wiąże plan, dane, admissions, receipt i poprzedni freeze.
Przejście lokalnej walidacji nie jest autoryzacją wysłania; nie należy
kierować tego wariantu do starego `jev_live_pilot.run` z jego stałą rezerwacją.
