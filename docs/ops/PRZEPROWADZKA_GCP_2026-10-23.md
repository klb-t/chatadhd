# Przeprowadzka koordynatora na GCP: przygotowanie (2026-10-23)

## Jedna komenda (R71)

Na świeżej VM (Ubuntu 24.04), przez ssh, jako zwykły użytkownik z sudo:

```
curl -fsSL https://raw.githubusercontent.com/klb-t/chatadhd/claude/chataddhd-cpp-loom-core-IRGRN/tools/vm_oneliner.sh | bash -s -- --android --emulator --bashrc
```

Skrypt `tools/vm_oneliner.sh` stawia pakiety, klonuje repo do `~/chatadhd`, uruchamia `tools/bootstrap_dev_vm.sh`
z podanymi flagami, instaluje Claude Code i wypisuje trzy kroki, które wymagają człowieka: `claude auth login`
(kod z przeglądarki), `gh auth login --web` (pushe), `claude remote-control` w tmux (pytania Enable/Trust). Po scaleniu
gałęzi do `main` adres skraca się do `.../klb-t/chatadhd/main/tools/vm_oneliner.sh`. Oba skrypty są nieprzetestowane na
prawdziwej VM (2026-10-23) — pierwszy raz z `--dry-run`.


Pakiet OPS1. To jest **przygotowanie**: nic nie zostało uruchomione na GCP (ta sesja nie ma żadnych
poświadczeń GCP), skrypt `tools/bootstrap_dev_vm.sh` nie był uruchomiony na prawdziwej VM, żadnej strony
GCP nie czytałem. Ty uruchamiasz polecenia jutro; ten dokument mówi które, w jakiej kolejności i co jest
niesprawdzone.

Twoje słowa (R68, `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md:773`):

> właśnie a ja zapomniałem że miałem cię przenieść bezpośrednio na gcp tam byś sobie kodził i od razu deployował. trzeba będzie, nie? Przygotuj się może na przeprowadzkę? na jutro?

**Jak czytać liczby.** Każdy rozmiar, kształt maszyny, cena i nazwa typu poniżej jest **presetem albo
przykładem**, nie wymogiem. Znaczniki: **[zmierzone]** (z tej maszyny / z repozytorium, 2026-10-23),
**[z pamięci, niezweryfikowane]**, **[do sprawdzenia]**. Nic tu nie wybiera za Ciebie: kształty z §1 są opcjami.

**Krótko na „trzeba będzie, nie?”.** Oba restarty kontenera z 23.10 miały przyczynę w **dysku** (build dev
4,4–6,4 GB × dwa + ccache dobijały do ~95 %), nie w braku GCP: `CLAUDE.md`, „Correction after a second
restart”. Można je też ograniczyć tutaj (jeden katalog buildu naraz; otwarta poprawka: `-g1` albo `-gz` w
presecie `dev`). GCP daje trzy rzeczy, których tu nie ma: dużo miejsca na dysku, maszynę, która stoi między
sesjami, i możliwość wdrażania od razu. Czy ich chcesz i za jaką cenę, to §1–§4.

---

## 1. Trzy kształty „GCP” (opcje, żadnej nie wybieram za Ciebie)

Punkt odniesienia **0: zostajemy tu.** Zero kosztów GCP; restarty znikają głównie dzięki regule „jeden build
naraz”; bramka dalej 40–90 min na 4 rdzeniach **[zmierzone]**; brak wdrażania od razu.

### (A) VM w GCP jako stała maszyna deweloperska; Claude Code działa na niej
- Claude Code działa na VM, a sesja jest widoczna w aplikacji Claude przez Remote Control (polecenie
  `claude remote-control`). Dokumentacja: https://code.claude.com/docs/en/claude-code-on-the-web oraz strona o
  Remote Control w dokumentacji Claude Code. **[do sprawdzenia]**: nazwy poleceń i wymagania logowania na
  serwerze bez przeglądarki (sprawdź `claude --help` na VM).
- Daje: duży dysk (§2), sesja nie ginie z restartem kontenera (VM stoi; proces w `tmux`), kod, build i bramka
  w jednym miejscu.
- Nie daje: nie zwiększa limitów Claude (te same okna 5-godzinne na koncie; `CLAUDE.md`, „Working with
  subagents”); nie wdraża sama; nie przyspiesza testów, które trwają długo same w sobie (§2).
- Niewiadoma: czy narzędzia, których dziś używa koordynator w sesji chmurowej (narzędzie `Workflow`, zadania
  zaplanowane, połączenie z GitHubem), istnieją w sesji na VM. **[do sprawdzenia]** w próbie (§5, §6).
- Koszt: VM + dysk (§3). Dane: tylko kod (publiczny) i logowania (§4).

### (B) ta sama VM obsługuje też serwer Loom + web za Caddy („koduję i od razu deployuję”)
- `deploy/install.sh` już istnieje i jest opisany w `deploy/README.md`: `git pull && sudo deploy/install.sh
  --domain …`, nowa wersja ląduje w `/opt/chatadhd/<wersja>`, powrót: `--rollback <wersja>`. Telefon otwiera
  `https://twoja.domena` w przeglądarce bez przebudowy APK (README, krok 5); aplikacja Android też umie połączyć
  się z kontem na serwerze.
- Kompromisy: **rozmowy leżą wtedy na dysku VM** (pytanie 2 w §4); VM musi działać, kiedy używasz telefonu
  (zatrzymana VM = serwer niedostępny); build/bramka (4 rdzenie obciążone przez godzinę) i serwer, z którego
  korzystasz, dzielą jedną maszynę. Wariant B′ (mała osobna VM tylko pod serwer) usuwa ostatnie dwa kłopoty
  kosztem drugiej maszyny; to Twój wybór, nie mój.
- Ekspozycja: porty 80 i 443 do internetu, reszta zamknięta; rejestracja domyślnie `closed`; serwer słucha
  tylko na `127.0.0.1` (README).
- Uwaga praktyczna: `install.sh` bez `--no-deps` doinstaluje `nodejs npm` z archiwum Ubuntu (starszy Node
  niż w CI **[z pamięci, niezweryfikowane]**). Po bootstrapie użyj `--no-deps` (toolchain już jest) i dopisz
  `--caddy` do bootstrapu albo zainstaluj `caddy` z apt.

### (C) własny runner GitHub Actions na VM (szybsze CI, własne rdzenie)
- Dziś `loom.yml` biegnie na runnerach GitHub: job `asan` ~2 godziny (zimny build ~25 min + 8 min + ctest ~80
  min CPU, limit 150 min) **[zmierzone, komentarz w `loom.yml`]**; minuty Actions płacisz Ty (komentarz w
  `android-smoke.yml`). Własny runner przenosi ten koszt na GCP i daje stały ccache.
- **Bezpieczeństwo:** repozytorium jest publiczne, a `loom.yml` ma wyzwalacz `pull_request` (**[zmierzone]**).
  Runner na własnej VM wykonywałby kod z cudzych pull requestów. GitHub odradza własne runnery dla publicznych
  repozytoriów (**[z pamięci, niezweryfikowane]**; sprawdź stronę GitHub o self-hosted runners). Środki, jeśli
  wybierzesz C: runner na osobnej VM bez Twoich danych, zatwierdzanie przebiegów od osób z zewnątrz, workflow
  tylko ręczne.
- Emulator Androida wymaga wirtualizacji zagnieżdżonej (`/dev/kvm` na VM). **[do sprawdzenia]**: czy typ
  maszyny i region ją obsługują oraz jak ją włączyć (szukaj w dokumentacji GCP hasła „nested virtualization”;
  nazwy opcji nie podaję z pamięci). Skrypt z `--emulator` sprawdza po fakcie, czy `/dev/kvm` istnieje.
- `android-smoke.yml` ma dziś `runs-on: ubuntu-24.04`, krok KVM z `sudo` i `ANDROID_HOME` z obrazu runnera;
  na własnym runnerze potrzebna byłaby zmiana etykiety `runs-on`, SDK (bootstrap z `--android`) i KVM. Nic z
  tego nie sprawdzałem.

Kształty można łączyć (A, A+B, A+C, A+B+C, a B′ osobno). Domyślnie, dopóki nie powiesz inaczej, nic się nie
zmienia.

---

## 2. Maszyna: presety i uzasadnienie

**Zmierzone dziś:**
- katalog buildu `dev`: 4,4–6,4 GB (dziś `/home/user/wt/m1d1/loom/build`: 6,4 GB)
- ccache `/home/user/.ccache`: 3,8 GB (limit w `tools/gate.sh`: 12 GB; w CI: 3 GB)
- `.git`: 327 MB; `loom/web/node_modules`: 147 MB
- dzisiejsza maszyna: 4 rdzenie, 15 GB RAM; bramka 40–90 min (`tools/gate.sh`), ponowny build z ccache ~15 min
  (`CLAUDE.md`)
- najdłuższe pojedyncze testy w CI `asan`: `onboarding_store` 350 s, `kb_pack` 280 s, `cli.smoke` 495 s; więcej
  rdzeni **ich** nie skróci, skróci kompilację

**Niezmierzone (zmierz na VM):** rozmiary buildów `asan`, `vendored`, `libcxx`; Chromium Playwright;
Android SDK + NDK + obrazy emulatora (**[z pamięci, niezweryfikowane]**: kilkanaście GB); czas zimnego buildu `dev`.

**Preset dysku: 200 GB, typ `pd-balanced`.** Suma szacunkowa w GB: system i narzędzia 10–15; Android
SDK/NDK/emulator 15–25; do czterech buildów natywnych naraz 20–32 plus drugi worktree `dev` ~6; ccache 12;
repozytorium, worktree, `node_modules`, logi bramek, zrzuty ekranu ~10; dane serwera (B) kilka–kilkanaście;
razem ≈ 100–130, zapas ~50 % daje 200. Dlaczego nie mniej: dwa restarty 23.10 (pierwszy zabił obie bramki,
drugi agenta W2b-1) wynikały z braku miejsca, a na VM dysk jest najtańszą częścią rachunku (§3). Można zacząć
od mniej i powiększać (**[z pamięci, niezweryfikowane]**: dysk da się powiększyć bez wyłączania, zmniejszyć
nie), 200 GB to mój preset, nie wymóg. Typ: `pd-balanced` to środek między tanim wolnym a szybkim drogim
(**[z pamięci]**); zwykły HDD odradzam przy tylu małych zapisach kompilacji (rozumowanie, nie pomiar).

**Preset rdzeni i RAM: 8 vCPU / 32 GB** (przykładowa nazwa typu `e2-standard-8` **[z pamięci,
niezweryfikowane]**). Dlaczego: bramka buduje z `--parallel 4` i `ctest -j3` (wartości dobrane pod 4 rdzenie /
15 GB; 15 GB wystarczyło, restarty były z dysku), a do tego dochodzi Chromium z e2e, agent i opcjonalnie
emulator. 32 GB daje zapas na to wszystko naraz. Zmiana `--parallel`/`-j` w `gate.sh` na `nproc` to mały osobny
pakiet. Mniejszy: 4 vCPU / 16 GB (kształt, na którym działa to dziś, jedna bramka naraz). Większy: 16 vCPU /
64 GB, jeśli chcesz równoległe bramki; reguła „jedna bramka naraz” była o dysku, więc przy większym dysku
trzeba ją zmierzyć od nowa, nie zakładać.

System: Ubuntu 24.04 LTS (jak runner CI i `install.sh`), x86_64 (CI i emulator są x86_64; ARM nie sprawdzałem).
Region i strefa: Twoja decyzja (cena, opóźnienie z telefonu, gdzie leżą dane).

---

## 3. Koszty: orientacyjnie, do sprawdzenia w kalkulatorze GCP

Kalkulator: https://cloud.google.com/products/calculator. **Wszystkie ceny poniżej: z pamięci,
niezweryfikowane, mogą być nieaktualne i zależą od regionu.** Nie są budżetem.

Wzór: **miesięcznie ≈ godziny działania VM × cena VM za godzinę + GB dysku × cena dysku za GB-miesiąc**
(+ migawki, jeśli włączysz; + zarezerwowany stały adres IP; + ruch wychodzący, dla tej aplikacji zapewne mały,
niezmierzony).

Trzy przykładowe kształty (ceny z pamięci, niezweryfikowane; dysk `pd-balanced` ~0,10 USD/GB-miesiąc z pamięci):

| Kształt (przykład) | VM/h | 220 h/mies. (≈ 10 h × 22 dni) | 730 h/mies. (cały czas) | dysk/mies. |
|---|---|---|---|---|
| S: 4 vCPU / 16 GB, 150 GB | ~0,13 USD | ~29 USD | ~98 USD | ~15 USD |
| M: 8 vCPU / 32 GB, 200 GB (preset) | ~0,27 USD | ~59 USD | ~196 USD | ~20 USD |
| L: 16 vCPU / 64 GB, 300 GB | ~0,54 USD | ~118 USD | ~391 USD | ~30 USD |

Zasady:
1. **VM można zatrzymać, gdy nikt nie pracuje**: godziny VM przestają się naliczać, **dysk (i zarezerwowany
   adres IP) nadal** (z pamięci, do potwierdzenia). Przy B zatrzymana VM = serwer niedostępny.
2. Obniżanie kosztu to Twoje pokrętła, nie moje decyzje: mniejszy kształt, zatrzymywanie na noc, maszyna typu
   Spot (z pamięci tańsza, ale GCP może ją zatrzymać w każdej chwili; do sesji koordynatora wymagałaby
   wznawiania; ani nie zalecam, ani nie odradzam).
3. Limit wydatków: w GCP są budżety z alertami (z pamięci: alert **powiadamia**, sam nie wyłącza usług; do
   potwierdzenia na stronie GCP). Dlatego jest pytanie 1 w §4.
4. VM nie zmienia kosztu ani limitów Claude.

---

## 4. Prywatność: co dokąd jedzie

- **Twój kod:** publiczny GitHub (`klb-t/chatadhd`), więc A/B/C niczego nowego nie ujawniają; na VM leży kopia.
- **Katalog danych (rozmowy, ustawienia, konta):** przy A i C **nie jedzie nigdzie**. Tylko przy **B** leży na
  dysku VM w `/var/lib/chatadhd` (README). Wg dokumentacji GCP dyski są domyślnie szyfrowane „at rest”
  (**do potwierdzenia**); szyfrowanie na poziomie aplikacji to osobna opcja (`CLAUDE.md` opisuje AES-256-GCM w
  wersji Python; w Loom tego w tym pakiecie nie sprawdzałem). Kto się loguje: Caddy + konta z
  `deploy/install.sh` (`--admin-login`, `chatadhd-user add|disable|…`), rejestracja domyślnie `closed`, hasło
  nie trafia do logów. Migawki dysku, jeśli je włączysz, zawierają te dane.
- **Klucze API (np. OpenRouter):** `secrets.json` (prawa 600) w katalogu danych, więc przy B leżą na VM. Które
  klucze (jeśli jakiekolwiek) mają jechać na VM do testów z prawdziwym modelem, ustalasz Ty; w tym pakiecie
  żadnego nie czytałem. Preset: na VM tylko klucze potrzebne do pracy, z limitem po stronie dostawcy.
- **Logowania na VM:** po zalogowaniu Claude Code i GitHuba na dysku VM leżą tokeny; kto ma root, SSH albo
  migawkę dysku, może ich użyć. Preset: SSH tylko kluczem (OS Login / IAP **[z pamięci, do sprawdzenia]**),
  nie otwarty dla świata; dostęp do GitHuba ograniczony do jednego repozytorium.
- **Ruch:** przy B publicznie tylko 80/443. **Co czyta Anthropic:** treść plików, które Claude czyta w sesji,
  trafia do Anthropic tak samo jak dziś; przeprowadzka tego nie zmienia.
- **Dane prywatne spoza gita** (m.in. ścieżki, których w tym pakiecie nie wolno było czytać) **zostają tam,
  gdzie są**, dopóki nie powiesz inaczej.
- **Nowy dostawca:** GCP (Google) staje się nowym miejscem, gdzie mogą leżeć Twoje dane. Dlatego pytanie 2.

**Dwa pytania tak/nie.** Zadaje je koordynator (nie ten dokument), dopiero gdy znana jest liczba z kalkulatora
i wybrany kształt:

1. **Pieniądze:** „Czy koszt GCP rzędu *[kwota z kalkulatora dla wybranego kształtu i liczby godzin]* USD
   miesięcznie mieści się w uzgodnionym budżecie? Tak / Nie.” (Przy „Nie” koordynator proponuje mniejszy
   kształt albo mniej godzin i pyta ponownie.)
2. **Dane:** „Czy Twoje rozmowy i ustawienia (katalog danych serwera) mogą leżeć na dysku maszyny w GCP?
   Tak / Nie.” (Dotyczy tylko B. Przy „Nie”: A i C bez B, albo B bez Twoich danych.)

---

## 5. Lista przeprowadzki (dla koordynatora)

Zasady: przeprowadzka **nie przerywa** żadnego pakietu, agenta ani bramki (agenty workflow nie wznawiają się;
`CLAUDE.md`); praca zostaje tutaj, aż VM przejdzie bramkę; **odwrót = kontynuacja tutaj**, bo źródłem prawdy
jest git.

### Co leży poza gitem dziś i jak jedzie
1. **Niewypchnięte gałęzie [zmierzone, 2026-10-23]:** `claude/m1-w1` (worktree `m1d1`) ma 1 commit przed
   `origin` (`715c2a92`; na `origin` jest `693a8d4f`); `claude/r35-android` (worktree `r35`) **nie ma gałęzi na
   `origin`** (sprawdzone `git ls-remote`; niescalone `c0d01690`, `9ac53bef`); `claude/m1-w0` i ta gałąź są zsynchronizowane. Wypchnąć **przed** przeprowadzką,
   sprawdzić ponownie tuż przed.
2. **Worktree `/home/user/wt/*`:** nie jadą; odtwarzane z gałęzi (`git worktree add`).
3. **Katalogi buildu `loom/build/*`:** nie jadą; zimny build na VM (czas nieznany, zmierzyć).
4. **ccache `/home/user/.ccache` (3,8 GB):** nie jedzie; ścieżki i wersje kompilatora mogą się różnić, zimny
   build jest bezpieczniejszy niż kopia niepewnego cache; ciepły po pierwszym buildzie.
5. **`node_modules`, Chromium Playwright:** `npm ci` + bootstrap.
6. **Briefy workflow:** od `a4d3d04a` są w repozytorium (`tools/workflows/briefs`). Zawierają ścieżki
   `/home/user/…` (21 plików w `tools/workflows`) i `tools/gate.sh` ma na sztywno `/home/user`, więc **decyzja D1: VM
   odtwarza układ `/home/user/{chatadhd,wt,.ccache}`** (użytkownik `user`); bootstrap ostrzega, gdy `HOME` jest inny.
7. **Zadania zaplanowane (dziś trzy):** „Przegląd gałęzi GPT” (cykliczne, `6 */6 * * *`, związane z tą sesją);
   „CI check (main 94872898)” (jednorazowe, 08:55); „Read android-smoke G1/K2c-2” (jednorazowe, sesja S2).
   Jednorazowe wygasną same; cykliczne odtworzyć w nowej sesji. Koordynator zapisuje treść promptów w repo
   **przed** przełączeniem. Dwa z nich używają narzędzi GitHub MCP; w sesji na VM trzeba konektora albo `gh`
   (**[do sprawdzenia]**).
8. **Kontekst sesji:** nie jedzie. Nowa sesja startuje z `CLAUDE.md`, `docs/STATE.md`, głowy
   `docs/reports/QUEUE_S1.md`, `docs/reports/INDEX.md`, `python3 tools/spec.py` i `git log`. Przed
   przełączeniem koordynator zamienia głowę `QUEUE_S1.md` w kartę startową: co biegnie, co czeka, otwarte pytania.
9. **Uruchomione agenty i bramki:** przełączenie dopiero między pakietami, bez agenta i bez bramki w toku.
10. **Narzędzia sesji chmurowej** (`Workflow`, zadania zaplanowane, wiadomości między sesjami, GitHub MCP):
    nie wiem, które są w Claude Code na VM. Sprawdzić w próbie, **przed** przeniesieniem pracy.
11. **Poświadczenia:** logowanie do Claude, dostęp do GitHuba, klucze API: zakłada właściciel; nie są w repo.
12. **Sekrety GitHub Actions** (`ANDROID_KEYSTORE_*` itd.): zostają w GitHubie.
13. **Sesja S2:** R68 mówi o koordynatorze; S2 zostaje tak jak jest, dopóki nie powiesz inaczej (odwrót:
    ta sama procedura).

### Kroki właściciela (tylko to, co wymaga jego konta, pieniędzy albo zgody)
- **O1.** Odpowiedzi na dwa pytania z §4 (po zobaczeniu liczby z kalkulatora).
- **O2.** Projekt GCP z rozliczeniami; zalecany budżet z alertami (§3).
- **O3.** Utworzenie VM o kształcie z §2 (konsola: Compute Engine, utwórz instancję; szkic polecenia niżej).
- **O4.** Wkleić jedną komendę z §6 na VM; potem pierwsze logowania: Claude Code (interaktywnie) i GitHub (klucz
  lub token ograniczony do repo).
- **O5.** (Tylko B) domena z rekordem A na IP VM; porty 80 i 443.
- **O6.** Powiedzieć „przełączamy”, kiedy bramka na VM jest zielona.

### Kroki koordynatora
- **K1 (przed):** wypchnąć gałęzie (punkt 1); zapisać prompty zadań cyklicznych; karta startowa w
  `QUEUE_S1.md`; wpis w `INDEX.md`.
- **K2 (po VM):** klon, bootstrap (`--dry-run`, potem prawdziwy), pomiary: zimny i ciepły build, bramka (§6).
- **K3:** sprawdzić w sesji na VM narzędzia z punktów 7 i 10.
- **K4 (przełączenie):** worktree z gałęzi, odtworzone zadania cykliczne, `QUEUE_S1.md` zaktualizowane.
- **K5 (odwrót):** przy każdej niespodziance wracamy do pracy tutaj; nic nie zostało tu usunięte.

### Kolejność próby (dry run), od najtańszego
1. VM stoi → 2. `tools/bootstrap_dev_vm.sh --dry-run`, potem prawdziwy → 3. klon → 4. **bramka zielona na VM**
→ 5. jeden mały pakiet wykonany na VM → 6. **dopiero wtedy** przenosimy pracę. Do tego czasu nic w chmurze
Anthropic się nie zmienia.

---

## 6. Plan pierwszego dnia na VM

Szkic utworzenia VM **[z pamięci, niezweryfikowany; porównaj z `gcloud compute instances create --help`]**.
Wartości to presety z §2, region wybierasz sam:

```
gcloud config set project NAZWA_PROJEKTU
gcloud compute instances create chatadhd-dev \
  --zone=STREFA --machine-type=e2-standard-8 \
  --image-family=ubuntu-2404-lts-amd64 --image-project=ubuntu-os-cloud \
  --boot-disk-size=200GB --boot-disk-type=pd-balanced
```
Wirtualizacja zagnieżdżona (potrzebna tylko do emulatora w C): **[do sprawdzenia]** w dokumentacji GCP, nie
dopisuję opcji z pamięci. Użytkownik `user` (decyzja D1): **[do sprawdzenia]** jak go dostać na VM
(OS Login albo klucz w metadanych); zwykłe `sudo adduser user && sudo usermod -aG sudo user` też działa.

**Jedna komenda do wklejenia przez ssh** (Twoje słowa, R71: „chodzi o to żebyś mi dał jedną komendę do
wklejenia przez ssh, żebym znów nie musiał szukać tych nazw wszystkich”). Skrypt leży tymczasowo w repozytorium,
na tej gałęzi. Na świeżej VM, jako zwykły użytkownik z sudo:

```
curl -fsSL https://raw.githubusercontent.com/klb-t/chatadhd/claude/chataddhd-cpp-loom-core-IRGRN/tools/bootstrap_dev_vm.sh | bash -s -- --clone --claude --home-link
```
Co robi: pakiety apt jak w CI, Python 3.11, Node 22, ccache, klon repozytorium do `~/chatadhd`, `npm ci` +
Chromium, Claude Code (`--claude`), dowiązanie `/home/user` (`--home-link`, patrz D1). Czego **nie** robi:
nie loguje do Claude ani GitHuba, nie włącza Remote Control (to kroki interaktywne; skrypt wypisuje je na
końcu), nie buduje. **To uruchamia kod z internetu bez czytania**; ostrożniej:
`curl -fsSL <ten adres> -o bootstrap.sh && less bootstrap.sh && bash bootstrap.sh --dry-run --clone --claude --home-link`.
Dodaj `--android --emulator`, jeśli VM ma budować APK i sprawdzić KVM, `--caddy` dla B. Skrypt **nie był
przetestowany na prawdziwej VM** (tylko `bash -n`, `shellcheck` i `--dry-run`, także w trybie `curl | bash`).
Adres `raw.githubusercontent.com` działa bez logowania, bo repozytorium jest publiczne; po scaleniu skryptu do
`main` zamień w adresie nazwę gałęzi na `main` (i `CHATADHD_BRANCH=main` przed `bash`).

Kolejność (kroki 2–3 robi komenda powyżej):
1. Zaloguj się i zapisz `df -h`, `nproc`, `free -g` (porównanie z §2).
2. Klon: `git clone https://github.com/klb-t/chatadhd ~/chatadhd` (robi to `--clone`).
3. Bootstrap (komenda powyżej albo `tools/bootstrap_dev_vm.sh --dry-run`, potem bez `--dry-run`).
   Wyloguj się i zaloguj ponownie (grupy, PATH).
4. Zimny build: komenda wypisana na końcu skryptu, z `time`. Potem ciepły (po usunięciu `loom/build/dev`).
   **Ile minut: nieznane, zmierzyć i wpisać do `INDEX.md`.**
5. Bramka: `tools/gate.sh /home/user/chatadhd vm1` w `tmux`, limit 2 h. Porównaj z 40–90 min tutaj.
6. Claude Code na VM (zainstalowany przez `--claude`): `claude`, zaloguj się kontem claude.ai; potem
   `tmux new -s claude -c ~/chatadhd` i w środku `claude remote-control` (**[do sprawdzenia]** nazwa i
   wymagania); sprawdź, że sesja widnieje w aplikacji, i sprawdź narzędzia z §5, punkty 7 i 10.
7. (B, po odpowiedzi „Tak” na pytanie 2) `sudo deploy/install.sh --no-deps --domain … --admin-login …`.
8. (C, jeśli wybrany) rejestracja runnera wg strony GitHub o self-hosted runners, z zastrzeżeniami z §1.
9. **Pytanie S2: czy `android-smoke` może biec na tej VM?** Warunki: `/dev/kvm` (§1 C), SDK z `--android`,
   zmiana `runs-on` w workflow. Niezweryfikowane; domyślnie emulator zostaje w GitHub Actions. Decyzja po
   krokach 4–5.
10. Dopiero potem przenoszenie pracy wg §5.

---

## Decyzje koordynatora i odwrót (technicznie moje; zgodnie z `CLAUDE.md`, „Decisions”)

| # | Decyzja | Odwrót |
|---|---|---|
| D1 | VM odtwarza układ `/home/user/{chatadhd,wt,.ccache}`, bo `gate.sh` i briefy mają te ścieżki na sztywno; gdy użytkownik nazywa się inaczej, `--home-link` robi dowiązanie `/home/user` -> `$HOME` (niesprawdzone; ccache może trafiać gorzej) | osobny pakiet: ścieżki z `$HOME`/zmiennych w `gate.sh` i briefach; wtedy dowolny użytkownik i bez dowiązania |
| D2 | Preset 8 vCPU / 32 GB / 200 GB `pd-balanced` | każdy rozmiar da się zmienić (§2); to preset, nie wymóg |
| D3 | Python 3.11 z PPA `deadsnakes` w bootstrapie | `pyenv` albo `uv` ręcznie; skrypt nie pyta, ale ostrzega przed dodaniem PPA |
| D4 | Node 22 z oficjalnego archiwum nodejs.org z sumą SHA-256 (nie apt: Ubuntu ma starszy) | `CHATADHD_NODE_MAJOR` albo apt/`nvm` ręcznie |
| D5 | Skrypt ma `--dry-run`, bo nie mógł być przetestowany | opcja nic nie kosztuje; można nie używać |
| D6 | Dokument nie wybiera między A/B/C ani nie podaje cen jako faktów | wybierasz Ty |
| D7 | Jedna komenda (R71) pobiera skrypt z `raw.githubusercontent.com` z tej gałęzi, tymczasowo | po scaleniu: gałąź `main`; albo wersja dwukrokowa z `less` (podana wyżej) |

## Czego nie sprawdziłem (nic z tego nie jest faktem)
- Żadna strona GCP, GitHub ani Claude Code nie była czytana; ceny, nazwy typów maszyn i obrazu, zachowanie
  dysków i adresów IP, szyfrowanie, budżety, OS Login/IAP, wirtualizacja zagnieżdżona.
- Polecenia `claude remote-control`, logowanie na VM bez przeglądarki, dostępność `Workflow`, zadań
  zaplanowanych i GitHub MCP w sesji na VM.
- `tools/bootstrap_dev_vm.sh`: tylko `bash -n`, `shellcheck` i `--dry-run` (także `curl | bash`); PPA
  `deadsnakes` dla 24.04, adres `cmdline-tools` Androida (z pamięci), nazwa pakietu npm `@anthropic-ai/claude-code`
  (z pamięci), `--home-link`, pakiety apt na świeżej VM, `install-deps` Playwright, pobieranie z
  `raw.githubusercontent.com`.
- Czas zimnego i ciepłego buildu na VM, rozmiary buildów `asan`/`vendored`/`libcxx`, rozmiar SDK i obrazów
  emulatora, czas bramki na 8 rdzeniach.
- Czy `android-smoke.yml` działa na własnym runnerze.
