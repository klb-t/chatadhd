# Poprawka §4.1 dokumentu BEZ_LOGOWANIA_R103_2026-10-28 (pakiet M1-GOSC-ZRODLO, 2026-10-30)

Dokument `BEZ_LOGOWANIA_R103_2026-10-28.md` leży na gałęzi `claude/chataddhd-cpp-loom-core-IRGRN`, nie na `main`, więc ta
poprawka jest osobnym plikiem: po scaleniu obu gałęzi zastąp nią akapit §4.1 (od nagłówka `### 4.1` do przed `### 4.2`).

### 4.1 Źródła modeli: pula bezkosztowa i pula płatna (stan kodu 2026-10-30)

Źródła są podzielone w danych (`free_providers.pack`, `source_pools`) na dwie pule. Pula bezkosztowa `cost_free` (nazwa w
`guests.model_sources`: `free`; poziom dostawców `free`) to darmowi dostawcy, także nasze własne darmowe klucze. Pula płatna
`paid` (nazwa `paid`; poziom `paid_credits`) to źródła kosztujące pieniądze albo kredyty (dziś Vertex). Każda pula ma listę
odbiorców: `cost_free` obsługuje gościa i konto, `paid` tylko konto, które świadomie wypróbowuje płatne funkcje
(`account_trial`).

`guests.model_sources` (preset `["byok","free","local"]`) ma teraz czytelnika w serwerze: wybór źródła dla gościa bierze tylko
te poziomy dostawców, które należą do puli wymienionej w `model_sources` I obsługującej gościa. Kolejność `model_sources`
rozstrzyga o kolejności pul; `free_model_choice.tier_order` porządkuje tylko poziomy wewnątrz jednej puli. `byok` i `local`
to własny klucz i własne środowisko gościa, serwer niczego im nie dobiera. Dawna nazwa `pool` jest ignorowana.

Gość bez konta dostaje wyłącznie źródła bezkosztowe, w każdym stanie i także po jakiejkolwiek zgodzie: poziom, którego nie ma
w żadnej puli albo który należy do puli nieobsługującej gościa (w tym brak pola `tier` u dostawcy), nie bywa wybrany, nawet gdy
tabela `free_model_choice.table` stawia go najwyżej; w odpowiedzi o źródle stoi to jako `skipped: tier_not_allowed`.
Płatna pula nie jest wybierana samoczynnie nigdzie. Dla osób z kontem istnieje w danych (`audience: account_trial`,
`per_person_eur_per_day`, budżet `vertex_gemini.budget`: 1 € dziennie na wszystkich razem, wartość startowa), ale ścieżka
wyboru dla kont jeszcze jej nie używa (luka zapisana w karcie M1-GOSC-ZRODLO).

Gdy nie ma żadnego źródła bezkosztowego (brak kluczy, `model_sources` bez `free`, wszyscy dostawcy wyczerpani), gość ma jawny stan
`none`: `loom_guest_source: "none"` w konfiguracji od początku sesji, `model_source.state = "none"` z komunikatem
(`free_model_choice.none_message`, pl/en) w `GET /api/auth/me`, a prośba o odpowiedź modelu dostaje 503 `no_guest_source` w
kształcie odmowy ochrony (`limit.mechanism`, `words.text`), z drogą do własnego konta lub klucza (R108); własny klucz gościa
dalej działa. Przy każdej prośbie o model stan `none` jest liczony ponownie, więc nowy klucz zostaje podjęty bez restartu.
