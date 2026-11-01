# Następna partia UI — odbiór w środowisku docelowym, 2026-11-01

**Odebrano trzy nowe przepływy UI**: jawne sprawdzenie/reprojekcję, wybór
istniejącego regionu OCR do kontekstu oraz eksport/odtworzenie rozmowy z aktywami.
`TARGET_ENV_POLICY_APPLIED`: wynik dotyczy naszego bieżącego środowiska;
brak innych OS/CI matrix nie blokuje tego odbioru. Nie uruchamiano płatnego CI.

Baza **9db00c7060b97a255ebea4dc02de6b353da6ca56**, kod **418dce4624ca4d288d209cf91a36c2e086ce1a2c**.
Admin receipt po naprawie odczytany i zgodny z czterema wierszami odbioru oraz
cached/świeżymi HEAD/status/CP/hashe. Projection/OCR/bundle nałożone raz.
Face: siedem jawnych plików portu/komponentu/danych/testów; **bez hooka ChatView**,
bez Python reference, backendu, biblioteki i renderera. Typecheck nie dowodzi
integracji Face. Żadne checkpointy/administracyjne koperty nie weszły do produktu.

## Wyniki wykonane

- Jedno `npm --prefix loom/web run build -- --outDir ../../.integration-afternoon-ui-face-20261101/resume/web-dist`:
  strict `tsc -b` i Vite **PASS**, faza Vite 45,35 s; ostrzeżenie chunk-size zachowane.
  Bez C++ buildu lub instalacji. Nowy fixture projection **1/1 PASS** przez
  istniejący TypeScript `transpileModule` i Node test; historyczny ERR_NO_TYPESCRIPT
  autora nie został uznany za błąd produktu ani ponawiany.
- Prawdziwy App/SourcePartsPanel: native check `refused`, jawny checkbox,
  świeży odczyt i odmowa zmienionego selektora **przed wysłaniem reprojekcji**;
  potem poprawna reprojekcja z expected receipt hash i tylko wybranym własnym ID.
  Globalny selektor/pozostałe ustawienia zachowane. Mapping `decision_required`
  nie udaje równoważności. Jawny istniejący przycisk dodaje nowe wystąpienie,
  nie usuwa starego. Nowa projekcja `compatible`, pusta `not_exercised`, oryginał
  zgodny SHA. Odmowy wadliwego receipt dodatkowo sprawdza fixture mechanizmu;
  nie przypisujemy mu dowodu native HTTP.
- Prawdziwy GraphView/ImageRegionContextPanel: odczyt zapisanej reprezentacji,
  wybrany region/tekst/metadane, globalny zapis/readback i onGraph tego samego ID.
  Rzeczywisty request ChatADHD do lokalnej atrapy zawiera ID i tekst regionu,
  `inferred`/nullconfidence, bez niewybranych regionów. Widoczna odpowiedź pochodzi
  z requestu. Tekstu nie wklejano w pytanie, nie dosyłano załącznika. Reużyto
  zachowany syntetyczny OCR; nie uruchamiano OCR engine ani vision ponownie.
- Prawdziwy ImportPanel/ConversationBundlePanel: wybór rozmowy, native export,
  pobrany ZIP i SHA, odmowa błędnego zewnętrznego pinu przed importem; poprawny
  restore z pointer→nowe ID, rodzicami, zarządzanym oryginałem i jego SHA.
  Oddzielny niekompletny pakiet wymaga jawnej zgody i pokazuje **INCOMPLETE 0/1**;
  kompletny **1/1**. Nie zmienia globalnego selektora ani nie wybiera rozmowy.
- Końcowy native restart i rzeczywisty UI/readback: dokładny obiekt odczytu
  wybranej części PDF identyczny przed/po, zachowany selektor, odtworzeni rodzice,
  oryginał/hash, widoczna rozmowa na liście i ten sam region OCR w UI.
  Brak kolejnego provider requestu w tej kontynuacji persistence.

To jedna partia z celowanymi kontynuacjami po błędach fixture, **nie trzy
nieprzerwane zielone przebiegi**. Pierwsza próba ma 2/3 ukończone grupy, potem
brak parent w źródłowej fixture. Poprawiono wyłącznie nową syntetyczną kopię przez
istniejący PATCH messages; asercja rodzica pozostała. Druga wykonała bundle i
restart, po czym błędnie wymagała PDF_WYBRANY w pierwszym wystąpieniu będącym
nagłówkiem. Naprawiono oczekiwanie na exact read equality i wykonano wyłącznie
affected tail (1/1 PASS), bez powtarzania projection/OCR/export/import. Zachowano
logi, kopie danych, oba stare harnessy i diagnozy. Błąd initScript localStorage
na about:blank był w harnessie; ograniczono go do loopback. Druga i końcowa próba
mają zero page errors. Produktu ani jego bramek nie osłabiano.

Syntetyczne źródła: wcześniejszy zamknięty OCR fixture, mały PDF z poprzedniej
partii i dwie rozmowy utworzone przez istniejący importer. Stare dane niezmienione;
kopie i source hashes w prywatnym seed.json. Serwer: jawne accounts/data/static,
Landlock, whitelist env i loopback, własny Chromium. Tylko atrapa transportu
modelu; nie jest to dowód jakości realnego modelu. Wszystkie procesy zakończone.

## Nowy immutable kandydat

| Obiekt | SHA256 / SHA |
|---|---|
| Archive | `d4901d1ebf599f4d87ec8e58a52fa150a1ceab628d437a06e6d299496943a3dc` |
| Manifest | `571fdef8afffd8a3516db940abf70460830f7d78ab8e760de5701bf6c06b1a68` |
| libloom.so | `7004d54bd106e281acddd5d352e8ab0e2ced0d710245f8a17c003ccb336e610a` |
| loom-server | `cc0f65f19cfaf609706064ae068f09073016aba257ce80509d1ae5431eb8ac90` |
| web index | `e23fb2f32538c7b4c083b99ef43b459a36ab30e3358b5454730e25649f3287f6` |
| runtime code | `e27ce9ff0076c287963a588d942d68e7bebaca3f` |
| loom tree | `b3b541017573e08b1b7471297e94b5b0cf5eb111` |

Pakiet `chatadhd-server-linux-x64-418dce46-release-afternoon-ui-20261101.tar.gz`
w `.integration-afternoon-ui-face-20261101/resume/`. **106/106 payloadów** zgodnych
z archiwum/stagingiem, **90 runtime/helper payloadów byte-identical** z 18d4.
Nowy web ma źródłowe hashbinding; bez relabelowania starego pakietu. Reużyto
immutable runtime przez hardlinki bez zapisów przez nie; cache nie kopiowano.
Istniejący `verify-package.sh` z BusyBox wykonał rzeczywiste extract/start/
healthz/static/auth-me HTTP200 **PASS**. GNU tar nie ponawiano ani nie deklarowano PASS.

Obowiązkowa projection upgrade/refusal/coldrestore bramka z poprzedniego przyrostu
pozostaje dokładnie przypięta do lib7004 i identycznych helperów/profili: manifest
nowego pakietu zawiera hash `gate-result.json` z rzeczywistym wykonaniem na 18d4.
Nie nazywamy tego nowym retestem. **RPO: cold restore traci wszystkie post-copy
writes; selektory migrowane tylko jawnie.** Nie jest to lossless in-place rollback.
Nowy UI nie zmienia tego kontraktu. Dotychczasowy 18d4 pozostaje dostępny;
informacja o produkcyjnym wdrożeniu pochodzi od administratora.

## Pozostały zakres i przekazanie

- Face host/library/React hook/controller/render: **NOT RUN**, port nie montuje się
  w ChatView i nie dodaje pracy do wyłączonej opcji; brak synchronizacji globalnego
  selektora innej karty nadal do jawnego zaprojektowania. Nie podłączano nowego API.
- CSV/AST picker i dalszy port Face: osobne zadania, nie włączono ich live worktree
  ani nie czekano na nie po rozpoczęciu wspólnego buildu.
- Bounded listy, brak CAS dla selector array i brak anulowania wysłanego zapisu
  pozostają jawne. Bundle normalizuje rozmowę, nie odtwarza konta/bazy/kluczy;
  INCOMPLETE oraz niepewny zapis nie są pełnym sukcesem. Brak realvision.
- Pełne P0/native/fleet/compat/GUI starego wydania niepowtarzane. Fizyczne mobile,
  inne OS, realny provider/model i nowe wdrożenie **NOT RUN**.

Prywatne dowody: `accepted-ui.json`, per-phase observations/logs/harness copies,
matching-audit, code-files, release-package, verify-package-result i manifest
odbioru. Raport nie publikuje danych kont ani prywatnego researchu.
Zachowane nowe unikalne bajty: 66541178 (<80 MiB), bez ponownego liczenia
90 odziedziczonych immutable payloadów; wolne 1374670848 B (>1 GiB).

Next: administrator odbiera dokładny commit i kandydat, publikuje do
`team/chatadhd-integrator` i aktywuje nową wersję w swoim zakresie. Integrator nie
czytał poświadczeń GitHub i nie uruchamiał publish/deploy/usług/VM.
