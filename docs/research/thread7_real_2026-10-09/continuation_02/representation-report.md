# Reprezentacje istniejącego panelu — pomiar offline, kontynuacja 02

Zmierzono **108 zachowanych widoków**: te same 12 rodzin (2079 wiadomości, 2085 węzłów), 3 reprezentacje i 3 rozdzielczości. Każdy został odtworzony przez przypięty historyczny producer i porównany bajtowo z `expanded-v2`: **108/108 zgodnych**. Źródła i poprzednie preparacje pozostają niezmienione. Nowych wywołań modeli: **0**, koszt: **0 USD**.

| Reprezentacja | Rozdzielczość | Węzły | Bajty JSON łącznie | Pola native-message zachowane w wybranych węzłach | Relacje zachowane względem całej rodziny | Mediana czasu przygotowania / rodzina | Maks. dodatkowa alokacja Python |
|---|---|---:|---:|---:|---:|---:|---:|
| exact_text | checkpoint | 1552 | 1816175 | 6.53% | 0.00% | 23.63 ms | 50191434 B |
| exact_text | full | 2085 | 2608478 | 5.98% | 0.00% | 29.88 ms | 50201522 B |
| exact_text | selected_subgraph | 36 | 50607 | 4.42% | 0.00% | 22.04 ms | 50087112 B |
| explicit_fields | checkpoint | 1552 | 5599849 | 100.00% | 74.36% | 29.27 ms | 50410184 B |
| explicit_fields | full | 2085 | 7797444 | 100.00% | 100.00% | 37.93 ms | 50443400 B |
| explicit_fields | selected_subgraph | 36 | 177201 | 100.00% | 1.73% | 23.21 ms | 50089008 B |
| source_graph | checkpoint | 1552 | 11193452 | 100.00% | 74.36% | 32.64 ms | 50726608 B |
| source_graph | full | 2085 | 15618348 | 100.00% | 100.00% | 37.20 ms | 50791216 B |
| source_graph | selected_subgraph | 36 | 346728 | 100.00% | 1.73% | 27.90 ms | 50090336 B |

Pełny tekst zajmuje 2 608 478 B, pola 7 797 444 B, graf 15 618 348 B. Graf ma około 5,99× rozmiaru tekstu oraz 2,00× rozmiaru pól. Nie jest to dowód większej jakości. Graf zachowuje pełne znormalizowane węzły, w tym `native_node` z ponowną kopią treści natywnej wiadomości; jawne pola zachowują `native_message`, lecz pomijają część opakowania węzła. Redundancja ma rzeczywisty koszt bajtowy.

Wszystkie trzy reprezentacje zachowują **100% literalnych pól tekstowych wybranych węzłów**: 2723/2723 w pełnym widoku, 2258/2258 na granicy, 51/51 w podgrafie. Niskie 5,98% wszystkich liści native-message dla pełnego tekstu oznacza utratę metadanych i danych nietekstowych, a nie utratę 94% tekstu. Tekst nie zachowuje jawnych relacji rodzic–dziecko. Pola i graf zachowują wszystkie 2079 relacji rodzica w pełnym panelu, w tym sześć nierozwiązanych referencji zewnętrznych; ich obecność nie dowodzi istnienia rodzica w dostępnym eksporcie.

Widok granicy zachowuje 1552/2085 węzłów. Podgraf zachowuje 36/2085. We wszystkich wariantach pomiar liczy osobno pokrycie wewnątrz wyboru i względem całej rodziny. Zachowanie wszystkich pól wybranych 36 węzłów nie oznacza kompletności całego źródła. Checkpoint jest dotychczasowym widokiem ścieżki/granicy tablicy, **nie streszczeniem**.

Graf odtwarza wartości JSON wszystkich wybranych znormalizowanych węzłów. Jawne pola odtwarzają wartości `native_message`, ale nie pełne znormalizowane węzły. Licznik `native_messages_reconstructed` w surowym wyniku obejmuje także sześć pustych slotów `native_message=null`: jednostką jest wartość pola, nie zawsze wiadomość. Żadna z reprezentacji nie odtwarza całego opakowania rozmowy, oryginalnej serializacji pliku ani binariów załączników. Literalne fragmenty tekstu i ich aliasy są sprawdzane po dokładnych JSON pointers.

Narzut samych kluczy i składni JSON (bez bajtów kanonicznych wartości skalarnych) wynosi dla pełnych widoków: tekst 295 577 B, pola 857 001 B, graf 1 693 972 B. Oddzielny wskaźnik bajtów ponad literalny tekst zawiera również metadane i opakowania, więc nie jest nazywany czystym narzutem serializacji. Tokenów **nie wyliczano**: tokenizer i token_count pozostają null, bez przeliczania bajtów na tokeny.

Czas to mediana trzech wykonań przypiętego `prepare_view` (walidacja, kopie i haszowanie włącznie), potem mediana rodzin. Czas serializacji jest dodatkowo zapisany osobno; producer także serializuje przy haszowaniu, więc wartości nie należy sumować jako niezależnych etapów. Pamięć: osobny przebieg tracemalloc, dodatkowa szczytowa alokacja Python, bez wcześniej załadowanych źródeł; to **nie RSS**. Mimo podgrafu z trzema węzłami na rodzinę najwyższa alokacja nadal wynosi około 50 MB: producer waliduje i kanonizuje całą rodzinę. Zmniejszenie wysyłanego widoku nie eliminuje kosztu przygotowania źródła. Czasy są obserwacją tej sesji, bez izolacji sprzętu lub wniosków o statystycznej przewadze kilku milisekund.

**Testy mechaniki: 13/13.** Zmiana białych znaków/serializacji i kolejności kluczy zachowuje tożsamość i pokrycie. Przeniesienie identycznego źródła zachowuje wersję. Jawne usunięcie fragmentu obniża pokrycie. Modyfikacja wartości nie przechodzi jako zachowana. Kontrolowana korekta zmienia cel wyszukiwania z jawnie zadanym zakresem, a ponowne użycie starej wersji zwiększa `wrong_version_selected`. Ten test korzysta z lokalnego retrieval; nie dowodzi automatycznego rozpoznawania korekt przez model i nie tworzy nowych autentycznych wypowiedzi.

Artefakty publiczne: `representation-policy.json`, `representation-results.json`, `representation-receipt.json`, pełny `representation-tests.log`. Prywatny katalog `representation/` zawiera START, 108 surowych pomiarów wraz z hashami źródeł i stratami, SUMMARY oraz MANIFEST; checkpoint kodu i polityki zapisano przed pomiarem. Każda rodzina zachowuje dotychczasowy podział 8 development / 4 validation. W wynikach są zarówno mikroagregaty, jak i makrośrednie rodzin. Nie wykonano strojenia ani oceny odpowiedzi LLM.

Odtworzenie pomiaru na nieistniejący wcześniej katalog:

```sh
PYTHONPATH=/tmp/thread7-python-deps:. python3 -m loom.tools.structure.research_representation_v1 \
  --preparation /tmp/thread7-continuation-2026-10-09/context/expanded-v2 \
  --output /var/tmp/thread7-representation-new-run \
  --policy docs/research/thread7_real_2026-10-09/continuation_02/representation-policy.json \
  --public-summary /var/tmp/thread7-representation-new-summary.json
```
