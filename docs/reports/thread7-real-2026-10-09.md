# Zadanie C — wątek 7, rzeczywiste dane i workflow

Gałąź `gpt/thread7-real-2026-10-09`, od main
`9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`. Zakres zapisu: narzędzia structure,
własne badania i ten raport. STATE/INDEX, produkcyjny runtime i UI niezmienione.

## Etap 1: odzyskanie i replay

Odzyskano bajty trzech prywatnych artefaktów: freeze źródeł, checkpoint promptów
i checkpoint wykonawcy. Freeze SHA-256:
`5f976bbf9cee15dfe232e329dc2c85ebc40cae981af7ceedc56fc8e08719a832`.
Checkpoint promptów: `45272501536ee49c448bc1f541e68b30a1ea5dc0d9a60720a6a3862633bdfdbe`.
Checkpoint kampanii: `fa3ae1497cf16af6d7db80b424b04d1c99e88208c061fb9c0cda23093684a5ec`.

Zweryfikowano 173/173 plików checkpointu i 146/146 plików zamrożonej
preparacji. Niezmieniony następca intake z `ffe6443e` weryfikuje freeze,
44 wiadomości i 47 węzłów trzech wybranych rozmów OpenAI. Preparacja odtworzyła
128 dokładnych requestów: 16 konfiguracji × 8 zależnych pytań. Zbiór nie jest
reprezentatywny, ślepy, niezależny ani próbką Anthropic. Reference gold jest
prowizoryczne, autorstwa asystenta; sześć nietekstowych części pozostaje
niezinterpretowanych. Bajty dostępnych źródeł i metadane są prywatne.

Odzyskany intake i preparacja: **37 + 43 testy PASS**. Odtworzono byte-for-byte
trzy historyczne grafy: etap 1, etapy 3/4 oraz 192 powtórki etykiet.
To replay zapisanych dowodów, nie nowe wykonanie modeli.

## Korekta rachunku kampanii

Raport main: 708 prób / 0,703661900 USD. Odzyskany późniejszy checkpoint:
**720 prób / 0,873216500 USD**. Istniejący `PrivateLedger` odtworzył i zweryfikował
wszystkie 720 skutecznych wierszy i ich zapisane dowody; archiwalne nierozstrzygnięte
rezerwacje: 0 USD. Dodatkowe 12 prób należą do stage5-old-stage3 (8) i
stage5-old-stage4 (4). Nie skasowano pierwotnych pending/uncertain rekordów:
ich append-only dowody dają zakończony stan efektywny.

Archiwalna pozostałość nieodnawialnego limitu 5 USD: **4,126783500 USD**.
Klucz nie występuje w środowisku ani odzyskanym checkpointcie; checkpoint
jawnie `credential_included=false`. Szyfrowane materiały przekazania bez
działającej referencji do klucza nie dają dostępu. Aktualna tożsamość klucza,
usage i rezerwacje dostawcy są **unknown**, nie 0. Nie użyto salda konta,
nie odnowiono limitu. Nowe płatne wywołania/koszt: **0 / 0 USD**.

Dowody: `docs/research/thread7_real_2026-10-09/source-receipt.json`,
`historical-replay.json`, `campaign-replay.json`, pinned upstream/import.json.
Pełne source/request/response/ledger pozostają prywatne.

## Etap 2: działający workflow badawczy

`experiment_workflow_v1.py` implementuje listy, leniwą wielowymiarową macierz,
renderowanie przez typed data references, kolejkę SQLite, opt-in, częstotliwość,
deduplikację triggerów, odrzucenie eventów eksperymentu, nieponawianie pending,
pierwszy niezmienny rekord odpowiedzi i jawne null w instrumentacji. Nie wysyła
requestów. Istniejący payer nadal odpowiada za wszystkie pieniądze i raw.
Kod nie wykonuje adopcji ustawień — polityka jest wejściem dla B.

Na rzeczywistych źródłach przygotowano alternatywy 3 / 5 / 16 konfiguracji:
**24 / 40 / 128 requestów**. Pełne 16 to rzeczywista leniwa macierz
strategia × temperatura × max_tokens × preferencja. Każdy body jest zgodny
byte-for-byte z odzyskanym panelem 7B. Nowe spec/queue/payer manifesty,
kolejność i freeze mają osobne hashe i snapshoty producentów. Nie uruchamiać
wszystkich alternatyw automatycznie; pierwszy mały panel jest proponowanym
zakresem następnej fazy, nie płatnym zobowiązaniem ani wybraną polityką użytkownika.

Ponadto przygotowano wszystkie 16 zamierzonych body dwóch istniejących receptur
Jev na dokładnych poprawionych source views. Stary bounded validator dopuszcza
**4/16**, pozostałe **12/16** przekraczają jego limit body; te sloty pozostają
brakujące, z zachowanymi dokładnymi wejściami. Nie przycięto źródeł, nie
przemianowano naturalnego scope w pozornie tożsamy stary enum i nie uznano
4 requestów za całe badanie. Fresh provider capability/price admission pozostaje
otwarte. To ograniczenie istniejącego instrumentu, nie dowód limitu dostawcy.

Actual source coverage: 2026-02-11–2026-04-17 UTC, 22 user + 22 assistant,
3 rodziny / 3 single-leaf saved graphs. Brak próbki Anthropic, niezależnej
walidacji i plików załączników w kapsule; 6 nontext parts nie zinterpretowano.
Oryginalne content/metadane zachowano. Szczegóły w `coverage.json`.

Eksport istniejącym `loom/tools/seeding/method_graph.py` daje
`loom.method_graph/1`, `loom.method_run_trace/1` i pełny packet bezpiecznej
projekcji przygotowania: **15 Entity, 30 Claim, 12 Observation, 6 result**.
Schema/codec i odzyskanie dokładnych source/result bytes PASS.
Nie wykonano CABI ani zapisu native store. Handoff dla B w `HANDOFF_B.md`.
Cache/provider rules sprawdzono w oficjalnej dokumentacji, opisano w
`CACHE_AND_ORDER.md`; przyszły payer ma wyłączyć cache odpowiedzi także dla
powtórek i zachować cache read/write oraz observed endpoint.

Świeże testy: **257/257 PASS** = intake 37, preparacja 43, nowy workflow 28,
existing runner 135, graph export 14. Pierwszy runner test run miał 6 błędów
brakującej zależności requests; pełny failing log zachowano. Po instalacji
requests 2.34.2 i jsonschema 4.26.0 wszystkie te same 135 testów PASS.
Nie zmieniono bramek/progów ani produkcyjnego kodu. Build/CTest/web/Actions
nie uruchamiano dla tego izolowanego przyrostu Python/danych.

## Dowiedzione i niedowiedzione

Dowiedzione: integralność odzyskanych bajtów, archiwalny rachunek kampanii,
replay trzech historycznych grafów, działanie badawczej kolejki i macierzy,
byte-preserving preparacja realnego panelu, kontrakt/projekcja i wymienione testy.
Niedowiedzione: jakość modeli na eksportach, nowy zwycięzca, niezależna walidacja,
różnice między korpusami, native execution, UI wiring i aktualny budżet dostawcy.
`candidate-presets.json` ma status niezwalidowane i materiały Basic/Expert;
`remaining-design.json` zachowuje jawne pozostałe osie, bez fikcyjnych wyników.

## Commity i wznowienie

Etap 1 opublikowany: **`c176c523c706e8e02ef2405df4feebff2dc67781`**;
lokalny początkowy `dc23efc` miał identyczne drzewo
`9b22a9d1580d43bb8c6605433a0fa1d1adab30c0`. Shell push nie miał credential;
publikacja odbyła się połączonym GitHub, bez force i zmian cudzych gałęzi.
Etap 2 opublikowany: **`18eae5f66d95e1bbfde7c2034af4346c02802672`**.
Pełne logi testów zachowano w `test-logs.zip` (hashe w TESTS.json).
Ostatni commit dokumentacyjny domyka manifest i prywatny checkpoint; exact tip to HEAD.

Prywatny checkpoint `PRIVATE_Thread7_real_workflow_2026-10-09.zip` zawiera
źródłowe kapsuły, pełne preparacje, nowe spec/kolejki/freeze, wersje narzędzi
i zachowany checkpoint 720 pierwszych prób z rachunkami. Nie ma klucza.
Publiczna projekcja nie daje możliwości odzyskania prywatnych rozmów.

Punkt wznowienia: rozpakować prywatny checkpoint poza Git, sprawdzić jego
manifest, przywrócić działającą referencję do tego samego klucza kampanii,
wykonać świeży bound-key/usage/reservations/endpoint/price preflight istniejącego
runnera. Nie zerować brakujących rezerwacji. Po jawnej decyzji o ewaluacji
prowizorycznego gold albo niezależnej adjudykacji zamrozić nową ocenę,
rozpocząć od małej alternatywy i raportować zależność 8 pytań od 3 rodzin.
Nie powtarzać 720 opłaconych prób; wszystkie nowe quality inference wyłącznie
na rzeczywistych źródłach. Potem analogiczny mały protocol dla kontekstu,
graph-vs-text i stability; niezależny split wymaga nowych rodzin.

Checkpoint prywatny zapisany i sprawdzony: **137c49ab13681d036b955039a580aff3cb86bc342b62981c484abcfe9363c28e**, 291 payloadów, CRC/SHA PASS. Nowy koszt nadal 0 USD.
