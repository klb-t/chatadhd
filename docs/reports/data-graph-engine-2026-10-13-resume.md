# B — checkpoint wznowienia, 2026-10-13

To zapis roboczy P4b.1, **nie gotowy przyrost na aktualnym main**.
Baza lokalnego kodu: `18a8428bde8750fdb784233dde3ba62c614c5a09`.
Cała wcześniejsza opublikowana historia B jest już w main. Ostatni sprawdzony
fetch: `ba02fcb8a57523394498fa4c9fe3509ce570dc46` (13.10,18:26 UTC),188 dalszych
commitów. Nie przesuwano main ani gałęzi Claude'a.

## Zachowany kod i rzeczywiste dowody

11 plików P4b.1: jawny native/headless `prepare_source_context`, istniejące
R40/DefaultLayers, ConversationView/Catalog i ContextEngine. Polityka z dwóch
kanonicznych packów, osadzenie przez generator. Wynik/tekst w pamięci, osobny
receipt metadanych; brak nowej wysyłki modelowej lub ścieżki UI.

- Końcowy scope29/29 PASS, guard29 wykonanych/0 pominięć,203 native/6742 asercje
  i85 Python; oba generatory PASS. Dowody `P4b1-scope-wp_4ijol`,69.80s.
- Nowe testy: core12/251 asercji i Runtime8/550. Realny publiczny ZIP,
  store/reopen, wykluczenie R40, brak trwałego payloadu w badanym fresh store,
  wersje, selektory, zerowy budżet i odmowa przed źródłowym I/O.
- Legacy25 przypadków + INDEX: dokładne bajty bez normalizacji,0 HTTP;
  `P4b1-legacy-after-r4`. Build5 `P4b1-build-s9p4gw40` zmienił tylko test;
  `legacy-r4-reuse.json` wiąże identyczne hashe źródeł i archiwów runtime.
- Zachowane negatywy: scope1 27/29 (zakres integer i składnia fixture),
  scope2 28/29 (fixture mylił fizyczny run z encją method-run oraz surowy tekst
  z renderowanym evidence Markdown). Naprawy nie usunęły asercji ani ochron.
- W3/W4 `P4b1-w3-b5l4rr7h`: linkowanie nie powiodło się przez brak gold;
  **0 wykonanych testów**, brak PASS. Pełne dev/Clang/ASan/web obecnego P4b.1
  nie zostały wykonane. Dawna macierz6d1231bd jest historyczna.

Receipty kodu/config/binariów, pełne logi/JUnit i review zachowano w
`/workspace/.onboarding/`. Kopia przerwanego WIP ma archiwum
`data-graph-engine-resume-20261012-work-in-progress.zip`, SHA256
`9005290b56ece7c9fec9a24c27565c42c896b661764f0688a4ce6901c64f44f3`.

## Zmiana punktu odniesienia po pracy Claude'a

Aktualne źródła kierunku to `PLAN_2026-10-13.md`,
`GRAF_HUB_KOD_SILNIK_2026-10-13.md` i wymaganiaR43–R55 na przypiętym main.
APK i paczka serwera są według planu zbudowane (Actions38414374642),
bez wykonanego odbioru na urządzeniu. Fale use_model oraz actions/rules są już
wdrożone. Akcja atomowa i złożona to jeden kontrakt; dopasowanie wynika z cech
oraz wymagań, nie sztywnej listy per typ. R54: dostęp z okna rozmowy; R55:
treść i jej postacie, otwarty katalog rozwijany przez propozycje.

Dawny bezwarunkowy blok Send przy nieodczytanej historii został zastąpiony
zgodnie z R43; nie przywracać go przez przyjęcie starego tekstu B. P4b.1 nie
uprawnia do egressu, a projektowane P4b.2 musi użyć aktualnych use_model,
actions/rules, per-user Runtime i istniejących zgód/UsageGuard/storage.
Bieżący main zmienił runtime.h, context_engine.cpp, embedded profile oraz proof
R42. **Nie kopiować starego wygenerowanego pliku na nowy main**; złożyć źródła
i uruchomić obecny generator. Żadna stara zielona bramka nie certyfikuje tego
przyszłego połączenia. Własność aktywnego main/INDEX/STATE pozostaje u S1.

## Niezależny domknięty przyrost i kolejny krok

Watchdog A4-WD-002: kod03e85b3128eb6423d055c4005fab34ee97a143f5,
raportfed830c8d2e199450ef301da0b6f589d8af967a6; push potwierdzony.
Pełne lokalne lint/build/test651/651 PASS,0 pominięć, bez driftu. Raport
w tym repo Watchdog zawiera źródło/dependencies/receipty i ograniczenia;
nie jest to zmiana main ani nowa ocena naukowa.

Następny krok B: dopasować zachowany P4b.1 w osobnym worktree do aktualnego
stosu Claude'a, sprawdzając wspólny port modeli i katalog akcji przed nową
implementacją MethodRegistry. Jeden tor, małe konteksty i trwałe checkpointy;
nie uruchamiać starej automatycznej kolejki. W tej sesji zero płatnych modeli/CI.
