# Strażnik kontekstu i środowiska działania (2026-10-14)

Nadrzędny względem planów wykonawczych; podrzędny wobec `GRAF_HUB_KOD_SILNIK_2026-10-13.md` i R1–R56.

## 0. Słowa właściciela (2026-10-14, wiernie)

> „może bym dał ogólnie moduł strażnika czystości kontekstu w rozmowie, w którym między innymi by robił te kompresje, jeszcze jakieś inne zadania do niego miałem”

> „kompresja kilku kolejek wiadomości doprecyzowujących do postaci pojedynczej kolejki dokładnie wytłumaczonej; budowanie, zgadywanie, hipotetyzowanie, optymalizacja Flow i preferencji usera na podstawie rozmowy, przykładowo korekta kursu rozmowy/zadania przez użytkownika najpierw zapisana jako wystąpienie (oczywiście według preferencji jak wszystko). hipotezy zakresu ważności, ważone na wielu danych (właśnie do hipotez chyba najwięcej krawędzi będzie szło), porównanie z historią poprawek (na przykład jak użytkownik dwa razy poprawia spokojnie później się już zaczyna wkurwiać to wiadomo że to zasada na stałe ma być), implicite oraz explicite podane wyjątki, itd.”

> „trzeba obsłużyć zarówno wszystkie konta użytkownika z uwierzytelnianiem na wszystkie sposoby, wszystkie lokalne zasoby użytkownika poprzez nas zarządzana maszyna od dowolnie wybranego dostawcy”

> „strażnik czystości to bardziej go widzę jako takiego sekretarza, stenografa, który równolegle do bezpośredniej listy wiadomości buduje jej najbardziej oszczędną i precyzyjną reprezentację. kontekst selektor to co innego”

> „pierwszy robi kompresję stratną z niskim progiem istotności - usuwa cichy jest ale skomplikowany szum o dużej entropii. w zależności od ustawień użytkownika może wszelkie dygresje i żarty usuwać, może tylko yyy aaa, powtórzenia, i takie rzeczy usuwać które nic poza wagą w tokenach nie wnoszą, może kaskadowo różne poziomy robić. ale tutaj znów się nakładają funkcjonalności trochę z analizatorem semantycznym”

> „sposób podejścia twojego jest zbyt szczegółowy pamiętaj że podaję tylko przykłady, jak przęsła mostu który ty masz zbudować”

Przykłady właściciela są **przypadkami** mechanizmów poniżej, nie ich zakresem.

## 1. Strażnik kontekstu — jeden mechanizm

**Czym jest (korekta właściciela 2026-10-14):** sekretarz/stenograf — stały proces nad grafem, który **równolegle do surowej listy wiadomości** buduje jej najbardziej oszczędną i precyzyjną *reprezentację* (skompilowane polecenia w miejsce serii doprecyzowań, cykl wystąpienie → hipoteza → zasada dla poprawek i preferencji, znaczniki wątków/segmentów tematycznych) i uczy się z przebiegu rozmów. Zapisuje ją jako węzły grafu z krawędziami do wiadomości źródłowych. **Nigdy nie zmienia surowej listy i nie decyduje, co trafia do modelu.** To robi istniejący selektor kontekstu (metody i wycinki grafu, wyłączalny dla oszczędności, tryb zgodności = prompt systemowy + surowa rozmowa): może wybrać per segment surowe wiadomości, reprezentację strażnika albo mieszankę — jako dane i ustawienie (preset: surowe, bajty żądania bez zmian). Rozmowy wielotematyczne (skoki między 2–3 dużymi tematami, pomijanie wiadomości spoza bieżącego tematu) należą do selektora; strażnik może tylko znaczyć wątki w reprezentacji. Nie jest kodem pod każdy przypadek: jego zadania to **dane** (wpisy w katalogu akcji i zasad), wykonywane tym samym silnikiem akcji co reszta (P9–P18).

**Jeden cykl, wiele zastosowań:**

```
obserwacja (zdarzenie w rozmowie)
   → wystąpienie (węzeł w grafie, ze źródłem)
   → hipoteza (węzeł: „X obowiązuje w zakresie Z”, waga z wielu wystąpień, krawędzie do dowodów i wyjątków)
   → propozycja (w rozmowie albo cicha — wg ustawienia)
   → zasada / przekształcenie kontekstu (nakładka użytkownika, zawsze odwracalna)
```

- **Wystąpienie** — dowolny rodzaj sygnału: poprawka kursu, doprecyzowanie, powtórzenie, odrzucenie propozycji, ton, akceptacja. Rodzaje sygnałów i sposób ich rozpoznania to dane (heurystyka, model, oba — jak w trasowaniu P16).
- **Hipoteza** — twierdzenie o użytkowniku, zadaniu, rozmowie lub stylu, z **zakresem ważności** (wiadomość → rozmowa → rodzaj zadania → osoba → wszędzie) i **wagą** liczoną z wystąpień (świeżość, liczba, siła sygnału, sprzeczności). Wyjątki to osobne węzły z krawędzią `except` — podane wprost albo wywnioskowane.
- **Eskalacja** — narastanie sygnału (kolejna poprawka tego samego, ostrzejszy ton, „mówiłem już”) podnosi wagę i skraca drogę do zasady. Krzywa i progi to dane.
- **Awans** hipotezy do zasady: `pytaj` / `proponuj` / `stosuj` — ustawienie z presetem (R1: decyduje użytkownik).
- **Reprezentacje** (zamiast „przekształceń kontekstu”) — tym samym cyklem: skompilowane polecenie zastępujące serię doprecyzowań (istniejąca specyfikacja zadania), oznaczenie nieaktualnego lub sprzecznego, oznaczenie dygresji i ruchu technicznego. Każda reprezentacja jest węzłem ze śladem, da się ją cofnąć; surowe wiadomości nigdy nie znikają ani nie są zmieniane. Selektor kontekstu decyduje, czy jej użyć.

### Kaskada poziomów stratnych i wspólna warstwa adnotacji (decyzja 2026-10-14)

Reprezentacja strażnika to **kaskada poziomów** na wiadomość lub segment, każdy jako węzeł pochodny powiązany ze źródłem (surowy tekst nietknięty). Poziomy to dane (otwarta lista); presety: **L0** surowy; **L1** bez szumu (wypełniacze „yyy/aaa”, powtórzenia, falstarty i samopoprawki w jednej wypowiedzi, wszystko, co kosztuje tylko tokeny); **L2** bez dygresji, żartów i dopowiedzeń wg ustawień użytkownika (które kategorie się liczą); **L3** skondensowane znaczenie: skompilowane polecenie i decyzje (istniejąca specyfikacja zadania dla serii doprecyzowań). Każdy poziom zapisuje, co odrzucił (kategoria, zakres), więc odrzucenie można obejrzeć i cofnąć.

Nakładanie się z analizatorem semantycznym rozwiązuje **jedna wspólna warstwa adnotacji segmentów** (zakres → kategoria: instrukcja, poprawka, doprecyzowanie, dygresja, żart, wypełniacz, powtórzenie, wątek tematyczny…), produkowana raz — to istniejące adnotacje wiadomość→węzeł z zakresem znaków (`origin` recorded|model|user), a kategoria jest węzłem. Heurystyka daje tanie kategorie (wypełniacze, powtórzenia), analizator (regex/LLM/batch) dodaje kategorie jakości modelu i wątki tematyczne. Czytają ją trzej odbiorcy: analizator (graf), strażnik (poziomy stratne, cykl poprawek) i selektor kontekstu (wybiera poziom na segment, pomija inne wątki). Jeśli analizator już wytworzył adnotacje wiadomości, strażnik je wykorzystuje i nie robi drugiego wywołania modelu; wywołania modelu idą przez `use_model` i strażników (×10, limity wydatków). Strażnik może działać wyłącznie heurystycznie (preset dla L1, koszt zero). Wybór poziomu na segment w selektorze to dane i ustawienie; preset: L0 (bajty żądania bez zmian), tryb zgodności bez zmian.

## 2. Środowiska działania — jeden mechanizm

**Czym jest środowisko:** wszystko, na czym można działać — konto w dowolnym serwisie, maszyna u dowolnego dostawcy, telefon i jego urządzenia, magazyn, przeglądarka, repozytorium, lokalne pliki. W grafie: węzeł klasy z **cechami zdolności** (jak urządzenia R9), dostępem przez **adapter** (jak porty R8/R10), widocznością per zakres (R47).

- **Dostęp** — jeden mechanizm poświadczeń z wymiennymi sposobami (klucz, OAuth, hasło+2FA, sesja przeglądarki, klucz SSH, certyfikat…). Sekrety tylko w sejfie, nigdy w rozmowie (istniejąca zasada); w grafie wyłącznie odniesienie. Każde użycie z pokwitowaniem.
- **Agent** — model, który wybiera akcje na środowiskach. To nie nowy silnik: to krok `użyj modelu` z katalogiem akcji jako narzędziami, a wykonanie idzie przez ten sam interpreter, zasady, potwierdzenia, ×10, limity wydatków i ślad w grafie co akcje z czatu.
- **Maszyna** — środowisko jak każde inne: dostawca maszyn to adapter (lokalny proces, kontener, dowolna chmura); tworzenie, uruchamianie, zatrzymanie to akcje z kosztem w propozycji.
- **Zewnętrzne źródła narzędzi** (np. serwery MCP) to adapter, który dopisuje akcje do katalogu.

## 3. Mapa na to, co już istnieje

| Potrzeba | Już jest | Brakuje |
|---|---|---|
| zadania jako dane | katalog akcji, zasady per zakres, nakładki | rodzaje sygnałów, hipotezy, eskalacja |
| ślad i odwracalność | węzły przebiegów, `derived_from`, cofanie definicji | przekształcenia kontekstu jako węzły |
| kompresja doprecyzowań | specyfikacja bieżącego zadania (`active_task_spec`) | automat proponujący |
| środowiska z cechami | urządzenia w grafie, porty, adaptery | klasy kont/maszyn, sejf poświadczeń ogólny |
| agent | interpreter akcji, potwierdzenia, ×10, limity | pętla narzędzi w `użyj modelu`, źródła narzędzi |

## 4. Kolejność (jednym torem, każda z bramą)

1. Rdzeń strażnika: wystąpienia, hipotezy (zakres, waga, wyjątki), awans wg ustawienia; pierwsze rodzaje sygnałów: poprawka kursu, doprecyzowanie, odrzucenie; kompresja doprecyzowań jako pierwsze przekształcenie.
2. Pętla narzędzi w `użyj modelu` (katalog akcji jako narzędzia), bez nowych uprawnień.
3. Środowiska: klasy, sejf poświadczeń ogólny, adapter źródeł narzędzi.
4. Maszyny jako środowiska — przy włączeniu konkretnego dostawcy pytanie do właściciela (jego pieniądze); konta — przy podłączeniu każdego konta pytanie do właściciela (jego dane).
