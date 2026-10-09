# Kontrakt odbioru zasobów zewnętrznych i discovery

Źródło właścicielskie: [dokładny cytat](OWNER_CLARIFICATION.md), jako uszczegółowienie R15/R20/R21. Poniższe warunki są interpretacją audytora służącą odbiorowi B. Nie tworzą drugiego silnika ani wspólnego schematu produktu. Nazwy V01–V12 są identyfikatorami testów audytu. Nie oznaczają istnienia API o takiej nazwie.

| Warstwa | Przedmiot odbioru | Co nie wystarcza jako dowód |
|---|---|---|
| Dostęp / transport | Autoryzowany odczyt konkretnego źródła, wersji i zakresu; jawny błąd niedostępności | Sam URL, zapis nazwy pliku, deklaracja provider capability |
| Kontener / kodowanie | ZIP, zagnieżdżony kontener, członkowie i kodowanie z pochodzeniem | Osobny parser dla każdej kombinacji lokalizacja × format |
| Składnia | Parsowanie bez utraty nieznanych pól; lokalizacja fragmentu | Rozpoznanie rozszerzenia lub pusta lista przy błędzie |
| Domena | Wersjonowane mapowanie, alternatywy i jawna niepewność | Niezatwierdzone wnioskowanie przedstawione jako obserwacja |
| Projekcja / konsumenci | Wnętrze jako dostępna struktura grafu, używana przez zapytanie, kontekst, zadanie i renderer | Opaque JSON/ZIP jako atrybut, który dopiero renderer interpretuje |

To rozdzielenie odpowiedzialności, nie wymóg pięciu nowych klas lub osobnych baz. Duże bajty mogą pozostać poza storage grafu. Istotne są sprawdzalne referencje, wersje, selektory, mapowania, pochodzenie i uprawnienia, a także dostęp konsumentów do sparsowanego wnętrza.

Każda z osi polityki pozostaje niezależna: osadzenie / referencja / oba; eager / lazy; cache / indeks; snapshot / live; readonly / overlay / write-back. Test zmienia jedną oś przy zachowaniu pozostałych. Nie narzuca cache jako synonimu kopii, eager jako warunku indeksu ani write-back jako konsekwencji parsowalności. Nieobsługiwane połączenie musi mieć jawny status, nie cichy wybór innego trybu. Wartości domyślne są profilami. Limity rozpakowania, źródła zdalne, koszt modelu i aktywacja są sterowane polityką użytkownika; kontrakt audytu nie wymyśla twardych progów.

## Pierwszy pion właściciela

1. **V01 — import = referencja ZIP w zakresie rozmów i relacji.** Ta sama syntetyczna paczka trafia do dwóch izolowanych instancji istniejącego silnika. Porównanie używa źródłowych identyfikatorów/selektorów, treści, ról, gałęzi i relacji, pomijając wyłącznie nowe lokalne identyfikatory i czas wykonania. Nie wolno normalizacją testową usuwać parent/reply ani zgubić gałęzi. Sprawdź również ZIP w ZIP. Asercja pojedynczego tekstu nie dowodzi równości grafu.
2. **V02 — ten sam odczyt bez UI i z widoku.** Wskaż konkretny resolver i jego konsumentów. Zadanie headless oraz host widoku pobierają tę samą rozmowę i relacje przez ten resolver. Przechwycenie końcowego transportu jest dozwolone; kopiowanie parsera do testu nie. Brak uruchomionego browser E2E pozostaje osobną luką nawet przy poprawnym hostowym teście odczytu.
3. **V03 — profil zewnętrzny → pola grafu → rzeczywisty runtime.** Dwa profile różnią się właściwym parametrem konsumenta. Zapytanie grafowe wskazuje jego wartość, wersję źródła, selektor i mapowanie; rzeczywisty konsument używa tego pola. Zapis/odczyt surowego JSON i późniejszy parse w kliencie dowodzą tylko części. Wybór modelu ani wysłanie danych nie następują w audycie: transport jest przechwycony.
4. **V04 — nieznane pola i niepewne mapowanie.** Zachowaj nieznane dane źródła i ich lokalizację. Niejednoznaczne mapowanie zachowuje alternatywy i jawny status; nie uruchamia nieznanego adaptera. Jawne odrzucenie nieobsługiwanej wersji wykonawczej jest poprawną bramką, lecz nie zastępuje nieinwazyjnego katalogowania źródła. Testujemy źródło i stan po restarcie.
5. **V05 — zmiana / niedostępność źródła.** Zapisz dawny wynik i recepturę, zmień lub usuń wyłącznie syntetyczne źródło. Wersja dawna i jej wynik nie zmieniają znaczenia; stan niedostępny, niepełny lub nieaktualny jest jawny. Graf nie znika i nie przechodzi w pozornie kompletny pusty zbiór. Snapshot może działać offline; live nie może po cichu udawać snapshotu. Po przywróceniu źródła stan odzyskania jest sprawdzalny.

## Dalsze bramki przekrojowe

- **V06:** zmiana jednej osi polityki nie nadpisuje innych; readonly nie zapisuje źródła, overlay zachowuje bazę. Write-back jest dostępny tylko z potwierdzoną zdolnością zachowania treści i jawnym uprawnieniem.
- **V07:** hash/weryfikacja wersji obejmuje źródło, selektor fragmentu i zastosowane mapowanie; stary wynik odwołuje się do starego zestawu również po ponownym odkryciu.
- **V08:** lokalny opis, schema, dokumentacja online, analiza struktury i model są strategiami wspólnego workflow. Online/model w testach otrzymują syntetyczną odpowiedź przechwyconego transportu. Brak zgody daje zero rzeczywistych żądań; wcześniejsza zgoda na odczyt lokalny nie jest zgodą na wysłanie.
- **V09:** dla tego samego opisu sprawdź niezależnie deklarację możliwości, implementację, wyniki testów i prawo wykonania. Każde może być nieznane lub niedostępne niezależnie od pozostałych. Discovery nie zwiększa uprawnień.
- **V10:** mapowanie/profil wystarcza tam, gdzie składa istniejące operatory. Nowy adapter wykonywalny jest osobną alternatywą; jego generowanie i aktywacja nie są automatycznym skutkiem discovery.
- **V11:** definicja, dokumentacja źródłowa, workflow discovery, hipotezy, testy i wyniki mają dostępne reprezentacje grafowe oraz odwołania do istniejących wersjonowanych metod. Import do GraphPacketStore nie dowodzi jeszcze akceptacji przez MethodRegistry ani obecności executora.
- **V12:** dane błędne, timeout, częściowe parsowanie i nieobsługiwana wersja nie stają się „pusty”, „wykonano” ani „poprawnie zmapowano”. Raport pokrycia podaje znane i nieprześledzone fragmenty, nie samą liczbę zaimportowanych rozmów.

## Granica A / B

A uruchamia istniejące komponenty, zachowuje receipts i wskazuje konkretne brakujące połączenia. B wybiera zgodne rozszerzenie istniejących Source/Unit/Locator, metod, profili, GraphPacket i runtime. Gdy brakuje publicznego konsumenta, test ma **BLOCKED_MISSING_CONTRACT**, zamiast wykonującej się atrapy. Reprodukcja błędu ma oddzielny wynik od akceptacji. Potwierdzona luka nowego doprecyzowania nie jest automatycznie regresją wcześniej przyjętej częściowej funkcjonalności.

Migracja powinna być addytywna względem surowych źródeł, starych receptur i receipts. To rekomendacja wykonawcza audytora wynikająca z zachowania historii; nie upoważnia do nadpisania prywatnych archiwów, schematów lub gałęzi w zadaniu A.
