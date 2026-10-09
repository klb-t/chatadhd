# Przegląd korekty natywnego przejścia po relacjach

Nie znaleziono otwartej blokady dla zapisanych pomiarów. Sprawdzono osobne
zamrożenie `observed_parent_reciprocal`, kod, komplet zachowanych wyników,
raport i 12 kandydatów na presety. Poprzednich receiptów przeglądu nie zmieniono.

Dodana krawędź służy wyłącznie do odwrotnego przejścia po istniejącej relacji
rodzica. Oba końce muszą występować w tym samym zamrożonym zbiorze kandydatów.
Nie powstaje relacja semantyczna ani sztuczny wspólny węzeł nieobecnego rodzica.

Kontrola trzech manifestów i kompletu zapisanych rezultatów potwierdziła:

- 144 odtworzone rankingi, wartości scores i zbiory kandydatów są identyczne;
- 144 zachowane pomiary operatorów oraz 288 kontekstów i list pominięć są zgodne;
- nowe wykonania dotyczą tylko 144 rankingów trzech zmienionych ramion;
- zadania, gold, protokół, budżety, wagi, liczba seedów i promień przejścia pozostały bez zmian.

Jedyną zmianą dwóch opisów operatora grafowego jest `adjacency_policy`.
Fuzję RRF przeliczono ze względu na zmieniony komponent, przy zachowaniu wag.
Raport zachowuje pogorszenie fuzji oraz wyjaśnia, że czasy kompozycji łączą
wcześniejsze pomiary niezmienionych operatorów z nowymi pomiarami poprawionych.
Kandydaci nadal mają wyłączoną adopcję, prowizoryczną walidację i null dla
jakości odpowiedzi modelu; wykonania w produkcyjnym runtime nie przypisano.

Wykryto i domknięto dodatkową granicę konfiguracji: sam niezmieniony opis
metody nie wystarcza do ponownego użycia jej wyniku, gdy zmieniła się zależność.
Nowy guard wymaga, aby całe domknięcie `seed_method` i `components` należało do
odtwarzanych metod. Odrzuca także brakujące zależności i cykle. Bieżący zamrożony
wariant miał prawidłowy podział już wcześniej; ta poprawka nie wymagała nowych
pomiarów. Hash producenta pomiaru i późniejszego kodu guardu są zapisane osobno.

Przeszło 9/9 celowanych testów mechaniki, w tym kontrola, że ranking nie czyta
pól gold, poprawny podział aktualnej konfiguracji oraz odrzucenie starej fuzji
zależnej od zmienionego komponentu. Pełny log to `review-reciprocal-tests.log.gz`.
Siedem przypadków powtarza istniejące regresje; nie są nowymi niezależnymi testami.

Przegląd nie uruchomił benchmarku ponownie. Nowe wywołania modeli: 0; koszt:
0 USD. Jest to kontrola tej samej sesji po ekspozycji na wyniki, nie niezależna
walidacja ani dowód rozumienia znaczenia rozmów.
