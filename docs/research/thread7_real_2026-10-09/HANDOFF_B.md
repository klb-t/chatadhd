# Kontrakt dla B — eksperymenty są danymi

Kod badawczy: `loom/tools/structure/experiment_workflow_v1.py`.
Własny plan: `workflow-plan.json`. W produkcji nie dodano dispatchera/UI.

`ExperimentSpec → variants → ordered_jobs → Queue → existing payer →
result_record → existing source-bound scorer → candidate preset`.
Lista obsługuje 3/5 i dowolną inną liczbę wariantów. Macierz ma jawnie
nazwane osie; warianty i balanced queue są leniwe. Pełny odzyskany panel to
2 strategie × 2 temperatury × 2 limity odpowiedzi × 2 preferencje.
Zakres i powtórzenia, metryki/evaluatorzy, budżet kampanii, częstotliwość,
trigger i adopcja są w spec. Scope rozdziela rodziny i identyczne hashe
między DEV/walidacją. `used_blind` nie jest nową walidacją.

Queue jest prywatnym dziennikiem planowania, nie drugim ledgerem pieniędzy.
`mark_dispatched` zapisuje referencję do rezerwacji istniejącego payera.
Pending nigdy nie wraca do prepared po timeout. `capture_first` zachowuje
pierwszy rekord; inny drugi rekord jest odrzucony. Dokładne raw i rachunki
zachowuje istniejący `research_programme_runner.PrivateLedger`. Samo
przekazanie stringa payer_reference nie uwierzytelnia rezerwacji.
Przed płatną fazą payer ponownie weryfikuje klucz, kampanię, cap, usage,
unknown reservations, aktualną cenę każdego komponentu i plan; ta sesja
nie stworzyła konkurencyjnego płatnego wykonawcy.

Event wymaga opt-in i pasującego rodzaju, deduplikuje ID oraz respektuje
częstotliwość między restartami. Zdarzenia z dowolnym
`origin_experiment_id` nie wyzwalają eksperymentu. Zmiana definicji wymaga
nowego ID. W badaniu automatyczne przyjęcie wyników nie zmienia ustawień:
`adoption_policy` to deklaracja dla B. B musi implementować konsumenta tej
polityki i rejestrować skutki; sam JSON nie dowodzi działania runtime.

Każdy wynik rozdziela requested/observed model i provider, parametry
obsługiwane/pominięte, request/response SHA, kolejność, czas, usage, cache,
koszt i jego uwierzytelnienie. Nieznane pola są null. Format, native execution,
zgodność ze źródłem, cel i preferencje pozostają osobnymi wymiarami.
Modelowy evaluator nie jest automatycznym ground truth.

Kanoniczna reprezentacja: istniejący
`loom/tools/seeding/method_graph.py`, profile w `graph-projection.json`,
`loom.method_graph/1` oraz `loom.method_run_trace/1`; wzorzec w
`loom/src/packet/METHOD_GRAPH.md`. `public-workflow-artifact.json.gz`
przechowuje rzeczywiste Entity/Claim/Observation i produced_by edges dla
bezpiecznej projekcji przygotowania. Nie jest execution artifact modeli.
Źródła, dokładne prompt/body i rezultaty modeli muszą zostać w prywatnej
analogicznej projekcji; nie zamieniaj ich hashów w rzekomo odzyskane bajty.

Źródłowe role/version bindings wspólnego kontraktu pozostają bez zmian.
Wydano schema/codec proof w Python. Nie wykonano CABI/native store dla tego
artefaktu; to osobna bramka B i integratora. Presety w
`candidate-presets.json` pozostają niezwalidowane na realnym zbiorze.

Otwarte wymiary: reprezentacja kontekstu i rozdzielczość, graf odpowiedzi
kontra tekst+struktura, nowy korpus Anthropic i stability repeats. Przygotowane
są jako jawne plany w `remaining-design.json`; nie są ukończonymi
requestami ani nowymi wynikami. Historycznych zwycięzców nie adoptuj.
