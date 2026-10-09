# C → B: kontynuacja 01, 2026-10-09

Ten pakiet zastępuje wyłącznie bieżącą prezentację wyników; wcześniejsze freeze,
specyfikacje, przykłady i wyniki pozostają zachowane. Nie zmienia runtime, UI,
wspólnych schematów ani preferencji użytkownika.

## Dane do integracji

| Element | Bieżący plik / status |
|---|---|
| Basic + Expert | `handoff-final-example-v2.json` |
| Maszynowy kontrakt tej projekcji | `handoff-final-contract-v2.json` |
| Istniejące kontrakty grafu i śladu | `handoff-final-artifact-v3.json.gz`: `loom.method_graph/1`, `loom.method_run_trace/1` |
| Weryfikacja kodeka i schematów | `handoff-final-verification.json`; native/runtime pozostaje poza zakresem C |
| Wersjonowana ocena | `evaluation-protocol.json`, `evaluation-response-forms.json` |
| Kandydaci na presety | `candidate-presets-v1.json`: niezwalidowane, bez adopcji |
| Gotowe małe alternatywy | `connection-plan.json`, `connection-receipt.json`: listy 3/5 wariantów na 2 rodzinach, 6/10 operacji |
| Połączenie preparacji z kolejką i payerem | `connect_prepared.py`; dokładne requesty i SQLite są prywatne |

Kontrakt prezentacji jest kontraktem tego wyniku badawczego, nie nowym wspólnym
schematem runtime. Przykład nie zawiera rozmów, natywnych identyfikatorów źródeł
ani prywatnych requestów. SHA prywatnego artefaktu jest referencją do dowodu,
nie dowodem, że integrator dysponuje jego bajtami.

## Co może pokazać Basic

Przygotowano 12 rodzin OpenAI i Anthropic, 2079 wiadomości
(23.01–09.09.2026). Gotowych jest 675 requestów; małe alternatywy mają 6 lub
10 wywołań. Koszt nowego etapu: 0 USD. Jakość modeli nie została jeszcze
zmierzona.

Expert rozwija pochodzenie, okres, liczebności rodzin i wiadomości, status
preparacji, źródła dowodów, metryki `null`, koszt i ograniczenia. Historyczne
720 prób / 0,873216500 USD nie jest nowym eksperymentem na tym panelu. Archiwalne
4,126783500 USD nie jest potwierdzeniem aktualnego budżetu dostawcy.

## Semantyka i granice

- Panel historyczny (3 rodziny, 44 wiadomości, 8 zależnych pytań) jest osobny.
  Nowy panel to 6 rodzin OpenAI i 6 Anthropic; 8 tuning i 4 prowizoryczna walidacja.
  Te 4 rodziny nie mają dowiedzionej niezależności od wszystkich wcześniejszych badań.
- Z 972 intencji 450 ma kompletną preparację, 225 gotowy pierwszy request i
  zależną ekstrakcję pending, a 297 oczekuje źródłowych adnotacji preferencji.
  Żadna preparacja nie jest wykonaniem modelu.
- Graf źródłowy jest grafem struktury eksportu. Checkpoint jest obserwowanym
  punktem odcięcia źródła, nie nowym streszczeniem ani zadeklarowanym przez
  użytkownika checkpointem. Braki i utraty transformacji są jawne.
- Wybrany profil ma wersję i pochodzenie z istniejących danych profili;
  nie deklaruje preferencji właściciela. Źródłowe preferencje mają dwie
  prowizoryczne, dokładnie zakotwiczone adnotacje w jednej rodzinie.
- Analiza rozdziela format, referencje, integralność, zgodność ze źródłem,
  cel/preferencje, stabilność, koszt i opóźnienie. Mechanika, sąd modelu/asystenta
  i niezależna adjudykacja mają osobne klasy dowodu. Brak oceny pozostaje `null`.
- Porównanie sparowane agreguje powtórzenia i pytania do rodziny. Liczba pytań
  nie zwiększa liczby niezależnych rodzin. Nie ma zwycięzcy ani automatycznej adopcji.

## Wznowienie i payer

Prywatny checkpoint zawiera pełne źródła, widoki, requesty, manifests i kolejki.
Bieżące wersje to `context/expanded-v2` oraz `connected-v2`; starsze wersje i
nieudana preparacja adnotacji są zachowane jako dowody, bez wykonanych wywołań.

`connect_prepared.py` weryfikuje hashe źródeł, preparacji i spec oraz przekazuje
dokładne bajty body do istniejącej kolejki i manifestu payera. Zachowuje kolejność
`queue_ordinal`. Obsługuje małe niezależne wywołania; wariant dwufazowy odrzuca
jednoznacznie. Dla dwufazowego wariantu należy najpierw utrwalić prawdziwą pełną
odpowiedź, następnie użyć `materialize_extraction`, osobno zaplanować/rezerwować
drugi etap i rozliczyć oba. Nie ma placeholdera udającego ekstrakcję.

Przed dispatch trzeba zweryfikować istniejącą tożsamość kampanii i osobnego
klucza, usage, rezerwacje/pending, aktualne ceny i możliwości endpointu, zasady
cache oraz górny koszt fazy. Obecnie preflight jest pending i żaden scope nie
jest gotowy do dispatch. Alternatywy 6 i 10 wywołań nie sumują się automatycznie.

Jedynym ledgerem pieniędzy pozostaje istniejący `PrivateLedger`. Wrapper
sprawdza prawdziwą referencję rezerwacji przed wysłaniem. Timeout po wysłaniu
nie powoduje ponowienia. Pierwszy dowód pozostaje niezmienny; kolejne dopisuje
się. Niejednoznaczny stan wymaga zewnętrznego dowodu i jawnego rozstrzygnięcia.
Istniejący reconcile GET obsługuje kwalifikujące się `billing_pending` z ID
generacji. Niepełny dziennik rezerwacji lub osierocona odpowiedź po crashu
pozostaje zablokowana; ten pakiet nie dodaje automatycznego rozliczania takich
stanów. Nie gwarantuje exactly-once inference.

## Sprawdzenie

`FINAL_TESTS.json`: 336 testów, 0 błędów, 0 pominięć. Kontrolowany transport;
0 nowych płatnych wywołań. Weryfikacja Python/kodeka i schematów nie zastępuje
niezależnych testów granicy native/runtime prowadzonych przez A i B.

Pełny raport: `docs/reports/thread7-real-continuation-2026-10-09.md`.
