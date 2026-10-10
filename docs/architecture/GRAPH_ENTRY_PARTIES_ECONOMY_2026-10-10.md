# Wejście do grafu, podmioty, ustawienia w zakresach, ekonomia zasobów — 2026-10-10

Uogólnienie wymagań właściciela R43–R50
([OWNER_REQUIREMENTS](OWNER_REQUIREMENTS_2026-09-26.md)). Właściciel improwizował
i zaznaczył, że to jedna z wielu możliwych ścieżek. Ten dokument jest więc
punktem wyjścia, a nie zamkniętą ontologią. Wszystkie listy poniżej są
**danymi**: w kodzie mieszka mechanizm, a listy trafiają do packów z warstwami
R40, które użytkownik może nadpisać, wyłączyć albo wykluczyć.

Słownik: *węzeł* = dowolny byt w grafie; *pack* = plik danych w `loom/data`;
*warstwa* = mechanizm R40 (`DefaultLayers`); *zakres* = poziom, na którym
ustawienie może mieć wartość (§6).

---

> **Status (2026-10-10, po uwadze właściciela, R53):** wszystkie listy w tym
> dokumencie są **przykładami kalibracyjnymi**, nie zamkniętymi zbiorami, a ich
> liczebność nie jest faktem projektowym. Silnik ma generować przestrzeń z osi
> (§11) i rozszerzać ją wybranymi modelami jako metodę (R48/R53); wynik wchodzi
> do grafu jako propozycje.

## 1. Bramki: każda blokada ma inicjatora (R43)

**Bramka** to każda operacja, która odmawia, zawęża, obcina, opóźnia albo ukrywa.
Każda bramka niesie w wyniku pole `initiator` oraz `reason`, a w
interfejsie widać, kto ją ustawił i jak ją zmienić.

Dopuszczalni inicjatorzy:

| Inicjator | Kiedy | Przykład |
|---|---|---|
| `user` (dowolny zakres §6: organizacja, osoba, projekt, rozmowa…) | ustawienie, zgoda, jawny wybór | „nie wysyłaj danych o zdrowiu do żadnego dostawcy” |
| `invariant:integrity` | operacja zniszczyłaby albo sfałszowała dane | niezgodny hash źródła, przerwana transakcja |
| `invariant:security` | wyjście poza dozwolony obszar, ujawnienie sekretu | ścieżka poza `local_roots`, klucz w logu |
| `invariant:external` | ograniczenie, którego nie kontrolujemy | limit API dostawcy, brak uprawnień systemu |
| `invariant:capability` | funkcji nie ma | brak mostu historii źródła do kontekstu (P4b) |
| `owner_rule` | reguła ustanowiona przez właściciela dla wszystkich | potwierdzenie wzrostu zużycia ×10 |

Zasady:
1. Brak zdolności (`capability`) **degraduje** operację i mówi, czego brakuje.
   Nie blokuje reszty: można wysłać wiadomość bez historii źródła, o ile
   użytkownik wie, że jej nie będzie.
2. Bez inicjatora z tej listy domyślnie **informujemy i wykonujemy**.
3. Ustawienie, które blokuje, jest zwykłym ustawieniem z §6: ma zakres,
   regułę konfliktu i wyjaśnienie.

Inwentarz znanych bramek (stan gałęzi data-graph-engine 18a8428):

| Bramka | Inicjator dziś | Decyzja |
|---|---|---|
| ChatView: brak wysyłki, gdy historia źródła niedostępna | brak | **ostrzeżenie + wysyłka**; blokada tylko jako ustawienie użytkownika |
| ChatView: brak wysyłki, gdy widok rozmowy się nie wczytał | brak | ostrzeżenie + wysyłka |
| ChatView: wysyłka wyłączona na czas trwającego odczytu źródła | `invariant:integrity` (sekundy) | zostaje, z wyjaśnieniem |
| ChatView: wysyłka z widoku gałęzi historycznej | brak | do zmiany: „rozgałęź stąd” zamiast odmowy (osobny przyrost) |
| Import: jeden nierozpoznany element psuje cały import | brak | **adnotacja w grafie** (§2) |
| Import ZIP: błąd jednego członka → pominięty | brak | członek jako węzeł nierozpoznany (§2) |
| Import: ten sam plik i ta sama wersja parsera → zwrot poprzedniego importu | R50 (identyczny wynik) | zostaje; `force` tworzy kopię; komunikat mówi o wyborze |
| Worker: nieudane i przerwane analizy tylko ręcznie, pojedynczo | brak | **tryb jako ustawienie** + masowe ponowienie |
| Worker: wybór trybu wsadowego automatycznie po progu | brak (decyzja kosztowa) | do objęcia strażnikiem ×10 (CH-005, osobny przyrost) |
| LLM semantyczny wyłącza się po 5 błędach z rzędu | `invariant:external` (zepsuty dostawca) | zostaje; próg jako ustawienie, stan widoczny |
| KB: zmiana hasha jednego pliku D/E w `data_domains.pack` zatrzymuje cały KB | `invariant:integrity` | do zmiany: raport niezgodnej domeny zamiast awarii całości |
| Badania 7: płatnik wymaga dokładnej zgodności zużycia z rejestrem | brak (ochrona rachunku) | do zmiany: „zapytaj” z jawnym potwierdzeniem różnicy (możliwy wyciek klucza = `security`, więc pytamy, nie odmawiamy na zawsze) |
| Prywatność: domyślny preset bez dostawców dla danych osobowych | `user` przez onboarding R39 | zostaje, to zgoda użytkownika |
| Strażnik ×10 | `owner_rule` | zostaje; brakuje go w zwykłym czacie i trybie wsadowym (CH-004/005) |

---

## 2. Rozpoznanie: wszystko wchodzi, rozpoznanie jest adnotacją (R44)

Każdy przyjęty fragment danych dostaje status rozpoznania:

| Status | Znaczenie |
|---|---|
| `recognized` | adapter i jego wersja odwzorowały całość |
| `partial` | część pól odwzorowana, reszta zachowana dosłownie |
| `unrecognized` | struktura nieznana; bajty zachowane dosłownie |
| `opaque` | nie da się parsować (binarne, uszkodzone); zachowany hash i bajty |
| `excluded_by_user` | użytkownik wyłączył; dane zostają, dopóki ich nie usunie |

Rekord nierozpoznany ma: źródło (`source_id`, członek archiwum, wskaźnik JSON
albo zakres bajtów), dosłowną treść albo odnośnik do niej w BlobStore,
rozpoznawacz i jego wersję, powód i historię prób. Gdy pojawi się nowy adapter,
można rozpoznać go ponownie, bo stan pochodny musi dać się odbudować.

Gdzie to obowiązuje:
- import JSON, JSONL, HTML i MHT, czyli elementy, których żaden handler nie zna;
- członkowie ZIP;
- eksporty dostawców (już robione przez węzły `export:member`);
- zasoby zewnętrzne D (YAML, XML, CSV, nieznane formaty);
- wyniki OCR i ASR;
- odpowiedzi modeli niezgodne ze schematem (surowa odpowiedź zostaje, status
  `unvalidated`);
- nierozwiązane odnośniki do załączników i rodziców w eksportach.

W grafie te węzły tworzą obszar „Dane i korpusy” (§3), podzielony według
statusu rozpoznania.

---

## 3. Widok wejściowy: co zwraca zapytanie bez parametrów (R45)

Graf nie jest drzewem, więc nie ma korzenia. Ma za to **widok w kontekście
zerowym**: zestaw obszarów, z których każdy jest węzłem z licznikami i
odnośnikami do żywych rejestrów. Lista obszarów i ich kolejność to pack
`loom/data/graph/entry.pack`, więc użytkownik może je przestawiać, ukrywać i
dodawać.

| Obszar | Co zawiera | Skąd dane (dziś) |
|---|---|---|
| **Samomodel** | cele, filozofia i zasady właściciela (R1–R50), zdolności, znane ograniczenia, stan i historia rozwoju | `philosophy/*`, `profiles/self.json`, rejestr metod, operacje API |
| **Technologie i adaptery** | formaty importu, dostawcy modeli, OCR i ASR, magazyny (DB, BlobStore, packet store) | `runtime/import_formats.pack`, `runtime/net.pack`, `runtime/model.pack` |
| **Interfejsy** | C ABI, HTTP, CLI, widoki web, Android | lista operacji eksportowanych |
| **Profile i ustawienia** | domeny profili runtime, wpisy warstw R40, łańcuch zakresów i reguły konfliktów | `RuntimeProfile::domains()`, `profiles/user.pack`, `graph/scopes.pack` |
| **Podmioty** | osoby, role w kontekście, grupy, organizacje, kolektywy, agenci AI | profil onboardingu, encje `actor`, `graph/parties.pack` |
| **Kontekst i projekty** | projekty, cele, decyzje, rozmowy | `profiles/self.json`, KB, DB |
| **Metody** | metody, wersje, presety, dowody z badań, eksperymenty | `MethodRegistry`, wyniki badań |
| **Dane i korpusy** | źródła, rozmowy i wiadomości według statusu, zasoby, statusy rozpoznania, duplikaty | DB, `loom_sources`, katalog |
| **Wiedza** | encje według klasy, twierdzenia według predykatu, obserwacje | KnowledgeStore |
| **Ontologia** | klasy, pakiety właściwości, relacje | `schema/types.json`, `graph/classes.pack`, `facets/*` |
| **Pochodzenie i historia** | przebiegi metod, krawędzie `produced_by`, wersje | ślady przebiegów, `loom_provenance` |
| **Ekonomia zasobów** | zużycie, budżety, rozmiary, duplikaty, cache | rejestr W2, raport ekonomii |
| **Prywatność i zgody** | reguły prywatności, zgody, wykluczenia | warstwy R40, onboarding |
| **Perspektywy i widoki** | zapisane soczewki grafu | `profiles/graph_perspective.pack` |

Widok jest odpowiedzią „co jest i ile”, a nie zrzutem całego grafu. Każdy obszar
ma `expand`, czyli zapytanie, które pokaże jego zawartość. Każdy licznik ma
`known: false`, gdy rejestr nie jest dostępny; nie zmyślamy zera.

---

## 4. Podmioty zamiast „użytkownika” (R46)

Użytkownik nie jest centrum. Jest węzłem sieci typu **podmiot**, najdokładniej
opisanym i z wysoką wagą w wielu decyzjach.

Poziomy podmiotów:

| Poziom | Przykłady |
|---|---|
| osoba | właściciel, członek rodziny |
| konto / persona | konto w systemie, pseudonim, ta sama osoba w innej roli publicznej |
| rola w kontekście | „pracownik firmy X”, „członek projektu Y”, „rodzic w rodzinie Z” |
| grupa | zespół, rodzina, gospodarstwo domowe |
| grupa grup | zespoły pracujące nad gałęzią projektu, dział |
| organizacja | firma, fundacja, szkoła, urząd |
| kolektyw doraźny | rodzina ważąca decyzję życiową, komisja, panel recenzentów |
| społeczność | społeczność open-source, forum |
| agent AI | asystent, wątek GPT, sesja Claude, subagent |
| instalacja | instancja aplikacji działająca w czyimś imieniu |
| strona zewnętrzna | osoby i organizacje wspomniane w danych, niebędące użytkownikami |

Relacje między podmiotami:
- `member_of`;
- `acts_as` (osoba → rola);
- `role_in` (rola → kontekst);
- `represents`, `delegates_to`;
- `decides_with` (z wagami);
- `has_authority_over` (zakres ustawień, §6);
- `trusts` (z wagą dla danego rodzaju decyzji).

Granica z kontekstem przebiega przez **rolę w kontekście**: „osoba jako
pracownik nad projektem” to już kontekst, nie sama osoba.

---

## 5. Typy węzłów: jak jest dziś i dokąd idziemy (pytanie właściciela, R46)

**Dziś typy to etykiety, nie węzły:**
- **Encja wiedzy (`Entity.kind`):** jedna etykieta z otwartej listy w danych
  (`schema/types.json`, poziomy 0–2). Nie ma dziedziczenia ani wielu typów
  naraz. `parent` to jeden główny rodzic, a `attrs` to wolny JSON. Twierdzenia
  mają predykaty z listy relacji w danych.
- **Projekty mają już zaczątek pakietów właściwości:** rodzaj projektu, typ
  artefaktu i **facety** (`loom/data/facets/*`). To jest dokładnie Twój „pakiet
  właściwości”, tylko na razie wyłącznie dla projektów.
- **Graf zgodny z Pythonem (`nodes.kind`):** dowolny napis bez rejestru.
- **Profil użytkownika z onboardingu:** pola tożsamość, praca, komunikacja,
  wiedza, prywatność. To najbliższy odpowiednik klasy „człowiek”, ale związany
  z kreatorem, a nie z klasą.

**Dokąd:** klasy są węzłami.
- Węzeł `class` ma tożsamość równą dzisiejszej etykiecie. Nie migrujemy
  identyfikatorów encji.
- `instance_of` łączy encję z klasą i może być wielokrotne.
  `Entity.kind` zostaje klasą główną.
- `subclass_of` łączy klasy; tworzą one DAG z wielokrotnym dziedziczeniem.
- `has_facet` łączy klasę z **pakietem właściwości**. Pakiet ma sloty: klucz,
  typ wartości, krotność, kategorię wrażliwości, stan domyślny `unknown` (R39)
  i pochodzenie.
- **Pakiety są wielokrotnego użytku:** „preferencje” pasują do osoby, zespołu,
  organizacji i agenta AI; „obszary” pasują do osoby i do organizacji.
- **Role nie są podklasami.** „Pracownik” nie jest podklasą „człowieka”, bo
  przestaje się nim być, nie przestając istnieć. Role to osobne węzły:
  osoba —`acts_as`→ rola —`role_in`→ kontekst.
- Nieznany typ jest dozwolony i dostaje status `unclassified` (§2).

Klasa **Osoba** (pakiet „człowiek”), z każdym slotem ze stanami
`unknown` / `declined` / `never` z R39:

| Pakiet | Sloty (przykłady) |
|---|---|
| tożsamość | jak się zwracać, języki, zaimki |
| komunikacja | styl, długość, język, potrzeby uwagi (ADHD), formaty |
| preferencje | według dziedzin: narzędzia, modele, interfejs |
| wartości i filozofie | światopogląd, zasady (np. filozofia kodowania właściciela) |
| zainteresowania | tematy, dziedziny |
| obszary geograficzne | miejsca, strefy czasowe |
| obszary zawodowe | dziedziny, umiejętności, role, pracodawcy |
| obszary teleologiczne | cele, motywacje, priorytety |
| wiedza i kompetencje | poziomy ekspertyzy, co pamiętać, czego unikać |
| zasoby i ograniczenia | czas, budżet, urządzenia |
| relacje | z innymi podmiotami (§4) |
| sprawy wrażliwe | zdrowie, sprawy osobiste; domyślnie tylko jawnie podane |
| prywatność | reguły dla każdej kategorii (R39) |

Wartość slotu może zależeć od kontekstu (praca a dom), więc sloty korzystają z
zakresów z §6.

---

## 6. Ustawienia w zakresach z regułami konfliktu (R47)

R40 ma dziś dwie warstwy: wbudowany pack i stan użytkownika. Uogólnienie to
**łańcuch zakresów**, od najogólniejszego. Każdy zakres jest węzłem i podmiotem
albo kontekstem:

```
silnik (niezmienniki, R42) → preset aplikacji → instalacja → organizacja →
grupa grup → zespół → osoba → rola w kontekście → projekt (gałąź) →
rozmowa → tura / pojedyncze wywołanie
```

Nie każde ustawienie używa wszystkich zakresów. Łańcuch jest danymi
(`graph/scopes.pack`).

Reguła konfliktu jest przypisana do każdego ustawienia, jest danymi i można ją
nadpisać:

| Reguła | Działanie | Typowe użycie |
|---|---|---|
| `most_specific_wins` | wygrywa najbardziej szczegółowy zakres | preferencje (domyślna) |
| `locked_by` | wyższy zakres przypina wartość; wymaga inicjatora z uprawnieniem (R43), widoczne w wyjaśnieniu | polityka organizacji |
| `merge` | obiekty scalane głęboko; listy jako suma albo dopisanie w kolejności zakresów | słowniki, aliasy |
| `intersection` | część wspólna | dozwoleni dostawcy, gdy ograniczają organizacja i osoba |
| `min` / `max` | liczbowo | budżety ustawione przez kilka podmiotów |
| `weighted_vote` | głosowanie z wagami podmiotów | kolektyw (rodzina ważąca decyzję) |
| `ask` | konflikt trafia do użytkownika, decyzja zostaje zapisana jako nadpisanie | sprzeczne zgody |
| `by_context` | wybór według predykatu kontekstu | praca a dom |
| `first_defined` | pierwsza wartość w podanym porządku | zgodność wstecz |

Każda wartość efektywna ma wyjaśnienie: które zakresy się złożyły, jaka reguła
zadziałała, kto był inicjatorem i kiedy. Operacje R40 (override, disable,
exclude, reenable) działają na każdym zakresie.

Kategorie ustawień, które to obejmie:
- model i rozumowanie;
- budżety i strażnik ×10;
- zakres i szczegółowość kontekstu;
- kolejność kontekstu (§8);
- język i styl;
- prywatność i dostawcy;
- import (nierozpoznane, duplikaty);
- ponawianie analiz;
- ekonomia (§9);
- presety metod (§7);
- widoki.

---

## 7. Metody obok ustawień: presety z badań i eksperymenty pod ręką (R48)

Łańcuch: **metoda** → **wersja** (hash promptu i kodu) → **preset** (model,
wersja promptu, parametry i podejście, dostrojone do zastosowania) →
**dowody** (przebiegi, zapytania API, odpowiedzi, metryki, koszty, daty) →
**rekomendacja** dla danego zastosowania i profilu podmiotu, z uzasadnieniem.
Wszystko są węzłami (R41).

Zastosowania, czyli presety do zbudowania:
- triaż importu: które rozmowy są ważne;
- ekstrakcja encji i twierdzeń;
- ocena relewancji: **Jev uogólniony** = (twierdzenie, węzeł, rodzaj krawędzi) →
  poziom relewancji, z presetem dla każdego rodzaju krawędzi;
- uzupełnianie grafu przez model frontier;
- odkrywanie wzorców;
- odpowiedź jako graf;
- streszczanie do kontekstu;
- wyszukiwanie kontekstu: na prawdziwych archiwach najlepiej działały n-gramy
  znakowe 3–5 (~0,60–0,65 trafności dowodów), BM25 dało ~0,44, a struktura
  0,12–0,31 (wątek 7, 9.10);
- propozycje scalania i deduplikacji;
- wywiad onboardingowy;
- klasyfikacja prywatności.

Eksploracja wielu ścieżek ma trzy tryby, wszystkie z oceną wielokryterialną:
- **automatyczny:** wiązka ścieżek z oceną;
- **półautomatyczny:** system proponuje N ścieżek, użytkownik wybiera;
- **ręczny:** użytkownik prowadzi, system adnotuje.

**Eksperyment N-torowy przy imporcie:**
1. Próbka: katalog wybiera k rozmów warstwowo.
2. Tory: N presetów, np. 5 promptów.
3. Wycena przez podgląd W2 (×10 → potwierdzenie).
4. Wykonanie w budżecie użytkownika.
5. Ocena według kryteriów:
   - poprawność schematu;
   - ugruntowanie (cytaty sprawdzalne w źródle);
   - zgodność między torami;
   - pokrycie (ile źródła odwzorowane, ile `unknown`);
   - koszt i czas;
   - stabilność przy powtórzeniu;
   - opcjonalnie ocena użytkownika.
6. Porównanie i rekomendowany preset, który użytkownik przyjmuje na wybranym
   zakresie z §6.

Każdy przebieg jest śladem metody z krawędzią `produced_by`.

---

## 8. Kolejność kontekstu: stałe najpierw, pytanie na końcu (R49)

Propozycja właściciela jest trafna. Pamięć podręczna promptów działa na
**prefiksie**, więc bloki ustawia się od najstabilniejszego do najbardziej
zmiennego:

| Klasa | Blok | Zmienia się |
|---|---|---|
| S0 | instrukcje aplikacji, filozofia | prawie nigdy |
| S1 | fakty o podstawowych encjach: samomodel, potrzebne pakiety podmiotu, słownik | rzadko |
| S2 | kontekst projektu: podsumowanie, decyzje, architektura | na projekt |
| S3 | historia rozmowy (tylko dopisywana, więc jej prefiks jest stały) | co turę dopisanie |
| S4 | szczegółowy kontekst dobrany do pytania, np. kod, do którego odwołuje się funkcja | co pytanie |
| S5 | pytanie | zawsze |

Punkty cache stoją po S1, S2 i S3; Anthropic pozwala na cztery. Historii
właściciel nie wymienił. Stoi przed S4, bo rośnie tylko na końcu. Kontekst
dobrany do pytania tuż przed pytaniem pomaga też jakości, bo model najlepiej
korzysta z początku i końca promptu.

**Stan obecny Loom:**
system → pamięć → **kontekst grafu dobrany do bieżącego pytania** → historia →
pytanie. Blok zmienny stoi przed historią, więc prefiks cache urywa się w każdej
turze. Kolejność staje się presetem danych: `legacy` (dzisiejsza, zgodna z
Pythonem) albo `stable_first`. Wybór to ustawienie z §6; skutek warto zmierzyć
eksperymentem z §7 (koszt i jakość).

---

## 9. Ekonomia zasobów: duplikaty dozwolone, zgłaszane, z propozycją (R50)

Zasada: unikamy marnotrawstwa, którego łatwo uniknąć.
- **Nigdy nie zabraniamy.** W prostym przepływie mówimy, że powstanie duplikat,
  i proponujemy przypięcie niezmiennych danych do kilku węzłów.
- **Analiza jest opcjonalna** (`economy.mode`: `off` | `report` | `propose` |
  `auto` dla wybranych klas).
- **Progi to presety:** minimalny rozmiar bloku, minimalna częstość.

Klasy marnotrawstwa:
1. Ponowny import tego samego źródła; ta sama rozmowa w dwóch eksportach
   (OpenAI i Anthropic, kopie na Drive).
2. Wynik pochodny liczony ponownie przy tej samej wersji metody i tym samym
   wejściu.
3. Powtarzalne bloki w metadanych, np. ~0,7 KB wartości profilu kopiowane do
   każdej próby analizy.
4. Te same prefiksy promptów w każdym wywołaniu (rozwiązanie z §8).
5. Narzut reprezentacji: forma grafowa zajmuje ~6× więcej niż tekst.
   Przechowujemy zwięźle, widok generujemy.
6. Dowody i ZIP-y powielające pliki repo.
7. Indeksy deterministyczne (FTS, embeddingi): przechowywać czy liczyć od nowa.
8. Wersje i gałęzie wiadomości o wspólnych prefiksach.

Działania, każde z kosztem i korzyścią:

| Działanie | Kiedy się opłaca |
|---|---|
| **przypięcie / link**: niezmienny blob adresowany treścią, wiele odniesień | wiele kopii, duży blok |
| **cache**: wynik kluczowany wersją metody, wejściem i parametrami | drogie, powtarzalne obliczenie |
| **generator / przepis**: zapis, *jak* odtworzyć (metoda, wersja, wejścia, parametry, koszt), bez materializacji | tanie i deterministyczne odtworzenie, rzadki odczyt |
| **kompresja**: zwykła kompresja zimnych danych | duże, rzadko czytane |
| **nic**: duplikat zostaje celowo | niezależność, pochodzenie, prostota |

Model decyzji:
- korzyść = (liczba kopii − 1) × rozmiar × koszt przechowania + zaoszczędzone
  tokeny × cena;
- koszt = pośredniość, obliczenie deduplikacji, utrata niezależności, oraz koszt
  odtworzenia × spodziewana liczba odczytów.

**Wolfram i generatory.** „Opis krótszy niż dane” to złożoność Kołmogorowa
(MDL). Nieredukowalność obliczeniowa Wolframa jest drugą stroną tego medalu:
dla wielu procesów nie ma skrótu, więc najtańszym sposobem uzyskania wyniku
jest przeliczenie całości. Generator nie zawsze jest więc tańszy od danych;
decyduje bilans pamięć–czas. Odpowiedzi modeli nie są deterministyczne, więc
ich jedynym minimalnym opisem jest sama odpowiedź; zostają. Dane deterministyczne
(indeksy, ekstrakcja regułowa, graf z zapisanej analizy) można oddać przepisowi.
To ta sama idea co ślady metod z `produced_by` i zasada AGENTS.md: „stan
pochodny musi dać się odbudować”.

---

## 10. Magazyn, szyfrowanie, bezpieczeństwo (R51)

Osobny obszar widoku wejściowego, widoczny od razu: gdzie leżą dane (serwer
aplikacji, chmury użytkownika, lokalnie; dowolna kombinacja, per kategoria), kto
ma dostęp, czym zaszyfrowane, kto trzyma klucze, kiedy rotowane. Kryptografia
odporna kwantowo: ML-KEM + ML-DSA hybrydowo z X25519, AES-256-GCM dla danych,
Argon2id dla haseł; RSA-OAEP z `credential_handoff_v2` do wymiany. Wszystko jako
ustawienia z zakresami (§6), preset bezpieczny i nieblokujący. Pierwszy krok:
inwentarz, gdzie dziś są dane i klucze (DB, BlobStore, secrets.json, eksporty,
packet store, logi).

## 11. Osie zamiast list (R53)

Przykłady z §3–§6 są punktami kalibracyjnymi. Generator przestrzeni:
- **Podmioty**: skład (jeden / wielu / wielu z wielu) × trwałość (stały / doraźny)
  × forma więzi (formalna / rodzinna / projektowa / przygodna) × natura (człowiek /
  AI / instytucja) × kontekst działania. „Rodzina ważąca decyzję” = wielu, doraźny,
  rodzinna, kontekst decyzji. Dodatkowo: podmiot *o którym* graf wie ≠ podmiot
  *który działa i płaci* w tej chwili.
- **Obszary wejścia**: o czym (aplikacja / świat / podmiot) × jaki byt (dane /
  metoda / ustawienie / wiedza / zasób) × czas (stan / historia / plan).
- **Pakiety właściwości**: każdy slot ma dodatkowo oś czasu (teraz / zwykle /
  kiedyś, z historią), pewność i źródło (podane / wywnioskowane / potwierdzone /
  odrzucone), stan podmiotu w chwili podania (istotne przy ADHD) oraz wrażliwość.
  Cele (teleologia) to osobna oś, nie kategoria obok geografii i zawodu.
- **Ustawienia**: dziedziczenie dotyczy klas; wartości ustawień nie dziedziczą,
  tylko rozstrzygają się przez zakresy i reguły konfliktu (§6) — dwa mechanizmy,
  nie jeden.
- **Samomodel** obejmuje także modele aplikacji: co umieją, ile kosztują, gdzie
  się mylą (R37) — spina się z metodami (§7).
- **Decyzja jako węzeł**: kto, w jakiej roli, z jakimi wagami, na jakich
  przesłankach; decyzja rodzinna to instancja.

Rozszerzanie tej przestrzeni to metoda N-torowa (§7): ten sam zalążek do kilku
modeli, porównanie zgodności/nowości/sprzeczności, propozycje do grafu.

## 12. Zalążek zerowy (R52)

Graf nie zaczyna pusty: samomodel, ontologia, metody z presetami, warstwy domyślne
i węzeł podmiotu ze slotami `unknown`. Onboarding wypełnia; wszystko można
wyłączyć albo usunąć.

## 13. Implementacja w tym przyroście i dalsze kroki

Sekcja uzupełniana przy integracji, patrz `docs/reports/INDEX.md`.
