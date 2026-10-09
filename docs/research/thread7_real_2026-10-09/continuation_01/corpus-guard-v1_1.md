# Addytywna poprawka strażnika historycznego panelu

Review wykrył lukę w preparatorze: `rglob` pustego lub nieistniejącego katalogu
mógł dać zero historycznych rozmów bez błędu, mimo deklarowanej kontroli starego
panelu. Faktycznie wykonane przygotowanie v1 odzyskało i zweryfikowało trzy
rozmowy; jego dane i dotychczasowe receipts pozostają niezmienione.

Nowe uruchomienia wymagają jawnej polityki `corpus-policy-v1_1.json` oraz
argumentu `--historical-checkpoint`. Strażnik kontroluje dodatnią spodziewaną
liczbę rozmów, dokładny komplet kanonicznych hashy obiektów, hash całego
poprzedniego checkpointu, powiązanie członka źródłowego ZIP-a w jego manifeście
oraz zgodność obiektów wewnątrz tego ZIP-a z lokalnym panelem. Brak, nadmiar,
zmiana obiektu lub brak dowodu checkpointu blokują przygotowanie przed zapisem
nowego katalogu. Stara polityka nie jest po cichu uzupełniana nowymi założeniami.

W rzeczywistych odzyskanych bajtach potwierdzono 3/3 obiekty i łańcuch dowodów
poprzedniego checkpointu. Oryginalnego freeze v1 nie wygenerowano ponownie;
jego SHA pozostaje w `corpus-historical-guard-receipt.json`. Dla ewentualnego
nowego przygotowania v1.1 trzeba podać nowy katalog `--output` i nowy receipt.

Testy: 33/33 PASS, w tym 13 nowych kontroli brakującego/pustego panelu, liczby,
hashy, powiązania checkpointu i braku cichego przejścia starego CLI. Są to testy
mechaniki, nie badania modeli. Koszt i liczba nowych wywołań: zero.
