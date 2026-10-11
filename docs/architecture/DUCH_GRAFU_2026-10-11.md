# Duch grafu — co ma w nim być i co silnik ma umieć (odczyt wszystkich wypowiedzi właściciela, 2026-10-04…11)

Ten dokument jest nadrzędny wobec list i tabel w pozostałych dokumentach projektu.
Czyta się go tak: właściciel pokazuje kilka przykładów, które wyznaczają **rozpiętość**
tematu; zadaniem silnika (i każdej sesji projektującej) jest zrekonstruować całą
przestrzeń między nimi i poza nimi, a potem pójść „delikatnie do przesady”.
Decyzje strategiczne należą do właściciela; wszystkie techniczne do silnika i sesji.

## 0. Jedna zasada, z której wynika reszta

**Graf jest podstawą całej pamięci systemu.** Wszystko, na czym system operuje, jest
adresowane przez graf: rozmowy, wiadomości, archiwa, pliki, kod, ustawienia, preferencje,
metody, prompty, wyniki, eksperymenty, decyzje, klucze, magazyny, wiedza o sobie,
wiedza o użytkowniku, to, czego nie rozpoznano, i to, co dopiero wyłania się.
Nie ma drugiej pamięci obok grafu. Bajty mogą leżeć w różnych magazynach (R51), ale
ścieżka do każdego bajtu, jego pochodzenie, wersja i powiązania są w grafie.

Konsekwencje dla silnika:
- każda operacja systemu czyta z grafu i pisze do grafu (również swoje własne
  wykonanie: metoda, wersja, parametry, koszt, wynik — krawędź „wyprodukowane przez”);
- każdy rekord niesie **metadane strukturalne**: skąd, kiedy, czym rozpoznany, w jakiej
  strukturze (predefiniowanej, wykrytej, zdefiniowanej przez użytkownika), z jaką pewnością;
- jedna forma dla wszystkiego (pakiet grafowy z bogatymi krawędziami), żeby model,
  interfejs i człowiek dostawali to samo w tej samej postaci;
- to, czego silnik nie rozumie, też wchodzi — z adnotacją, nie znika.

## 1. Główne byty — rekonstrukcja z przykładów właściciela

Właściciel wymienił: samomodel aplikacji, technologie i adaptery, ustawienia z preferencjami,
użytkownik wielopoziomowy, metody, magazyn i bezpieczeństwo. Przestrzeń między tymi
punktami:

**Źródła i ich ziarna.** Rozmowa, wiadomość, gałąź rozmowy (edycja = wersja), archiwum
eksportu, załącznik, plik, commit, notatka głosowa (z ASR i jego błędami jako częścią
rekordu), zrzut ekranu, strona, wiadomość z innego systemu. Każde ziarno ma lokator do bajtu.

**Struktury nad ziarnami.** Predefiniowane (schemat eksportu, typy projektu, facety),
wykryte (temat, encja, twierdzenie, wzorzec nienazwany, który model dopiero zauważył),
zdefiniowane przez użytkownika (jego własne kategorie, jego własne krawędzie, jego własne
widoki). Wszystkie trzy są równorzędnymi węzłami; różni je pochodzenie, nie ranga.

**Twierdzenia jako bogate krawędzie.** Podmiot–predykat–obiekt z kwalifikatorami,
cytatami, lokatorami, wyprowadzeniem, przesłankami, alternatywami, konsekwencjami,
otwartymi pytaniami. To jest podstawowy nośnik „powiązania”: także powiązania
nieoczywistego, które silnik znalazł i jeszcze nie wie, jak nazwać.

**Podmioty.** Osoba, konto, rola w kontekście, grupa, grupa grup, organizacja, kolektyw
doraźny (rodzina ważąca decyzję), społeczność, agent AI, instalacja, osoba wspomniana.
Użytkownik jest węzłem tego typu: najdokładniej opisanym i wysoko ważonym, ale węzłem.
Podmiot *o którym* graf wie i podmiot *który działa i płaci* w tej chwili to dwa odniesienia.

**Pakiety właściwości** („klasa człowiek” i jej krewni): tożsamość, komunikacja, preferencje,
wartości i filozofie, zainteresowania, obszary geograficzne/zawodowe/celowe, wiedza
i kompetencje, zasoby i ograniczenia (uwaga, czas, budżet), relacje, sprawy wrażliwe,
prywatność. Każdy slot ma stan `unknown`/`declined`/`never`, czas (teraz/zwykle/kiedyś),
pochodzenie (podane/wywnioskowane/potwierdzone/odrzucone) i stan podmiotu w chwili podania.

**Konteksty.** Projekt, gałąź projektu, decyzja, okres, miejsce, rola, sesja, urządzenie.
Kontekst jest tym, co zmienia znaczenie preferencji (w pracy inaczej niż w domu) i tym,
po czym selektor kontekstu wybiera.

**Ustawienia i preferencje.** Każde ustawienie ma wartość w wielu zakresach (silnik →
preset → instalacja → organizacja → zespół → osoba → rola → projekt → rozmowa → wywołanie)
i regułę rozstrzygania konfliktu. Preferencje są węzłami, do których ustawienia się
odwołują. Domyślne to presety, nie reguły; każdy można nadpisać, wyłączyć, wykluczyć na zawsze.

**Metody.** Metoda → wersja (hash promptu i kodu) → preset dostrojony do zastosowania →
dowody (przebiegi, zapytania, odpowiedzi, metryki, koszty, daty) → rekomendacja. Metody
leksykalne, statystyczne, wektorowe, Jev, modele tanie i frontier, metody wykorzystujące
wewnętrzną złożoność dużych modeli (uzupełnij graf, znajdź wzorce, krytykuj, pytaj o luki),
metody strukturalne. Prompty są danymi i w trybie eksperckim użytkownik je edytuje.

**Wyniki.** Odpowiedź liniowa i jej odpowiedź grafowa obok, osadzona automatycznie;
analiza; propozycja (kandydat do przyjęcia); eksperyment z torami i porównaniem; decyzja
z przesłankami, wagami uczestników i konsekwencjami; raport (ekonomii, bezpieczeństwa).

**Samomodel.** Cele, filozofia właściciela (R1–R53 jako węzły), zdolności, ograniczenia,
modele i ich profile (co umieją, ile kosztują, gdzie się mylą), interfejsy, stan rozwoju,
własne pomyłki i ich naprawy. Aplikacja wie o sobie zanim pozna użytkownika (zalążek zerowy).

**Magazyny i bezpieczeństwo.** Gdzie leżą bajty (serwer aplikacji, chmury użytkownika,
lokalnie, w dowolnej kombinacji per kategoria), kto ma dostęp, czym zaszyfrowane, kto
trzyma klucze, kiedy rotowane, zgody na wysyłkę do dostawców. Widoczne w pierwszym widoku.

**Nierozpoznane, niepewne, wyłaniające się.** Węzły ze statusem rozpoznania, propozycje
modelu bez nazwy, sprzeczności między źródłami, alternatywy. To nie jest brud do
wymiecenia; to krawędź wzrostu grafu.

**Ekonomia.** Zużycie i koszty per metoda, per podmiot, per projekt; duplikaty; przepisy
(generatory) zamiast zmaterializowanych danych tam, gdzie się opłaca.

## 2. Jak silnik obsługuje powiązania

Dwie klasy powiązań, ten sam mechanizm:
- **Wcześniejsze, z wiedzy o sobie:** zaszczepione z packów (ontologia, metody, zasady,
  domyślne), więc od pierwszej chwili graf wie, co jest czym i jak się do czego odnosi.
- **Nowo wyłaniające się:** z analiz, z rozmów, z importów, z eksperymentów, z edycji
  użytkownika. Wchodzą jako propozycje z pewnością i pochodzeniem; polityka przyjęcia
  (zapytaj / kandydat / automatycznie) jest ustawieniem w zakresach.

Zasady wspólne:
1. Powiązanie zawsze wie, kto je stworzył (człowiek, metoda i wersja, pack) i na jakiej
   podstawie (cytat, lokator, wyprowadzenie).
2. Powiązanie nieoczywiste (między rozmową sprzed roku a dzisiejszym commitem, między
   nawykiem komunikacyjnym a porą dnia) jest tak samo pełnoprawne jak oczywiste, tylko
   z inną pewnością i z pytaniem do użytkownika, jeśli jego preferencja tak każe.
3. Nic nie jest „prawdą” przez samo istnienie w grafie; inferencja nigdy nie udaje
   obserwacji; osąd właściciela wygrywa.
4. Krawędzie mają kierunek czasu: wiadomo, co było znane kiedy (`known_at`), więc da się
   zapytać „co myślałem rok temu” bez skażenia dzisiejszą wiedzą.
5. Scalanie encji nie jest jedyną relacją tożsamości: „to samo”, „ten sam w innej roli”,
   „ta sama osoba, inne konto”, „rzut tego samego” to różne krawędzie (R35).

## 3. Przepływ zapytania

Każde wejście użytkownika (tekst, głos z błędami ASR, kliknięcie, import, edycja grafu)
przechodzi tę samą drogę, a każdy krok jest widoczny i edytowalny w trybie eksperckim:

1. **Interpretacja intencji** w świetle preferencji i kontekstu: kim jest podmiot w tej
   chwili, w jakiej roli, w jakim projekcie, jakim urządzeniem, w jakim stanie (ADHD:
   dziś krótko i konkretnie). Niejednoznaczność ASR to zwykła niepewność, nie błąd.
2. **Wybór kontekstu** (selektor): zakres i szczegółowość jako osobne pokrętła; liczba
   skoków po grafie; budżet tokenów; które metody wyszukiwania (semantyczne jako podstawa,
   leksykalne jako cień); kolejność bloków (stałe najpierw, pytanie na końcu); co wolno
   wysłać do którego dostawcy (prywatność); czy dołączyć historię z archiwów.
3. **Tory wykonania**: jeden lub wiele (sekcja 4), każdy z własną metodą, modelem,
   promptem, parametrami i kosztem; podgląd kosztu przed wysyłką; ×10 pyta.
4. **Odpowiedź** liniowa dla człowieka i grafowa dla grafu, obok siebie; grafowa jest
   kompilowana, sprawdzana i osadzana z pochodzeniem.
5. **Uczenie**: nowe powiązania, preferencje, wzorce wchodzą jako propozycje; polityka
   przyjęcia jest ustawieniem; wszystko da się cofnąć i wyjaśnić („skąd ta wartość?”).

## 4. Wielotorowość

Tor = metoda + wersja + model + prompt + parametry + budżet + kryteria oceny. Tryby:
- **ręczny**: użytkownik składa tory i ocenia sam;
- **półautomatyczny**: silnik proponuje N torów z presetów dopasowanych do profilu,
  użytkownik wybiera, silnik ocenia;
- **automatyczny**: silnik składa, wykonuje, ocenia wielokryterialnie (poprawność schematu,
  ugruntowanie w cytatach, zgodność między torami, pokrycie, koszt, stabilność) i proponuje
  preset do przyjęcia na wybranym zakresie;
- **dowolnie inaczej**: kompozycja torów jest danymi (język opisu torów), więc użytkownik
  może wymyślić własny tryb (tory sekwencyjne, turniej, krytyk + obrońca, tory na różnych
  fragmentach, tory z różnymi dostawcami dla różnych kategorii prywatności).
Eksperyment jest pod ręką tam, gdzie się przydaje: przy imporcie, przy edycji promptu,
przy sporze między modelami, przy pytaniu „który model jest dla mnie lepszy do X”.
Każdy eksperyment zostaje w grafie jako dowód dla metod.

## 5. Przypadki graniczne — dziwne rzeczy, które użytkownik może wymyślić

W rozumieniu właściciela: **nietypowa sytuacja, którą silnik ma zinterpretować mechanicznie**
(preferencjami, zakresami, metodami), osadzić w grafie i znaleźć nieoczywiste powiązania.
Nie są to testy jednostkowe. Poniżej rozpiętość zrekonstruowana z przykładów właściciela;
sesja S3 ma ją poszerzyć „aż do przesady” i dla każdego przypadku podać: co silnik musi
zrozumieć, gdzie to ląduje w grafie, które preferencje rządzą, jakie powiązania ma szukać,
czego dziś brakuje.

Pamięć i czas
- „Co myślałem o X rok temu, a co dziś, i kiedy zmieniłem zdanie?”
- „Ta rozmowa była w złym dniu; nie ucz się z niej, ale jej nie kasuj.”
- „Pokaż mi wszystko, co o mnie wiesz, skąd to wiesz, i pozwól mi to poprawić.”
- „Zapomnij o tym na zawsze i nigdy więcej nie pytaj.” (never ≠ declined ≠ unknown)
- „Połącz tę rozmowę z commitem, który z niej wyniknął, i z notatką głosową z samochodu.”

Podmioty i decyzje
- „Rodzina decyduje o przeprowadzce: zbierz za i przeciw z naszych rozmów, mama ma wagę 2,
  dzieci głos doradczy, pokaż, gdzie się nie zgadzamy.”
- „W pracy jestem formalny, w domu nie; w tym projekcie piszę po angielsku.”
- „To moje drugie konto; ta sama osoba, inna rola publiczna.”
- „Agent GPT z wczoraj zrobił to i to; traktuj go jak członka zespołu, ale nie ufaj mu
  w sprawach kosztów.”

Metody i eksperymenty
- „Puść ten fragment przez pięć promptów i powiedz, który najlepiej wyciąga decyzje z moich
  notatek, i ile to kosztowało.”
- „Zapytaj trzy modele o to samo i pokaż tylko to, w czym się nie zgadzają.”
- „Edytowałem prompt analizy; przelicz tylko to, co się zmieniło.” (przepis, nie dane)
- „Znajdź w archiwach wzorce, których jeszcze nie nazwałem.”
- „Odpowiedz grafem, nie tekstem.” / „Odpowiedz tekstem, ale graf i tak zapisz.”
- „Jev ma oceniać relewancję tego twierdzenia dla tego węzła tylko po krawędziach typu Y.”

Dane, import, duplikaty
- „Zaimportuj eksport z OpenAI i Anthropic; to częściowo te same rozmowy; nie dubluj, ale
  nic nie trać.”
- „Tego formatu nie znasz — i tak go weź, pokaż mi, co z niego zrozumiałeś.”
- „Ten plik ma 2 GB; nie czytaj wszystkiego, powiedz, co w nim jest i co by kosztowało.”
- „Dane z telefonu i z laptopa to jeden graf; konflikt rozstrzygaj tak, jak ustawiłem.”

Prywatność, magazyn, bezpieczeństwo
- „Zdrowie zostaje na telefonie; praca może iść do chmury firmowej; nic o rodzinie nie
  idzie do żadnego dostawcy.”
- „Podpisz tę decyzję tak, żeby za 20 lat dało się sprawdzić, że to ja.” (odporność kwantowa)
- „Pokaż, gdzie fizycznie leżą moje dane, i przenieś tę kategorię gdzie indziej.”
- „Klucz wyciekł; co było wysłane, komu, kiedy?”

Ekonomia i sterowanie
- „Ile kosztowało myślenie w tym tygodniu, o czym, i co z tego było warte?”
- „Przepal na to tysiąc dolarów, ale pytaj przy każdym skoku ×10.”
- „Ustaw, żeby wszystko było automatyczne, ale żebym widział każdą decyzję po fakcie.”
- „Dlaczego ta wartość ustawienia jest taka? Kto ją ustawił i jak ją zmienić?”

Niepewność i wejście
- „Nie wiem, czego chcę — zaproponuj.” / „Nie rozumiem swojego pytania; dopytaj mnie.”
- Wejście z ASR z literówkami, bez polskich znaków, z niedokończonym zdaniem.
- „Zrób to, co ostatnio, ale inaczej.”

Wspólny mianownik: żaden z tych przypadków nie wymaga nowej funkcji „pod przypadek”;
wymaga, żeby byty z sekcji 1, mechanizm powiązań z sekcji 2, przepływ z sekcji 3
i wielotorowość z sekcji 4 były dość bogate. Jeśli przypadek nie daje się wyrazić
w tych terminach, to brakuje abstrakcji, nie przypadku.

## 6. Decyzje strategiczne, które zostają u właściciela

Tylko takie, których nie da się zastąpić ustawieniem ani wyprowadzić z R1–R53:
- czy ChatADHD pozostaje produktem dla jednej osoby, czy od razu projektuje się pod
  podmioty wieloosobowe (rodzina, zespół) jako pierwszorzędnych użytkowników;
- czy aplikacja Python/Kivy na Androidzie ma żyć równolegle z Loom, czy zostać przepisana
  na Loom (dziś: równolegle, z poprawkami błędów);
- czy publiczne repozytorium ma pokazywać historię badań w pełni (z negatywami na gałęziach
  archiwalnych, jak dotąd), czy tylko linię produktu;
- ile pieniędzy i czasu idzie na badania modelowe względem produktu.
Każdą można podjąć później; do tego czasu silnik działa na presetach.

## 7. Co z tego wynika dla sesji

- S2 (synteza ekosystemu): paradygmat z sekcji 0 jest definicją; audyt GPT i inwentarz danych
  w kodzie są materiałem, nie celem; celem jest to, żeby każde repo i każda dana była
  adresowalna z grafu, a to, co użytkownik chciałby zmienić, było ustawieniem.
- S3 (przypadki graniczne): sekcja 5 jest zalążkiem; poszerzyć „aż do przesady”, dla każdego
  przypadku podać interpretację, miejsce w grafie, preferencje, powiązania, lukę; z luk
  wyprowadzić brakujące abstrakcje, nie listę funkcji.
- S1 (kod): pakiety z `THREADS` realizują sekcje 1–4 kawałek po kawałku; każdy pakiet
  cytuje, który fragment ducha wypełnia.
