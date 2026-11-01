# Rozmowa i język urządzenia — diagnoza oraz poprawka AUTO, 2026-11-01

Baza `6f5ed6fccfa51ac087a827499522c84b58fb7472` zachowana. Kod poprawki:
**5389c6455755baaba48ee3d6f6872841ed406fdb**. **Implemented + typechecked; nowy browser/build/package NOT RUN.**
Nie jest to nowy kandydat do wdrożenia. ZIP UI i wszystkie poprzednie artefakty zachowane.

## Co faktycznie odtworzono

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

## Weryfikacja i konkretny bloker

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
