# Strażnik kontekstu i środowiska działania (2026-10-14)

Nadrzędny względem planów wykonawczych; podrzędny wobec `GRAF_HUB_KOD_SILNIK_2026-10-13.md` i R1–R56.

## 0. Słowa właściciela (2026-10-14, wiernie)

> „może bym dał ogólnie moduł strażnika czystości kontekstu w rozmowie, w którym między innymi by robił te kompresje, jeszcze jakieś inne zadania do niego miałem”

> „kompresja kilku kolejek wiadomości doprecyzowujących do postaci pojedynczej kolejki dokładnie wytłumaczonej; budowanie, zgadywanie, hipotetyzowanie, optymalizacja Flow i preferencji usera na podstawie rozmowy, przykładowo korekta kursu rozmowy/zadania przez użytkownika najpierw zapisana jako wystąpienie (oczywiście według preferencji jak wszystko). hipotezy zakresu ważności, ważone na wielu danych (właśnie do hipotez chyba najwięcej krawędzi będzie szło), porównanie z historią poprawek (na przykład jak użytkownik dwa razy poprawia spokojnie później się już zaczyna wkurwiać to wiadomo że to zasada na stałe ma być), implicite oraz explicite podane wyjątki, itd.”

> „trzeba obsłużyć zarówno wszystkie konta użytkownika z uwierzytelnianiem na wszystkie sposoby, wszystkie lokalne zasoby użytkownika poprzez nas zarządzana maszyna od dowolnie wybranego dostawcy”

> „sposób podejścia twojego jest zbyt szczegółowy pamiętaj że podaję tylko przykłady, jak przęsła mostu który ty masz zbudować”

Przykłady właściciela są **przypadkami** mechanizmów poniżej, nie ich zakresem.

## 1. Strażnik kontekstu — jeden mechanizm

**Czym jest:** stały proces nad grafem, który decyduje, co z rozmowy i grafu trafia do modelu, w jakiej postaci i z jaką wagą — i który uczy się tego z przebiegu rozmów. Nie jest kodem pod każdy przypadek: jego zadania to **dane** (wpisy w katalogu akcji i zasad), wykonywane tym samym silnikiem akcji co reszta (P9–P18).

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
- **Przekształcenia kontekstu** — tym samym cyklem: zastąpienie serii doprecyzowań jednym poleceniem (istniejąca specyfikacja zadania), usunięcie nieaktualnego lub sprzecznego, wyciszenie dygresji i ruchu technicznego (propozycje, pokwitowania). Każde przekształcenie zostawia ślad i da się je cofnąć; oryginał nigdy nie znika.

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
