# Continuous graph UI — lokalny odbiór, 2026-11-01

Kod UI: **8866e1f2f5ed4fe1cb0f21b44b151c78b986f1d2**, baza integratora
**4d9cd959485ef2aa104ce5197755837719db3fc8**. Implementacja i zakresowy odbiór
przeglądarkowy wykonane. Brak wdrożenia; operacyjny preflight, aktualna spójna kopia
z zaakceptowanym RPO i decyzja właściciela nadal osobno.

## Pochodzenie i zakres zmiany

Task `chatadhd-continuous-graph-ui-20261101`, donor
`/var/lib/codex-team/worktrees/chatadhd-continuous-graph-ui-5912012c-20261101`,
baza `5912012c5d700e3c3ed4f89d7fbb17bfdc4c9fc9`, snapshot
`1793523805897232157`, archive attestation
`5dcf7bca8c3cf0b86e2059ef2560bca716345c2f2ed4247949671ae38dd6be31`.
Archiwum prywatne nie było czytane. Nowy matching receipt i rzeczywisty odczyt
UID990 zamknęły wcześniejszy EACCES; [poprzedni raport](CONTINUOUS_GRAPH_UI_INTAKE_2026-11-01.md)
pozostaje historycznym wynikiem.

Odczytano checkpoint, pięć wskazanych dokumentów/provenance, pełny patch i nowe
pliki. Stabilne 26 hashy przekazania, zamknięte 10 plików produktu, HEAD/status
oraz jawny diff autora zgodne przed/po. Manifest SHA256
`d981036da54c76e33a21eda55db606cf7f657ab1684431f830a659ae4d376310`;
patch SHA256 `3404b71161b1e707e852edb02ef022cf1aba490f12f1189c10abc7768f10b5b7`.
Strict apply-check, jedno apply, własne 10/10 hashy zgodne. Nie kopiowano CP,
researchu, transportu ani administracyjnych danych do produktu.

Dziesięć plików UI autora przyjęto bez zmian semantycznych. Własny jeden harness
`loom/web/e2e/continuous-graph-native.mjs` korzysta z istniejącego lifecycle,
App/ImportPanel/ChatView/GraphNavigator i zainstalowanego Playwright. Bez nowych
zależności, API backendu i renderera. `NavigatorMap3D` używa istniejącego
`ViewScene3D`/Three/`projectSpatial`; ViewEnginePanel nadal korzysta z tego samego
silnika. Presety i polskie słowa są w danych, sprawdzenia numeryczne i zachowanie
ID w kodzie — kanon §0 rewizja 2.

## Wykonany wspólny przepływ

`npm --prefix loom/web run build -- --outDir ../../.integration-continuous-graph-ui-20261101/web-dist`
wykonał strict TypeScript i jeden Vite build (23,51 s fazy Vite). Ostrzeżenie o
rozmiarze chunków zachowane; brak obniżenia typecheck. Nie ponawiano sanity autora.

Własny browser: `python3 .integration-continuous-graph-ui-20261101/run-browser.py -accepted`
wywołał `node e2e/continuous-graph-native.mjs` z CWD `loom/web`. **Jeden zbiorczy
flow PASS, exit 0**, bez page errors. Własny Chromium/software WebGL, rzeczywisty
zaakceptowany serwer i biblioteka, jawne nowe syntetyczne data-dir/accounts-root,
static-dir, Landlock, env whitelist i wyłącznie loopback. Atrapa odpowiadała
wyłącznie na podstawie rzeczywiście otrzymanej treści systemowej.

| Sprawdzenie | Faktyczny wynik i granica |
|---|---|
| Plik i kontekst | Mały syntetyczny TXT z polskim zdaniem: upload, materializacja, read i globalny wybór wystąpienia. Dwa żądania przed/po pracy z widokiem otrzymały wybrany ID i tekst; widoczna i zapisana odpowiedź, ta sama rzeczywista rozmowa. Tekstu nie wklejano do pytania. |
| Wejście do grafu pliku | Ten sam oryginał przeszedł również rzeczywisty zwykły import pliku; Importy → natywny `text_file` → przycisk 3D. Adres `imp:…`, `same_as` natywnego węzła i rozmowa zachowane przy 2D↔3D/fallback. |
| Rzeczywiste Z | Jawne „Głębiej” pobrało głębokości 1 i 2. Osie ustawiono na istniejący wiek dowodu/głębokość/głębokość; world Z wynosiło −0,6 i −5,4. To rzeczywiste miary z backendu; nie obrót płaskiej mapy. Jednostki danych i skala prezentacji opisane w legendzie. |
| FOV i ruch | FOV 55→78° nie zmieniło odległości ani targetu; osobne dolly zmniejszyło odległość przy FOV 78°. Klawiaturowy orbit również nie zmienił FOV. |
| Fade i alfa | Dla niewybranego węzła uzyskano kolejne krycia 0,098 / 0,313 / 0,527. Bez dodatkowego graph.navigate i bez zmiany selektora/hops kontekstu. Wybrany węzeł zachował mapowanie alpha użytkownika 0,625. Reset i liczniki działają. |
| Profile / brak danych | Trzy presety układu, legendy/kanały i unplaced sprawdzone. Import nie dostarcza `order`, więc presety potrzebujące tej miary jawnie pokazują brak pozycji. Osie można zmienić na dostępne. Nie fabrykowano wartości. |
| Relacje | Skróty Kod (`dependencies`, `calls`), Historia (`version_history`), Cele (`teleology`) wykonały rzeczywiste zapytania katalogowe. W tym pliku brak tych relacji: 0 wierszy i jawny komunikat; wybór zachowany. Nie jest to dowód istnienia relacji celu/kodu. |
| Obsługa i fallback | 2D keyboard zoom/pan/reset, touch PointerEvent przesuwa rzeczywiste znaki, tap otwiera 3D. Kontrolowany `webglcontextlost` daje widoczne 2D/listę/przyczynę, bez utraty wyboru. Fizyczne urządzenie dotykowe i WebXR niebadane. |

Natywny adres pliku z importu i ID wystąpienia SourceParts są **odrębnymi
projekcjami tego samego oryginału**. Nie przepisano ich ani nie wymyślono wspólnego
ID. Graf potwierdzenia żądania pokazuje zbiorcze `unified_context`, nie osobną listę
wystąpień. Tego agregatu nie uznano za dowód wizualizacji wystąpienia.

Przed finalnym PASS zachowano nieudane próby harnessu: błędne założenie o
`observations` zwykłego tekstu (właściwy odczyt przez read), końcach linii w
cytacie, agregacie receipt, dokładnym selektorze etykiety/CSS oraz wrapperze rozmowy
zależnym od układu. Naprawiono test względem rzeczywistych kontraktów; końcowa
kontrola wymaga rzeczywistego `done.conv_id`, utrwalonej odpowiedzi i kontekstu.
Nie zmieniano produktu ani asercji na „opcjonalny PASS”. Jedna próba przerwana
przez uncaught rejection nie ma pełnego receipt; jej log/screenshot zachowano,
nie zaliczono jej jako zakończonej bramki. Finalny harness domyka cleanup.

## Nowy kandydat i tożsamość runtime

Archiwum lokalne:
`.integration-continuous-graph-ui-20261101/chatadhd-server-linux-x64-8866e1f2-release-continuous-graph-20261101.tar.gz`

- archive SHA256: `18d4a5b4fc5be7785e69fcfde4eebe1bcfbea04989c65cf5a312363afe962839`
- manifest SHA256: `012a99a26a0812e204fead96a6b3b6546cd9881b53a70291602309b6949a14bf`
- web index SHA256: `ac5b8d6c5b13d41b9bec9842557d8ea65ee6400658f6758ea19c7b2be0afe811`
- runtime source: `e27ce9ff0076c287963a588d942d68e7bebaca3f`
- library SHA256: `7004d54bd106e281acddd5d352e8ab0e2ced0d710245f8a17c003ccb336e610a`
- server SHA256: `cc0f65f19cfaf609706064ae068f09073016aba257ce80509d1ae5431eb8ac90`
- combined loom tree: `a3da5a89c9fb9c5a2cb926488cf58da38e4f7eb2`

106 payloadów: **90 runtime/helper payloadów identycznych z e27**, 14 aktualnych
web i 2 metadane. Poprzednie 108 obejmowało również stary web, nie 108 helperów.
Nie przebudowano C++, nie zmieniono ABI/profile/tool pins, nie relabelowano starego
pakietu `0caab73321bc7d9a565d1ec6f69e48be9108af6c4e885b2f36dfda2b4f62c63e`.
Backend i nowy web linkowano w świeżym staging wyłącznie jako niemutowane payloady;
nie klonowano cache/node_modules, stare pliki nie zostały zapisane przez linki.

Nowe archiwum: 106/106 hashy i bezpieczne ścieżki członków zgodne. Istniejący
`deploy/verify-package.sh` na NOWYM archiwum **PASS** (BusyBox, GNU tar nieponawiany):
rzeczywiste rozpakowanie, zależności dynamiczne, start, health, auth HTTP 200,
static. Landlock i nowe syntetyczne katalogi, cleanup zakończony. Nie jest to
ponowny test helperów/GUI/native. Browser używał dokładnie tych samych binariów
i bajtów web; ich tożsamość sprawdzono podczas pakowania. Przed rozpakowaniem
free 1237504000 B, regular payload 99944734 B, margines ponad próg 1 GiB
63817442 B (dodatkowa rezerwa 8 MiB). Po teście 1237417984 B. Bez kasacji starych
artefaktów, pełnych kopii cache ani zmian VM.

## Obowiązkowa bramka projekcji — historyczne dokładne fazy

Web-only zmiana nie uruchamiała ponownie odebranego backendu. Zachowana tożsamość
90 payloadów pozwala powiązać ten kandydat z wcześniejszym zakresem dowodu; nie
zamienia go w nowe wykonanie pełnej bramki na lib7004.

| Wykonana faza | SHA256 faktycznie testowanej lib |
|---|---|
| PDF check/reproject, odmowa starego przypięcia, mapowanie | `b7329f37ccd959b7cb770fd1c0f873544e775f66c3bb48f12bd01992a4fa0f5c` |
| Reopen PDF; późniejsza faza bundle/shared sentinels | `c41b3e364a8b90e297946015b051d3c1291f35382a7d7f7d5af825a9d867cf0e` |
| Puste check → `not_exercised` | `029e18c21b8460ac72f9ec2072c10b6d450ee770242cbaceda5b5432991e5a01` |
| Finalny parent/cache zakres mediów | `7004d54bd106e281acddd5d352e8ab0e2ced0d710245f8a17c003ccb336e610a` |

Intermediate b732/029 pochodziły z eae+dirty, bez osobnego immutable commita i
pakietu tych faz. Dokładne fazy zachowuje
`.integration-projection-media-batch-20261101/runtime-binding.json`; pełnego
old→new PDF retestu finalnej lib7004 nie wykonano i nie deklarujemy go.
Stary pakiet f9767a0c…754fb / lib
`0c2052d8bd31214876a5690f05394db0e4f4ca862c7d882dfe7e6f6f2202e4c5`
odmówił nowej wybranej projekcji i odczytał zachowaną starą. Spójna zimna kopia
sprzed zmiany została rzeczywiście odtworzona i uruchomiona. **RPO: utrata wszystkich
zmian po kopii; nie lossless rollback in-place.** Reproject zachowuje stare dane i
proponuje mapowanie; selektor zmienia się tylko jawnie. ZIP tree previous_source_id
pozostaje unsupported. Dawny freeze ed139231/ee2117e8 nie zawierał nowego gate.

## Granice i przekazanie

NOT RUN: nowa pełna bramka repo/native/P0/accounts/backup, fizyczny mobile/WebXR,
realvision i realny model/API, produkcyjne backup/restore/cutover, deploy. Nie ma
potwierdzonej skonfigurowanej/autoryzowanej ścieżki realvision z pokryciem kosztowym.
Nie czytano kluczy ani prywatnych danych. Przykładowy graf jest mały, a widok działa
na już pobranym ograniczonym podgrafie; nie dowodzi dowolnych osi, semantycznego
LOD ani automatycznego ujawniania dalszych części podczas zoom.

Next: administrator odbiera konkretny kandydat/hash i historyczne phasebindings;
operacyjny preflight, świeża spójna kopia/RPO i decyzja właściciela przed aktywacją.
Nie wykonywać kolejnego cyklu integratora automatycznie. Deadline
2026-11-01T17:21:19.260820Z bez zmiany. Stare ETA 11–12 PL nie stanowi obietnicy.

Dowody prywatne: `.integration-continuous-graph-ui-20261101/admitted/`,
`tested-source-binding.json`, `web-build.log`, `browser-accepted/observations.json`,
screenshots, `release-package.json`, `verify-package-result.json`, `resume-manifest.json`/`resume-final-result.json`
odbioru. Poprzedni finished manifest/final-result/checkpoint pozostają historią.
