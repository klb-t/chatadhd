# G1 i prezentacja importu PNG — odbiór integratora, 2026-10-31

## Przyjęte commity i zakres

Baza integratora: `6c46bd286dd9063e4c1c3f7f459e2d9eab1eef23`.
Zachowano P0/M1/M2/DOCX oraz package/GUI `daf4f484`/`3319312f` jako przodków.
Dwa odrębne lokalne commity `[skip ci]`:

| Etap | Przyjęty, testowany code_head | Delta SHA256 |
|---|---|---|
| Backend G1, 14 plików | `42e5c5be77448bb1c228573672fcc164d05bf1da` | `0ba74c5a3f767cb0b64137d87c0689cb49aedbe7ec18b9db0161c7650881dd07` |
| Komunikat/tytuł PNG, 6 plików | `5d653f73e25033881242bad3f72a6a45e0473d97` | `15a5e71ea8f3af3d57bd6ed12d2adaaec0f00e8c1a7e2708b9ddc68db2fb57e9` |

19/19 różnych plików kodu/testów/generatów zgodnych z końcowym code_head.
Wyniki G1 dotyczą pierwszego SHA; drugi zmienia importer/prezentację i runtime
chat_ops, zachowując kod i dane G1. Nie przypisujemy późniejszemu SHA nowej
pełnej bramki G1. Końcowy commit dokumentacji wskazuje tę samą bazę produktu.

**Implemented/tested:** jawne akcje backendowe G1 oraz dwie poprawki PNG.
**GUI G1 (automatyczny upload/materializacja/wybór fragmentu): niewykonane.**
GUI PNG: 2/2 opisanych scenariuszy przyjęte. Deployed: **NIE**.

## Pochodzenie i stabilność odbioru

Task `chatadhd-graph-source-parts-20261101`; worktree autora
`/var/lib/codex-team/worktrees/chatadhd-graph-source-parts-d0af04a-20261101`;
base/HEAD `d0af04a2f5a24205618ec827bec02d0765887af9`.
Completed snapshot `1793485310488154795`, archive attestation
`a5e7a7ae5038fb49415bdd92f95bc858285b7de0950f2f52e5681ab121db1f09`.
Archiwum root-only nie było odczytywane.

Poprzedni [bloker EACCES](G1_ADMISSION_BLOCKER_2026-10-31.md) jest historią.
Po naprawie dostępu przez administratora zwykły odczyt w sandboxie UID990
potwierdził **20/20 plików** i świeży HEAD, status-z, diff oraz listę untracked.
Użyto jawnych wykluczeń docs/exports i eval/real-holdout-key, bez czytania ich
zawartości. Status i diff były dokładnie zgodne z audytem właściciela plików:

- status-z SHA256 `7fefc011722963e43f0a9fd72e0df00afe33db0d49cf02b5344fe014920f9e34`;
- tracked diff SHA256 `ebb2bccd7cdcd5bdc776a0380e6c7551cfd464c77899a1a5db209f3296aaf902`;
- wybrany patch SHA256 `4ad8513bc944a2f66c343693421ac995b6ac816e98a5375ebf01b4aa63b7a187`.

Przeczytano pełną treść nowych źródeł/testów i raport autora. Odczyt hashy,
statusu/diffu i manifestów powtórzono przed zastosowaniem: bez zmian.
Patche tracked-core/shared-hooks/new-files są transportem tego samego pakietu;
nałożono tylko raz wybraną deltę po `git apply --check`. Pominięto historyczny
raport autora oraz oba generaty. Scalono source .pack i minimalne hooki;
text_pack.inc i runtime_profiles_embedded.inc odtworzono własnymi generatorami.
11 niegenerowanych plików pakietowania DOCX pozostało bajtowo zgodnych,
w tym nowe defaulty `package:docx_basic`. Nie kopiowano checkpointu, handoffu,
logów, .git ani danych administratora do produktu. Cudzy worktree bez zapisów.

## Co rzeczywiście robi G1

Istniejący upload zapisuje oryginalny blob. Dopiero jawne `client.blob_text`
(`POST /api/client/blob/text`) materializuje SourceRecord, Unit, genezę
wersji i wystąpienia przez istniejące Segmenter/genesis/nodes/links/provenance.
`client.blob_text_read` (`POST /api/client/blob/text/read`) odczytuje wybrane
wystąpienie względem oryginału. Istniejący unified context używa seed IDs,
przejścia grafu, budżetu i renderera do podania jego treści modelowi.
Nie dodano nowego eksportu C ABI ani migracji schematu DB.

Tożsamość źródła to jawny source_key oraz hash oryginalnych bajtów wersji.
Ten sam tekst w innym źródle lub miejscu ma odrębne wystąpienie. Fingerprint
tekstu służy współdzielonej reprezentacji, nie identyfikacji źródła.
Poprzednik wersji jest jawny i musi należeć do tego samego source_key.
Powrót do identycznych bajtów nie tworzy nowego zdarzenia edycji; nie ma tu
pełnego dziennika edycji ani dopasowania wszystkich fragmentów między wersjami.

Odebrany zakres: surowy UTF-8, TXT, Markdown i kod jako tekst. Zakresy
`[begin,end)` oznaczają bajty oryginalnego UTF-8, a nie znaki, strony czy piksele.
Testy potwierdzają odmowę ujemnych/ułamkowych/tekstowych współrzędnych oraz
obcego układu współrzędnych. Niepoprawny UTF-8 nie jest po cichu naprawiany.
Markdown przechowuje obserwacje istniejącego adaptera nagłówków/list/kodu;
nie jest to pełna hierarchia rozdziałów, AST ani parser tabel. Surowe bajty
tabel i kodu pozostają dostępne. Wynik `partial` i powód ograniczenia są jawne.
Nie przyjęto adaptera współrzędnych PDF/OCR/DOCX dla G1.

### Dwie uwagi review i własne poprawki

Historyczne `Segmenter::language()` przechowuje kod żądania także po fallbacku.
Bez zmiany istniejących tożsamości genezy dodano `effective_language()`;
nowy G1 buduje kanoniczny segmenter z tym językiem. Metoda, Unit i geneza G1
zapisują faktycznie użyte reguły. Testy sprawdzają błędny kod → `*`, pl-PL → pl,
idempotencję wobec jawnego fallbacku oraz zachowanie starego `language()`.
To nie jest automatyczna migracja dawnych grafów.

Cache projekcji jest zachowanym receipt idempotencji, **nie audytem ani
naprawą całego podgrafu**. Test uszkadza treść wystąpienia: ponowna materializacja
zwraca receipt, niczego nie naprawia, a odczyt i walidacja wystąpienia odmawiają;
oryginał pozostaje zgodny. `read` sprawdza blob/hash/zakres/dokładną treść.
Unified context sprawdza metadane i treść DB wybranego wystąpienia; nie ponawia
odczytu/hashowania bloba i nie certyfikuje integralności całego grafu.

## Własne wykonane testy

| Zestaw | Wynik |
|---|---|
| G1 native | **11/11**, 76 asercji (8 autora + fallback/cache/współrzędne) |
| Segmentacja | **14/14**, 1 113 603 asercje; w tym historyczna i efektywna tożsamość języka |
| Geneza tekstu | **7/7**, 37 840 asercji |
| Chat flow | **8/8**, 93 asercje |
| Unified context | **14/14**, 142 asercje |
| G1 HTTP | **3/3**, 18,594 s (2 autora + autoryzacja/CSRF/izolacja sesji) |
| Regresja HTTP dialog/tekst | **2/2 + 3/3**; rzeczywiste żądania loopback, reopen i błędy |
| ABI na G1 | **3/3**, 2,107 s |
| PNG import truth/legacy JSON | **9/9**, 6906 asercji, 3,35 s |
| PNG GUI | **2/2**, nowy backend i zachowany własny webdist, zero pageErrors |

CTest wyłącznie `-j1`: native 5/5 zestawów (54 przypadki), 68,12 s;
HTTP 3/3 zestawów (8 przypadków), 69,80 s. Pozostałe przypadki doctest były
wykluczone filtrem źródła, nie wykonane ani doliczone do PASS. Testy PNG
stanowią osobny etap po commicie G1; nie sumowano powtórek.

HTTP sprawdza prawdziwy graf/relacje, odrębne wystąpienia identycznego tekstu,
oryginały i zakresy, nową wersję bez utraty starej, reopen/idempotencję oraz
wybrany fragment w faktycznym systemowym kontekście żądania do loopback
providera, bez załącznika ani treści źródła w pytaniu. Niewybrany wiersz jest
nieobecny. Nowe trasy przechodzą przez istniejące account middleware i rctx:
bez sesji 401, obcy Origin 403, druga poprawna sesja nie widzi obcego
bloba/wystąpienia (404). Materializacja sama nie wywołuje dostawcy.

Dane są syntetyczne: polskie TXT/Markdown/kod o znanej treści, lokalny
ScriptedTransport lub RecordingProvider. To testy mechanizmu i treści żądania,
nie jakości rzeczywistego modelu. Żadnych płatnych lub rzeczywistych API.

Własny przyrostowy build Debug/WERROR `-j2` w zachowanym
`.integration-p0-20261031/build`. Szeroki target loom_tests przerwano, zachowując
obiekty; dokończono wyłącznie zakresowe obiekty i własne linkowanie.
Pierwsze kompilacje testu autora wykryły dwa porównania string/JSON;
dodano jawne get<string>(). Własna pętla otrzymała const-reference zgodnie
z Werror. Bez osłabienia asercji. Logi negatywne zachowane. Ninja zgłaszał
odzyskanie końca logu po przerwaniu; końcowe buildy i linkowanie zakończone 0.

Odtwarzalne polecenia i parametry zapisano w lokalnych `results.json`,
`png-results.json`, `run_gates.py`, `run_png.py` oraz plikach CTest.
Build używał targetów loom-server/loom i obiektów test_text_source_parts,
test_text_fragments, test_text_genesis, test_chat_flow, a w etapie PNG
obiektu test_import_truth. Unified verify skompilowano flagami własnego
compile_commands.json. `gen_text_pack.py --check`,
`gen_runtime_profiles.py --check` oraz `git diff --check`: PASS.

## Osobny odbiór poprawki PNG

Wcześniejszy [patch prezentacji](IMPORT_PRESENTATION_FOLLOWUP_2026-10-31.patch),
SHA256 `4b2852dbe889c47853500edd958a9d1d375f61ce8acbcdfcb1f86d1d0cabcc72`,
został teraz sprawdzony, nałożony raz i przetestowany. Historyczny plik patcha
pozostaje zachowany; **nie stosować go ponownie**.

ImportPanel pokazuje „Import zakończony. Rozmowy: 1. Wiadomości: 1.
Pliki źródłowe: 1.”. Nowa rozmowa ma tytuł `[Import] synthetic-pl.png`.
Nie ma fałszywego komunikatu JSON bez ZIP. Licznik opiera się na zachowanym
source_id/blob_hash, nie na fikcyjnej liczbie załączników. Nazwa jest display_name
pliku, a nie technicznym req_…; nie zmieniono ścieżki odczytu bajtów.
Historyczne zapisane tytuły nie są migrowane. Regresja zachowuje właściwy
komunikat o braku załączników dla zwykłego JSON i pozostałych importów.

W GUI użyto rzeczywistych ImportPanel/ChatView, istniejącego własnego
Chromium/Playwright i lokalnego Tesseract. Scenariusze 1,2 sprawdzają również
polski tekst OCR w kontekście i odpowiedź wyprowadzoną przez atrapę z odebranej
treści. PNG jest wcześniejszą syntetyczną fixture:
`05bb01e18f58fefb8fc984d14771d7c831268684ef6279089bdec0572d54bde1`.
16 plików istniejącego własnego webdist zweryfikowano hashami; źródła web
produktu nie zmieniły się, więc nie wykonywano zbędnego web buildu.
Nie ponawiano package5/GUI7/component4 ani całych M1/M2/DOCX/PDF/BYOK.

## Dowody, ograniczenia i next

Lokalne ignorowane dowody: `.integration-g1-resume-20261031/`.
`accepted-evidence.json` SHA256
`77cbe51ca2cd278702939edddf1f80dbe80da842de6ac4051a2f2f0b98e91462`;
G1 `g1-accepted.json` SHA256
`8174106c8794a23d06617d127439b1931bc130ce4ec2740c908b2f0f4c1664fa`;
PNG GUI receipt SHA256
`c4b31e418f973772e953c2ce8c73923c8192eff907dbd134b1d37e7a747bd9b4`.
Manifesty wiążą źródła z commitami, logami i binariami obu etapów.
Końcowy serwer SHA256 `20914a56f8babff5f2be395683b142b07c16fc84841d7915bd20f4a0fd3b34b3`,
biblioteka `f2d35ad312a29aaea54b7a9d0c4658f36fd4f3f93333b05941c206230bc32bac`.
Dowody surowe/buildy/screenshoty/checkpoint pozostają lokalne i niecommitowane.

Odczytano wszystkie cztery absolutne dokumenty context, w tym §0 rewizja2;
wcześniejszy brak dotyczył wyłącznie błędnej ścieżki względnej. Kanon rozdziela
dane/metody/inwarianty oraz źródła/obserwacje/inferencje. Hierarchia logowania
z jednym wyłącznikiem przy deploy nadal nieudowodniona; bez pobocznego refaktoru.
Szeroka struktura każdego obiektu, kontrolka → binding → setting, pełna
hierarchia kodu, wieloperspektywiczny graf i model ciągły pozostają wymaganiami,
nie wynikami tego przyrostu. Nie wdrożono hiperkrawędzi ani nowych rendererów.

**Next:** trwałe source_key po uploadzie, jawna materializacja i wybór
wystąpienia do kontekstu w GUI; osobny mały odbiór rzeczywistego przepływu.
Nie ma aktualnego blokera odebranego backendu G1 ani dwóch poprawek PNG.
Zachowane wcześniejsze luki role labels/raw503 i szersza polonizacja nie są
przez to zamknięte. Viewport 420×900 nie dowodzi fizycznego mobile.
Brak pełnego repo gate, Release/sanitizer, rzeczywistego modelu, deploy/rollback.
Domyślny DOCX nadal wymaga jawnego systemowego Pythona zgodnie z poprzednim
odbiorem; ten przyrost nie instaluje runtime na hoście.

Odwracalność: odrębne małe commity, brak migracji DB; ewentualne wycofanie hooków
nie usuwa oryginałów ani istniejących rekordów grafu. Nie wykonywano rollbacku.
Następny cykl uruchamia wyłącznie administrator. Bez push/CI/deploy i zmian hosta.
