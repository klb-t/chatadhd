# Silnik wizualizacji grafu (2026-10-15)

Podrzędny wobec `GRAF_HUB_KOD_SILNIK_2026-10-13.md` i R1–R56. Wykonanie: P28.

## 0. Słowa właściciela (2026-10-15, wiernie)

> „chcesz zobaczyć na przykład historię wersji przede wszystkim ale też z pewną głębokością stos wywołań, testy które zahaczają o to co robisz, inne projekty ekosystemu które wywołują te funkcje”

> „wszystkie wizualizacje wymiarów można w jakiś sposób wykorzystać nie tylko osie łącznie z czasem ale też kolory nasycenie alfa, tylko większość kombinacji będzie bezsensowna, trzeba by jakoś żeby system widział który mają sens i nie promował”

> „dlatego że nasycenie i jasność osłabiają wybór barwy, to promowane będą kombinacje przy których niskie wartości dwóch wymiarów spływają na niską wagę trzeciego”

> „głębia się widzi jako ogólna odległość przez wszystkie osie […] albo nie przez wszystkie osie przez kierunek w który patrzysz jeśli patrzysz w kierunku wersji No to w głąb będą najstarsze albo jak się odwrócisz do tyłu to projektowane w przyszłość”

> „patrząc na historię wersji intuicyjnie widzę że kierunki pod kątem to będą branche. w boki to sąsiedzi ekosystemu”

> „to jest tylko jedna z konfiguracji, a zakodować trzeba silnik który wszystkie elementy w różnych kombinacjach obsłuży, i format węzłów w grafie które będą mówiły silnikowi jak się obsłużyć”

> „przypisać działania klawiszom modyfikującym i stanom *lock i strzałkami jeździć […] Locki to do alternatywnych widoków. oczywiście jako podstawowe ustawienia bo jak się użytkownik uprze to może przepisać do czegokolwiek, do powiększenia chociażby”

> „tak jak w raporcie mniejszości się będzie pracować” (AR/VR)

Przykłady są presetami silnika, nie jego zakresem.

## 1. Zasada

Silnik składa widok z deklaracji; deklaracje są węzłami grafu (wzorzec: gramatyka grafiki, Wilkinson 1999; Vega-Lite). Żadna konfiguracja nie jest zakodowana. Reguły sensu są osobną warstwą nad składaniem (wzorzec: Draco, Moritz i in. 2018): silnik dopuszcza każdą poprawną kombinację, reguły decydują, co promować, a czego nie promować — z wyjaśnieniem, bez blokady.

## 2. Elementy (rodzaje węzłów, otwarta lista)

1. **Źródło** — perspektywa (P24) albo mieszanka: perspektywy z wagą (główna/poboczna) i głębokością.
2. **Miara** — wartość liczona na elementach: odległość od fokusu (reguła łączenia: suma ważona — preset, minimum, inne), wiek, rozejście gałęzi, ważność (stopień zainteresowania, Furnas 1986).
3. **Układ współrzędnych** — kartezjański; sektorowy (kierunek → perspektywa, mieszanie według kąta); głębia z odległości; MDS; drzewo z kątami gałęzi; kolejne.
4. **Kodowanie** — kanał ← miara: pozycja, głębia, barwa, nasycenie, jasność, przezroczystość, rozmiar, rozmycie, znacznik, ruch/czas.
5. **Złożenie** — warstwy, sektory, pasy, małe wielokrotności.
6. **Reguły** — (a) dopasowanie kanału do rodzaju danych (ranking w stylu Mackinlaya 1986); (b) pary zakłóceń (nasycenie/jasność → barwa, przezroczystość → nakładanie, rozmiar → barwa) z warunkiem: wymiar na kanale zakłócającym rośnie razem z ważnością, sprawdzanym na danych widoku; (c) budżet węzłów na widok (preset) i porządek zwijania; (d) uczenie z reakcji użytkownika cyklem strażnika (wystąpienie → hipoteza → zasada).
7. **Interakcja** — wejście → działanie, per urządzenie: trzymane modyfikatory wybierają osie strzałek; locki przełączają widoki alternatywne; wzrok + ruch palców i głos w XR; duże gesty tylko do zgrubnych ruchów sceny; podpowiedź bieżących przypisań zawsze widoczna. Każde przypisanie da się przepisać (np. lock → powiększenie); kolizje ostrzegane, nie blokowane.
8. **Renderer** — 2D (web, Android), WebXR VR i AR (Android, okulary do komputera).

## 3. Pierwszy preset przestrzenny (jeden z wielu)

Historia wersji przed tobą, w głąb starsze; gałęzie w stożku wokół tego kierunku (kąt stały dla gałęzi, rozejście nasyceniem); ekosystem po bokach; przyszłość (plany, cele — teleologia) z tyłu. Świat stoi, obraca się użytkownik. Kierunki i szerokości sektorów to dane; kierunek przeszłości odwracalny.

## 4. Otwarte

Rzut 4D→3D jako eksperyment (intuicja 4D po treningu: Ambinder i in. 2009), nie priorytet. Uczenie reguł z reakcji — po pierwszym rendererze.

## 5. Kolejność

1. Format węzłów (punkty 1–6) i silnik dla 2D, preset z punktu 3 jako test.
2. Interakcja (punkt 7) z presetami klawiatury, dotyku i XR.
3. Renderer WebXR (VR, potem AR).
