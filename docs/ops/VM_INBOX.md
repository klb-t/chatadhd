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

8. (2026-10-23 19:3x UTC, **R94 — słowa właściciela w spec, przeczytaj je w całości**) **Poszukaj skryptów z 2024** od
   wywłaszczenia: właściciel pamięta, że „w 2024 Claude albo GPT ogarniali preempting skryptami”. Sprawdź: metadane
   PROJEKTU (nie tylko instancji) w `clever-stone-467803-g7` (startup/shutdown-script), Cloud Scheduler / Cloud Functions /
   Cloud Run jobs w projekcie, stare instancje i obrazy, jego repozytoria na GitHubie (`gh repo list klb-t`, grep
   preempt/shutdown/hibern), katalogi na maszynie (`~/devbox-agent`, `~/.handoff-stage-*`, `~/.resident-stage-*`).
   Wynik (znalezione albo „nie ma”) jako sekcja w `VM_RECON` i jedna linia do S1.
9. (R94) **Zabezpieczenie przed wywłaszczeniem — zaprojektuj i zbuduj wg jego przykładu, fakty z dokumentacji GCP
   (z datą i linkiem, nie z pamięci):** ile uprzedzenia daje Google (ACPI + metadane `instance/preempted`), co zostaje
   (dysk trwały: maszyna STOPPED, nie skasowana), co ginie (RAM, procesy, tmux), czy `suspend` jest możliwy przy
   wywłaszczeniu Spot, czy model provisioningu zmienia się tylko na zatrzymanej maszynie, skąd może biec stróż po
   zatrzymaniu (poza maszyną: Cloud Scheduler + Cloud Run job / Function, albo zadanie w chmurze Claude; ta maszyna nie
   może sama siebie wystartować). Etapy: (a) skrypt wyłączenia w limicie uprzedzenia: commit+push WIP każdego
   worktree, znacznik „praca w toku” poza maszyną, zapis wskaźnika sesji Claude (transkrypty są na dysku →
   `claude --resume` po starcie — sprawdź, jak wznowić Remote Control nieinteraktywnie); (b) stróż poza maszyną:
   po STOPPED uruchamia ją ponownie (jako Spot, gdy jest pojemność; z promocją na Standard tylko, gdy znacznik „praca
   w toku” — to Twój spot-guard) i wraca do Spot w bezczynności; (c) jednostka auto-startu wznawia tmux + sesję;
   (d) usługi (WatchDog, serwery) wstają z systemd — zmierz przerwę w sekundach i podaj ją właścicielowi uczciwie
   (jedna maszyna = przerwa minutowa, nie sekundowa; „niezauważalne przepięcie” wymaga drugiej maszyny i load
   balancera — policz koszt, nie buduj bez jego tak). Wdróż od razu to, co nie kosztuje (a, c, d, stróż bez promocji);
   promocję na Standard WŁĄCZ dopiero po jego tak/nie z szacunkiem: cena Standard × godziny pracy/dobę.
10. (R94, zgoda właściciela na pieniądze w tym zakresie) **Dysk:** gdy braknie miejsca, dokładasz sam przez API GCP
   (`gcloud compute disks resize` + powiększenie systemu plików online), tyle, ile potrzeba; każde powiększenie zapisz
   (GB, cena z datą) w `VM_RECON` i w jednej linii do S1. Przed buildem: ≥ 5 GB wolnego, inaczej dokładasz.
