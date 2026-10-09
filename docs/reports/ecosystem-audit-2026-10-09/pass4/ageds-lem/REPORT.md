# PASS4 — AGEDS i LEM, 2026-10-09

Nowe hipotezy wykonano na niezmienionych bazach: AGEDS `9c1d513bc19d177bd324d7506a21fbab98c2e268`, LEM `1755e1dfaa69fcaac6fbdd12aa95ea1a593ec456`. Nie powtarzano migracji Room, starych testów konfiguracji ani całych bramek autora. Nie edytowano produktu, schematów ani wcześniejszych raportów.

| Zakres nowych testów | Wynik | Znaczenie |
|---|---:|---|
| AGEDS Kotlin: selekcja/cytat/projekcja/controller | 12 contract PASS | Rzeczywiste źródła; backend audio jest atrapą infrastruktury |
| AGEDS ES modules: selekcja/historia/odsłuch | 13 contract PASS | Headless; brak DOM, Chromium i rzeczywistego audio |
| LEM: odtworzenie dwóch przyczyn, trzy warianty | 3 A PASS | Potwierdza występowanie problemów |
| LEM: odbiór tych wariantów | 3 B FAIL | Produkt nie przechodzi akceptacji |
| LEM: stabilny klucz i cofnięcie klucza | 2 contract PASS | Zachowania do zachowania podczas naprawy |
| LEM: blokada socketów | 1 control PASS | Żaden transport rzeczywisty nie został użyty |

Łącznie 34 wyniki konsumentów. Powtórzenia rozwojowe harnessu nie zwiększają licznika. Osobny test narzędzia potwierdza odmowę nadpisania istniejącego katalogu receipts.

**A4-LEM-001:** realny `ResearchViewModel → ExperimentRunner → ResearchAgent → Retrofit/Moshi/OkHttp → ResearchRepository` wykonuje dwa requesty jednego eksperymentu pod różnymi poświadczeniami, gdy `ApiKeyStore` zostaje zmieniony pomiędzy requestami. Wynik nadal ma `INSTRUMENT_OK` i trafia do testowego DAO, bez zapisanej zmiany autoryzacji. Test rejestruje wyłącznie syntetyczne etykiety poświadczeń A/B. Zmiana parametru `key` jest faktem wykonania; przejście do innego rzeczywistego konta to możliwy skutek, nie wykonany pomiar. Nie dowodzi rzeczywistego kosztu, wycieku danych ani utrwalenia w Room. W zwykłym UI zapis/clear klucza pozostają dostępne niezależnie od aktywnego instrumentu (prześledzony kod SettingsScreen).

Odbiór dopuszcza przypięcie autoryzacji do runu albo jawne przerwanie przy zmianie, nigdy ciche A+B w jednym wyniku. Przypięcie nie może wyłączyć cofnięcia uprawnień: odrębny test wykazuje, że obecny clear między requestami blokuje drugi request i daje `INSTRUMENT_FAILED`. Docelowy wynik może przechowywać niejawny dla osoby postronnej identyfikator/wersję kontekstu autoryzacji, **nie sekret**.

**A4-LEM-002:** zatrzymano w przechwyconym transporcie odpowiedź rejestru konta A, wywołano rzeczywiste `clearApiKey()` albo `saveApiKey(B)`, następnie zwolniono odpowiedź. W obu wariantach modele A i komunikat „Live registry” wracają do bieżącego StateFlow. Po rekey nie wykonano requestu B: `refreshModels` pomija go z powodu już aktywnego odświeżania. Clear/rekey to warianty jednego braku sprawdzenia wersji poświadczeń przy publikacji odpowiedzi, nie osobne findingi. Compose nie był renderowany; konsumenci modeli i deklaracja „configured account” zostały prześledzone źródłowo. Nie twierdzimy, że po clear możliwe jest wysłanie requestu bez klucza.

**A4-AG-M001:** w nowo przejrzanych ścieżkach AGEDS potwierdzono mechanizmy, które należy zachować. Selekcja zamraża treść i uporządkowane referencje; kolejność kluczy obiektu jest obojętna, ale zmiana kolejności segmentów, wersji lub wystąpienia zostaje odrzucona. Kotlin dopuszcza dokładnie równoważne zapisy `0`/`0.0` w indeksie selektora — nie jest to ogólna reguła o wszystkich liczbach i kontraktach. Projekcja 10001 słów zachowuje źródło, identyfikatory i jawnie oznacza 10000 widocznych jako niepełny widok. Nie stanowi to zatwierdzenia zaszytego progu przez R42.

Headless pager po przerwaniu ponawia ten sam cursor/snapshot z nowym tokenem. Stara odpowiedź nie modyfikuje stanu; błędny drugi wiersz nie powoduje częściowej publikacji pierwszego. Mały budżet treści ujawnia część historii, nie „koniec”. Kontrolery audio odrzucają spóźnione callbacks; timeout nie odtwarza zatrzymanej operacji. Te dowody nie dowodzą dostępności grafu z zewnętrznego źródła, sprawności Android MediaPlayer ani jakości alignmentu. Nie traktujemy limitów, timeoutów ani tekstów UI jako wyjątków R42; pozytywny wynik dotyczy mechanizmu tożsamości i publikacji.

Pokrycie: AGEDS ma nowe nazwane zakresy w **6** plikach (5 wykonywanych, CitationUi źródłowo), **45/63** łącznie, **18 bez zakresów**. LEM pozostaje **24/24 z zakresami**, przyrost plików 0; doszły wykonane zachowania. To nie odsetek całkowicie zweryfikowanych plików. `coverage.json` wymienia funkcje, linie, hashe i granice niewykonanych wywołań.

Kotlin host używa dostępnego kompilatora 2.2.10, JDK17; AGEDS JSON runtime 1.8.0. Nie jest to pełny pinned build Android AGEDS. Zależności i źródła są hashowane w receipt. W LEM Android preferences, lifecycle i DAO są fixtures infrastruktury; mechanizm produktu nie został przepisany w teście. Brak nowego kodu/warunków nie uzasadniał ponownego uruchamiania zaakceptowanego Room ani zablokowanego emulatora.

Pakiety B: `handoff-B.json`. Wykonywalny indeks: `test-index.json`; runner w `tools/ecosystem-audit-2026-10-09/pass4/ageds-lem/`. Opublikowany plik nie oznacza wysłania wiadomości sesji B.
