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
11. (R94, uzupełnienie do wpisu 9, z Twojego własnego rekonesansu) Dziennik pokazał `Refusing poweroff operation, shutdown
   is inhibited by … unattended-upgr` — przy wywłaszczeniu 23.10 maszyna ODMÓWIŁA czystego zamknięcia, bo aktualizacje
   pakietów trzymały blokadę, i po 120 s uprzedzenia dostała twarde zatrzymanie. Skrypt wyłączenia musi zdążyć mimo
   tego: albo aktualizacje bez blokady zamknięcia (konfiguracja unattended-upgrades / `InhibitShutdown`), albo
   aktualizacje tylko w oknie, gdy nic nie biegnie — zdecyduj, zapisz powód, zmierz czas od sygnału do commit+push.
12. (2026-10-23 19:4x UTC, **R96 — cel właściciela: przepięcie w ok. 20 s, „zrzut RAM-u na SSD + power down”**; uzupełnia
   wpis 9) Zmierz, nie zgaduj. (a) Zmierz na maszynie: używany RAM w typowej pracy (`free -m` podczas builda i agenta),
   typ i rozmiar dysku rozruchowego (`gcloud compute disks describe`) oraz realną przepustowość zapisu (`fio` albo `dd`
   z `oflag=direct`, 2 GB); policz czas zrzutu = używany RAM / przepustowość i podaj, jaki dysk (pd-ssd o jakim
   rozmiarze albo hyperdisk z provisioningiem) dawałby 20 s — to pieniądze, na które zgodził się w R94 (dysk), więc
   dolicz cenę z datą. (b) Sprawdź w dokumentacji GCE (link + data): czy natywny `suspend` jest dostępny dla Spot VM
   i czy wywołany z wnętrza w oknie uprzedzenia wyprzedza zatrzymanie przez Google; jeśli tak — to najczystsza droga.
   (c) Hibernacja Linuksa jako plan B: swap ≥ używany RAM na dysku trwałym, `resume=` w parametrach jądra, Secure Boot
   (czy włączony), obraz Ubuntu 26.04 na GCE — czy `systemctl hibernate` działa i czy po `start` maszyna wraca z obrazu
   z żywymi procesami (tmux, build, sesja Claude). (d) Test całego przepływu prawdziwym wywłaszczeniem:
   `gcloud compute instances simulate-maintenance-event devbox-20260923 --zone europe-west4-c` (na Spot wywołuje
   preemption) — najpierw z commit+push WIP, w porze bez biegnącej pracy; zmierz: sygnał → zrzut gotowy, stop → start
   (stróż), start → usługi i sesja z powrotem; podaj trzy liczby i co wracało żywe. (e) Stróż zdarzeniowy zamiast
   odpytywania co minutę: wpis `compute.instances.preempted` w Cloud Logging → sink → Pub/Sub → Cloud Function/Run
   `start` — policz, ile sekund to zdejmuje. Wynik jako sekcja w VM_RECON i linia do S1; wdrożenie tego, co bez kosztu.
13. (2026-10-23 20:05 UTC, zasada gałęzi) Zmiany w KODZIE (`loom/`, `tools/` używane przez bramkę, CI) nie na gałąź
   dokumentów `claude/chataddhd-cpp-loom-core-IRGRN` — ona nigdy nie idzie na `main`. Kod na gałąź roboczą
   `claude/m1-w1` (worktree jądra), przez bramkę `tools/gate.sh` i FF na `main` (opis w `TELEPORT_S1.md` §7). Twoją
   poprawkę g++15 w `candidate_graph.cpp` (`1a7df48b`) S1 przeniósł na `claude/m1-w1` sam; `tools/cloud/login_all.sh`
   i `tools/gcp/` mogą zostać na gałęzi dokumentów (narzędzia maszyny, nie jądra) — ale gdy chcesz je mieć na main,
   ta sama droga. Spot-guard: zostaje NIE wdrożony do jego tak/nie o koszt Standard (wpis 9).
