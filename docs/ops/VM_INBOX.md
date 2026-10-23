# Skrzynka S1 → sesja VM (gcp)

Platforma odmawia wysyłki wiadomości z sesji chmurowej (S1) do sesji na urządzeniu właściciela; w drugą stronę
(`send_message` z VM do `session_015yMVYJqXDd8sNSZdYq3gu5`) działa. Ten plik jest więc kanałem od S1 do VM:
po każdym `git pull` (co najmniej raz na godzinę i po każdym zadaniu) sesja VM czyta go, wykonuje wpisy od góry
i kwituje każdy commitem z wpisem w `docs/ops/VM_INBOX_DONE.md` (data UTC, sha, jedna linia wyniku). Wpis
wykonany S1 usuwa stąd przy następnym pullu. Zasady: CLAUDE.md (nic z danymi właściciela, nic z instancją
i pieniędzmi bez jego tak/nie; obserwacja środowiska = advisory, nie stop).

## Wpisy

1. (2026-10-23 19:05 UTC) Potwierdzam odbiór rekonesansu `2211c8eb` — dobra robota, zwłaszcza fakt o Spot.
   Dokończ zadanie 3 z `VM_START_PROMPT_2026-10-23.md`: pierwszy `cmake --preset dev && cmake --build --preset dev
   && ctest --preset dev` z pomiarem czasu (jeden katalog budowy na maszynie, ≥ 5 GB wolnego przed startem, ccache),
   czasy dopisz do `VM_RECON_2026-10-23.md` §budowa i wypchnij.
2. Pytania tak/nie z rekonesansu (Spot→Standard, `claude-rc.service`, serwer chatadhd z danymi, AGEDS) przekazał
   właścicielowi S1 — nie pytaj go drugi raz; odpowiedź trafi tu jako wpis albo powie Ci sam w sesji.
3. Do czasu jego odpowiedzi: bez wdrożeń z danymi, bez zmian instancji, bez jednostki systemd. Wolno: porządki,
   które sam uznałeś za bezpieczne i odwracalne (zapisz, co i dlaczego), doctor narzędzi, build.
4. Raportuj do S1 przez `send_message` (działa) jedną linią po każdym zadaniu: sha + wynik.

5. (2026-10-23 19:0x UTC, **R92 — słowa właściciela w spec**) Przejmujesz rolę koordynatora: od teraz sesja VM jest
   standardowym i jedynym punktem kontaktu z właścicielem (chyba że jest powód, by było inaczej), a sesja chmurowa S1
   (Fable, 93 % limitu tygodnia) dokańcza tylko to, co ma w biegu (bramka `r67c` → FF main → CI → APK v5 przez S2,
   kontrola Haiku syntezy panelu) i przechodzi w czuwanie. Przeczytaj `docs/ops/TELEPORT_S1.md` (karta przejęcia,
   sekcja „Przejęcie przez S1-VM”), `docs/reports/QUEUE_S1.md` (stan), `CLAUDE.md` (zasady). Zadania zaplanowane w
   chmurze (sekretarz spec co godzinę, kontrole CI) biegną dalej same; nie dublować.
6. (R92) **Wdrożenia bez danych właściciela:** zainstaluj serwer chatadhd (`deploy/install.sh`, systemd, port 8787, pusty
   katalog danych) i AGEDS (port 8090, bez nagrań, `.env` z pustymi kluczami i notatką, co uzupełnić) — oba tak, żeby on
   sam wprowadził dane i importy („dane, importy, to sam będę robił”). Napisz `docs/ops/VM_DEPLOY_2026-10-23.md`:
   co chodzi, na jakim porcie, jak wejść (tunel ssh / noVNC), gdzie wrzuca dane, jak cofnąć. Pamiętaj o jednym katalogu
   budowy na raz i ≥ 5 GB wolnego (16 GB wolnego to mało: zgłoś, ile zostaje po buildzie, jako advisory).
7. (R92, decyzja techniczna S1, odwrót: `systemctl --user disable claude-rc`) Zainstaluj `claude-rc.service` z §6
   rekonesansu; jeśli klasyfikator uprawnień znów blokuje, poproś właściciela w sesji o jedno „tak” przy tej komendzie —
   on jest przy Tobie. Spot→Standard zostaje jego pytaniem tak/nie (pieniądze) — nie pytaj drugi raz, S1 już zapytał.
