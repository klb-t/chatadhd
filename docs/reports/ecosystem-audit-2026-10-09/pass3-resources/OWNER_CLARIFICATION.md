# Doprecyzowanie właściciela — 2026-10-09

Poniżej cytat z bieżącej wiadomości właściciela. To źródło wymagań dla kolejnego etapu audytu, nie deklaracja wdrożenia. Interpretacje i rekomendacje audytora są zapisywane oddzielnie.

> Doprecyzowanie właściciela: pełnoprawne zewnętrzne zasoby w grafie
> oraz przekrojowe discovery i adaptacja.
>
> To uszczegółowienie istniejących R15/R20/R21, nie zamiana wcześniejszych
> wymagań ani dowód, że funkcjonalność jest już zaimplementowana.
>
> 1. Referencja do pliku/archiwum nie jest wyłącznie metadanymi ani linkiem UI.
> Silnik ma udostępniać sparsowane wnętrze jako pełnoprawną strukturę grafu,
> dostępną dla zapytań, analizy, budowy kontekstu, workflowów i rendererów.
> Dotyczy to również ZIP-ów, zagnieżdżonych archiwów, eksportów rozmów oraz
> plików ustawień, profili i dostawców, lokalnych i zdalnych.
>
> 2. Rozdziel dostęp/transport, kontener/kodowanie, parser składni,
> interpretację domenową oraz projekcję do grafu. Składaj istniejące adaptery,
> zamiast mnożyć implementacje każdej kombinacji formatu i lokalizacji.
> Rozwinięcie widoku może inicjować odczyt, ale parser nie może działać
> wyłącznie na potrzeby renderera.
>
> 3. Zachowaj niezależne polityki: osadzenie/referencja/oba, odczyt eager/lazy,
> cache/indeks, snapshot/live oraz readonly/overlay/write-back.
> Nie utożsamiaj parsowalności z możliwością bezstratnego zapisu.
> Wersjonuj źródło, selektor fragmentu i zastosowane mapowanie.
> Brak dostępności lub niepełne pokrycie nie oznacza pustych danych.
>
> 4. Discovery/adaptacja to wspólny mechanizm z wyspecjalizowanymi strategiami,
> nie osobna implementacja w każdym importerze czy providerze.
> Obsłuż opisy lokalne, schematy, dokumentację online, analizę struktury
> i wnioskowanie modelu. Wynik może być mapowaniem danych, złożeniem istniejących
> operatorów albo nowym adapterem wykonywalnym.
> Nie wymagaj generowania kodu, gdy wystarcza profil.
>
> 5. Oddziel deklarowaną możliwość, dostępną implementację, dowody poprawności
> i uprawnienie do użycia. Zachowuj alternatywne interpretacje.
> Automatyczna walidacja i aktywacja są sterowane jawną polityką.
> Discovery nie rozszerza samo uprawnień ani zgody na wysyłanie prywatnych danych.
>
> 6. Opisy możliwości, adaptery, workflowy discovery, dokumentacja źródłowa,
> hipotezy, testy i ich wyniki również mają reprezentację w grafie.
> Korzystaj z istniejących kontraktów metod, zamiast tworzyć drugi silnik.
>
> Pierwszy pionowy przyrost ma wykazać:
> - te same rozmowy i relacje po pełnym imporcie oraz przez referencję do ZIP;
> - odczyt rozmowy przez ten sam mechanizm z widoku i z zadania bez UI;
> - pola z zewnętrznego profilu widoczne w grafie i używane przez runtime;
> - zachowanie nieznanych pól oraz jawny status niepewnego mapowania;
> - obsługę zmiany/niedostępności źródła bez cichego usunięcia danych.
>
> Użyj źródeł i testów bezpiecznych do publikacji. Nie zmieniaj prywatnych
> archiwów. Najpierw wykorzystaj i przetestuj istniejące komponenty.
> A audytuje kontrakt i jego rzeczywistych konsumentów; B implementuje przyrost.
