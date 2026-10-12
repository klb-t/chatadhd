# B-CH-03 — Legacy ingest grafu: polityka przyjęcia, scalanie, `produced_by` na każdej relacji

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/chatadhd` @ `9e20f99a` |
| Fala / priorytet | W2 / P1 |
| Ustalenia | CH-006, CH-010; odpowiednik LOOM-002 w aktywnym kernelu (sprawdzić) |
| Niezmienniki EITG | G2, G8, G9 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | T2 (worker dotyka `semantic_worker.cpp:368–385`) |
| Worktree | `t9-ch-03` |

## Zakres
Relacje z odpowiedzi modelu nie są dopisywane do kanonu automatycznie: polityka przyjęcia
`ask` / `candidate` / `automatic` jest ustawieniem w zakresach (DUCH §2: „polityka przyjęcia jest
ustawieniem”), każda wynikowa relacja wskazuje metodę, wersję, parametry i pochodzenie, kolizja
nazwy przy różnych typach zachowuje oba świadectwa albo jawny powód wyboru (MergePolicy).

## Pliki
- `loom/src/graph/graph_engine.cpp:21–88`, `:129–163`; `loom/src/worker/semantic_worker.cpp:368–385`.
- `loom/src/semantic/semantic_llm.cpp:199–230` (dedup nazw, waga 0,8); lustro Python
  `engine/semantic_llm.py:223–241`.
- Dane: `loom/data/runtime/graph_ingest.pack` (istnieje: DIC-0015…0020 — `default_kind`,
  `min_relevance`, `predicate`, `membership`), do rozszerzenia o sekcje `admission` i `merge`.
- Konsumenci: `MSG_CREATED → GraphEngine::on_message_checked → ingest_common`,
  `SemanticWorker → ingest_analysis`, `SemanticLLM::analyse/_merge → GraphEngine`.

## Mechanizm docelowy (istniejący)
`graph_ingest.pack` z warstwami R40; klasy dowodu z `LOOM_CONCEPTUAL_MODEL` §2.3 (`observed`
vs `inferred`); `produced_by` w `loom_provenance`; krawędź kandydata ze statusem zamiast kanonu
(DUCH §1 „nierozpoznane, niepewne, wyłaniające się”).

## Kroki
**Rozważenie.** Sprawdzić na bieżącym SHA, czy gałąź B lub T2 nie wprowadziły już admission
(pass4: „nie znaleziono nowego równoległego store/resolvera” — więc raczej nie); sprawdzić
odpowiednik LOOM-002 (`create_node`/`create_link` cicho nadpisuje?) w aktywnym `db_graph`;
rekord decyzji: domyślna polityka `automatic` dla relacji regex (deterministyczne, tanie) i
`candidate` dla relacji LLM — **to jest preset, właściciel może ustawić `automatic` wszędzie**.

**Implementacja.** (a) fixture: ta sama syntetyczna odpowiedź LLM przez `ingest_common`;
(b) sekcja `admission` w packu, obecne zachowanie = `automatic` jako preset `legacy`;
(c) `candidate` tworzy krawędź ze statusem bez zapisu do kanonu, `ask` czeka na akceptację
(stan, nie blokada — R43); (d) każda relacja z `produced_by` {metoda, wersja, parametry};
(e) MergePolicy: klucz tożsamości, konflikt typów, waga kanału pomocniczego jako dane;
kolizja zachowuje oba wyniki z klasą dowodu; (f) `known_at` na krawędzi; (g) Python: ten sam
pack albo jawny status.

**Optymalizacja.** Pomiar: liczba relacji kanon vs kandydat na fixture; rozmiar metadanych
per relacja (klasa 3 marnotrawstwa z `GRAPH_ENTRY` §9 — profil ~0,7 KB kopiowany do każdej
próby: tu odniesienie, nie kopia).

**Iteracja.** Przegląd kandydatów w UI (B-CH-05 / T3) i raport ekonomii (T4) konsumują statusy.

## Kryterium zamknięcia (CH-006, CH-010)
- To samo wyjście LLM daje: `candidate` bez zapisu kanonu, `ask` bez akceptacji, `automatic`
  z jawnie wybraną polityką.
- Każda wynikowa relacja wskazuje metodę/wersję/parametry i pochodzenie, nie tylko wiadomość.
- Kolizja tej samej nazwy przy różnych typach zachowuje oba wyniki zgodnie ze strategią; waga
  kanału i polityka dedup zmieniają się przez dane.
- Istniejące nieoznaczone relacje: migracja oznacza je `provenance: unknown`, nie dopisuje
  zmyślonej pewności (ryzyko audytu).
- Brama pełna.

## Czego nie robić
- Nie przepisywać istniejących relacji na `observed`.
- Nie wprowadzać drugiej tabeli kandydatów; status jest polem krawędzi.
- Nie usuwać scalania po nazwie — uczynić je jedną ze strategii.

## Co zapisać
Rekord decyzji; hash presetu `legacy`; liczby relacji per status na fixture.
