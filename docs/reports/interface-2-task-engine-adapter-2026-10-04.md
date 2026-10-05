# Wątek 10 — kontrakt publicznego adaptera TaskEngine

Data: 2026-10-04. Status: dokumentacja istniejących wywołań i braków; bez zmian
w API, serwerze lub `App.tsx`, bez nowych ekranów i bez wywołań modelowych.

Punkt odniesienia integratora: `origin/main`
`30ad7d37337d6641cb7714b03e9feff0e6e25d25`. Kontrakt metod poniżej
zweryfikowano w kodzie tej rewizji oraz bieżącej gałęzi W10. Źródła:
`loom/web/src/api/{loom-api.ts,loom-http.ts,loom-jni.ts,types.ts}`,
`loom/server/src/app.cpp`, `loom/include/loom/loom.h`,
`loom/src/capi/capi_core.cpp`, `loom/src/core/tasks.cpp`.

## Co można wywołać teraz

`publicTaskEngine` jest nazwą oczekiwanej zdolności integracyjnej, a nie
istniejącym polem `LoomApi`. Aktualny `LoomApi` udostępnia cztery metody:

| Metoda TypeScript | HTTP i payload | Wynik publiczny | Znaczenie |
| --- | --- | --- | --- |
| `listTasks(filter?: Record<string, unknown>): Promise<Task[]>` | `GET /api/tasks`; query: `kind`, `status`, `parent_id`, `limit`; bez body | Tablica rekordów | Filtry dokładnego dopasowania; kolejność `created DESC, rowid DESC`. Domyślny `limit` 100 jest presetem istniejącego ABI, a nie dodatkowym limitem UI. Brak paginacji/kursora i potwierdzenia kompletności listy. |
| `getTask(id: string): Promise<Task>` | `GET /api/tasks/{encodeURIComponent(id)}`; bez body | Jeden rekord | Odczyt aktualnego stanu zadania. Nieistniejący rekord: HTTP 404. |
| `resumeTasks(): Promise<{ recovered: number }>` | `POST /api/tasks/resume`; bez body | `{"recovered": n}` | Globalne `recover_interrupted()` i wybudzenie workerów. Nie przyjmuje `task_id`, checkpointu ani receipt. |
| `cancelTask(id: string): Promise<void>` | `POST /api/tasks/{encodeURIComponent(id)}/cancel`; bez body | HTTP 200: `{"cancelled": true}`; transport TypeScript odrzuca body i zwraca `undefined` | Przyjęcie żądania anulowania; dla zadania `running` nie potwierdza końcowego stanu. |

Serwer `listTasks` interpretuje tylko cztery wymienione parametry. Typ
`Record<string, unknown>` nie oznacza obsługi innych filtrów. `limit` jest
parsowany przez `std::atoi`; nie jest to API walidacji ustawień. Transport
nie powinien dodawać własnego sztywnego progu ani udawać, że ograniczona
tablica obejmuje całą historię.

Przykład korzystania wyłącznie z dostępnych metod:

```ts
const rows = await api.listTasks({ status: "running", limit: chosenLimit });
const task = await api.getTask(selectedTaskId);
await api.cancelTask(selectedTaskId);
const afterCancel = await api.getTask(selectedTaskId);

// Oddzielna operacja globalna, po ustaleniu bezpiecznej granicy recovery:
const recovery = await api.resumeTasks();
```

Odpowiedniki C ABI: `loom_list_tasks(ctx, filter_json)`,
`loom_get_task(ctx, task_id)`, `loom_resume_tasks(ctx)`,
`loom_cancel_task(ctx, task_id)`. Transport JNI mapuje je na `list_tasks`
z `{filter}`, `get_task` z `{task_id}`, `resume_tasks` oraz `cancel_task`
z `{task_id}`. Jest to weryfikacja źródeł; ten raport nie potwierdza
uruchomienia aplikacji Android.

## Rekord, błędy i semantyka recovery

Natywny `TaskRecord::to_json()` zwraca:

```ts
{
  id, kind, status, params, input_hash, output_hash,
  checkpoint, attempts, max_attempts, error, parent_id,
  result, created, updated
}
```

`params`, `checkpoint` i `result` są JSON; brak checkpointu lub wyniku daje
`null`. `parent_id`, hashe i `error` mogą być pustymi stringami. Statusy
silnika: `pending`, `running`, `paused`, `done`, `failed`, `cancelled`.
Typ webowy `Task` opisuje jawnie jedynie `id`, `kind`, `status` i opcjonalny
`parent_id`, pozostałe pola mają typ `unknown`: konsument musi sprawdzić
ich kształt przed użyciem. Checkpoint i wynik są już dostępne do odczytu;
brak dotyczy publicznego zapisu i powiązania wykonania.

HTTP zgłasza błąd przez `{"error":{"code":...,"message":...}}` oraz
status, m.in. 400 dla nieprawidłowego argumentu, 404 dla braku zadania
i 409 dla konfliktu. `cancelTask()` odrzuca Promise przy odpowiedzi
niepoprawnej. Anulowanie już `cancelled` jest idempotentne; anulowanie
`done` lub `failed` zwraca konflikt. Dla `pending`/`paused` silnik
przechodzi do `cancelled`. Dla `running` zapisuje żądanie w pamięci;
handler musi je zaobserwować. Po sukcesie żądania UI odświeża rekord
i pokazuje rzeczywisty stan, bez zamiany `{"cancelled":true}` w dowód
zakończenia pracy.

`recover_interrupted()` wybiera wszystkie rekordy `running`, sprawdza
ponownie ich status, obniża `attempts` o jeden do minimum zera, zachowuje
checkpoint i przechodzi do `pending` z komunikatem
`interrupted; resuming from checkpoint`. Liczba `recovered` oznacza
liczbę takich przejść, nie liczbę zakończonych wznowień. API nie dowodzi,
że dany rekord pochodzi z zatrzymanego procesu, ani nie rozróżnia go od
aktualnie pracującego workera. Wywołanie podczas aktywnego wykonania
może ponownie zakolejkować żywe zadanie. Dlatego adapter nie może
automatycznie wywoływać tej metody podczas otwierania widoku ani wiązać
jej z przyciskiem „wznów ten workflow”. Bezpieczna granica recovery musi
wynikać z właściciela wykonania w rdzeniu, a nie z arbitralnego progu UI.

Istnieją zdarzenia `task:changed` i `task:progress` przez
`subscribeEvents(...)`; można użyć ich do odświeżenia `getTask(id)`.
`task:progress` nie jest trwałym receipt zakończenia. Odczyt wyniku
zadania pozostaje osobną czynnością.

## Czego brakuje publicznemu adapterowi

| Brak w publicznym C ABI / HTTP / LoomApi | Co już istnieje i czego nie należy z tym utożsamiać |
| --- | --- |
| Generic `submit` z typem handlera i kontraktem idempotencji | Prywatny C++ `TaskEngine::submit`, rejestracja handlerów i `run_sync`; wybrane istniejące operacje aplikacji mogą mieć własne trasy. |
| Zapis checkpointu i powiązanie wyniku/`complete` | Odczyt `checkpoint`/`result` istnieje. `TaskContext::save_checkpoint` i `set_result` są wewnętrznymi operacjami handlera. |
| `pause` i `resume_one(task_id)` | Wewnętrzne C++ `pause(id)`/`resume(id)` istnieją. Publiczne `resumeTasks()` ma semantykę globalnego recovery. |
| Atomowe powiązanie task → przygotowane żądanie → usage operation → wynik | Lokalny `workflow-checkpoint.ts` zapisuje stan i marker nieznanego wyniku w pamięci przeglądarki. Nie jest trwałym zleceniem TaskEngine ani dowodem wykonania zdalnego skutku. |
| Wersja rekordu i CAS przy checkpoint/complete/resume | Obecny TaskRecord nie wystawia publicznego warunku `expected_version` i operacji compare-and-swap dla tych zapisów. |
| Publiczny immutable prepared request i kontynuacja dokładnie tego żądania po receipt | Ponowne `Chat.send` tworzy nowe identyfikatory run/turn; nie kontynuuje poprzedniej decyzji ×10. |

Nie zdefiniowano tu nowych tras jako już dostępnych. W9 musi przydzielić
właścicieli kontraktów i publikacji C ABI / HTTP, zanim W10 zarejestruje
zdolność `publicTaskEngine` dla wykonywania workflow. Już teraz można
zbudować rzeczywisty konsument list/get/cancel; nie może on deklarować
submit, checkpoint-write, completion ani wznowienia pojedynczego zadania.

## Receipt W2 i przygotowane wznowienie W3

Stan źródła W3: `origin/gpt/chat-selector-2026-10-04`
`03c670caa6ee3d8ac2c478186548114f8e83927f`, w szczególności
`docs/reports/chat-selector-2026-10-04-evidence/API.md`, końcowa sekcja
„Do wątku 9” raportu oraz `loom/src/chat/graph_reply.cpp`.
W3 wskazuje brak publicznego `PreparedRequest/resume`; niskopoziomowa
kontynuacja wiąże już dokładne żądanie z decyzją usage. To nie jest
obietnica dostępnego endpointu.

Istniejące wywołanie potwierdzenia W2 ma kształt:

```ts
await api.usagePolicy({
  action: "confirm",
  operation_id: receipt.operation_id,
  receipt_id: receipt.receipt_id,
  approved,
  confirmation_ref: explicitConfirmationRef,
});
```

To decyzja księgi usage, nie wywołanie modelu ani potwierdzenie dostarczenia
odpowiedzi. Receipt trzeba odczytać z konkretnej decyzji dla tej operacji;
nie wolno generować zastępczego ID lub użyć receipt innej operacji.
`task_id`, `operation_id`, `receipt_id` i identyfikator przygotowanego
żądania są odrębnymi tożsamościami.

Publiczny adapter W3 musi udostępnić następujący kontrakt zachowania
(do implementacji i ustalenia nazw, nie są to istniejące metody):

1. Przygotowanie zamraża ostateczne żądanie po wszystkich nakładkach:
   model/URL, metodę, nagłówki, body, streaming i timeout, wraz z kontekstem,
   pochodzeniem, wersją przepisu i powiązaniem grafu. Publiczny uchwyt musi
   odnosić się do dokładnie tych bajtów i haszy, bez ujawniania sekretów
   transportu w UI lub logach repo.
2. Wynik przygotowania zawiera tożsamość żądania i dokładną decyzję W2:
   `operation_id`, `receipt_id`, stan autoryzacji i zużycie z tej decyzji.
   Po `requires_confirmation` żądanie pozostaje przygotowane bez dispatch.
3. Potwierdzenie i wznowienie przyjmują ten sam uchwyt przygotowania oraz
   tę samą parę operation/receipt. Odmowa, receipt nieaktualny, inne bajty,
   inny kontekst lub zużycie operacji muszą dać jawny wynik wymagający
   ponownego przygotowania; nie mogą wysłać zmienionego żądania.
4. Dispatch wykonuje atomowe przejęcie tej operacji raz. Ponowna próba
   po rozpoczęciu otrzymuje stan poprzedniej operacji lub jawny nieznany
   wynik, zamiast automatycznego retry. Reconciliation musi oprzeć się
   o zapis rdzenia, nie o lokalne przekonanie UI.
5. Wynik wiąże odpowiedź, graf, błędy schematu, checkpoint i rzeczywiste
   usage z tym samym przygotowaniem. Brak danych providerowych pozostaje
   brakiem, nie wymyślonym zerem.

W źródle W3 `ModelUsageGuard` wiąże `requests:1`, liczbę bajtów body,
`request_identity_sha256` dla `{method,url,headers,body,timeout_ms,stream}`
oraz `request_body_sha256`; blokuje drugą próbę już wykonanej operacji.
Wewnętrzne wejście `confirmation` używa pola `ref`; publiczne W2 używa
`confirmation_ref`. Nowy adapter musi jawnie obsłużyć ten mapping.
`GraphReplyPrepared` zawiera dziś także callbacki C++; nie stanowi sam
w sobie serializowalnego, trwałego publicznego uchwytu. W10 nie może
zaimplementować brakującej trwałości przez ponowne `Chat.send`.

## R39–R40: dostarczone komponenty W12, poza main

Sprawdzono źródła 2026-10-05: `origin/gpt/onboarding-2026-10-04`
`3c0bc36552ef9851f1174946cfb108549aae3228`, raport
`docs/reports/onboarding-2026-10-04.md`, README UI i natywny oraz pliki
w `loom/web/src/onboarding/`. Kod tego zakresu nie zmienił się od
`40907ad28f3244bfe508da647273d2e1d3bcf1b3`; późniejszy commit publikuje
dokumentację i dowody. Jest to **dostarczony przyrost na gałęzi W12,
jeszcze poza `origin/main` `30ad7d3`**, nie wdrożony ekran W10.

R39 (profil/onboarding z jawnym unknown/declined/never) i R40 (warstwowe
domyślne grafy, override, wyłączenie i trwałe wykluczenie) należą do W12.
W12 dostarczył komponenty i publiczną klasę C++ `OnboardingStore`;
**brakuje publicznego mostu C ABI / HTTP / JNI / `LoomApi` i wpięcia
w `App.tsx`**. Źródła tych granic na main nie zawierają onboarding,
a przyrost W12 nie dodaje do nich wywołań. W10 podłącza powłokę
i faktyczny transport po publikacji mostu; nie kopiuje UI W12 ani nie
tworzy drugiego magazynu profilu w przeglądarce.

### Eksporty i jeden adapter

`loom/web/src/onboarding/index.ts` eksportuje `OnboardingPanel`,
`WhatAppKnows`, `CandidateReview`, `OnboardingController`,
`normalizeNativeSnapshot`, `nativeDispatchAction` i typy kontraktu.
Propsy obu widoków są już zdefiniowane:

```ts
interface OnboardingPanelProps {
  adapter: OnboardingAdapter;
  providerChoices?: { id: string; label: string }[];
}
interface WhatAppKnowsProps {
  adapter: OnboardingAdapter;
}
```

Host przekazuje **ten sam stabilny adapter dla wybranego użytkownika**,
utworzony poza renderowaniem lub memoizowany. `providerChoices` to
opcjonalne podpowiedzi hosta; provider pozostaje jawnym wyborem użytkownika.
Każdy widok tworzy własny kontroler i na mount wywołuje `getSnapshot()`.
Mount nie rozpoczyna rozmowy z modelem. Rozmowa i formularz zapisują
do tego samego stanu natywnego; różni je akwizycja `user_stated` / `form`.
Brak opcjonalnego callbacku jest ujawniany, a odpowiednia funkcja pozostaje
wyłączona — komponenty nie zastępują go zapisem w localStorage.

`OnboardingAdapter` z `types.ts` przyjmuje opcjonalne `RequestOptions`
z `signal?: AbortSignal`. Dostarczone kontrakty i oczekiwane delegowanie:

| Metoda adaptera | Natywna granica hosta i wynik |
| --- | --- |
| `getSnapshot(options?)` | Po inicjalizacji `open(user, legacy?)`, `read(user)` i `normalizeNativeSnapshot(raw)`; wynik `Promise<OnboardingSnapshot>`. Otwarcie tworzy trwały profil, nie wywołanie modelu. |
| `dispatch(action, options?)` | `nativeDispatchAction(action, eventIdentity)` oraz `apply(user, expected_revision, envelope)`; po sukcesie zachować nowy raw snapshot i zwrócić jego normalizację. |
| `dispatchLayer?(action, options?)` | Ten sam `apply`, z `target:"layers"`; wynik ten sam typ snapshotu. |
| `modelRequest?(provider, options?)` | `model_request(user, provider)` najpierw sprawdza natywną prywatność i przygotowuje `Promise<ModelRequest>`; nie wykonuje transportu. |
| `completeModelRequest?(request, options)` | Istniejący transport hosta po preflight W2. `options` zawiera jawny `provider` i opcjonalny `signal`; wynik `Promise<ModelReply>`. |
| `ingestModelReply?(reply, options?)` | `apply(user, prepared_revision, {target:"model_reply", reply, time})`, z bindingiem oryginalnego requestu; wynik znormalizowany snapshot. To osobny envelope, nie `nativeDispatchAction` dla akcji formularza. |
| `saveScenario?(source, options?)` | `update_pack(user, expected_revision, pack, scenario)` jako nowa wersja metody grafu; host zachowuje właściwy pack, wynik jest snapshotem. |

Są to kontrakty wstrzykiwanej implementacji, **nie istniejące metody
`LoomApi` ani nazwy opublikowanych tras**. Publiczna klasa C++ w
`loom/include/loom/onboarding_store.h` ma `open`, `read`, `apply`,
`update_pack`, `model_request` i `policy_decision`; ta dostępność nie
zastępuje eksportowanego C ABI ani transportu do web/Android.

### Tożsamość, raw revision i mapowanie akcji

Surowy snapshot natywny ma `user_id` i zewnętrzny `revision`.
Adapter wiąże wszystkie odczyty, zapisy i przygotowania z tym samym
wybranym `user`; nie wyprowadza go z provider ID, profilu aplikacji,
grafowego runu czy TaskEngine task ID. `normalizeNativeSnapshot()`
projektuje scenariusz, pola, session, candidates, privacy, history,
settings i `effectiveDefaults`, zachowując kolejność pytań, natywne
atrybuty prywatności oraz stabilne klucze warstw. Pełny natywny dokument
scenariusza trafia do `scenario.source`.

**Normalizacja nie zachowuje zewnętrznego `revision` ani `user_id`
w `OnboardingSnapshot`.** Host musi utrzymać te dane przy raw odpowiedzi
i użyć dokładnego outer revision w `apply` / `update_pack`. Wewnętrzny
revision profilu oraz licznik unieważnienia lokalnego kontrolera są
odrębnymi licznikami. Natywny CAS odrzuca nieaktualny snapshot przez
`Conflict`; host przekazuje błąd i wymaga ponownego odczytu, zamiast
automatycznie powtarzać zapis na nowej rewizji.

`nativeDispatchAction(action, {id, time})` tworzy envelope, ale nie
generuje tożsamości użytkownika ani CAS:

- Zwykła akcja otrzymuje `target:"profile"`; dla `answer` zachowane są
  przesłane `id`, `time`, `source_refs` i `provenance`.
- `review.id` jest **ID kandydata**. Helper przenosi je do `candidate`,
  a `id` i `time` zdarzenia bierze z osobnego `eventIdentity`; dodaje
  `source_refs:[]` i `target:"profile"`.
- Akcja z `key`, albo `set_area_mode`, otrzymuje `target:"layers"`.
  Mutacje używają stabilnego `key`, a nie wersjonowanego `EffectiveDefault.id`.
  Operacje to `override`, `disable`, `exclude`, `reenable`,
  `clear_override`, `accept_proposal` i `set_area_mode` (`proposal` / `direct`).

### Przygotowanie modelu i współbieżność

`model_request` zwraca `calls_authorized:false`: przygotowanie nie jest
zgodą na wykonanie. Host przed transportem korzysta z W2; oczekiwany
wzrost ×10 wymaga potwierdzenia. Do `completeModelRequest` kontroler
przekazuje wyłącznie `prompt`, `section`, `questions`, `context`,
`candidates`, `policy`, `reply_schema`. Raw profil, drafty, token,
CAS, metoda, graf i przyszłe nieznane pola envelope nie trafiają do
modelu przez spread całego requestu.

Po odpowiedzi kontroler wiąże `provider`, `request_token`,
`snapshot_revision` i `graph_run_id` z oryginalnym przygotowaniem,
nadpisując kontrolne wartości zwrócone przez model. Host używa
**prepared revision**, nie późniejszego ostatnio odczytanego revision,
do natywnego ingest. Review pojedynczego kandydata i `confirm_section`
są odrębnymi operacjami. `user_stated|form|model_inferred` opisują
akwizycję, niezależnie od kanonicznego Origin `recorded|model|user`.

Kontroler serializuje swoje zapisy; nowa akcja, pause, reload lub
unmount unieważniają lokalną odpowiedź i przekazują abort. **Wspólny
adapter sam nie zapewnia odświeżania wszystkich kontrolerów ani
współbieżności między urządzeniami.** Host i rdzeń muszą zachować CAS,
unieważnić przygotowanie po zmianie polityki/scenariusza w innym widoku
i przekazać aktualny stan pozostałym konsumentom. `policy_decision(user,
request)` sprawdza efektywną warstwę; zachowane `profile.privacy` jest
inspection, nie upoważnieniem.

Miejsce wpięcia W10 pozostaje `App.tsx`: `PanelId`, `PANEL_LABELS`
i renderowanie w `drawer-body`. Działająca nawigacja wymaga komponentów
przyjętych przez W9 i realnego adaptera; nie należy deklarować pełnego
R39–R40 ani publicTaskEngine na podstawie samych eksportów UI.
RuntimeProfile W11 pozostaje zależnością integracji: W12 zlinkowany z main
bez tego loadera raportuje jawnie `available:false`, a nie zastępczą walidację.

Hashe SHA-256 sprawdzonych źródeł przy pinie W12 `3c0bc36`:

| Źródło | SHA-256 |
| --- | --- |
| `loom/web/src/onboarding/index.ts` | `8629366b7b708ffc553e2cb5e7206e1d48d2b3a7728257204be3f0ccbcc460e5` |
| `loom/web/src/onboarding/types.ts` | `1e24746ed00a1cd7119272dff48231644f3674dc1b29297d8276fa8ce855b13e` |
| `loom/web/src/onboarding/native-snapshot.ts` | `24e8494279a6ed92d7d2c26842f35dbd2567430a0eef4893ec9072c0ca25ef08` |
| `loom/web/src/onboarding/controller.ts` | `7aa3fe958897a61d090909fbc5d2e25bf9b00ba36a62c9eadffe05a984c77616` |
| `loom/include/loom/onboarding_store.h` | `fd9ce5cc3b2e7734ef20af83a1f5284c8599e1f7d29f2555be98132ca793a074` |

## Weryfikacja, liczby i niezrobione prace

Przed i po: cztery callable metody TaskEngine w `LoomApi`, cztery
odpowiadające trasy HTTP, zero dodanych metod/trasy/ekranów, zero
płatnych wywołań. Zweryfikowano kod wskazanych rewizji i zgodność tego
opisu z transportem. Ta zmiana dokumentacyjna nie uruchamia recovery
na żywych zadaniach i nie stanowi nowego dowodu runtime/Android/ctest.

Aktualizacja W12 z 2026-10-05 zastępuje stary pin `67e2124` pinem
`3c0bc36`: potwierdzono dwa rzeczywiste eksporty widoków na gałęzi W12,
zero dodanych ekranów/wywołań bridge na main. Porównano źródła propsów,
adaptera, normalizatora, kontrolera i C++ store; zachowano hashe powyżej.
Nie uruchamiano tu komponentów W12 ani ich raportowanych testów.

Nie opublikowano adaptera wykonującego generic workflow, ponieważ brak
publicznych operacji submit/checkpoint-write/complete/resume_one/CAS
oraz immutable prepared resume uniemożliwia prawdziwą implementację.
Wyniku odczytu TaskRecord nie należy nazywać brakującym API wyniku:
jest dostępny, lecz brakuje kontraktu zapisu i powiązania wykonania.

## Przyrost W10 z 2026-10-05

Publiczne cztery operacje TaskEngine pozostają bez zmian na bazie integratora
66da570d + API W1 50e6bb9 + W12 3c0bc36. W10 dostarcza źródłowy most HTTP
OnboardingStore i wspólny user-bound host w App dla obu widoków. Zachowane
exact outer revision, native CAS, stable layer keys i prywatność. Nowe mosty
`/api/methods`, `/api/analysis` i fragment GraphReply korzystają z istniejącego
Runtime; nie są publicznym C ABI/JNI. Prepared analysis request jest process-local,
nie TaskEngine record ani trwały resume. Host model-interview ma jawnie brakującą
zdolność completion; nie wysyła prywatnego profilu przez zwykły chat.

Szczegóły oraz dowody bramek nowego przyrostu są w
[raporcie API](interface-2-api-2026-10-05.md). Powyższa starsza sekcja W12 opisuje
oryginalny kontrakt, nie deklaruje jego aktualnej niedostępności w HTTP W10.

## Do wątku N

- **Do wątku 9:** przydzielić publiczne TaskEngine submit/checkpoint-write/complete/
  resume-one/CAS i granicę globalnego recovery. HTTP OnboardingStore oraz nawigację
  obu widoków dostarczył W10; C ABI/JNI i RuntimeProfile W11 nadal są osobnymi
  zależnościami. Odczyt wyniku istnieje, brak dotyczy zapisu/wiązania wykonania.
- **Do wątku 3:** trwały publiczny immutable PreparedRequest czatu i wznowienie
  oryginalnej operacji po receipt W2 ze stanami stale/already-attempted/unknown;
  nie zastępować go powtórzeniem Chat.send.
- **Do wątku 2:** dokładne operation/receipt i mapping confirmation_ref;
  approval księgi nie oznacza delivery.
- **Do wątku 12:** utrzymać outer revision CAS i effective privacy. Completion
  model-interview wymaga dedykowanego hosta i W2-bound filtrowanego transportu;
  przedstawienie RuntimeProfile status w widokach wymaga projekcji tego pola.
