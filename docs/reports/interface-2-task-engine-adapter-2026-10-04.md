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

## R39–R40: właściciel W12 i miejsce podłączenia

Źródło przydziału: `origin/gpt/onboarding-2026-10-04`
`67e2124ecae25d841e611b629268987372b658fd`, raport
`docs/reports/onboarding-2026-10-04.md`. R39 (profil/onboarding z jawnym
unknown/declined/never) i R40 (warstwowe domyślne grafy, override,
wyłączenie i trwałe wykluczenie) należą wyłącznie do W12. W12 dostarcza
komponenty z `loom/web/src/onboarding/`; W10 dostarcza hook powłoki po
otrzymaniu rzeczywistych eksportów. Wskazana rewizja raportuje nadal
pracę nad kontraktem, więc nie jest dowodem gotowego komponentu.

Miejsce integracji: `loom/web/src/App.tsx`, istniejące `PanelId`,
`PANEL_LABELS` i warunkowe renderowanie w `drawer-body`, albo jawny
callback powłoki wskazany przez finalny kontrakt W12. Dopiero po
publikacji komponentu, jego propsów i zarejestrowanego adaptera należy
dodać działającą pozycję nawigacji oraz przekazać faktycznie dostępne
metody. Nie należy dodawać pustego panelu, atrap recovery, metod
`resume_one` czy adaptera deklarującego nieistniejące możliwości.

Profilowe `unknown/declined/never` nie zmieniają kanonicznych typów
pochodzenia `recorded|model|user`. Mechanizm RuntimeProfile W11 i
trwałość w rdzeniu muszą być dostępne w zaakceptowanej rewizji przed
deklarowaniem pełnej obsługi R39–R40.

## Weryfikacja, liczby i niezrobione prace

Przed i po: cztery callable metody TaskEngine w `LoomApi`, cztery
odpowiadające trasy HTTP, zero dodanych metod/trasy/ekranów, zero
płatnych wywołań. Zweryfikowano kod wskazanych rewizji i zgodność tego
opisu z transportem. Ta zmiana dokumentacyjna nie uruchamia recovery
na żywych zadaniach i nie stanowi nowego dowodu runtime/Android/ctest.

Nie opublikowano adaptera wykonującego generic workflow, ponieważ brak
publicznych operacji submit/checkpoint-write/complete/resume_one/CAS
oraz immutable prepared resume uniemożliwia prawdziwą implementację.
Wyniku odczytu TaskRecord nie należy nazywać brakującym API wyniku:
jest dostępny, lecz brakuje kontraktu zapisu i powiązania wykonania.

## Do wątku N

- **Do wątku 9:** przydzielić publikację publicznego TaskEngine
  submit/checkpoint/complete/resume_one/CAS i bezpieczną granicę
  globalnego recovery. Cztery gotowe callable metody, payloady i wyniki
  są w tabeli powyżej. Zachować różnicę między odczytem wyniku a
  brakującym zapisem/wiązaniem wykonania.
- **Do wątku 3:** dostarczyć publiczny trwały immutable PreparedRequest
  i wznowienie tego samego żądania po dokładnym receipt W2; określić
  błędy stale/already-attempted/unknown i receipt odpowiedzi. Nie
  zastępować kontynuacji ponownym `Chat.send`.
- **Do wątku 2:** utrzymać dokładną parę operation/receipt i jawny
  mapping `confirmation_ref` → wewnętrzne `confirmation.ref` w
  zaakceptowanym adapterze W3; approval księgi nie oznacza delivery.
- **Do wątku 12:** opublikować komponent/eksport, propsy i wymagane
  zdolności R39–R40; W10 wtedy podłącza go w `App.tsx`. Onboarding
  i warstwy danych pozostają zakresem W12.
- **Do wątku 10:** po przyjęciu prawdziwych API zarejestrować wyłącznie
  obsługiwane zdolności. Konsument list/get/cancel jest możliwy już
  teraz; resume workflow wymaga nowych kontraktów rdzenia.
