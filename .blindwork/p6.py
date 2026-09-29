# ---------------------------------------------------------------------------
# PART 6 - Polish diacritics: the sources above are ASCII-folded (as typed on a
# phone keyboard). ~55% of conversations are re-rendered WITH diacritics using
# this word list (keys are derived by folding), so the archive mixes both, like
# a real one. Only messages that look Polish are touched.
# ---------------------------------------------------------------------------
import unicodedata

_POLISH_WORDS = """
będę będzie biała białego biebrzańskie bieżący biorę błąd błędach błędów błędu błędy błonnika bóg ból bólu brać budzę
budżet być był była byłby było były bywają
cała całe całkowity całoroczne całorocznych całość całości cały ciągle ciągłość ciągły ciepło cofając cofnąć coś ćwicz
ćwiczenia ćwiczenie część często człowiek człowieka czuję czekać czynników czytać
dała dało decydował demontaż dług długiej długo długość długości dłużej dłuższy dobę dochodów doczepiać dodać dodałem
dodawać dokąd dokładnie dokończy dokupić dołożę dopisać doprowadzić dorzucę dość dostają dostarczają doświadczenia
dotyczącej dotykając dowód drożej drukarkę drżało duża dużej dużo dwóch działa działać działał działalność działalności
dzięki dzień dziesiętny dziś
ekipę ekranów elektronikę etykietę
faktów fakturę Francję
galerię gdańskiej gładka głębi głęboki głębokości głośność głowica głowicę główna głównie górę górna góry górze goście
gotowości Grażyna grzać grzeją
hałas hałaśliwe hałaśliwych histerezę historię hiszpańskiego hiszpańskim
idź iść
jadę jakieś jakiś jednostkę jeść jeśli jeżdżę
kalibrować kąpać kasuję każda każde każdego każdej każdy każdym każę kiełbasa kierować kilkadziesiąt kłamie klasę klientów
klikał kliknięcia klocków kłopot kogoś kolejność kolejności kołowy końca końcem końcowa końcowej końcu kończy kończyć
konfliktów koordynowałam korektę korygująca korygującej korygujący korzyści Kościuszki kosztów kotłownia kotwicę krawędzi
krótka krótki krótkie krótko kryminały krzaczyć krzyczała książce książek książka książkę książki księgowa księgowej
księgowości którą która które który których którym którymś ktoś kubków kupić kupił kupować kupujący kwaśny
ładnie ładowanie ładowarek ładowarkę ładuje łagodna łamiące łańcuch łańcucha łatwo łazience łazienki leżąc leży liczbę
liczyć literówkę listę łódź łóżku ludźmi luźna łyse lżejszy lecą
mają mała małe malinkę metodę metrów miałem mieć między mierzą mieście miesiąc miesiąca miesiące miesiącu migał migruję
młode młody moduł modułu moduły mogą mogę mógł mój montaż mówi mówił mówiła może możesz mózgu możliwość można mylić
myślałem
nagłówki nagłówków najczęściej najprościej najtańszych nakładka napięciowo napisać napisał naprawdę naprawić naprawię
narzędzie następny nauczyłem nazwałem negocjują niechęcią niedokończonych niedzielę Niemcy nietknięte nietknięty niewysłane
niezależność niezależny niż niższa niższe
obecność obejrzeć obietnicę obniżona obniżyć obowiązkowo obowiązuje obsługa obsługiwałam obsługiwane obsługuje oczyść odbiór
odbudować odczytać oddać oddała odłącz odlatują odliczać odliczyć odnowić odpływy odpowiedź odrzucać odrzucał odwołanie
ogarnąć oględziny ogóle ogród około określonym omów opłaca opóźnieniem orientują oryginał oś osób osobników oświadczenia
oświadczenie otworzyć
padł padło pamięci pamiętał państwo patrząc pchły pędzel pękła pełna pół płaska płasko płatki płatności pleców płótnie
płytki pływa pociągnięć pociągnięcia podgrzewać podjechać podróż podsmaż podsmażona podświetleniem podwójnie podwyższony
podzielić pogodzić poinformują pojemność pokaż pokaże pokazują pokazywać polecę północnej północy połowę pomiędzy pomóż
pomoże pomożesz pomysł poniżej popęka poprawiać poprawić poprawię poprosić porównaj porównanie porządkującego porządnego
porządnie postać poszła potrzebuję poważaniem powiększają powieść powieści powód powtórzyć powyżej później pracowałam prądu
pralkę próbowałem próg proponować prośba prostokątne proszę prowadząca prowadzę przeciążona przeciwpchłowa przeczekała
przedłużam przedstawić przegląd przeglądzie przekażę przekaźnik przekaźnika przekształcasz przełącz przełączam przełącznik
przerażony przerwę przerywając przesunąć przesunięciu przeszłości przeznaczeń przyczepność przygotowuję przypłynęła
przypływa przyszedł pyłu pytają
radę ręcznego ręcznie ręczny ręcznym reguła regułach regułę reguły rękopisów rekordów ręku remontują robią robić robię
roczników rodziców rosnące rosnącym również równoległych rozciąganie rozdział rozdziałami rozdziałów rozdziału rozdziały
rozjeżdżają rozliczeń rozstawiają różna różni różnica różnicy rozwiń rurę rzędu rdzeń
są sąsiedniej samochód schodów ścianie serów serwisowalność sformułujmy sieć sierść sierści siła skończę skończona
słaba słońca słów słowo słuchając słupkowy służą służy służyć słyszał słyszałem śniadania śnieg śnieżynki śpię śpimy
spisać spójność spójności sposób spóźnia sprawdź sprawdzić spróbuję sprzątanie sprzedaż sprzedaży sprzęt sprzętowy średnich
średnik średniowiecznych środek środku stację stała stawkę stłuczkę straciłem stronę stukała świat światło świeże sygnału
szczególnie szczotkę szkło szkodę sztukę sztywność szufladę szybkości
tabelę tabliczkę telefonów teściowej też tłumaczy tłuszczów tłuszczu tortownicę treść treści trochę trzymać turystów tydzień
uciążliwość uczą uczę udostępniane udziałem układ ulgę umowę umówieniu utknąłem uwagę uzależnień użycia użyj użytkowej
użytkownikowi używa używać używaj używamy używany urządzenia urządzenie urzędu urzędzie
wannę warstwę wartość wartością warunków wątek wątki wątpliwości ważne wcześniej wcześniejsze wdrożyłam wędlin widoków widzę
widział więc więcej wieczór wiedział wiedzieć większość większości większym wilgotność Wiślany włącz włączać włączony własne
właśnie własny wodę wokół wolę wołowina worków wpłacie wracają wracając wróć wrzątku wrześniu wrzuciłem wskazać wskazówki
wskazujący wskazywać wspólna wspólne wspólny wspólnym wstęp wstępnie wszędzie wybór wyboru wybrałem wycenę wyciągnij wyjątek
wyjątkowym wyjeździe wykryć wykrywać wyłącz wyłączał wyłączenie wyłączony wyłączonym wymagań wymianę wymień wymieniam
wymienić wymienię wypuścić wyrażeniach wyraźniejsza wyraźnych wysłania wysłanie wysokość wystawę wystawiają wystawić
wystawił wyświetlaniu wysyła wysyłaj wysyłają wysyłka wytrzymają wywalić wywołanie
zabrał zacząć zacznę zadziała zadziałać zadzwonić zagęszczające zainstalować zakończenia zakończyć zakręć zaktualizować
załącz załączam załaduj zależą zależnie zależności zależy zaliczkę zamarzł zamarznąć zamawiać zamienił zamknięcia zamknięciu
zamknięte zamów zamówienia zamówienie zaokrąglaj zaokrąglenia zapamiętuje zapewnić zapisałem zaporę zaproponować żarty
zarządcy zasłoniętego zasnąć zatwierdzeń zauważyłem zbóż zdarzeń zdjęcia zdjęcie zdolności że żeby żebym żebyś zepsuć
zeszłego zeszły zgadzały zginąć zgłasza zgłoś zgłosić źle złożoną złożyć zły zmianę zmień zmieniać zmienić zmienił żadnego
zniknąć zobaczyć żona żonie żony zorganizować zostają zostawić zostawię zresetować zrób zrobić zrobię źródłowe zróżnicować
zróżnicowania zsumować żurawi żurek żurawie zużyte zwalniają związku zwiedzającym zwiększając zwłaszcza zwróć zwrócił
zwymiotował życiu życzenia zł zła
chcę chciałbym cenę już opisać opróżnianie pipetę pisać pisał planuję poproś pudełka skręty sobotę znacząca
zwykły zwykłego zwykłych zwykłym
""".split()
DIAC: dict[str, str] = {}
for _w in _POLISH_WORDS:
    _k = unicodedata.normalize("NFKD", _w.replace("ł", "l").replace("Ł", "L"))
    _k = "".join(ch for ch in _k if not unicodedata.combining(ch)).lower()
    DIAC.setdefault(_k, _w)
for _k, _w in {"os": "oś", "slowo": "słowo", "slow": "słów", "sa": "są", "sie": "się", "wiec": "więc",
               "tez": "też", "ze": "że", "moze": "może", "mozna": "można", "zl": "zł", "niz": "niż", "byl": "był",
               "pol": "pół", "maja": "mają", "lezy": "leży", "kaze": "każę"}.items():
    DIAC[_k] = _w
# words that must stay ASCII because two Polish words fold to the same form
for _k in ("lodzie", "umowie", "sum", "koci"):
    DIAC.pop(_k, None)

