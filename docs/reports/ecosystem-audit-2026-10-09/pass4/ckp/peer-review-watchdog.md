# Niezależny przegląd narzędzia Watchdog PASS4

Przegląd statyczny przez agenta CKP, 2026-10-09. Sprawdzono `watchdog/probe.ts`,
`watchdog/run.py`, REPORT/findings oraz rzeczywiste RunOrchestrator, exportJson i
lokalizacje route/schematu. Nie uruchamiano ponownie zestawu: poniższe uwagi
wynikają z konkretnych zależności harnessu, nie z nowego wyniku produktu.

**A4-WD-001: podstawowy oracle jest poprawny.** Nieprawidłowy pin zostaje
odrzucony przez rzeczywistego wykonawcę bez schema, a schema pozwala wykonać
inną ścieżkę po usunięciu danych. To dowód utraty wybranego kontraktu, nie dowód
obejścia source ownership. Dopuszczenie `!validation.success` jako naprawy jest
właściwe: jawne odrzucenie nieobsługiwanych pól spełnia ten wariant kryterium.

Konieczne rozdzielenie zależności próbek: gdy przyszły schema zachowa pin,
prawidłowy executor zakończy `accepted` statusem FAILED. Następne, niezależne
próby odczytują `art.getManifest(accepted).object_uri` poza funkcją `check` i
przerwą cały program. Próby 002, metamorfizm i reopen powinny mieć osobny,
zawsze jawnie poprawny `safeInput` run. To zabezpiecza odbiór przed sytuacją,
w której poprawka001 uniemożliwia wykonanie pozostałych bramek. Gdy schema
odrzuca wejście, obecny fallback do safeInput już działa; brak dotyczy drugiej
poprawnej strategii, zachowania pól i odmowy wykonawcy.

Akceptacja001 sprawdza tylko `method_spec_id`. Warianty `plan` i
`personal_credentials` nie mają osobnej bramki. Można zachować wspólne finding ID
jako przyczynę strippingu, lecz nie ogłaszać pełnej naprawy wszystkich pól po
jednym PASS. Minimalna korekta raportowania: wskazać dokładnie odebrany wariant;
pełniejsza korekta testów: niezależne żądania dla pól o rzeczywistych konsumentach.
Przy zachowanym błędnym pinie warto wymagać rzeczywistego FAILED i dowodu odmowy,
bo `not COMPLETED` dopuszcza również zawieszone/niezakończone wykonanie.

**A4-WD-002: zachowanie manifestu jest potwierdzone przez rzeczywisty runtime.**
Analiza czyta source_run_id; finalizeManifest czyta bieżący run_id. Zachowanie
`Hi.isMissing` i source_run_id jest uczciwie zaznaczone. Nie wolno nazywać tego
utratą całej informacji o brakach czy wszystkich referencji.

Granica eksportu jest słabsza: `exportJson` dostaje ręcznie złożony przez harness
obiekt, który powtarza obecne zapytania route. To wykonany serializer z wejściem
od testu, nie wykonana logika wyboru obserwacji route. Jeśli naprawiony zostanie
sam route, test może nadal generować stare błędne wejście. Należy rozdzielić
wynik: runtime manifestu, serializer z wejściem testowym, przegląd źródłowy route;
albo wywołać zarejestrowany rzeczywisty handler bez zewnętrznego transportu.
Akceptacja002 obecnie dotyczy manifestu, nie eksportu route — zakres zamknięcia
findingu musi to zachować.

Wymaganie jest o sprawdzalnym pochodzeniu i jawnym stanie, nie o kopiowaniu całych
źródeł. Inline missing/flags/input_hashes to poprawna mała naprawa aktualnego
kontraktu. Jeżeli B wprowadzi wersjonowaną rozwiązywalną referencję wraz z jawnym
statusem braków, nie odrzucać jej tylko dlatego, że tablice zmieniły miejsce;
wiązać test z rzeczywistym resolverem i nadal odrzucać pozorny pełny pusty wynik.

**A4-WD-003:** permutowana jest kolejność niejednoznacznych obserwacji o tym samym
entity/role, bez semantyki kolejności wiadomości. Skutek last-wins i brak jawnej
polityki są uzasadnionym przedmiotem audytu. Obecny test wymaga odmowy bez polityki;
nie może oznaczać zakazu jawnie wybranego, wersjonowanego wariantu rozstrzygania.
Nie wolno przy okazji zmieniać zablokowanej metody naukowej ani dawnych wyników.

Powyższe zalecenia przekazano root jako przegląd harnessu. Ten plik nie dowodzi,
że root je już wdrożył; późniejszy raport/receipt modułu Watchdog określa stan.

## Ponowny przegląd po korekcie harnessu

Sprawdzono zmieniony probe bez ponownego wykonania testów. `reuse` jest teraz
oddzielnym prawidłowym runem używanym przez manifest, metamorfizm i reopen;
akceptacja/odmowa `accepted` z001 nie odcina tych bramek. Eksport jest pobierany
przez rzeczywisty handler z `buildApiRouter`, z przechwyconą odpowiedzią.
Dwie krytyczne uwagi o zależnościach testów i kopiowaniu zapytań route są tym samym
zaadresowane. Nie jest wykonywane auth/middleware/HTTP; probe jawnie to zaznacza.
Aktualny wynik wykonania wskazuje `watchdog/receipt-reviewed.json`, osobno od
historycznego `receipt.json`.

Nadal obowiązuje ograniczony zakres kryteriów:001 odbiera pin metody,002.B odbiera
manifest. Osobne pola plan/personal_credentials oraz poprawiony eksport route
wymagają odpowiednich asercji przed deklaracją ich pełnej akceptacji.
