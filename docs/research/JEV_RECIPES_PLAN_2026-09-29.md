# T3 — zamrożony plan receptur Jev, przed wynikami

[U] Zadanie T3 z `docs/GPT_OFFLINE_TASKS_2026-09-29.md`, R37/R38.
[P/H] Plan i autorski korpus do przyszłego uruchomienia. **Żadnych nowych
odpowiedzi Jev, żadnej wykonanej inferencji ani autoryzacji kosztów.**

## Podstawa i odróżnienie źródeł

Punkt odniesienia: `494b164c4515b875014b3cfd1c3760de23bda401`.
Audyt `JEV_OFFLINE_POLICY_AUDIT_2026-09-29.md` oraz `JEV_RESULTS_2026-09-28.md`
pokazują rozbieżność rubryki q01 z celem odkrywania struktur. To motywacja nowego
badania, nie wynik nowej receptury. Kontrakt żądania pochodzi z zapisanego
`JEV_LIVE_PROTOCOL_2026-09-28.md` oraz dostarczonego w rozmowie Jev Lab
(`jev_lab/protocol.py`); nie sprawdzano dziś działania zdalnego API.

Dokument Gemini „Wydajność i optymalizacja zapytań dla modelu klasyfikacyjnego
jev.docx”, str. 3–5 i 8–9, sugeruje badanie semantyki kluczy, struktury rubryki i
liczby opcji/hierarchii. To raport wtórny, nie dowód skuteczności. Nie przejmujemy
jego twierdzeń o gwarantowanej kalibracji, bezbłędności, niezmiennej latencji ani
„jedynym słusznym” drzewie. Bieżąca praca nie kopiuje tego dokumentu do repo.

## Korpus i niezależność

36 nowych fikcyjnych przypadków: 18 PL / 18 EN; 16 relacyjnych, 12 kontekstowych,
8 routingu. Nie kopiowano poprzednich 64 tekstów ani nie odczytywano ukrytych
branchy. To jednak **ten sam asystent jako autor danych i narzędzi**, nie niezależny
panel ludzkich annotatorów ani ślepy holdout. Połowa development/validation jest
ustalona przed odpowiedziami; obie połowy są autorskie i obejrzane.

`corpus.json` zawiera dane wejściowe; `gold.json` trzyma etykiety i uzasadnienia
poza requestami. Podział, rodzina i identyfikator nie są wysyłane jako state.
Przykłady nie są parami tłumaczeń. W kolejnej rundzie potrzebne rzeczywiste,
niezależnie ocenione materiały. Liczbą przypadków jest 36, nie liczba pytań lub
wariantów. Wewnątrz jednego przypadku oceny są sparowane/zależne.

Przed inferencją zamrozić SHA-256 źródeł, gold, receptur, tego planu, generatora i
requestów. Zmiana po obejrzeniu wyników tworzy nową wersję; nie nadpisywać v1.

## A. Wyrażone a sensowna abstrakcja

Każdy z 16 przypadków ma dwie niezależne oceny Noul dotyczące tej samej relacji:
`expressed` — co jest stwierdzone przez źródło (wierna parafraza dopuszczalna;
bez wymogu liter P/Q), oraz `inferred` — czy to uzasadniony, ograniczony zakresem
kandydat interpretacji/rozwoju. Drugi wynik nie jest faktem ani uprawnieniem do
promocji. Możliwe jest jednocześnie TAK/TAK; to nie rozłączne klasy.

Korpus zawiera poprawne i odwrócone relacje, brak danych, mylenie przyczyny z
czasem, błędną tożsamość, a także **propozycje** rozszerzeń architektonicznych,
których nie należy oznaczać jako wykonanych/stwierdzonych. Gold jest oceną autora
według tej rubryki. Sporne rozumienie abstrakcji oznaczać osobno, bez zmiany gold
po odpowiedzi. Dla innej semantyki pytania zrobić nową wersję badania.

## B/C. Nazwy kluczy i string kontra obiekt

Cztery ramiona na identycznym state oraz identycznych wartościach instrukcji:
`object_meaningful` (task/criterion/boundary), `object_neutral`
(field_1/field_2/field_3), `object_nonsense` (zxq_1/vkp_2/mjr_3), `string`
(te same trzy wartości połączone nowymi liniami). Zmieniają się tylko
**wewnętrzne klucze instructions**, nie wymagane klucze API.

Pierwotne kontrasty: meaningful−neutral, meaningful−nonsense, meaningful−string.
Nie dodawać drugiej instrukcji w znaczącym wariancie: to miesza treść z formatem.
Różnica tokenizacji/długości kluczy pozostaje możliwym mediatorem; nie nazywać
wyniku izolowanym pomiarem mechanizmu attention. Nie zakładać niewrażliwości na
kolejność; zapisujemy rzeczywistą kolejność JSON i body hash.

## D. Choice płaski, hierarchiczny, liczba opcji

8 przypadków, stałe zagnieżdżone uniwersa 4/8/16 końcowych kategorii.
Wszystkie właściwe kategorie celowo mieszczą się w pierwszych czterech: badamy
wpływ **dodawania dystraktorów**, nie pełną jakość klasyfikacji szesnastu klas.
Dwie grupy nadrzędne: software/operations. Ich opisy budowane z aktualnie
obecnych liści; bez podawania poprawnej grupy z gold.

Dla każdego uniwersum: flat (jedna odpowiedź) kontra root → child
(dwie odpowiedzi). Gotowe są **obie** gałęzie child, ale wykonawca uruchamia
wyłącznie wybraną przez faktyczną odpowiedź root. Bez oracle routing, bez
przerzucania do drugiej gałęzi po zobaczeniu błędu, bez liczenia trafności dziecka
z pominięciem błędu korzenia. Dokładne max-p tie → pierwszy element zapisanej
kolejności; błąd/niekompletna odpowiedź → nierozstrzygnięty epizod.
Kolejność opcji jest deterministycznie rotowana według numeru przypadku,
nie według jego etykiety. Primary nie dodaje powtórzeń.

Raportować end-to-end accuracy i pokrycie na wszystkich zaplanowanych epizodach,
błędy korzenia i dziecka osobno, całkowity koszt/latencję obu etapów. Rozkład
dziecka jest warunkowy na wybranej grupie: nie porównywać jego max P wprost
z globalnym flat jako wzrostu pewności. Nie mnożyć score'ów i nie nazywać tego
skalibrowanym globalnym prawdopodobieństwem bez osobnego badania.

## E. Trzy rodziny pytań w selektorze

12 przypadków, po 3 pokazane podgrafy z rzeczywistymi tekstami reprezentacji.
Każdy request ma 7 niezależnych pytań: przydatność podanej relacji; trzy Noul
przydatności podgrafów; trzy Choice najmniejszej wystarczającej reprezentacji
(omit/label/summary/full/raw/unavailable). Wiele podgrafów może być przydatnych.
`unavailable` jest jawne, nie utożsamiane z omit. Poziom reprezentacji nie jest
prawdą o świecie ani Confidence. Nie pytać o treść, której model nie otrzymał.

Miary: micro i macro P/R/F1 oraz exact-set dla subgrafów; macierz pomyłek
szczegółowości, liczba niedostatecznych i nadmiarowych reprezentacji; osobno
konieczne cytaty/wyjątki/kontrdowody. Wszystkie estymacje przydatności nadal
wymagają późniejszego pomiaru jakości produktu downstream; ten pilot go nie robi.

## Analiza z góry, nie po dobraniu wygodnych progów

Binarne decyzje: p>=0.5. Raport dodatkowy: pozostawić p<=0.2 lub p>=0.8;
pozostałe do weryfikacji. To progi diagnostyczne, nie gwarancje bezpieczeństwa.
Dla każdego pytania podać TP/TN/FP/FN, mianowniki, P/R/F1, Brier, log loss
(clip 1e-6), ECE w 5 równych przedziałach oraz liczebność każdego przedziału.
Przy małych próbkach nie traktować ECE jako certyfikatu kalibracji.

Brakujące wywołania/niepoprawne odpowiedzi zachować w mianowniku planu i
raportować osobno. Brak precyzji przy zerze predykcji dodatnich = null, nie 1.
Dla kontrastów B/C parować po case_id i typie pytania, pokazać każdą różnicę
prawdopodobieństwa i zmianę decyzji; przedziały niepewności przez bootstrap
**przypadków**, nie odpowiedzi (seed 29092026, 2000 próbek). To mały pilot
opisowy: brak dowodu populacyjnej przewagi po przetestowaniu wielu receptur.

Nie wykonywano tych analiz na nowych wynikach, bo takich wyników nie ma.
Nie generujemy pliku pozorowanych odpowiedzi do pomylenia z prawdziwym pilotem.

## Wykonanie i budżet

208 gotowych body JSON: 112 bezwarunkowych oraz 96 obejmujących oba możliwe
childy. Pełne poprawne wykonanie obu strategii = maks. **184 wywołania**,
nie 208: 112 + 24*(flat 1 + hierarchy 2). Bez niejawnego retry.

Plan jest disabled; autoryzowany koszt pozostaje null. Przed LIVE Claude musi
sprawdzić bieżący kontrakt/cenę/endpoint, przypiąć rzeczywisty model/provider,
uzyskać limit kosztu, rezerwować próbę i zapisywać pierwszą odpowiedź wraz z
request id/hash, modelem zwróconym, usage i czasem. Cena z dawnego pilota nie jest
obietnicą dzisiejszego kosztu. Niejawna zmiana providera jest niedopuszczalna.

Archiwum requests.zip zawiera requests.jsonl (rekord file + body) i oddzielny
request_index.json. Eksporter --export-to tworzy samodzielne pliki z **samym body**
do edytora raw JSON Jev Lab. Nie wysyłać całego rekordu JSONL jako requestu.
Metadata, gold i routing plan nie należą do payloadu.
Generator tylko eksportuje/weryfikuje pliki, nie ma klienta HTTP ani dostępu do
sekretów. Kompresja ZIP ogranicza powtarzanie tych samych state w diffie, a nie
zmienia wysyłanych danych. Manifest zamraża dokładne bajty każdego body.
