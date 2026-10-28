# Korzystanie bez logowania (R103) — darmowe API, pula kluczy właściciela, historia u użytkownika

Projekt, 2026-10-28. Podstawa: R103 (`docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` §R103, `docs/spec/spec.json`)
z trzema dopiskami właściciela z tego samego dnia (budżet próbny dla każdego; wdrożenie produkcyjne po trybie gościa i profil
z pamięci lokalnej użytkownika; logika za darmo dla użytkowników niekomercyjnych) oraz R105 (gość od razu w pełnym interfejsie
z najlepszym dostępnym darmowym modelem; po pierwszej odpowiedzi — standardowo, bez metryki wartości, z której właściciel
zrezygnował — nieblokująca uwaga, że nie jest zalogowany i gdzie rozmowa może przepaść), R18/R20 (izolacja kont), R100 (dane
do dostawców tylko za zgodą), R101 (pulpit), zasada wydatków z `CLAUDE.md`. Stan kodu sprawdzony na gałęzi
`claude/chataddhd-cpp-loom-core-IRGRN` (commit 608228a3). Każdy konkretny wybór poniżej jest **presetem** (wartość domyślna w
danych, do zmiany) albo **przykładem**; mechanizm ma obsłużyć każdą konfigurację. Fakty o cudzych usługach (limity, warunki
użycia danych) są **obserwacjami datowanymi** z adresem źródła, nie ograniczeniami projektu — mogą się zmienić i wtedy zmienia się
plik danych, nie kod.

## 0. Słowa robocze (wyjaśnione raz)

- **Gość** — osoba korzystająca z aplikacji bez konta. Nie „konto anonimowe” (właściciel: „to nie konta anonimowe tylko po
  prostu funkcjonalność bez logowania”): nie ma loginu, hasła ani wpisu w bazie kont.
- **Sesja gościa** — ciasteczko z losowym żetonem, jak sesja konta, ale wskazujące na katalog roboczy gościa, nie na konto.
- **Katalog roboczy gościa** — zwykły katalog danych Loom (KOD ≠ DANE), zakładany na czas sesji; nie jest źródłem prawdy
  historii (tym jest schowek po stronie użytkownika, §4), chyba że gość wybierze inaczej.
- **Schowek historii** — miejsce, w którym gość trzyma rozmowy: przeglądarka, plik, jego własna chmura lub dysk.
- **Dostawca darmowy** — dostawca modeli, który za wywołania nie liczy pieniędzy (ma za to limity i warunki użycia danych).
- **Warunki danych** — co dostawca robi z treścią zapytań: czy przechowuje, czy ktoś czyta, czy uczy na nich modele.
- **Pula kluczy** — klucze API właściciela, które serwer wypożycza gościom według przydziałów; gość klucza nigdy nie widzi.
- **Przydział** — reguła „komu ile”: budżet (kwota/okres), zakres modeli, co się dzieje po wyczerpaniu; wiąże się z gościem
  przez kod lub link zaproszenia albo obowiązuje wszystkich gości.
- **Własny klucz (BYOK)** — klucz, który gość wkleja sam; zostaje tylko u niego, serwer go przekazuje i nie zapisuje.
- **Dowód pracy** — małe zadanie obliczeniowe, które przeglądarka rozwiązuje w tle przed założeniem sesji; kosztuje ułamek
  sekundy człowieka, a kilka godzin tego, kto chce otworzyć sto tysięcy sesji. Zastępuje captchę.
- **Paragon „Co wysłaliśmy”** — istniejący podgląd tego, co poszło do dostawcy (`provider_request_preview`).

## 1. Sedno

R103 to trzy niezależne mechanizmy, które razem dają aplikację bez konta:

1. **Tryb gościa w serwerze i w web** — trzecia droga obok „bez kont” (jeden użytkownik) i „konta”: serwer w trybie kont
   przyjmuje też sesje bez konta, każda w swoim katalogu roboczym, bez dostępu do czegokolwiek innego.
2. **Źródła modeli dla gościa** — w kolejności: własny klucz gościa, przydział z puli właściciela, dostawcy darmowi, model
   lokalny na maszynie. Wszystko jako dane (manifesty dostawców już są: `provider_manifests`, `loom/include/loom/providers.h`),
   każdy wpis z warunkami danych pokazanymi przed pierwszym wysłaniem.
3. **Historia u użytkownika** — serwer domyślnie nic nie zatrzymuje po sesji; rozmowy żyją w schowku, który gość wybrał
   (preset: przeglądarka), z eksportem i importem w formatach, które aplikacja już ma, oraz z opcją szyfrowania.

Zasada właściciela „dajemy wszystko, za co nie trzeba płacić” jest czytana dosłownie: gość dostaje **całą aplikację** (graf,
gałęzie, wiele modeli, tory R102, import, eksport, pamięć, wyszukiwanie), a jedyną różnicą wobec konta jest to, kto płaci za
modele i gdzie leży historia. Nic nie jest wyłączone „bo gość” — poza tym, co dotyka cudzych danych lub cudzych pieniędzy.

## 2. Co już jest, czego brak (obserwacja datowana 2026-10-28, commit 608228a3)

Jest:
- Tryb kont: `--accounts-root`, baza kont (SQLite) obok katalogów użytkowników `<root>/<users_subdir>/<id>/`, sesje jako losowe
  żetony (w bazie tylko SHA-256), Argon2id, zaproszenia jednorazowe (`create_invite`, kod przechowywany jako SHA-256), dławienie
  logowań per login i per adres (`loom/include/loom/accounts.h`, `loom/src/accounts/`). Rejestracja: `open | invite | closed`
  (`loom/data/runtime/server_accounts.pack`, preset, nie reguła).
- Filtr przed trasami: bez sesji każda trasa `/api/*` poza `auth/me`, `healthz`, `auth/login`, `auth/register` odpowiada 401
  „sign in required” (`loom/server/src/app.cpp` ~494–512). Jeden kontekst Loom na użytkownika (`context_for_user`, ~330–356),
  otwierany przez `loom_init_ex` z opcjami izolacji R18 (`confine_paths`, `block_private_hosts`, pułapy ZIP, CSRF po Origin).
- Serwer odmawia nasłuchu poza localhostem bez kont lub żetonu (`main.cpp` ~243), chyba że `--allow-unauthenticated-public`.
- Web: maszyna stanu sesji (`loom/web/src/context/auth-state.ts`): `open | signed_out | signed_in | unverified`; ekran logowania
  i rejestracji; po pierwszym 401 przełącza się na logowanie. W `localStorage` leżą tylko układ okien i ustawienia czatu, nie
  rozmowy (`App.tsx`, `workspace/state.ts`, `context/chat-settings.ts`).
- Dostawcy jako dane: `ProviderManifest` (id, adres, nazwa sekretu, schemat uwierzytelnienia, zdolności), wbudowani:
  openrouter, anthropic (batch), groq (mowa), google_speech, ocr_space, github; `llm_provider` = `openrouter | local | <dodany>`,
  `local_model_base_url` dla serwera zgodnego z OpenAI na tej maszynie (llama.cpp, Ollama) (`settings_fields.pack` ~600–670).
- Księga zużycia i limity wydatków per katalog danych: `usage_spend_limits` jako lista `{period, amount, currency, on_exceed}`,
  egzekwowana centralnie przy każdym wywołaniu modelu (`loom/include/loom/usage_spend.h`, `action_spend.h`); strażnik ×10.
- Eksport rozmowy w kilku formatach (`GET /api/conversations/:id/export`, ~1250) i import wielu formatów (`POST /api/import`).
- Wdrożenie publiczne: `https://chat.34-158-117-249.sslip.io` → Caddy → `loom-server` na `127.0.0.1:8787`, tryb kont (konto
  `marcin`), dane w `/var/lib/chatadhd`; maszyna 8 vCPU / 31 GB RAM (`docs/ops/VM_DEPLOY_2026-10-23.md`, `docs/ops/places.json`).

Brak: sesji bez konta; katalogów roboczych z czasem życia; przekazywania kluczy z puli bez zapisu w katalogu użytkownika;
własnego klucza „na żądanie” (dziś klucz leży w `secrets.json` katalogu); wpisów darmowych dostawców z warunkami danych; ekranu
zgody na warunki danych przed wysłaniem; schowka historii w przeglądarce (IndexedDB) i w chmurze użytkownika; ograniczeń
nadużyć innych niż dławienie logowań i rozmiar ciała; dowodu pracy; księgi puli; modelu lokalnego na maszynie (ustawienie jest,
usługi nie ma); testów izolacji gościa.

## 3. Tryb gościa w serwerze i w web

### 3.1 Co to jest sesja gościa

Serwer w trybie kont dostaje nowy przełącznik w profilu `server_accounts`: `guests` (dane, presety):

```
guests: {
  enabled: false,                       // preset na dziś; na publicznym wdrożeniu włączany w pakiecie B3 (§10), po ochronie B2
  start: "auto",                        // R105: pierwsze wejście od razu zakłada sesję i pokazuje pełny interfejs; "explicit" = przycisk
  first_answer_notice: {on: true, ask_again: "per_device"},   // §3.5: uwaga po pierwszej odpowiedzi, z „nie pytaj więcej”
  workdir: {
    root: "<accounts_root>/guests",     // katalog roboczy gościa = zwykły katalog danych Loom
    idle_ttl_seconds: 86400,            // po tylu sekundach bez ruchu katalog znika (preset; 0 = nigdy sam)
    max_bytes: 268435456,               // pułap operatora na rozmiar katalogu (0 = brak)
    keep_after_session: "ask"           // ask | never | always: czy zostawić kopię na serwerze po końcu sesji (preset: zapytać gościa)
  },
  secrets_routes: "write_only_session", // §3.4: gość nie czyta sekretów; może je podać na czas sesji
  model_sources: ["byok", "pool", "free", "local"]   // kolejność źródeł modeli (§4.1), lista otwarta
}
```

Sesja gościa jest tym samym ciasteczkiem co sesja konta (ten sam `cookie.name`, te same flagi), tylko w bazie kont rekord
sesji ma `user_id = NULL` i `guest_id`. Dzięki temu cały istniejący filtr (CSRF po Origin, żeton tylko jako SHA-256, wygasanie,
unieważnianie) działa bez drugiej ścieżki. `GET /api/auth/me` odpowiada `{accounts: true, authenticated: false, guest: {id,
started, expires, grant?}}`; web dostaje nową fazę `guest` obok `signed_out` i `signed_in`.

Przejście gość → konto: gość, który później zakłada konto, dostaje pytanie, czy przenieść katalog roboczy (przeniesienie
katalogu, bez kopiowania) — nic nie przepada. Przejście konto → gość nie istnieje (konto można po prostu nie używać).

### 3.2 Izolacja (konieczność, nie wymyślony limit)

Gość nie może dotknąć danych kont ani innych gości. To nie jest ograniczenie użytkownika, lecz ochrona cudzych danych (R18) i
jedyny warunek, pod którym wolno wpuścić kogokolwiek bez konta na maszynę z danymi właściciela. Mechanizm jest ten sam co
między kontami, bo katalog gościa jest zwykłym katalogiem danych:
- `context_for_user` dostaje bliźniaka `context_for_guest`: ten sam `loom_init_ex` z `confine_paths` (korzenie ścieżek =
  tylko katalog gościa), `block_private_hosts` (gość nie użyje serwera do skanowania sieci wewnętrznej), pułapy ZIP i brak
  `git_allow_repo_config`; nic z tego nie jest nowym kodem izolacji, tylko nowym wywołaniem istniejącego.
- Trasy, których gość nie dostaje: wszystko pod `/api/admin/*` i `/api/accounts/*` (gdy powstaną), `GET /api/secrets` i
  `GET /api/secrets/:k/has` dla kluczy z puli (§3.4), dzienniki serwera (`expose_server_logs` jest już `false`).
- Test akceptacyjny izolacji (pakiet B1): dwie sesje gości i jedno konto na jednym serwerze; każda próba odczytu cudzej
  rozmowy, cudzego eksportu, cudzego pliku przez ścieżkę względną i cudzego sekretu kończy się 401/403/404, nigdy treścią;
  po `idle_ttl` katalog nie istnieje i żeton nie działa.

### 3.3 Co gość może z całej aplikacji

Wszystko, co konto, poza trzema rzeczami, które z natury dotyczą konta: ustawieniami serwera, kluczami innych osób i trwałością
na naszym dysku bez wyraźnego wyboru. Konkretnie (lista przykładów; cokolwiek nowego w aplikacji dostaje gość domyślnie, a
wyjątek wymaga uzasadnienia w danych):
rozmowy z każdym modelem dostępnym ze źródeł §4; strumieniowanie i rozumowanie; gałęzie wersji; graf wiedzy i pamięć; tory
wielotorowe R102; import własnych archiwów (ChatGPT, Claude, pliki) do katalogu roboczego; eksport w każdym formacie; akcje i
flow; ustawienia osoby (motyw, język, modele, limity wydatków własnego klucza); paragony „Co wysłaliśmy”.

Działania, które kosztują czas maszyny (import dużych archiwów, analiza semantyczna, osadzenia, model lokalny), są u gościa
objęte pułapami operatora z §7 — jako presety, z komunikatem „to kosztuje X, w tej chwili wolne jest Y” zamiast cichej odmowy.

**Za darmo dla użytkowników niekomercyjnych (właściciel: „logikę całą, grafy i tak dalej to dajemy za darmo dla użytkowników
niekomercyjnych”).** Cała logika aplikacji — graf, pamięć, flow, tory, import, eksport — jest dla gościa niekomercyjnego
bezpłatna. Użycie komercyjne to **osobne warunki, które właściciel ustali później**; dziś nic z tego nie jest w kodzie
egzekwowane i żadne ograniczenie nie jest wymyślane: zapis sprowadza się do jednego zdania w warunkach użycia na pierwszym
ekranie („bezpłatne do użytku niekomercyjnego; użycie komercyjne — napisz”) i do pola w danych `terms.commercial_use:
"ask_owner"` (otwarta wartość; gdy właściciel zdecyduje, zmienia się tekst i pole, nie kod). Odwrócenie: usunięcie zdania.

### 3.5 Pierwsze wejście gościa (R105): pełny interfejs, najlepszy darmowy model, uwaga po pierwszej odpowiedzi

Gość nie widzi żadnej bramki ani ekranu „zaloguj się lub zacznij jako gość”: pierwsze wejście pokazuje **standardowy
pełnoekranowy interfejs** (okna, zamykanie, pulpit — R101) i pole rozmowy. Pierwsza wiadomość idzie od razu do **najlepszego
dostępnego darmowego modelu** według reguły wyboru z danych (§4.5). **Po pierwszej odpowiedzi** — standardowo, zawsze,
bez żadnej metryki wartości odpowiedzi (właściciel: „niepotrzebnie ta metryka, standardowo po odpowiedzi, nie będziemy
komplikować”) — w rozmowie pojawia się jedna nieblokująca uwaga: nie jesteś zalogowany; ta rozmowa jest w tej chwili tylko
w kopii roboczej; bez wybrania miejsca zapisu może przepaść **po okresie podanym uczciwie z faktycznie dostępnego schowka**:
przy zapisie w przeglądarce — zgodnie z tym, co przeglądarka zgłasza (`navigator.storage.estimate()`, wynik `persist()`,
znana polityka eksmisji danej przeglądarki jako wpis w danych z datą), a bez schowka — do końca sesji albo do upływu
`idle_ttl` kopii roboczej na serwerze (preset 24 h). Uwaga ma przyciski „Wybierz, gdzie zapisać” (§6), „Zaloguj / załóż
konto” i „Nie pytaj więcej” (zapamiętane na urządzeniu, odwracalne w ustawieniach). Nie zatrzymuje rozmowy i nie wraca
przy każdej wiadomości (`first_answer_notice.ask_again`: `per_device` preset; `per_session`, `never` do wyboru).

### 3.4 Sekrety gościa

Katalog gościa ma własny `secrets.json` jak każdy katalog danych, ale:
- klucze z puli (§5) **nigdy tam nie trafiają**: serwer przekazuje je kontekstowi w pamięci (opcja `loom_init_ex`
  `credential_overlay`, czytana przez `ProviderRegistry` przed `secrets.json`), więc żadna trasa ani eksport ich nie ujawni;
- własny klucz gościa (BYOK) ma dwie formy, do wyboru gościa: **na czas sesji** (zapis w pamięci kontekstu, znika z sesją;
  preset) albo **w katalogu roboczym** (jak dziś; znika z katalogiem). Web trzyma go w `sessionStorage` lub, gdy gość wybierze,
  w IndexedDB zaszyfrowany jego hasłem (§4.4). `GET /api/secrets` zwraca gościowi tylko nazwy jego własnych kluczy, nigdy puli.

## 4. Źródła modeli dla gościa: dostawcy darmowi jako dane

### 4.1 Kolejność źródeł (preset, lista otwarta)

`guests.model_sources` to kolejność, w której aplikacja proponuje źródło przy pierwszej rozmowie i do której wraca po
wyczerpaniu: `byok` (własny klucz: bez limitów naszych, limity dostawcy), `pool` (przydział z puli właściciela), `free`
(dostawcy darmowi na kontach właściciela), `local` (model na maszynie). Gość zmienia kolejność i wyłącza źródła w ustawieniach;
każda zmiana źródła w trakcie rozmowy jest widoczna przy wiadomości (model i dostawca już są w metadanych wiadomości).

### 4.2 Wpis dostawcy darmowego (dane)

Nowy plik `loom/data/runtime/free_providers.pack` (profil czasu wykonania jak inne packi; nadpisywalny nakładką w
`<accounts_root>/profiles`), każdy wpis rozszerza `ProviderManifest` o:

```
{
  id, display_name, base_url, auth_secret, adapter,          // jak ProviderManifest
  tier: "free",
  models: [{id, context, notes}],                            // lista przykładów, odświeżana z API dostawcy, gdy ma models.list
  limits: {rpm, rpd, tpm, tpd, reset: "utc_midnight" | "pacific_midnight" | "rolling", shared_with: "account" | "project"},
  data_terms: {                                              // §4.3 — pokazywane PRZED pierwszym wysłaniem
    retention: "none_by_default" | "days:30" | "unspecified",
    human_review: true | false | "unspecified",
    training: "no" | "yes" | "opt_out_available" | "unspecified",
    region_rules: [{region, rule}],
    source_url, observed_at, source_rating                   // obserwacja datowana (R59)
  },
  availability: {regions_blocked: [], reason},
  fallback_order: 10                                         // pozycja w kolejce „gdy wyczerpane”
}
```

Obserwacje zebrane 2026-10-28 z oficjalnych stron (wszystkie to wpisy danych do odświeżania, nie decyzje):

| Dostawca | Co za darmo (obserwacja) | Warunki danych (obserwacja) | Źródło |
|---|---|---|---|
| OpenRouter, modele `:free` | 20 zapytań/min; 50/dzień, gdy konto kupiło łącznie < 10 kredytów, 1000/dzień przy ≥ 10; licznik `free_model_daily_requests` w `GET /api/v1/key` | Osobne ustawienie konta „nie kieruj do dostawców uczących na danych” dla modeli darmowych; per zapytanie można wymagać polityki danych dostawcy (strona wskazuje `provider-selection#requiring-providers-to-comply-with-data-policies`; dokładna nazwa pola do potwierdzenia w pakiecie B4) | openrouter.ai/docs/api-reference/limits; …/features/privacy-and-logging |
| Groq (czat) | Plan Free i Developer rozróżnione; tabela Developer np. `openai/gpt-oss-120b` 30 RPM / 1 000 RPD / 8 000 TPM / 200 000 TPD; wartości planu Free nie dały się odczytać z pobranej strony — do zmierzenia w B4 | „Domyślnie Groq nie przechowuje danych klientów dla zapytań inferencji”; logi tylko przy diagnozie/nadużyciu do 30 dni; Zero Data Retention do włączenia w ustawieniach | console.groq.com/docs/rate-limits; …/docs/your-data |
| Cloudflare Workers AI | 10 000 neuronów/dzień dla każdego, reset 00:00 UTC; np. `llama-3.1-8b-instruct-fp8-fast` 4 119 neuronów/M tokenów wejścia, 34 868/M wyjścia (≈ 2,4 M wejścia lub ≈ 0,29 M wyjścia dziennie); 300 zapytań/min dla generowania tekstu | Nie sprawdzone 2026-10-28 (warunki danych do pobrania w B4 przed włączeniem) | developers.cloudflare.com/workers-ai/platform/pricing, …/limits |
| Gemini API, warstwa bezpłatna | Modele Flash / Flash-Lite „free of charge”; liczby RPM/RPD tylko w AI Studio (nie na stronie), reset o północy czasu pacyficznego, limity per projekt | **Warunki (obow. od 2026-03-23):** Google używa treści „to provide, improve, and develop Google products … and machine learning technologies”, recenzenci-ludzie mogą czytać; „Do not submit sensitive, confidential, or personal information”; **„You may use only Paid Services when making API Clients available to users in the EEA, Switzerland, or the UK”** | ai.google.dev/gemini-api/terms; …/docs/pricing; …/docs/rate-limits |
| Mistral, tryb Free | Najniższe limity (RPS, TPM, tokeny/miesiąc), do oceny i prototypów; wyższe od 20 €/$ zafakturowanych | „Mistral może używać wejścia i wyjścia trybu Free do uczenia modeli”, rezygnacja w panelu administratora (Privacy); ZDR tylko w pay-as-you-go | help.mistral.ai (artykuły 347617, 455207, 698531) |
| Hugging Face Inference Providers | Konta darmowe: brak kredytów miesięcznych (wymagany zakup); PRO 2 $/mies. | nie dotyczy (brak darmowej puli) | huggingface.co/docs/inference-providers/pricing |
| Model lokalny na maszynie (llama.cpp, serwer zgodny z OpenAI) | Limitem jest 8 vCPU / 31 GB RAM dzielone z resztą usług; przepustowość do zmierzenia (B9); właściciel: „tyle ile mocy obliczeniowej starczy do rozdania” | Nic nie opuszcza maszyny; dziennik tylko lokalny | `docs/ops/places.json` |

Wnioski dla **presetu** zestawu darmowych dostawców gościa (decyzja koordynatora, odwracalna edycją `free_providers.pack`):
- **Włączone od razu:** Groq (brak przechowywania domyślnie), OpenRouter `:free` z włączoną w koncie właściciela opcją „nie
  kieruj do uczących” i filtrem polityki danych per zapytanie, model lokalny (po B9). Cloudflare Workers AI po pobraniu i zapisaniu
  warunków danych (B4).
- **Obecne jako wpisy, ale domyślnie poza kolejką gościa na kluczu właściciela:** Gemini — warunki wprost mówią, że klienta
  API udostępnianego użytkownikom w EOG (a maszyna i właściciel są w Polsce) wolno budować tylko na Paid Services; to warunek
  dostawcy, więc traktowany jak prawo (lista zamknięta z powodem i datą, do otwarcia, gdy zapis zniknie). Mistral Free — uczy na
  danych, chyba że właściciel wyłączy to w panelu; wpis pozostaje wyłączony, dopóki w danych nie pojawi się `training: "no"`
  z datą sprawdzenia. Oba dostępne natychmiast jako **własny klucz gościa** (wtedy warunki wiążą gościa, i tak mu je pokazujemy).
- Każdy wpis z `training: "yes"` lub `human_review: true` jest w UI oznaczony przed pierwszym wysłaniem i w każdym paragonie.

### 4.3 Ujawnienie warunków danych przed wysłaniem (nigdy ukryte)

Przed pierwszym wysłaniem do dostawcy, którego `data_terms` nie są `retention: none…` i `training: no` z datą, gość widzi
jedno okno: skąd wiemy (źródło i data), co dostawca robi z treścią, że to konto właściciela (gdy `pool`/`free`), i trzy wybory:
wyślij, wybierz innego dostawcę, nie pytaj dla tego dostawcy (zapamiętane jak `declined`, odwracalne). Częstość pytania to
preset (`data_terms_prompt: first_use | every_session | every_message | never`), a `never` jest dostępne gościowi tylko dla jego
własnego klucza — przy kluczu właściciela najmniejsza częstość to `first_use`, bo to właściciel odpowiada za to, co przez jego
konto idzie do dostawcy (R100 w drugą stronę). Ten sam opis wchodzi do paragonu „Co wysłaliśmy”.

### 4.4 Wyczerpanie i przełączanie

Limit dostawcy (429, liczniki typu `free_model_daily_requests`, lokalny licznik per dostawca w księdze) kończy się **jawną**
zmianą źródła według `fallback_order`, z komunikatem w rozmowie („Groq wyczerpał dzienny limit o 13:40, odpowiada model
lokalny; jakość niżej”) — nigdy cichym przełączeniem. Gość może wskazać `on_exhausted: next | wait | ask` (preset `ask`
dla zmiany na słabszy model, `next` między równorzędnymi — a „równorzędność” to pole w danych, nie ocena w kodzie).

### 4.5 „Najlepszy dostępny darmowy model” — reguła wyboru jako dane

Wybór modelu dla gościa bez własnego klucza to wpis danych `free_model_choice` (w `free_providers.pack`, nakładka operatora
i ustawienia gościa nad nim), nie stała w kodzie:

```
free_model_choice: {
  rank_source: "table",                 // table = ręczna tabela jakości z datą; później: wynik własnych pomiarów (eksperymenty), ocena zewnętrzna
  table: [{model, provider, quality: 0..1, observed_at, source}],       // lista przykładów, odświeżana, gdy darmowe warstwy się zmieniają
  require: {training: "no"},            // warunek danych przy kluczu właściciela (§4.2)
  prefer: ["quality", "remaining_quota", "latency_p50"],                // kolejność kryteriów, otwarta
  min_remaining_quota: 0.05,            // poniżej tego udziału dziennego limitu model schodzi na koniec kolejki
  reevaluate: "per_message"             // per_message | per_session | manual
}
```

Reguła liczy się przy każdej wiadomości (preset) z aktualnych liczników §4.4, więc wyczerpanie Groq w ciągu dnia samo
przesuwa gościa na kolejny wpis, a zmiana darmowych warstw u dostawców to edycja tabeli z nową datą. Gość widzi w pasku
rozmowy, który model odpowiada i dlaczego („najlepszy z dostępnych dziś darmowych; Groq ma 62 % dziennego limitu”), i może
przypiąć inny. Tabela jakości na start (B1) to **przykładowy** ranking koordynatora z datą; właściwy pomiar robią później
eksperymenty wielotorowe (R102) na tych samych pytaniach.

## 5. Pula kluczy właściciela: budżet próbny dla każdego i przydziały z zaproszeń

Właściciel (dopisek 2026-10-28): „te darmowe tokeny to będą ogólnie dla użytkowników, żeby sobie mogli, nawet nie mając
wielkiego zainteresowania, przetestować funkcjonalności płatne za parę centów ode mnie”. Pula ma więc dwa zastosowania:
**budżet próbny** — kilka centów dla **każdego** gościa, bez zaproszenia, na wypróbowanie płatnych modeli i funkcji (preset
kwoty w §5.1) — oraz **przydziały z zaproszeń** jako dodatek dla testerów, grup i warsztatów.

### 5.1 Dane puli

`<accounts_root>/pool/keys.json` (prawa 600, poza każdym katalogiem użytkownika) i `pool.sqlite` (księga):

```
keys: [{id, provider, secret_ref, label, budgets: [{period, amount, currency, on_exceed}], enabled, notes}]
grants: [{id, name, applies_to: "all_guests" | "invite:<hash>" | "group:<name>", keys: [key_id…],
          budgets: [{period, amount, currency, on_exceed}], models: {allow: [...], deny: [...]} | null,
          max_context_tokens: null, valid_from, valid_to, uses: {max, used}}]
```

Kształt budżetu jest **ten sam** co `usage_spend_limits` (`period`, `amount`, `currency`, `on_exceed`), więc egzekwuje go ta sama
centralna admisja księgi (`usage_spend.h`), tylko z dwiema dodatkowymi warstwami nad limitem osoby: limit przydziału i limit
klucza. Najciaśniejsza z trzech warstw decyduje; każda ma własne `on_exceed`. Nowa wartość `on_exceed: fallback_free`
(dla puli i przydziału; dla osoby zostaje `confirm | deny | notify`): po wyczerpaniu rozmowa idzie dalej na źródłach `free`/`local`
z komunikatem, nie urywa się.

Presety budżetów (odczyt reguły właściciela z `CLAUDE.md`: płatne ≤ 1 €/dzień, ≤ 3 €/tydzień; kredyty Google do 10 % każdego;
to jest mój odczyt, a liczby są presetami w danych, nie regułą kodu):
- klucz płatny (OpenRouter, Anthropic): `[{day, 1, EUR, fallback_free}, {week, 3, EUR, fallback_free}]` łącznie na wszystkich gości;
- klucz z kredytem Google (Vertex): `{month, 10 % wartości kredytu, EUR, fallback_free}` — wartość wpisuje właściciel;
- przydział `all_guests` = **budżet próbny**: `{trial, 0.05, EUR, fallback_free}` na gościa — „parę centów” jako preset
  (okres `trial` = jednorazowo na urządzenie/schowek, bez odnawiania; okres jest otwartą wartością, więc właściciel może
  zmienić na `day` lub `week`); przy 1 €/dzień puli to ≈ 20 nowych gości dziennie — po przekroczeniu nowi goście dostają
  komunikat „budżet próbny na dziś wyczerpany, wrócę jutro; darmowe modele działają” i źródła `free`/`local`;
- przydział z zaproszenia: bez presetu kwoty — kwotę wpisuje właściciel przy tworzeniu („klucz do testów dla użytkowników”).
Budżet próbny obejmuje płatne modele **i** płatne funkcje (osadzenia, analiza semantyczna modelem, tory R102 na płatnych
modelach) — każde wywołanie liczy ta sama księga. Wiązanie budżetu próbnego z gościem bez konta jest z natury słabe
(nowe urządzenie = nowy gość); dlatego limitem twardym jest pula dzienna właściciela (§7), a nie szczelność rozpoznania.
Wzrost zużycia o rząd wielkości ponad dotychczasowe tempo puli zatrzymuje się na strażniku ×10, tak jak wszędzie; „tak” daje
właściciel (w panelu puli lub przez CLI), nie gość.

### 5.2 Przydział gościowi: link lub kod zaproszenia

Istniejące zaproszenia kont (`create_invite`, SHA-256, jednorazowe) dostają rozszerzenie: `kind: account | grant | both`,
`uses_max` (1 = jak dziś), `grant_id`. Link `https://…/?invite=KOD` albo kod wpisany w UI wiąże przydział z sesją gościa
(w rekordzie sesji `grant_id`); jeden kod może obsłużyć grupę („zaproś 20 osób na warsztat, razem 5 €, każdy do 0,50 €”).
CLI: `loom-server pool key add|list|disable`, `loom-server pool grant add|list|revoke`, `loom-server user invite --grant ID
--uses N` — wszystko też jako akcje w katalogu akcji, więc da się to zrobić z poziomu aplikacji właściciela.

### 5.3 Pomiar

Każde wywołanie modelu z klucza puli zapisuje się w księdze katalogu gościa (jak dziś, `cost_usd` od dostawcy) **i** w
`pool.sqlite` (`key_id`, `grant_id`, `guest_id` skrócony, `provider`, `model`, `cost_usd`, `tokens`, czas) — bez treści. Panel
właściciela (okno „Pula” w jego aplikacji + `GET /api/pool/summary` tylko dla konta operatora) pokazuje zużycie per klucz,
przydział i dzień, prognozę do końca okresu i listę ostatnich przełączeń na `free`. Gość widzi własny stan przydziału
(„zostało 0,07 € z 0,10 € dziś”) w pasku rozmowy; kwoty są prawdziwe z księgi, nie szacowane w UI.

### 5.4 Po wyczerpaniu

Komunikat w rozmowie z powodem i godziną odnowienia, przełączenie według §4.4, nic nie znika; właściciel dostaje wpis w
podsumowaniu dnia. Gość z własnym kluczem nigdy nie jest tym objęty (jego pieniądze, jego limity z `usage_spend_limits`).

## 6. Historia bez konta: schowek użytkownika

### 6.1 Zasada

Serwer po końcu sesji (`idle_ttl`) **nie ma** historii gościa — chyba że gość wybrał `keep_after_session: always` i dostał
kod odzyskania (§6.3). Źródłem prawdy jest schowek po stronie użytkownika; katalog roboczy to pamięć podręczna na czas pracy,
odtwarzana ze schowka przy następnym wejściu (import istniejącym `POST /api/import`). Właściciel: „na dysku użytkownika … będzie
musiał dać dostęp” — stąd schowek zawsze wymaga wyraźnego wyboru i zgody gościa, a każdy wybór jest odwracalny.

### 6.2 Rodzaje schowków (lista przykładów; każdy to wtyczka web z tym samym interfejsem `save(bundle)`, `load()`, `list()`)

| Schowek | Jak | Zgoda | Uwagi |
|---|---|---|---|
| **Przeglądarka (preset)** | IndexedDB (a OPFS, gdy dostępne, dla dużych załączników); po każdej odpowiedzi web zapisuje paczkę rozmowy (eksport w formacie własnym Loom + metadane); `navigator.storage.persist()` żeby przeglądarka nie czyściła sama | brak dodatkowej (to nasz własny origin) | znika z czyszczeniem danych witryny — mówimy to na pierwszym ekranie; tryb prywatny = tylko sesja |
| **Plik** | „Pobierz archiwum” (ZIP: rozmowy, graf, pamięć) i „Wczytaj archiwum”; format to ten, który eksport już ma | wybór pliku | działa wszędzie, także offline |
| **Folder na dysku użytkownika** | File System Access API (`showDirectoryPicker`), zapis przyrostowy do wskazanego katalogu | uprawnienie przeglądarki do folderu, odnawiane po restarcie | Chromium i pochodne; w innych przeglądarkach wtyczka wyszarzona z powodem |
| **Jego własna chmura** | Google Drive `appDataFolder` (OAuth gościa, zakres `drive.appdata`), WebDAV (Nextcloud, ownCloud), Dropbox/OneDrive jako kolejne wtyczki | logowanie u dostawcy chmury, nie u nas; żeton tylko w przeglądarce | to nie jest „logowanie do ChatADHD” — serwer nie widzi żetonu, zapis idzie z przeglądarki prosto do chmury |
| **Nasz serwer (na życzenie)** | `keep_after_session: always` + kod odzyskania; katalog trzymany `server_keep_days` (preset 30) | wyraźny wybór z opisem ryzyka (dysk właściciela, bez gwarancji) | zawsze zaszyfrowany kluczem gościa (§6.4), serwer nie czyta |

### 6.3 Odzyskiwanie i urządzenia

Schowek w przeglądarce jest per urządzenie. Przeniesienie między urządzeniami: plik, własna chmura, albo (opcjonalnie) krótki
kod odzyskania dla kopii serwerowej. Kod jest generowany u gościa, pokazany raz, u nas tylko jego skrót (ten sam wzór co żetony
sesji). Bez kodu kopia serwerowa jest nieczytelna także dla właściciela.

### 6.4 Szyfrowanie (opcja, wszędzie ta sama)

WebCrypto: AES-256-GCM, klucz z hasła gościa przez PBKDF2-SHA-256 — identyczny format jak `core/crypto.py` i `crypto_suite.h`
(ten sam plik da się odszyfrować aplikacją desktopową). Preset: przeglądarka bez szyfrowania (wygoda, własny origin), plik i
chmura z pytaniem „zaszyfrować?”, kopia serwerowa zawsze. Hasło nigdzie nie jest wysyłane; zgubione hasło = utrata kopii,
powiedziane przy wyborze.

### 6.5 Co z grafem i pamięcią: profil budowany w schowku użytkownika

Paczka schowka obejmuje rozmowy, węzły i krawędzie grafu, drzewo pamięci i ustawienia osoby — to, co dziś eksportuje
`loom` CLI jako archiwum katalogu danych. Odtworzenie importuje całość; analiza semantyczna nie liczy się od nowa, bo wyniki są
w paczce.

Właściciel (dopisek 2026-10-28): „z opcją wykorzystania pamięci lokalnej użytkownika na budowanie profilu”. **Profil** gościa
— pamięć hierarchiczna, graf wiedzy, preferencje (modele, styl, język), tematy — powstaje z jego rozmów tak samo jak na
koncie (`MemoryEngine`, `GraphEngine`, kontekst pamięci w `build_messages`), tylko jego trwałym miejscem jest **schowek
użytkownika**, nie nasz dysk: po sesji profil jest w paczce, przy następnym wejściu wraca do kopii roboczej i od pierwszej
wiadomości kształtuje kontekst. Opcja (`profile.build: on | off`, preset `on` z pytaniem w uwadze po pierwszej odpowiedzi,
bo wymaga schowka), z podglądem „co o mnie wie” (istniejące okno pamięci) i czyszczeniem jednym ruchem. Profil należy do
użytkownika: eksportuje się razem z paczką, da się go wczytać do konta, do aplikacji desktopowej albo do innej instancji.
Gdy schowka nie ma, profil żyje tylko do końca sesji i uwaga §3.5 to mówi.

## 7. Ochrona przed nadużyciami i kosztami publicznego punktu

To jest **konieczność** publicznego punktu, nie wymyślony limit: bez niej jedna osoba ze skryptem wyczerpie w minutę dzienne
limity darmowych dostawców dla wszystkich i budżet puli właściciela, albo użyje serwera jako otwartego pośrednika do cudzych
adresów. Wartości są presetami w danych (`server_accounts.pack` → `guests.protection`), każdą można podnieść do „bez limitu”;
zmienia się je bez przebudowy. Lista przykładów mechanizmów (otwarta):

- **Dowód pracy przy zakładaniu sesji** (zamiast captchy: nic od stron trzecich, działa bez cookies zewnętrznych): wyzwanie
  SHA-256 o trudności `pow_bits` (preset 18, ≈ 0,2–0,5 s w przeglądarce), trudność rośnie z obciążeniem (`pow_bits_under_load`),
  wyłączalny (`pow: off`) dla sesji z zaproszeniem.
- **Wiadro żetonów per sesja**: zapytań/min i /dzień (preset 20/min, 300/dzień), tokenów wyjścia/dzień na źródłach puli i
  darmowych (preset 200 000), równoległych strumieni (preset 2). Własny klucz gościa: tylko limit równoległości (jego pieniądze).
- **Per adres klienta** (z `trust_forwarded_for`, bo przed serwerem stoi Caddy): sesji/godz. (preset 20), zapytań/min (preset 60).
  Adres to wskazówka, nie tożsamość (NAT, CGNAT) — dlatego limity per adres są luźniejsze niż per sesja, a komunikat mówi
  „z tego adresu jest dziś dużo ruchu”, nie „zablokowano cię”.
- **Globalne**: łączna liczba żywych sesji gości (preset 200), łączny budżet puli (§5.1), łączne obciążenie modelu lokalnego
  (kolejka z widocznym miejscem w kolejce).
- **Rozmiary**: istniejące `--max-body-bytes`, `--max-upload-bytes`, pułapy ZIP; dla gości osobne, niższe presety (import 50 MB).
- **Wyjście sieciowe**: `block_private_hosts` już jest; dla gości dodatkowo lista dozwolonych hostów = adresy dostawców z
  manifestów (konektory do dowolnych adresów dostępne po podaniu własnego klucza lub na koncie).
- **W Caddy** (warstwa przed serwerem, konfiguracja w `tools/gcp/`): limit połączeń i `rate_limit` jako pierwsza zapora; żaden
  limit tam nie zastępuje tych w serwerze, bo tylko serwer wie, która sesja i który klucz.
- **Obserwowalność**: licznik odrzuceń per mechanizm w `GET /api/pool/summary`; raport dzienny dla właściciela; żadnej treści
  rozmów w dziennikach.

Przekroczenie zawsze odpowiada 429 z `retry_after` i tekstem po ludzku; web pokazuje czas i proponuje inne źródło (własny klucz,
model lokalny), nigdy pustą odmowę.

## 8. „Dużo lepsze niż darmowe u innych” — co dajemy bez konta

Obserwacje datowane 2026-10-28 o cudzych darmowych wersjach (źródła: help.openai.com/en/articles/9125172 i wyszukanie
oficjalnego artykułu o używaniu bez konta; support.google.com/gemini/answer/13275745 i 13594961; claude.com/pricing,
privacy.claude.com artykuł 10023580):
- **ChatGPT bez konta**: można rozmawiać, ale „logged-out chats can't be saved” — jedna rozmowa naraz, w tej samej
  przeglądarce, do końca sesji; zapis wymaga zalogowania. (Artykuł 9275245 odpowiedział 403 przy pobieraniu; treść potwierdzona
  przez wyszukiwarkę z cytatem — ocena źródła: oficjalne, pośrednio; do ponownego pobrania w B4.)
- **Gemini bez konta**: tylko tekst; bez historii („save your Gemini Apps activity … need to sign in”), bez zmiany modelu, bez
  plików i obrazów; po zalogowaniu domyślnie aktywność 18 miesięcy, część rozmów czytają recenzenci, używana do uczenia przy
  włączonym „Keep Activity”.
- **Claude Free**: wymaga konta („signing up using an email address”); Sonnet i Haiku, bez Opus/Fable; limity w oknach 5 h;
  wiersz „model training” w cenniku: Free/Pro/Max „Opt-out”.

Co gość ChatADHD dostaje (lista przykładów; wszystko już w aplikacji lub w zatwierdzonych projektach):
1. **Historia bez konta** — w jego przeglądarce, pliku, folderze lub chmurze; wiele rozmów naraz; bez terminu ważności u nas.
2. **Wiele modeli i dostawców w jednej rozmowie**, zmiana modelu w środku, własny klucz do każdego dostawcy, bez uwiązania.
3. **Tory wielotorowe (R102)**: to samo pytanie do kilku modeli naraz, porównanie, rozbieżności.
4. **Gałęzie wersji** wiadomości zamiast nadpisywania; geneza tekstu.
5. **Graf wiedzy i pamięć hierarchiczna** z jego rozmów, lokalnie w jego paczce; wyszukiwanie semantyczne.
6. **Import archiwów ChatGPT/Claude/Gemini i plików** — wszystko, co ma u innych, może przynieść; **eksport we wszystkich
   formatach**, zawsze, bez proszenia.
7. **Paragon „Co wysłaliśmy”** i jawne warunki danych każdego dostawcy przed wysłaniem — u innych jest odwrotnie (domyślna
   zgoda na uczenie).
8. **Limity wydatków i strażnik ×10 własnego klucza** — kontrola kosztu w aplikacji, nie u dostawcy.
9. **Model lokalny na naszej maszynie** jako źródło zerowej prywatności-ryzyka (nic nie opuszcza VM) — za darmo, dopóki starcza mocy.
10. **Bez telemetrii, bez reklam, bez profilu** — i kod otwarty, więc to sprawdzalne.
11. **Budżet próbny z puli właściciela dla każdego gościa** („parę centów”, by wypróbować płatne modele i funkcje) oraz większe
    przydziały z zaproszeń (właściciel: „a nawet parę euro tokenów”).
12. **Profil budowany lokalnie** (pamięć, graf, preferencje) w schowku użytkownika, jego własność, przenośny — u innych profil
    i „pamięć” istnieją tylko po zalogowaniu i tylko u dostawcy.
13. **Od pierwszego wejścia pełny interfejs i najlepszy darmowy model**, bez bramki rejestracji; uwaga o braku logowania
    dopiero po pierwszej odpowiedzi, nieblokująca.

## 9. Mechanizm a presety (zebrane)

| Mechanizm (kod) | Preset (dane) | Gdzie zmienić |
|---|---|---|
| Sesje bez konta w bazie kont | `guests.enabled: false` do B3, potem `true` na publicznym; `start: auto` | `server_accounts.pack`, nakładka `<root>/profiles` |
| Katalog roboczy z TTL | `idle_ttl 86400`, `keep_after_session: ask` | j.w. |
| Uwaga po pierwszej odpowiedzi (R105) | `on`, `ask_again: per_device`; okres przepadnięcia liczony ze schowka | j.w. + ustawienia gościa |
| Najlepszy darmowy model | tabela jakości z datą, `require training: no`, `reevaluate: per_message` | `free_providers.pack` → `free_model_choice` |
| Profil w schowku użytkownika | `profile.build: on` (pytanie przy wyborze schowka) | ustawienia gościa |
| Użytek niekomercyjny | zdanie w warunkach, `terms.commercial_use: ask_owner` | dane warunków użycia |
| Kolejność źródeł modeli | `byok, pool, free, local` | j.w. + ustawienia gościa |
| Wpisy dostawców darmowych | Groq, OpenRouter `:free`, Cloudflare (po B4), lokalny; Gemini i Mistral wyłączone z powodem i datą | `free_providers.pack` |
| Ujawnienie warunków danych | `first_use` (klucz właściciela), gość może `never` dla własnego klucza | `free_providers.pack`, ustawienia gościa |
| Budżety puli i przydziałów | 1 €/dz., 3 €/tydz. płatne; 10 % kredytu; budżet próbny 0,05 € na gościa | `pool/keys.json`, CLI/akcje |
| Ochrona (PoW, wiadra, per adres, globalne) | wartości §7 | `server_accounts.pack` → `guests.protection` |
| Schowek historii | przeglądarka; szyfrowanie: plik/chmura „zapytaj”, serwer „zawsze” | ustawienia gościa |

## 10. Plan pakietów (każdy ≈ 50 wywołań narzędzi, commit + karta wznowienia)

Właściciel: „wdróż wszystko, czata po wprowadzeniu darmowych funkcjonalności bez logowania” — więc kolejność daje najpierw
**wdrażalne MVP gościa** (B1 + B2 + B3 = pierwsze wdrożenie produkcyjne), a resztę dokłada do działającej aplikacji.

1. **B1 MVP gościa** — rekord sesji z `guest_id`, `POST /api/auth/guest` (zakładana automatycznie, `start: auto`), katalog
   roboczy z TTL i sprzątaniem, `context_for_guest` z izolacją R18, faza `guest` w `auth-state.ts` bez bramki (pełny interfejs
   R101 od wejścia); minimalny `free_providers.pack` z Groq i OpenRouter `:free` (klucze właściciela przez `credential_overlay`,
   warunki danych w paragonie) i reguła `free_model_choice` z przykładową tabelą; **uwaga po pierwszej odpowiedzi** (§3.5) z
   okresem liczonym ze schowka; schowek w przeglądarce (IndexedDB/OPFS, `persist()`, paczka po każdej odpowiedzi, odtworzenie
   przez `/api/import`) oraz „Pobierz/Wczytaj archiwum”. Akceptacja: test izolacji §3.2 (ctest + Playwright na serwerze
   testowym); `test_abi_compat.py` zielony; e2e: wejście bez bramki → pytanie → odpowiedź darmowego modelu → uwaga z okresem →
   zamknięcie → nowa sesja → rozmowa z grafem wraca; sesja i katalog znikają po TTL.
2. **B2 Ochrona publicznego punktu** — dowód pracy, wiadra per sesja/adres/globalne, limity dla gości w Caddy, liczniki
   odrzuceń, 429 z `retry_after`, dzienny pułap puli darmowych kluczy właściciela. Akceptacja: test obciążeniowy skryptem z
   jednego adresu (zmierzone: ile sesji/zapytań przechodzi przy presetach), brak wpływu na zalogowane konto `marcin`.
3. **B3 Pierwsze wdrożenie produkcyjne i przekazanie** — `guests.enabled: true` na `chat.34-158-117-249.sslip.io`, pełny
   przebieg gościa na prawdziwych dostawcach z osobnego urządzenia, raport ze źródłem każdego wyniku, lista funkcji
   wyszarzonych z powodem (§11). Akceptacja: raport + zielona bramka; właściciel dostaje link i listę ograniczeń przed testem.
4. **B4 Dostawcy darmowi — pełne dane** — reszta tabeli §4.2 (dopobrać: Cloudflare warunki danych, Groq plan Free, dokładne
   pole polityki danych OpenRouter), okno warunków danych §4.3, przełączanie §4.4 z komunikatem, wpisy wyłączone z powodem.
   Akceptacja: test na dostawcy-atrapie zwracającym 429 → jawne przełączenie; wpisy z `training: yes` nie są w kolejce klucza
   właściciela; tabela ma datę każdego wpisu.
5. **B5 Własny klucz na czas sesji** — klucz w pamięci kontekstu, `sessionStorage`/IndexedDB zaszyfrowane po stronie web,
   `GET /api/secrets` bez kluczy puli. Akceptacja: po restarcie serwera klucz nie istnieje na dysku (test czyta katalog).
6. **B6 Pula: budżet próbny i zaproszenia** — `pool/keys.json`, `pool.sqlite`, warstwy budżetów w admisji, okres `trial`,
   `fallback_free`, zaproszenia `kind: grant`, CLI i akcje, okno „Pula” dla właściciela, stan budżetu u gościa. Akceptacja:
   test z dostawcą-atrapą raportującym `cost_usd`: wyczerpanie budżetu próbnego → przełączenie i wpis w księdze; strażnik ×10
   na puli; drugie wdrożenie produkcyjne z realnym budżetem próbnym (raport z kwotami).
7. **B7 Profil w schowku użytkownika** — pamięć, graf i preferencje w paczce, odtwarzane do kopii roboczej przed pierwszą
   wiadomością, `profile.build`, okno „co o mnie wie”, czyszczenie, wczytanie profilu do konta. Akceptacja: e2e: dwie sesje
   — druga odpowiedź korzysta z kontekstu pamięci zbudowanego w pierwszej (widoczne w paragonie); wyłączenie opcji = brak
   kontekstu.
8. **B8 Schowki zewnętrzne i szyfrowanie** — wtyczki: folder (File System Access), Google Drive appData, WebDAV; szyfrowanie
   WebCrypto zgodne z `crypto_suite.h`; kopia serwerowa na życzenie z kodem odzyskania. Akceptacja: e2e na atrapie WebDAV w
   ctest; Drive na prawdziwym koncie testowym koordynatora (raport mówi, które dane były testowe); plik zaszyfrowany otwiera
   się w CLI `loom`.
9. **B9 Model lokalny na maszynie** — usługa llama.cpp (systemd, port lokalny), wpis `local`, kolejka z miejscem w kolejce;
   **pomiar** tokenów/s i RAM na 8 vCPU dla dwóch–trzech modeli (np. 1–4 B parametrów, Q4) — liczby do `places.json`, model
   domyślny po pomiarze. Akceptacja: raport z pomiarem; serwer bez modelu nie psuje reszty.

Kolejność (decyzja koordynatora, preset; odwrócenie: zmiana kolejności w `docs/reports/INDEX.md`): B1 → B2 → B3 (produkcja) →
B4 → B5 → B6 → B7 → B8 → B9, każdy z B4–B9 wdrażany od razu po zielonej bramce. **Gość na publicznym adresie pojawia się
dopiero po B2** (ochrona) — do tego czasu tryb gościa działa tylko na serwerach testowych. Projekt przed pakietami przechodzi
tani przegląd czterech antywzorców (CLAUDE.md, „Check before use”).

## 11. Przekazanie właścicielowi do testu (reguła z jego `~/.claude/CLAUDE.md`)

Zanim właściciel dostanie cokolwiek do klikania: żadnych atrap i danych syntetycznych w tym, co widzi (dostawcy-atrapy zostają
w ctest i Playwright); każdy schowek, dostawca lub źródło, którego pakiet jeszcze nie zrobił, jest w UI **wyszarzony z
powodem**, nie ukryty i nie udawany; przed jego testem pełny przebieg gościa wykonuje koordynator na prawdziwych dostawcach
(B3, potem B6) i raport mówi, które liczby pochodzą z pomiaru, a które z danych testowych; ograniczenia przebiegu (np. „Cloudflare jeszcze
bez warunków danych, więc wyłączony”) są wypisane **przed** jego uruchomieniem.

## 12. Decyzje zapisane (odwracalne) i pytania do właściciela

Decyzje (każda do odwrócenia edycją danych lub jedną linią w `docs/reports/INDEX.md`):
- Gemini poza kolejką klucza właściciela z powodu zapisu o EOG w warunkach (data 2026-10-28, źródło w §4.2); własny klucz
  gościa — tak. Odwrócenie: zmiana `availability` we wpisie, gdy zapis zniknie lub właściciel włączy płatny projekt.
- Mistral Free wyłączony, dopóki wpis nie ma `training: no` z datą (właściciel może to osiągnąć wyłączeniem w panelu Mistral).
- Historia domyślnie w przeglądarce, nie u nas; kopia serwerowa tylko na życzenie i zawsze zaszyfrowana.
- Presety budżetów puli = mój odczyt reguły wydatków (1 €/dz., 3 €/tydz., 10 % kredytu); budżet próbny 0,05 € na gościa
  („parę centów” — kwota jest presetem, właściciel zmienia jedną liczbą).
- Uwaga o braku logowania po pierwszej odpowiedzi, standardowo, bez metryki wartości (właściciel zrezygnował z metryki).
- Użytek niekomercyjny bezpłatny; warunki komercyjne odłożone do decyzji właściciela, nic nie jest egzekwowane w kodzie.
- Kolejność pakietów: MVP gościa (B1–B3) przed resztą, bo właściciel chce wdrożenia produkcyjnego po trybie gościa.

Pytania (tylko o pieniądze poza regułą):
1. **OpenRouter:** modele `:free` dają 50 zapytań/dzień, dopóki konto nie kupiło łącznie ≥ 10 kredytów, potem 1 000/dzień.
   Jeśli Twoje konto OpenRouter nie ma jeszcze kupionych łącznie 10 kredytów (≈ 10 USD, jednorazowo — ponad 1 €/dzień), czy
   dokupić do tego progu? Tak/nie.

Sprawdzone wobec filozofii: nie ma innych pytań — liczba źródeł, schowków, mechanizmów ochrony i kwot to otwarte dane z
presetami; nowi dostawcy dostają tylko dane gościa, który widzi warunki przed wysłaniem (R100 zachowane); dane właściciela do
nikogo nowego nie idą.
