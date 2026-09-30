# ChatADHD — równoległa praca i wznowienie

Stan organizacyjny: 2026-09-30. Integrator: ROOT w nowym wątku kontynuacji.
Punkt odzyskania: `fafc77f8eeebdff4c32897b5e8ef94dd26d4ec38`.
Scalono także historię gałęzi Claude'a do `46066308ac6306a1b30f65b98e19a40da6df7e9c`;
zachowano nowsze poprawki badawcze i aktualne wyjaśnienie właściciela.
Gałąź integracyjna: `gpt/research-2026-09-30`. Nie jest to wdrożenie ani scalenie PR6.

## Cel i źródła decyzji

Właściciel w bieżącej rozmowie zlecił przejęcie projektu, autonomiczną pracę,
znacznie większą równoległość oraz przygotowanie podziału na osobne wątki.
Więcej tokenów ChatGPT przeznaczamy na niezależne implementacje, przeglądy
i eksperymenty. Wynik mierzymy działającymi przyrostami i krótszym czasem
oczekiwania, nie długością odpowiedzi. Nie obiecujemy liniowego przyspieszenia ×10.

Źródła: `../research/RECOVERY_COORDINATION_2026-09-30.md`,
`../research/OWNER_CLARIFICATION_2026-09-30.md`, `../../ECOSYSTEM.md`,
`../architecture/OWNER_REQUIREMENTS_2026-09-26.md` oraz bieżący kod.
Podział poniżej jest decyzją wykonawczą ROOT, nie cytatem właściciela.

## Co działa równolegle w tym przebiegu

| Tor | Konkretny przyrost | Własność |
|---|---|---|
| Kontekst rozmowy | ContextEngine na rzeczywistej ścieżce ChatEngine, niezależne źródła kontekstu, ślad wiadomości | `src/chat`, `chat_engine.h`, małe adaptery runtime/C ABI |
| Interfejs | Sterowanie powyższym i inspekcja zapisanego śladu w istniejącym czacie | `web` |
| Koordynacja | Trwałe przydziały pracy, fencing, rozróżnienie przerwania przed i po wykonaniu | `tools/coordination` |
| Źródła grafu | Ścisłe sprawdzanie locatorów i niejednoznacznego JSON bez zmiany zamrożonych badań | `tools/structure/retrieval_exploration_v1` |
| Ocena epistemiczna | Pewność twierdzenia użytkownika niezależna od jego uprawnienia do decyzji | `src/resolve/assess.cpp`, testy |
| Weryfikacja | Osobny pomiar odzyskanej bazy, kompilacja, testy integracji i niezależny przegląd | artefakty testowe; bez edycji implementacji |

Ten przyrost wykonało sześciu agentów plus integrator w jednym wątku. Agenci
prowadzili również wzajemne przeglądy. Dodatkowe rozmowy nie zostały automatycznie
utworzone. Ich gotowe pakiety są poniżej; wynik wspólnej weryfikacji opisuje
aktualny `../STATE.md`.

## Osobne wątki — gotowe pakiety

Otwieraj wątki od W1 do W6 zależnie od dostępnej pojemności. Każdy może użyć
własnych agentów, ale najpierw sprawdza faktyczne limity swojego środowiska.

| Pakiet | Następny rezultat | Zależność |
|---|---|---|
| [W1 — Kompilator instrukcji](W1_REQUEST.md) | ActiveTaskSpec z historią pochodzenia trafia do rzeczywistego requestu | obecne wpięcie ContextEngine |
| [W2 — Wyszukiwanie i kontekst](W2_RETRIEVAL.md) | zakres/szczegółowość per teza planu, kanały semantyczne i shadow pipeline | podstawowe osie ContextRequest już wdrożone |
| [W3 — Modele i receptury](W3_MODELS.md) | poprawa na zidentyfikowanych błędach i profile instrumentów | zapisane odpowiedzi; live tylko po sprawdzeniu budżetu |
| [W4 — Graf i wykonanie](W4_GRAPH_RUNTIME.md) | użyteczne wykonanie transformacji grafu z recovery i śladem | GraphPacket, AnalysisPlan, koordynacja |
| [W5 — Przestrzeń pracy](W5_WORKSPACE.md) | trwałe niezależne i sprzężone widoki nad rzeczywistymi danymi | natywne API; bez czekania na jakość modeli |
| [W6 — Niezależna walidacja](W6_EVIDENCE.md) | kontrprzykłady, import i pomiary końca-do-końca | współpracuje ze wszystkimi; oddziela autora i oceniającego |

Krótki prompt startowy do nowej rozmowy:

> Przejmij pakiet WN z docs/coordination w repo klb-t/chatadhd,
> gałąź gpt/research-2026-09-30. Najpierw przeczytaj AGENTS.md, bieżący
> docs/STATE.md i pakiet. Sprawdź aktualny zdalny HEAD i już wykonane zmiany,
> aby ich nie powtarzać. Pracuj autonomicznie na własnej gałęzi w zakresie
> pakietu; użyj agentów do niezależnych zadań i przeglądu. Publikuj małe
> sprawdzone commity i receipt z dokładnymi SHA. Nie edytuj wspólnego STATE
> i nie scalaj innych gałęzi; integruje ROOT. Kontynuuj aż uzyskasz działający
> przyrost oraz uczciwy wynik testów, nie tylko plan.

Zastąp WN wybranym numerem. Każdy uruchomiony wątek najpierw potwierdza aktualny
HEAD oraz wolny zakres z integratorem. Przebieg wewnętrznych agentów nie oznacza
samoczynnego uruchomienia ani dalszej pracy sześciu osobnych rozmów.

## Reguły integracji i restartu

1. Źródłem synchronizacji są commity i receipts w repo. Wyszukiwanie historii
   rozmów pomaga odzyskać intencje, ale nie jest mechanizmem blokad ani kolejką.
2. Nowy wątek pobiera aktualny HEAD, zapisuje pełny `base_sha`, tworzy gałąź
   `gpt/wn-<opis>-<data>` i sprawdza, czy rezultat nie został już zintegrowany.
   Nazwa gałęzi nie stanowi dowodu przydziału. ROOT przydziela wspólne pliki.
3. Każdy pakiet ma jednego właściciela. Zmiany wspólnych typów/API wymagają
   uzgodnienia z integratorem, który może wykonać małą zmianę interfejsu jako
   osobny pierwszy commit. Nie czekamy z całym torem na implementację zależności.
4. Pracuj na własnym checkout/worktree. Nie uruchamiaj dwóch niezależnych
   kompilacji w tym samym katalogu budowania. Nie nadpisuj cudzych zmian.
5. Commit i push po każdym logicznym przyroście z testem. Protokół badania
   zapisuj przed uruchomieniem; pierwsze odpowiedzi i wyniki przed poprawką.
6. Receipt ma `base_sha`, `head_sha`, pliki, cel, wynik, dokładne polecenia/testy,
   pochodzenie danych, ograniczenia, zależności i następny krok. Każdy tor zapisuje
   własny plik `docs/coordination/receipts/WN-<data>-<opis>.md`.
7. Tylko ROOT aktualizuje `docs/STATE.md`, wspólny indeks, globalne rozliczenie
   eksperymentów i gałąź integracyjną. PR6 pozostaje draftem. Bez force-push.
8. Przed integracją: przegląd przez innego agenta, testy zmienionego zakresu,
   wymagane sentinele zgodności, a na koniec wspólna regresja. Wynik historyczny
   lub test z niezapisanym źródłem nie jest pomiarem obecnego checkoutu.
9. Po resecie odtwarzamy zdalny commit, receipt i artefakty. Nie uznajemy lokalnego
   niezapisanego wyniku za odzyskany. Wywołania z nieznanym skutkiem nie są
   automatycznie powtarzane. Koordynacja SQLite dotyczy współdzielonego lokalnego
   systemu plików; nie jest rozproszoną blokadą między niezależnymi rozmowami.
10. Progi jakości i oddzielenie DEV/VAL pozostają. Nie czytaj `eval/real-holdout-key`,
    zamkniętego korpusu katalogu ani zapieczętowanej walidacji grafu podczas rozwoju.
    Zwiększenie puli tokenów nie zmienia istniejącego budżetu OpenRouter.

## Kolejność i pomiar przepustowości

Najpierw działający czat konsumujący wybrany kontekst; równolegle źródła,
koordynacja i niezależne błędy. Następnie ActiveTaskSpec, szersze wyszukiwanie,
sprzężenia widoków i faktyczna jakość analizy. Nie budujemy kolejnego kompletnego
abstrakcyjnego silnika, gdy istniejący mechanizm można wpiąć w produkt.

Receipt zapisuje czas rozpoczęcia, blokady, gotowość do przeglądu i integrację,
jeżeli zostały rzeczywiście zmierzone. Obserwujemy czas zadania, czas czekania,
liczbę zweryfikowanych zachowań, regresje i poprawki po przeglądzie.
Nie wpisujemy szacunków jako zmierzonych czasów ani nie mnożymy liczby testów
przez liczbę agentów. Następne zadanie wybieramy z odblokowanej kolejki.
