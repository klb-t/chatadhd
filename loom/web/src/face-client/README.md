# Face → ChatADHD: port klienta

Baza: `2c789e5847d1057ce284a35ae6e07350761588b3`. Przyrost implementacyjny,
bez researchu, drugiego controllera, backendu, renderera i zmian istniejącego UI.

`port.ts` przyjmuje jawny, wersjonowany zestaw dziewięciu liczb. Kolejność i domeny
są zgodne z `face-controller-reference/async_controller.py`: indeksy 4, 5, 8
mają zakres [0,1], pozostałe [-1,1]. Brak konwersji tekstu, clamping, embeddingów,
neutralnego fallbacku, obliczeń emocji, celu optymalizacji ani loss. Wejścia są
kopiowane i zamrażane. Zmiana kontekstu usuwa poprzedni wybór Face; nie zmienia
ID, gałęzi, źródeł, wersji wiadomości ani selektora ChatADHD.

Biblioteka jest dostarczana jako tablica `{id, version, label: {pl, en}, params}`
przez `FaceParameterPreview.library`. `params` to pełne dziewięć liczb. Para
id/version identyfikuje niezmienny zestaw; nowa treść wymaga nowej wersji.
Wbudowana biblioteka JSON jest celowo pusta: referencja nie dostarcza nazwanych,
autorytatywnych zestawów. Atrapy liczb występują wyłącznie w teście mechanizmu.
Brak biblioteki nie blokuje dostarczenia jej przez integratora i nie oznacza
oczekiwania na załączniki właściciela.

## Granica transportu

Wstrzykiwany `FaceConnector(onDisplay)` zwraca jedną sesję `post(event)/close()`.
Host implementuje nieblokującą komunikację z istniejącym controllerem. Metody
muszą jedynie kolejkować pracę i wracać; render/join/czekanie należą do hosta,
nigdy do React ani portu. Nie ma domyślnego endpointu, fetch, WebSocket,
reconnectów, kolejki optymalizatora ani emulacji produktu.

Każde zdarzenie `context` ma `contextId`, `version`, `explicit9Params`, `evidence`.
Identyfikator powstaje lokalnie (`crypto.randomUUID`, bezpieczny kontekst
przeglądarki), nie zawiera ID rozmowy. Wersja wzrasta przy każdej invalidacji
i każdym wyborze, także A→B→A. Null parametrów oznacza invalidację, nigdy neutralną
ekspresję. Jedna mikrokolejka przechowuje tylko najnowsze zdarzenie, które jeszcze
nie opuściło portu. Nie anuluje ani nie optymalizuje aktywnej pracy hosta.

Host zwraca `display` z dokładnie tym samym contextId/version i pełnymi parametrami
oraz evidence. Port przyjmie odpowiedź dopiero po przekazaniu odpowiadającego
zdarzenia do `post`. Odpowiedzi starych epok/sesji i callbacki po dispose są
odrzucane. `dispose` usuwa listenerów i slot, zamyka sesję raz, bez join.
Wszystkie aktualne stany są dostępne przez `getSnapshot/subscribe`.

Evidence jest rozłącznym kontraktem: `user` ma parameterSetRef/libraryVersion,
`observed` ma observationRef, `inferred` ma methodRef/premiseRefs/
expectedPropertyRef/confidence (null = nieznane). Są to wyłącznie nieprywatne,
niezależne od rozmowy referencje. Host nie może wkładać do nich tekstów ani
oryginalnych ID. Port nie sprawdza źródeł ani kalibracji confidence; panel opisuje
to jako deklarację backendu. Nie promuje wniosku do obserwacji. Panel tabelą
pokazuje wyłącznie wybrane wejście biblioteki, nigdy zmienione parametry backendu.

Brak connectora daje `disabled/NOTRUN`, również po lokalnym wyborze wektora.
Samo podłączenie portu nie daje PASS. `received` oznacza odebrane zdarzenie,
nie sprawdzony render, pomiar jakości, prawdziwy model czy odbiór produktu.
Błędy hosta nie ujawniają payloadów wyjątków.

## Hook proposal dla jedynego integratora

Propozycja `ChatView.hook-proposal.patch` pozostaje w prywatnym handoff autora, **nie została nałożona ani włączona do produktu**.
Prywatny `proposal-binding.json` wiąże stare/proponowane bajty z bazą. `git apply --check`
przeszedł; przy zmianie ChatView przez innych autorów integrator przegląda punkty
wpięcia i aktualizuje kontekst patcha.

Propozycja dodaje opcjonalne `faceClient: {preview, library, connector}`. Brak
`preview: true` ukrywa panel. Bez connectora panel pozostaje lokalny/NOTRUN.
`useFaceClient` dostaje tylko pusty token tożsamości. Jego zmiana jest memoizowana
na rzeczywistych zależnościach klienta: rozmowa, gałąź, widoczne wiadomości,
ustawienia następnego requestu oraz carrier `waitingTurn`. Osobny licznik
`pendingTurnVersion` wykrywa także zbatczowane A→B→A cariera, którego końcowy
obiekt może być taki sam. Żadna wartość zależności nie jest serializowana.
Hook invaliduje w layout effect, ukrywa dawny podgląd już podczas renderu
i zamyka port przy unmount/zmianie connectora, również w cyklu React StrictMode.

To obserwacja lokalnego stanu tej instancji ChatView. Istniejący SourcePartsPanel
nie publikuje rewizji globalnego selektora do ChatView; niezależna zmiana w tym
panelu/innej karcie może być widoczna dopiero przez refreshKey/nowe wiadomości.
Nie dodano pollingu ani odczytu konfiguracji. Integrator może przekazać do tokena
istniejącą rewizję selektora, jeżeli wspólny przyrost ją udostępni. Nie twierdzimy,
że obecny patch dowodzi pełnej synchronizacji globalnego selektora.

## Weryfikacja

`node loom/web/src/face-client/sanity.cjs`: istniejący systemowy Babel, bez
instalacji, konfiguracji repo i web buildu. Przekształca trzy nowe TS/TSX w temp,
sprawdza JSON/PL/EN i uruchamia osiem testów faktycznego portu z atrapą transportu.
Nie kompiluje React ani zależności aplikacji. Temp jest usuwany w finally.
Wynik: 3/3 syntax, 8/8 testów PASS. Pierwsze podejście Node strip-types
zatrzymało się na ERR_NO_TYPESCRIPT (0 wykonanych przypadków); nie jest PASS.

Typecheck, montaż/unmount hooka w React/browser, integracja ChatView, controller,
render, rzeczywista biblioteka parametrów, backend/model: NOTRUN. Integrator
wykona jeden wspólny batch z pozostałymi trzema ChatUI: typecheck/build oraz
przeglądarkowe zmiany kontekstu/ABA/unmount/StrictMode, zachowanie ID/selektora,
brak tekstów w transporcie i widoczny disabled/NOTRUN bez backendu.

Closed manifest zawiera dokładną listę nowych payloadów z rozmiarem/SHA256,
osobno przypina wejście Python/PROVENANCE. Manifest i jego plik checksum są
kopertą, nie własnymi payloadami. CHECKPOINT jest końcowym przekazaniem;
nie zmieniono docs/STATE.md, runtime, budżetu, VM ani deadline.

## Odbiór podzbioru przez integratora

Przejęto port, hook, opcjonalny komponent, dane i testy. ChatView pozostaje bez
zmian: brak skonfigurowanego hosta i biblioteki, więc w tym wydaniu port nie jest
montowany. Nie dodaje subskrypcji ani pracy do wyłączonej opcji. Typecheck jest
wspólny z trzema panelami UI; nie stanowi odbioru Face backendu/renderera.
Pełny hook wymaga późniejszego jawnego opt-in i bramki React/context/privacy.
