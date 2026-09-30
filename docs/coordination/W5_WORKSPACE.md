# W5 — Komponowalny interfejs i inspekcja

Start: `README.md`, `../STATE.md`, R29–R31, T5 i bieżący workbench.
Sprawdź ukończone sterowanie kontekstem czata; nie implementuj go ponownie.

Własność: `loom/web/` i testy przeglądarkowe. Native/serwer/kontrakty koordynuj
z ROOT. Równoległe widoki już istnieją; pięć nie jest limitem projektu.

Cel: użytkownik zapisuje i odtwarza spójny układ widoków nad tymi samymi danymi,
może je sprzęgać wybranymi parametrami albo zachować niezależną referencję.

1. Zmapuj istniejące komponenty, persistence i couplings na referencyjny T5.
2. Dodaj najmniejszy brak: per-view identity, query/filter/selection, freeze lub
   unlink oraz trwały zapis aktualnie działającego stanu.
3. Oddziel profil wyglądu/interakcji od modelu, uprawnień, pamięci i sposobu
   składania requestu. Zmiana wyglądu nie ma niejawnych skutków wykonawczych.
4. Inspekcja pokazuje rzeczywistą wersję danych, ślad kontekstu, brakujące
   informacje i status wykonania; preview nie jest dowodem wysłania.
5. Browser test: dwa sprzężone widoki i niezależna referencja, zmiana zakresu,
   reload, poprawne odtworzenie, brak zmiany modelu/prywatności przez UI profile.

Kryteria: działające API, klawiatura i wąski ekran, brak zakleszczeń sprzężeń,
build TypeScript i scenariusz przeglądarkowy. Nie buduj nowego Site ani nowej
aplikacji. W tym pakiecie nie ma zgody na zmianę widoczności lub deployment.
