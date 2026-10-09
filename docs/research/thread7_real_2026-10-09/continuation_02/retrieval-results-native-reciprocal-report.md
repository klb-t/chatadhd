# Korekta adaptera struktury po pierwszym pomiarze

Zachowano ujemne wyniki pierwszego adaptera, ale zamknięto konkretną lukę wykonawczą: pole dzieci w znormalizowanych wiadomościach OpenAI było puste mimo dostępnych relacji rodziców. Wersja `observed_parent_reciprocal` dodaje wyłącznie odwrotne przejście po zaobserwowanej krawędzi rodzica, gdy konfiguracja włącza kierunek dzieci. Nie zmienia źródeł, nie tworzy węzłów dla brakujących zewnętrznych rodziców i nie dodaje relacji semantycznych. Stare `declared_fields` nadal jest obsługiwane.

Nową konfigurację zamrożono przed wykonaniem poprawionych operatorów. To jawna korekta instrumentu po obejrzeniu pierwszych wyników, nie niezależna walidacja. Gold, pytania, budżety, wagi, liczba seedów i promień przejścia pozostały bez zmian. Zmieniły się trzy ramiona: graf, rozszerzenie BM25 oraz zależna od niego fuzja. Dla pozostałych trzech zweryfikowano i odtworzono dokładnie 144 pierwotne rankingi; operatorów leksykalnych nie uruchamiano ponownie.

Wykonano **144 nowe rankingi poprawionych ramion i 288 ich ocen budżetowych**. Łącznie pakiet ma 432 faktyczne wykonania rankingu (288 pierwszych + 144 poprawionych), bez nowych wywołań modelu i z kosztem API 0 USD. Pełna tabela wyników nowej konfiguracji ma 576 wierszy, z czego połowa odtwarza niezmienione ramiona.

| Część | Metoda | Budżet | Recall v1 | Recall po korekcie | Różnica |
|---|---|---|---:|---:|---:|
| provisional_validation | bm25_native_expansion | 32KiB | 0.6042 | 0.6042 | +0.0000 |
| provisional_validation | bm25_native_expansion | 8KiB | 0.5000 | 0.5833 | +0.0833 |
| provisional_validation | lexical_bm25_character_graph_rrf | 32KiB | 0.4583 | 0.4583 | +0.0000 |
| provisional_validation | lexical_bm25_character_graph_rrf | 8KiB | 0.5208 | 0.5208 | +0.0000 |
| provisional_validation | native_graph_distance | 32KiB | 0.3125 | 0.3125 | +0.0000 |
| provisional_validation | native_graph_distance | 8KiB | 0.2292 | 0.2292 | +0.0000 |
| tuning | bm25_native_expansion | 32KiB | 0.4323 | 0.4896 | +0.0573 |
| tuning | bm25_native_expansion | 8KiB | 0.4010 | 0.4505 | +0.0495 |
| tuning | lexical_bm25_character_graph_rrf | 32KiB | 0.5495 | 0.4974 | -0.0521 |
| tuning | lexical_bm25_character_graph_rrf | 8KiB | 0.5885 | 0.5052 | -0.0833 |
| tuning | native_graph_distance | 32KiB | 0.1198 | 0.1198 | +0.0000 |
| tuning | native_graph_distance | 8KiB | 0.1198 | 0.1198 | +0.0000 |

Korekta poprawiła rozszerzenie BM25, ale pogorszyła fuzję na części tuningowej; prosty graf nie zmienił wyniku. Te negatywne wyniki pozostają w raporcie. Nie dobierano nowych wag, aby poprawić tabelę. Metoda znakowa zachowuje wcześniejsze pomiary i wcześniejsze ograniczenia.

Czas całego złożonego ramienia jest tutaj sumą zachowanych pomiarów niezmienionych operatorów i nowych pomiarów zmienionych operatorów. Nie jest nowym jednym pomiarem czasu całego workflow; rzeczywiste czasy poszczególnych wywołanych operatorów i flagi replay są w surowych wynikach. Pamięć pozostaje pomiarem alokacji Python operatora, nie RSS.

28/28 testów mechaniki obejmuje odtworzenie starego profilu, przejście od dziecka przez rodzica do rodzeństwa, brak wymyślania wspólnego węzła dla nieobecnego rodzica oraz brak ponownego wywołania odtwarzanego operatora. Aktualne kandydaty na presety: `retrieval-presets-v3.json`, bez automatycznej adopcji. Aktualny raport liczbowy: `retrieval-results-native-reciprocal-v2.json`; wcześniejsze wersje i dokładne producentskie pliki Python pozostają zachowane.
