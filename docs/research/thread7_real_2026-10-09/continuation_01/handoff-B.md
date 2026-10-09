# C → B: analiza i prezentacja badania, continuation 01

To przyrost narzędzi badawczych od `69880859802267f1b38b82eeaecd6fdc5522a47a`.
Nie wprowadza runtime, UI, nowych wspólnych schematów ani silnika profili.
Poprzedni checkpoint ma SHA-256
`137c49ab13681d036b955039a580aff3cb86bc342b62981c484abcfe9363c28e`.

## Wywoływalne operacje

`loom/tools/structure/experiment_analysis_v1.py` przyjmuje zamrożone plany
operacji i dokładne bajty ocenianej odpowiedzi. `evaluate_response` zachowuje
osobno format, integralność grafu, rozwiązywanie referencji, zgodność ze źródłem,
cel, preferencje, koszt i czas. Brak wykonanej oceny semantycznej daje `null`.
Syntaktycznie poprawny graf nie ustanawia prawdziwości, zgodności ze źródłem
ani wykonania native. Hash odpowiedzi ma zakres `evaluated_reply_bytes`,
odrębny od raw transport envelope w payerze.

`evaluation-protocol.json` jest wersjonowanym protokołem autorstwa asystenta.
Wiąże poprzedni freeze i istniejące prowizoryczne annotations. Wymaga osobnego
source/view/request/response/protocol SHA dla każdego sądu. Nie jest akceptacją
właściciela ani niezależną adjudykacją. Importowana ocena modelu, asystenta
i niezależnego adjudykatora jest oddzielną warstwą. Nawet wymagany hash dowodu
niezależności nie uwierzytelnia automatycznie osoby oceniającej; wynik jawnie
pozostawia tę weryfikację poza tym narzędziem.

`select_response_form(protocol, forms, name, stage=...)` stosuje dane z
`evaluation-response-forms.json` i daje nowy hash protokołu. Wspólny format z
preparacją: graf bezpośredni `{nodes, edges, evidence, limitations}`; pojedyncze
wywołanie tekst+struktura `{text, structure}`; węzeł `{id, kind, text,
source_refs}`, krawędź `{source, target, relation, source_refs}`. Wariant
`text_then_structure` najpierw daje tekst, potem osobne modelowe wydobycie
struktury. Etap drugi pozostaje pending do zachowania pierwszej odpowiedzi.

`compose_two_stage_outcome` przyjmuje obydwa plany i rekordy faz oraz dokładne
bajty pierwszej odpowiedzi i drugiego requestu. Sprawdza hashe, tożsamość
rodziny/pytania/wariantu i rzeczywiście zużyty tekst w drugim requestcie.
Dla `materialize_extraction` selektor zależności to `/messages/1/content`,
potem jawne dekodowanie JSON i `/answer_text`. Wynik zachowuje oba dowody oraz
osobne błędy formatów faz; daje jeden slot wariantu do porównania. Koszt jest
sumą wyłącznie dwóch zweryfikowanych rachunków, inaczej `null` i osobna znana
suma częściowa. Czas to suma zmierzonych czasów sekwencyjnych requestów,
bez czasu kolejki. Złożony hash nie udaje requestu lub odpowiedzi modelu.
Semantyczna jakość całego tekstu wraz z grafem wymaga osobnej oceny i nie jest
kopią oceny samej ekstrakcji.

## Porównanie i niepewność

`paired_comparison` paruje tę samą rodzinę, hash źródła, pytanie i numer
powtórki. Najpierw uśrednia powtórki pytania, następnie pytania rodziny, potem
rodziny. Osiem pytań trzech rozmów pozostaje trzema rodzinami. Braki zachowują
mianownik; wynik pokazuje liczbę zaplanowanych/obserwowanych par i rodzin.
DEV/walidacja i różne panele mają osobne strata. Zmiana `split` tej samej rodziny
lub identycznego źródła jest błędem również między panelami.

Dla różnych form odpowiedzi przekazać jawne
`arm_protocols={left: exact_protocol_left, right: exact_protocol_right}`.
Porównanie sprawdza dokładny hash każdego ramienia i wspólną definicję metryki,
rubrykę, rodzaj referencji, źródła i politykę parowania. Samo różne formatowanie
odpowiedzi jest dozwolonym badanym wariantem. Dowolnych rewizji ocen nie wolno
połączyć bez takiego wiązania. Przy mniej niż sześciu rodzinach domyślna polityka
daje `null` przedziału; próg, liczba bootstrapów i seed są danymi. Przedział
dotyczy obserwowanych rodzin, nie reprezentatywności całego archiwum.

`stability` grupuje też dokładny widok, request i protokół, aby zmiany promptu
nie stały się powtórkami tego samego wariantu. Wymaga odrębnych attempt IDs
i zaobserwowanego wyłączenia response cache. HIT/unknown/replay nie dowodzi
niezależnego wykonania. Zgodność bajtów odpowiedzi nie jest stabilnością
semantyczną. Seed nie obiecuje identycznego inference ani exactly-once.

`tradeoff` pokazuje oddzielne sparowane wymiary jakości, kosztu i opóźnienia.
Nie tworzy automatycznego ważonego zwycięzcy. Wspólne pokrycie wszystkich
zaplanowanych slotów jest warunkiem statusu complete; inaczej raport jest
częściowy. `measurement_summary` zachowuje dokładne Decimal rachunki: znana
suma częściowa nie staje się całym kosztem, gdy są nieznane rachunki.

## Basic i Expert

`handoff-contract.json` jest lokalnym schematem bezpiecznej projekcji tego
niezmierzonego etapu, a nie nowym kontraktem runtime. `handoff-example.json`
zawiera prawdziwe liczby historycznego panelu: 3 rodziny, 44 wiadomości,
8 zależnych pytań, brak nowego inference. Osobny nowy panel to 12 rodzin,
2079 wiadomości i 2085 węzłów; 6 rodzin OpenAI i 6 Anthropic, 8 tuning
i 4 prowizorycznie walidacyjne. Okres: 2026-01-23–2026-09-09. Źródło tych
liczb to `corpus-receipt.json`, freeze `324d33a17ac09cc3b1c1dd11fb875b10d2a7ccf1f4c7b2fb06c313f9ba7e209f`.
Nie ustanowiono niezależności ani kompletności załączników. Cechy semantyczne
wykryte leksykalnie są kandydatami, nie adjudykacją. Basic mówi krótko o braku
pomiaru i zwycięzcy; Expert rozdziela liczebność, jakość, koszt, ograniczenia
i źródła. Wersja projekcji measured musi być nowa: nie dopisywać wyniku do
zamrożonej wersji unmeasured.

Archiwalny rachunek nadal: 720 prób / 0,873216500 USD, historycznie
4,126783500 USD z nieodnawialnych 5 USD. Bieżący stan dostawcy `null`.
Nowy koszt tego narzędzia i testów: 0 USD / 0 wywołań.

`build_handoff_artifact` używa istniejącego
`loom/tools/seeding/method_graph.py`. `handoff-projection.json` to dane
projekcji. Wynik przechodzi istniejące `loom.method_graph/1`,
`loom.method_run_trace/1`, codec GraphPacket i odzyskanie dokładnych bajtów
źródeł/rekordów. To dowód kompatybilności projekcji w Python. Wykonanie native,
zapis CABI, granica runtime i UI należą do B/A; nie są tu deklarowane.

Surowe rekordy ocen, prywatne locatory, tytuły, treści requestów i wyjątki
nie są materiałem do tej publicznej projekcji. `build_handoff_artifact` nie
jest anonimizatorem: przed użyciem wymaga już sprawdzonej projekcji.
`candidate-presets-v1.json` odwołuje się do istniejących profili wyłącznie jako
niezwalidowanych propozycji. Żadne ustawienie użytkownika nie jest zmieniane.
