# Rozmowa i język urządzenia — diagnoza oraz poprawka AUTO, 2026-11-01

Baza `6f5ed6fccfa51ac087a827499522c84b58fb7472` zachowana. Kod poprawki:
**5389c6455755baaba48ee3d6f6872841ed406fdb**. **Poprawka AUTO odebrana w poniższym zakresie na nowym kandydacie.**
ZIP UI i wszystkie poprzednie artefakty zachowane. Produkcji nie zmieniono.

## Zamknięcie bramki po wznowieniu — 17:55 UTC

Od result `0f2a055f0581df6aca54fd20540956e2bda58944`, bez zmian kodu
`5389c6455755baaba48ee3d6f6872841ed406fdb`. **TARGET_ENV_POLICY_APPLIED.**
Jeden Vite build: **PASS, 20,99 s**. Typecheck i fixture z poprzedniego
przyrostu pozostają dowodem historycznym; nie wykonano ich ponownie.

Jedna docelowa partia actual browser na własnym backendzie, z syntetycznymi
gośćmi i lokalną atrapą upstream: **PASS zakresowy**. Sprawdzono:

- nowy gość PL i EN: brak zapisanego `ui_language`, właściwy język urządzenia,
  pochodzenie `device` i odpowiedni katalog;
- jawny profil EN: zachowany; AUTO na urządzeniu PL omija go świadomie;
- jawny wybór EN, reload, reset AUTO i kolejny reload: właściwy język oraz
  pochodzenie potwierdzone po odczycie konfiguracji; profil EN nie został nadpisany;
- rzeczywisty ChatView → `/api/chat` → lokalne `/chat/completions`: dwie odpowiedzi
  oraz jedna kontrolowana odmowa upstream400 widoczna w UI; 3 wywołania atrapy,
  zero rzeczywistych wywołań dostawcy, zero browser page errors.

Zachowano dwa nieudane wykonania harnessu: wskazanie legacy SettingsPanel zamiast
SettingsHuman oraz zbyt wczesną asercję po reload (bootstrap EN przed konfiguracją).
Naprawiono wyłącznie nawigację i oczekiwanie na język **oraz** pochodzenie.
Nie zmieniono produktu/asercji semantycznych ani nie powtórzono buildu.
Wszystkie własne procesy browser/server/provider zakończone.

### Dokładny kandydat i granice dowodu

- web/code: `5389c6455755baaba48ee3d6f6872841ed406fdb`;
- drzewo Loom: `e2d90a41d021d5811cc3911d088f4bdec0a45a44`;
- archive: `chatadhd-server-linux-x64-5389c645-release-device-language-20261101.tar.gz`;
- archive SHA256: **`f093065f77e7019e754a4a49b144740d41d4c44050633d62b08eaa6a08bc05cd`**, 38223774 B;
- manifest SHA256: `7dd43de14d46b78055ee4c95104607b4d4c2de514c256a25f1df90abaf1527d0`;
- index SHA256: `93608824007fad4189a68908ca8dcab82af1989a588ef5026a048f4567887c32`;
- server SHA256: `ada3e54e89b3a84064fd0774bf0f553e4c8426bae0498e06fe6c8d0f5bd71bab`, source
  `3d022d3bb8dfe4fe00dd04c1fd3794edbce6092c`;
- lib SHA256: `7004d54bd106e281acddd5d352e8ab0e2ced0d710245f8a17c003ccb336e610a`, source
  `e27ce9ff0076c287963a588d942d68e7bebaca3f`.

106 payloadów sprawdzono strumieniowo w archiwum i względem stagingu;
90 runtime payloadów identycznych z de0. Nowy web nie oznacza nowej bramki C++
ani ABI. ZIP preview pochodzi z servera ada3, nie ze starszej lib7004.
Izolowany staged startup **PASS**: JSON `/api/healthz` 200 i `{ok:true}`,
statyczny index zgodny hashem, auth JSON200, config bez uprawnień401.
Start nastąpił ze stagingu zgodnego bajtowo z archiwum, nie z nowej ekstrakcji.

Admin17:46 dopuścił rezerwę 900MiB i zapis <=100MiB. Przed pakowaniem:
1022787584 B wolne, szacunek 79846086 B zapisu łącznie; wynik oszacowania
zapisu 78802405 B. Po utworzeniu archiwum 983273472 B wolne.
Pełna dodatkowa ekstrakcja **NOT RUN**: payload100216784 B, brak69943248 B
względem rezerwy i bufora przy pomiarze verifiera. Nie kasowano danych/cache,
nie zmieniano zasobów; stary de0/d490 zachowany. Archiwum ma tryb0444;
staging jest aliasem payloadów i nie jest deklarowany immutable.

**Owner API400 rootcause nadal NIEUSTALONA.** Lokalna atrapa dowodzi kontraktu
żądania/odpowiedzi i propagacji błędu, nie naprawy incydentu ani sukcesu realnego
providera. Potrzebne: bezpieczny kod/komunikat błędu, dostawca/model i faza awarii.
Nie udowodniono pełnej polonizacji ani fizycznego telefonu; viewport420×900.
Bez rzeczywistych modeli/zewnętrznych API, C++, starych suite, CI, push lub wdrożenia.

Dowody prywatne: `.integration-chat-device-language-20261101/resume/`
(`accepted-ui.json`, `release-package.json`, `archive-verification.json`,
`candidate-smoke-result.json`, log buildu i wszystkie zachowane błędy) oraz
`target-browser-settle-fix/diagnosis.json`. Nie trafiają do commita produktu.
Next: administrator odbiera konkretny hash i wykonuje własny backup/cutover;
osobno zbiera brakujący bezpieczny dowód incydentu400. Completed nie aktywuje produkcji.

## Co faktycznie odtworzono — wcześniejsza diagnoza

Użyto dokładnych lokalnych bajtów wdrożonego d490:
- kod web `418dce4624ca4d288d209cf91a36c2e086ce1a2c`;
- archive `d4901d1ebf599f4d87ec8e58a52fa150a1ceab628d437a06e6d299496943a3dc`;
- server `cc0f65f19cfaf609706064ae068f09073016aba257ce80509d1ae5431eb8ac90`;
- index `e23fb2f32538c7b4c083b99ef43b459a36ab30e3358b5454730e25649f3287f6`.

Własne nowe syntetyczne konta gości, mobile viewport 420×900, Landlock,
whitelist env i loopback. Nie czytano ani nie zmieniano produkcji.

Nowy gość PL: `navigator.languages=[pl-PL]`, brak top-level `ui_language`,
HTML `pl`, placeholder „Napisz wiadomość”. Nowy gość EN analogicznie `en`/„Message”.
Nie potwierdzono tezy, że obecny bootstrap sam narzuca wygenerowane EN.
Dostępne katalogi PL/EN rzeczywiście wyświetliły właściwe napisy.

Rzeczywisty formularz wysłał `POST /api/chat` z `message`, `new_conversation:true`
i `client_id`; sesja działała przez cookie, bez nagłówka Authorization. Model
pominięty w żądaniu został poprawnie rozstrzygnięty przez konfigurację syntetycznego
konta. Jawnie wskazany lokalny provider bez uwierzytelniania otrzymał
`/chat/completions`, `model=synthetic/device` i system/user messages.
HTTP200/SSE start→delta→done oraz lokalna odpowiedź widoczne w ChatView.
To dowód kontraktu z atrapą, **nie rzeczywistego sukcesu dostawcy**.

Osobny lokalny upstream zwrócił HTTP400 z kodem `synthetic_invalid_model`.
Aplikacyjne `/api/chat` miało HTTP200/SSE i zdarzenie `error`, kod `http`,
komunikat „API error 400” z syntetycznym error body; UI go pokazało.
Pusty POST nie był reprodukcją. Nie zmieniano serializera ani routingu rozmowy,
ponieważ nie wykazano ich defektu. Liveness `/api/healthz` nie zastępuje rozmowy;
`/health` jako HTML SPA nie jest właściwą bramką API.

**Przyczyna właścicielskiego API400 pozostaje nieustalona.** Brakuje bezpiecznego
kodu/komunikatu błędu, wybranego dostawcy/modelu i fazy awarii. Nie potrzeba treści
rozmów, kluczy ani prywatnych konfiguracji. Zgłoszone EN również nie odtworzyło się
na świeżym gościu; źródło ustawienia/preferencji konkretnego urządzenia nieznane.

## Potwierdzony defekt i mała poprawka

Na tym samym wdrożonym bundle zapisano przez istniejące native API jawną
preferencję profilu EN. Następnie `ui_language=auto` na urządzeniu PL nadal dało EN.
Przyczyną było `pick(setting) ?? pick(preference)` — AUTO pomijało ustawienie,
ale nie preferencję. Ten defekt resetu jest potwierdzony niezależnie od incydentu.

Nowy resolver rozdziela `auto/explicit` i źródło `config/user_preference/device/
catalog_fallback`. Jawne `auto` lub pusty reset wybiera urządzenie. Brak ustawienia
może respektować wyłącznie jawną warstwę `user`; builtin/generated nie stają się
wyborem użytkownika. Zapisany `ui_language=en` pozostaje respektowany. Nie ma
zbiorczej migracji ani zgadywania, czy dawny zapis EN pochodził z generatora.

Istniejące ustawienia mają prosty wybór AUTO/obsługiwane języki, zapis przez
istniejące `/api/config/ui_language` dopiero po działaniu użytkownika oraz opis
pochodzenia. Brak forcePL, IPcountry, zmian rozmów/modelu i native API.

## Historia poprzedniego przyrostu — stan przed decyzją capacity17:46

Poniższe NOT RUN i next opisują poprzedni zamknięty etap. Aktualny wynik jest wyżej.

- Nowy fixture granicy AUTO/provenance **1/1 PASS, 10 asercji**, bez ponowienia starych suite.
- `tsc --project loom/web/tsconfig.json --noEmit` **PASS**.
- Pierwszy harness diagnozy zapisał obie poprawne obserwacje, ale zakończył exit1
  przez pozostały guard 0/0 bez `complete()`. Nie jest liczony jako PASS suite.
  Druga reprodukcja AUTO + upstream400 zakończyła exit0; wszystkie procesy zamknięte.
- Vite, browser poprawionej wersji, nowy pakiet: **NOT RUN — pojemność**.
  Pomiar 1069174784 B wolne, próg 1073741824 B.
  Poprzedni webdist 13008919 B i archiwum
  38218879 B; z buforem 8 MiB potrzeba dodatkowo
  **25964567 B** na web/browser albo
  **64183446 B** na web/archiwum. Pomiar
  jest chwilowy — przed wznowieniem trzeba zmierzyć ponownie.

Nie kasowano dowodów/cache, nie instalowano, nie zwiększano VM/dysku, bez CI/push/deploy.
Brak C++ zmian: server ada3/lib7004 zachowane; **de0c4930 nie zawiera tej poprawki**.

Next: zapewnić pojemność, wznowić ten sam task od nowego HEAD bez ponownego apply.
Jedna wspólna kompilacja web i przygotowany target browser: PL/EN newguest,
jawne EN + reload, reset AUTO przy profilu EN, pochodzenie konfiguracji,
rzeczywiste wysłanie do lokalnej atrapy i propagacja błędu. Dopiero potem exact
nowy candidate/manifest z prawdziwym bindingiem i odbiór administratora.

Dowody: `.integration-chat-device-language-20261101/diagnosis-receipt.json`,
`deployed-repro/diagnosis.json`, `deployed-auto-error/diagnosis.json`,
`choice-test.log`, `typecheck.log`, `capacity-blocker.json`, przygotowany
`run-target-browser.py`/`target-browser.mjs` (NOT RUN), manifest i CHECKPOINT.
TARGET_ENV_POLICY_APPLIED. Architektoniczna notatka AGEDS↔iOmatrixFRM przyjęta
wyłącznie jako planowanie; bez dodatkowego researchu, źródeł ani testów.
