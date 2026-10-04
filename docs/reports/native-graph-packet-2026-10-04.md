# Wątek 4 — natywny GraphPacket, 2026-10-04

Gałąź: `gpt/native-graph-packet-2026-10-04`. Baza: `30ad7d3` — przyjęty W2 i R39–R41.
**Status: GOTOWY do wspólnego odbioru 3/4.** Kanoniczny eksport, aliasy, nested i pełny CTest PASS.
Rebase, checkpoint i poprawki formatu wypchnięte jako `2a7ad39`, `932a3b4` i `3fe2723`.
Poprzedni kompletny stan wraz z obszernym raportem i negatywnymi reprodukcjami:
`archive/2026-10-04/native-graph-packet-before-usage-intake` (`1377e20`).
Nie zmieniono main, STATE, README, UI, profili ani kodu innych wątków.

## Zrobione

Natywna algebra pakietów (diff/apply/inwersja/pełna historia), kompilator
`loom.graph_reply/1,/2`, adresowanie fragmentów i API `loom_packet` / `POST /api/packet`.
Zapis korzysta ze sklepu PR9, jego CAS i pokwitowań. Weryfikuje całą historię
przed zapisem; replay zachowuje stare pokwitowania. Opcjonalne szacowanie zużycia
używa przyjętej polityki W2. Zapis struktury nie ustanawia prawdziwości treści.
Opis API: `loom/src/packet/README.md`.

Na aktualnej bazie sprawdzono oryginalny eksport W3, bez zmiany jego bajtów.
Pakiet i pokwitowanie są identyczne z dowodem W3; natywny zapis, restart,
odczyt, replay i ponowne przyjęcie przechodzą.

**Jeden kanoniczny kontrakt:** `loom/src/packet/METHOD_GRAPH.md`.
**Jeden kanoniczny artefakt, ten sam co W3:**
`docs/reports/chat-selector-2026-10-04-evidence/golden-consumer/input.json`
z `32e2381`; SHA-256
`8db8175c3b70c3947711ddf5daee5073b99bc5a5114ce0075e06e51932cec54e`.
Pokwitowanie: `gpr_f4bb8b56d667eaa5defa539be4412a3759a521083bd4322aa7fb31b3fdadc49a`.
W3 już wskazuje ten kontrakt w swoim raporcie. Wszystkie 36 hashów źródeł i testów
z jego dowodu odpowiadają przypiętemu `03c670c`.

Zgodność aliasów i zagnieżdżeń oparto na rzeczywistym `MethodRegistry` W3
`03c670caa6ee3d8ac2c478186548114f8e83927f`, jego regresjach oraz trzech nowych
przebiegach prepare/compile/bind/native accept/restart/replay. Model żądany
pozostaje niezmieniony; obserwowany model wynika z pochodzenia wyników.
Nieznany deskryptor jest null; nazwa aliasu nie dowodzi równoważności modeli.
Sprawdzane są także dokładne bajty i hashe żądania oraz model w żądaniu.
Kombinacje to uporządkowany DAG z aktywnymi krawędziami, powtórzeniami i wagami
ze znakiem. Efektywne wersje powstają od liścia, zmieniając tylko zużytą pozycję;
oryginalna ścieżka i rodzeństwo pozostają. Definicje i hashe są niezmienne,
adnotacje mogą się zmieniać z zachowaniem historii.

## Liczby przed i po

| Sprawdzenie | Przed | Po |
|---|---:|---:|
| Kanoniczny rzeczywisty eksport W3 na świeżej bibliotece W4 | PASS | PASS; 19 bytów / 30 twierdzeń / 17 źródeł / 3 wyniki |
| Alias / nested / nested+alias — stary konsument versus poprawiony | 0/3 | 3/3 |
| Rzeczywisty rejestr W3 i natywny zapis wariantów | brak wspólnego dowodu tych wariantów | 3/3; nested: 4 ścieżki, waga −24, 2 efektywne wersje |
| Zwykłe przypadki zestawu GraphPacketStore | 26 | **29/29 PASS**, 201,56 s; wpis CTest 201,73 s |
| Pełny CTest | historyczne 110/110 na `7282437` | **110/110 PASS**, 437,69 s; WERROR + vendored SQLite |

Eksporty wariantów mają odpowiednio 19/30/18, 22/35/22 i 22/35/23
bytów/twierdzeń/źródeł; każdy ma 3 wyniki. Zamrożone eksporty włączono do zwykłych
testów wraz z sześcioma negatywnymi przypadkami aliasu/zagnieżdżeń oraz regresją
adnotacji i zmienionego/przywróconego promptu. Każdy negatywny pakiet przechodzi
natywną walidację przed semantycznym odrzuceniem. Nie zmieniono progów ani timeoutów.

Dowody: `loom/src/packet/tests/evidence/2026-10-04-shared-intake/`.
Reprodukcja: `loom/src/packet/tests/reproduce_method_registry_variants.py`.
Pełne eksporty są zachowane bezstratnie, również jako fixture gzip dla CTest;
manifest zapisuje hashe nieskompresowanych bajtów.

Zero płatnych wywołań. Dodatkowe warianty wykonują rzeczywisty rejestr oraz lokalną
projekcję zapisanej odpowiedzi: **0 wywołań transportu**. Oryginalny dowód W3
niezależnie obejmuje jego kontrolowany fake-provider. Sam konsument nadal uczciwie
zapisuje `producer_execution_verified:false`; nie nadaje metadanym statusu dowodu
wykonania. Zachowano nieudaną konfigurację bez ścieżki Ninja, pierwszy błąd harnessu
(capture przed dopisaniem hasha żądania) i błąd helpera bytes/str; poprawki nie
zmieniają asercji ani kryteriów odbioru.

## Czego nie robiono

Nie wykonywano płatnych badań, zmian UI ani implementacji rejestru/czatu W3.
Nie tworzono drugiej polityki zużycia, sklepu grafu ani tabeli równoważności modeli.
Dane metod i presety produkcyjne pozostają zadaniem właścicieli packa/profili.
Wcześniejsza poprawka okna KB zachowuje domyślny preset 12 i dopuszcza całe
nieujemne wartości reprezentowalne przez natywny typ; reszta inwentarza KB
wymaga osobnego odbioru. Historyczne dowody pozostają przypięte do swoich baz.

## Do wątku 3

Wspólny format: wyłącznie `loom/src/packet/METHOD_GRAPH.md`; kanoniczny artefakt
wyłącznie wskazany wyżej eksport `32e2381`. Warianty są dodatkowymi regresjami,
nie konkurencyjnym goldenem. Alias i nested przechodzą rzeczywisty rejestr W3
oraz natywny konsument W4; brak zależności blokującej po stronie formatu.
Zachowaj rozdział requested/observed i oryginalnych/efektywnych kombinacji.
Nie stempluj domyślnego hasha na wynikach z innymi parametrami. Cztery tryby
odpowiedzi, żądania dostawców i zachowanie tekstu po błędzie schematu pozostają
w twoim zakresie; API fragmentów i kompilator `/2` są dostępne.

## Do wątku 1 i 7

Prompt/przepis/parametry/preset to wersjonowane byty z dokładnym capture i hashem.
Wynik ma rzeczywiste krawędzie do przebiegu i wersji metody. Oceny metod/modeli
są twierdzeniami z dowodami i datą, nie pomiarem wynikającym z samego zapisu.

## Do wątku 2

Polityka jest już na main i w świeżym buildzie tej gałęzi. Packet korzysta z jej
admission/reservation/settlement; nie wprowadza drugiego strażnika. Niezależne
zamknięcie P1 replay jest w raporcie W9 i historycznych dowodach W4.

## Do wątku 8

CI potrzebuje istniejących `loom/tools/contracts/requirements.txt` dla ścisłej
walidacji JSON Schema i dat. Inherited `unit.test_catalog_scale` wybiera 0 przypadków;
nie wyciszono ani nie usunięto tego wpisu w tej gałęzi.

## Do wątku 10 i 12

Przegląd/edycja metod używa zwykłych bytów, twierdzeń i pokwitowań grafu.
Format requested/observed i wersje zagnieżdżonych kombinacji są opisane w kontrakcie.
Onboarding R39–R40 pozostaje poza W4. Odbiór 3/4 poprzedza wpięcie R41 w UI.

## Do wątku 11

Pozostały odziedziczone polityki KB: `kb/candidates.cpp` maksimum 1000,
`kb/store.cpp` zastępowanie unlimited przez 1 000 000 oraz sufity w `kb/pack.cpp`.
Wymagają koordynacji publicznego knowledge API i loadera danych/profili; nie
przenoszono ich do prywatnego loadera W4 ani cudzych plików danych.

## Do wątku 9

Format i eksport wspólnej bramki 3/4 są zgodne oraz przechodzą natywny zapis.
**W4 gotowy do wspólnego odbioru 3/4: pełny CTest aktualnej bazy 110/110 PASS.**
Weryfikacja źródeł przed i po całym przebiegu potwierdza niezmieniony kod.
Odziedziczony test lineage wykonał 2/2 przypadki po pobraniu dokładnie 166
potrzebnych publicznych obiektów Git; nie zmieniono ani nie pominięto testu.
Integrator powinien użyć tego samego kontraktu i tego samego goldenego artefaktu,
przejrzeć osobny dowód rzeczywistego wykonania W3, potem puścić pełny CTest i build
web na połączonych gałęziach i wykonać fast-forward. Nie cofaj zmian innych wątków.
