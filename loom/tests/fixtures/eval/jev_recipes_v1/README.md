# Jev recipes v1 — T3, przygotowanie offline

[U] T3 / R37–R38. [P/H] Nowy pakiet do przyszłego uruchomienia, **nie wyniki Jev**.
Baza repo: `494b164c4515b875014b3cfd1c3760de23bda401`.
Plan analizy: `docs/research/JEV_RECIPES_PLAN_2026-09-29.md`.

## Zawartość

36 fikcyjnych przypadków (18 PL / 18 EN): 16 relacyjnych, 12 kontekstowych,
8 routingu. 18 development i 18 validation. Teksty nie zostały skopiowane z
poprzednich 64 przypadków. Ten sam asystent napisał korpus, etykiety i kod:
**nie jest to ślepy ani niezależnie oceniony zbiór walidacyjny**.

`corpus.json` = materiał wejściowy; `gold.json` = etykiety/uzasadnienia autora;
`recipes.json` = jawne rubryki, warianty i plan; `requests.zip` = kompaktowy
zestaw przygotowanych żądań. `example_request.json` można otworzyć bezpośrednio
w edytorze raw JSON Jev Lab. Gold nie jest wejściem generatora.

ZIP zawiera dwa człony: `requests.jsonl` (rekordy `file` + `body`) i
`request_index.json` (hash każdego body, pochodzenie, plan hierarchii).
**Nie wysyłać rekordu JSONL jako body API.** Eksporter tworzy samodzielne,
czyste body JSON o dokładnie sprawdzonych bajtach.

Cztery warianty instrukcji: znaczące klucze, neutralne klucze, bezsensowne
klucze, string z tymi samymi wartościami. State i treść rubryki są stałe;
nie zmieniamy zastrzeżonych nazw pól API. Przydatność trzech podgrafów jest
oceniana niezależnie, nie jako jeden zwycięzca Choice. Szczegółowość wybiera
najmniejszą wystarczającą spośród rzeczywiście pokazanych reprezentacji,
z osobnymi `omit` i `unavailable`.

Choice: flat kontra root→child, 4/8/16 liści. Kategorie prawdziwe są w pierwszych
czterech — ten eksperyment bada dodawanie dystraktorów, nie pokrycie 16 klas.
Obie możliwe gałęzie są przygotowane, ale wykonywana jest tylko wybrana przez
rzeczywistą odpowiedź korzenia. Nie wolno wybierać dziecka na podstawie gold.
`next_child` jest funkcją czysto lokalną; zły/niekompletny rozkład → brak wyboru.

## Liczby i koszt

**208 przygotowanych body; maks. 184 wywołania przy pełnym wykonaniu bez retry.**
Różnica wynika z dwóch gotowych dzieci, z których wybiera się jedno.
112 żądań relacyjnych/kontekstowych plus 24 epizody × (flat 1 + hierarchy 2).
Nie są to 208 nowych odpowiedzi. Liczba wysłanych żądań i nowy koszt API = 0.

Indeks zawiera `execution_enabled=false` i `authorized_cost_usd=null`.
To nie upoważnia do live. Claude musi osobno potwierdzić budżet, bieżący kontrakt
API i cenę, tożsamość modelu/providera oraz zapisywanie pierwszych odpowiedzi.
Przy testach narzędziowych trzeba unikać powielania efektów zewnętrznych.

## Odtworzenie

Z katalogu repo; generator i jego testy wymagają tylko biblioteki standardowej:

```sh
python loom/tools/eval/jev_recipes.py
python -m unittest discover -s loom/tools/eval -p test_jev_recipes.py -v
python loom/tools/eval/jev_recipes.py --export-to /tmp/jev-recipes-v1
```

Katalog eksportu musi być nowy (brak nadpisywania). W `requests/` powstaną 208
plików, z oddzielnym indeksem. Modele nie są wywoływane przez ten program.

`source_freeze.json` zamraża korpus, gold, rubryki i plan przed wynikami.
Jego amendment dokumentuje wyłącznie zmianę opisu pakowania ZIP/eksportu;
nie zmieniono przy tym treści korpusu, gold ani rubryk. `manifest.json`
zawiera dodatkowo hash generatora, testów, archiwum i przykładowego body.
Weryfikacja sprawdza hash plików, ponowne zbudowanie requestów bez gold oraz
zgodność dokładnych bajtów każdego body po eksporcie. To lokalny freeze,
nie podpis cyfrowy ani zewnętrzna rejestracja badania.

## Wykonane kontrole / ograniczenia

**30/30 testów mechanizmu przeszło**: liczniki, niezmienność stanów/wartości
między ramionami, brak etykiet w body, wieloetykietowość, wymagane reprezentacje,
partycja drzewa, błędny routing/tie, kontrola nazw plików, hash, round-trip,
CLI i brak sieci. Dodatkowo **208/208** body przyjęło `validate_request`
z dostarczonego Jev Lab (`protocol.py` SHA-256
`de0b83da481f14aeb7a4073438f1515c582ebb7615e7ce02a22a70478489addb`).
To zgodność z lokalnym kontraktem, nie weryfikacja działającego endpointu.

Nie ma automatycznej analizy rzeczywistych odpowiedzi w tym przyroście; zasady
analizy zapisano przed wynikami. Nie ma modelu klasyfikacji, parsera struktury,
pełnego klienta inferencji ani integracji natywnej. Jakość rubryk i etykiet
pozostaje do niezależnej kontroli. Żaden współczynnik produkcyjny nie został zmieniony.
