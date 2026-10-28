# Flow wielotorowy (R102) — rozmowa z wieloma modelami naraz i geneza tekstu

Projekt, 2026-10-28. Podstawa: R102 z dwiema poprawkami właściciela (`docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md`
§R102, `docs/spec/spec.json`), MR21 (`docs/architecture/SLOWA_WLASCICIELA_2026-10-16.md`), `docs/architecture/DUCH_GRAFU_2026-10-11.md` §4.
Stan kodu sprawdzony na gałęzi `origin/claude/m1-w1` (commit a781156a). Wklejona notatka z R102 to **inspiracja**: każdy
konkretny wybór poniżej jest oznaczony jako **preset** (wartość domyślna w danych, do zmiany) albo **przykład**; mechanizm
ma obsłużyć każdą konfigurację, także mało użyteczną (wtedy nieblokująca podpowiedź, „advisory”, z pytaniem „czy celowo?”
i „nie pytaj więcej”).

## 0. Słowa robocze (wyjaśnione raz)

- **Flow** (przepływ) — ciąg kroków zapisany jako dane; w grafie leży w obszarze „Przepływy”. Technicznie: akcja złożona
  katalogu działań (`loom/data/runtime/actions.pack`), wykonywana przez interpreter (`loom/include/loom/action_run.h`).
- **Tor** — jedna linia pracy w eksperymencie: metoda + wersja + model + prompt + parametry + budżet + kryteria oceny
  (DUCH_GRAFU §4). W rozmowie tor to „jeden rozmówca-model ze swoimi ustawieniami”.
- **Uczestnik** — ktoś, kto mówi w rozmowie: model, człowiek, agent, narzędzie, inny flow. Ma pseudonim i role.
- **Runda** — jeden obieg, w którym wybrani uczestnicy dostają wiadomość i odpowiadają.
- **Reguła widoczności** — kto w danej rundzie widzi co z wcześniejszych wypowiedzi.
- **Schemat odpowiedzi** — kształt, w jakim uczestnik ma odpowiedzieć (wolny tekst, lista, sloty pytań…).
- **Agregacja** — sposób złożenia odpowiedzi wielu uczestników w jeden wynik (suma, tylko różnice, głosowanie…).
- **Fragment** — kawałek tekstu, któremu system nadaje tożsamość (akapit, zdanie, słowo), żeby śledzić jego losy.
- **Geneza tekstu** — historia fragmentu: skąd przyszedł, co go zastąpiło, gdzie i kiedy zniknął.
- **Wklejka** — tekst wklejony do pola wiadomości z innego miejsca (inna rozmowa, inna aplikacja).
- **Strażnik ×10** — istniejący mechanizm zużycia: wzrost spodziewanego zużycia o rząd wielkości czeka na „tak” użytkownika.
- **Paragon „Co wysłaliśmy”** — istniejący podgląd tego, co poszło do dostawcy (RI-2, `provider_request_preview`).

## 1. Sedno

R102 nie jest nowym silnikiem. To **flow konwersacyjny** w obszarze „Przepływy”: przypadek ogólnego eksperymentu
wielotorowego, w którym torami są uczestnicy rozmowy. Wszystko, co notatka opisuje (rozesłanie jednej wiadomości do N
modeli, pseudonimy, ślepa pierwsza runda, rundy krzyżowe, schematy, agregacje), to **dane jednego flow** — presety, które
użytkownik zmienia, łączy i zastępuje. Drugi, niezależny filar to **geneza tekstu**: tożsamość fragmentów i porównanie
wersji na poziomie zdań i słów, z usunięciem jako pełnoprawnym zdarzeniem. Ona działa wszędzie (wersje wiadomości, różne
rozmowy, wklejki, importy, dokumenty), a R102 jest jednym z jej klientów. Przypadek testowy (zdanie cicho znika przy
przeklejaniu między dwiema rozmowami) sprawdza właśnie ten drugi filar i jest testem akceptacyjnym całości.

## 2. Co już jest, czego brak (obserwacja datowana 2026-10-28, `origin/claude/m1-w1` a781156a)

Jest:
- Obszar grafu „Przepływy” (`loom/data/graph/entry.pack` ~399, wiązanie `actions.flows`) i klasy `flow`, `flow_step`,
  grupa `flows` (`loom/data/graph/classes.pack` ~482, ~1515). Obszar „Eksperymenty i kalibracja” (entry.pack ~551).
- Interpreter akcji złożonych z **wariantami kroku**: krok może mieć `variants` (każdy z własnym `with`: model, prompt,
  parametry) i `variant_mode` (`all` | `first_success`), wyjścia wariantów są adresowalne (`inputs: [{from, variant}]`)
  — `loom/src/actions/run.cpp` ~1116–1153, `action_run.h`. To jest ziarno „wielotorowych eksperymentów”, o których mówi
  właściciel. Warianty wykonują się **po kolei** (pętla w run.cpp ~1130), nie równolegle.
- Kosztorys mnoży wywołania przez liczbę wariantów (`loom/src/actions/estimate.cpp` ~195, run.cpp ~576); strażnik ×10
  i limity wydatków działają na każdy bieg akcji (`action_run.h`, `action_spend.h`, `usage_policy.pack` growth_factor 10).
- `use_model` zwraca `ModelOutcome` z listą `VariantResult` (model, dostawca, tekst, użycie, pochodzenie) —
  `loom/include/loom/use_model.h` ~310.
- Składanie żądania rozmowy (`ChatEngine::build_messages`, `loom/src/chat/chat_engine.cpp` ~1280–1690): historia idzie
  jako pary `role`/`content`; istnieje już tryb „pakietu”, w którym historia jest wstawiona jako jeden blok tekstu w roli
  system (`flow->packet`, ~1652–1662) — gotowy precedens dla „transkryptu w jednej roli”.
- Adaptery dostawców: `openai_chat` (także OpenRouter) i `anthropic_messages` (`loom/src/llm/adapters.cpp`); Gemini dziś
  tylko przez OpenRouter.
- Rozgałęzianie wersji wiadomości (`version_group_id`; web `ConversationBranches.tsx`, `ChatView.tsx`), pochodzenie na
  poziomie węzłów (`derived_from`, `part_of`, `metadata.provenance`), diff Myersa po **liniach**, liczący tylko długość
  skryptu edycji (`loom/src/resolve/lineage.cpp` ~71, `resolve.h` ~148).
- Jeden kształt nieblokującej podpowiedzi (`loom/include/loom/advisory.h`), rejestr rodzajów okien
  (`loom/web/src/shell/data/window_kinds.json`, lista otwarta), zdania intencji (`loom/data/runtime/intent_routing.pack`).

Brak: równoległych torów, rund i reguł widoczności, uczestników z pseudonimami, schematów i agregacji jako wtyczek,
widoku porównania i rozbieżności, diffu tekstu na poziomie zdań/słów między wersjami, rozmowami i wklejkami, zapisu
wklejek, MR21. Uwaga do MR21: zdania „Zapytaj trzy modele…” nie znaleziono w transkryptach (zastrzeżenie w
SLOWA_WLASCICIELA) — traktuję je jako przykład, nie cytat.

## 3. Model flow wielotorowego

### 3.1 Flow konwersacyjny = flow w „Przepływy”

Flow konwersacyjny to akcja złożona katalogu, jak każda inna: ma wejścia (wiadomość, rozmowa, dokument), kroki i wyjścia.
Jego definicja leży w danych (`actions.pack` + nakładka użytkownika), węzeł w grafie w obszarze „Przepływy”. Uruchomienie
tworzy **bieg** (jak dziś `action_run`), który jest trwały, wstrzymywalny (×10, potwierdzenie) i zostawia ślad w grafie.
Zwykła rozmowa z jednym modelem to zdegenerowany flow konwersacyjny: jeden uczestnik, rundy bez końca, widoczność „wszystko”.
Dzięki temu nie ma dwóch światów: przejście z rozmowy jednotorowej do wielotorowej to zmiana danych, nie trybu aplikacji.

### 3.2 Tor jako przypadek ogólnego eksperymentu

Tor w rozmowie = wariant kroku interpretera rozszerzony o uczestnika. Rozszerzenia interpretera (mechanizm):
- `variant_mode` dziś: kod rozpoznaje tylko wartość `first_success`, każda inna (także `all`) znaczy „wszystkie po kolei”
  (run.cpp ~1119; obserwacja 2026-10-28). Zgodnie z zasadą „listy opcji tylko gdy konieczne” staje się **otwartą wartością**
  z presetami (przykłady): `all` (sekwencyjnie), `parallel` (równolegle, z otwartą liczbą
  jednoczesnych wywołań, preset = liczba torów), `first_success`, `race` (pierwszy, który skończy), `until` (predykat nad
  wynikami, np. „aż dwa tory się zgodzą”), i każdy inny opisany predykatem. Nieznany tryb = podpowiedź, nie błąd.
- Tor może się różnić **czymkolwiek**: modelem, dostawcą, promptem, temperaturą, rozumowaniem, kontekstem (inny profil
  selektora), budżetem, schematem odpowiedzi, a nawet metodą (model vs. regex vs. człowiek). Te same ustawienia w kilku
  torach (czysta powtórka) też są dozwolone — przykład użyteczny do mierzenia stabilności; kiedy wygląda na przypadek,
  podpowiedź `tracks_identical` pyta, czy celowo.
- Tryby DUCH_GRAFU §4 (ręczny, półautomatyczny, automatyczny, „dowolnie inaczej”) zostają danymi: kto składa tory i kto
  ocenia.

### 3.3 Uczestnicy i role (otwarte dane)

Uczestnik: `{id, pseudonim, wiązanie, role[], widoczność, ustawienia toru}`. Wiązanie (lista otwarta, przykłady): model (przez port/politykę),
człowiek (użytkownik albo inna osoba), agent, narzędzie, inny flow, każde inne. Role to otwarte etykiety z opisem i fragmentem promptu;
**przykłady** ról: odpowiadający, krytyk, obrońca, redaktor, sędzia, sekretarz-streszczacz, „adwokat diabła”, tłumacz.
Jeden uczestnik może mieć wiele ról, jedna rola wielu uczestników, rola może się zmieniać między rundami (dane rundy).
Zgodnie z drugą poprawką właściciela („we flow wielotorowym to można przydzielać wiele ról”) role nie są ograniczone do
`user/assistant/system` dostawcy — to warstwa nad nimi; jak je przekazać danemu dostawcy, mówi §3.6.

**Pseudonimy** (preset z notatki): „Model A/B/C…”, stałe w obrębie sesji, losowane między sesjami, bez nazw dostawców w
tym, co widzą inne modele. Mechanizm: generator pseudonimów jako dane (alfabet, wzór, zakres stałości: bieg / rozmowa /
projekt / zawsze; czy losować). Osobne ustawienie: czy **użytkownik** też widzi tylko pseudonimy (ocena w ciemno) —
preset: użytkownik widzi pod pseudonimem prawdziwy model po kliknięciu; tryb „ślepy także dla mnie” jest jednym słowem w
ustawieniach. Odkrycie tożsamości po ocenie jest zdarzeniem w grafie (żeby ocenę przed i po dało się odróżnić).

### 3.4 Rundy i reguły widoczności (dane)

Runda: `{id, kto mówi, co widzi, szablon wiadomości, schemat odpowiedzi, warunek wejścia, warunek końca}`. „Co widzi” to
predykat nad wcześniejszymi wypowiedziami (autor, runda, rola, fragment, wynik agregacji). **Preset z notatki**: runda 1
ślepa (każdy widzi tylko wiadomość użytkownika, te same słowa dla wszystkich), rundy 2+ krzyżowe (każdy widzi odpowiedzi
pozostałych pod pseudonimami). **Inne przykłady** tej samej mechaniki: każdy widzi tylko sumę z poprzedniej rundy;
turniej par; krytyk widzi tylko obrońcę; łańcuch (B poprawia A, C poprawia B); użytkownik wtrąca się w dowolnej rundzie;
różne tory na różnych fragmentach dokumentu. Liczba rund otwarta (stała, predykat „aż przestaną się zmieniać”, ręcznie).
„Te same słowa dla wszystkich” (preset) to reguła rozsyłania; tor może dostać własny dopisek promptu (wtedy diff promptów
torów jest widoczny w paragonie, żeby różnica wyników nie myliła się z różnicą słów).

**Pytania w slotach rozsyłane raz** (przykład z notatki): gdy uczestnicy zadają pytania w slotach (cel, odbiorca,
ograniczenia, brakujące fakty, forma — lista otwarta), agregacja łączy pytania podobne w jedno, użytkownik odpowiada raz,
a odpowiedź trafia do wszystkich torów zgodnie z regułą widoczności następnej rundy. To runda z uczestnikiem-człowiekiem
i agregacją „deduplikacja pytań”, nie osobny mechanizm.

### 3.5 Schematy odpowiedzi i agregacje (wtyczki z presetami)

Schemat = `{id, opis, dopisek promptu, struktura (JSON Schema albo opis tekstowy), parser, walidator}`; wtyczka to wpis w
danych, a parser/walidator to akcja katalogu (więc może być regexem, kodem albo modelem). **Presety-przykłady**: wolny
tekst; lista punktów; twierdzenia z cytatem; sloty pytań; pytania otwarte z cytatem fragmentu, którego dotyczą; mapa
niejednoznaczności (fragment promptu → możliwe odczytania → które wybrałem). **Pole otwarte „czego brakuje / jak
zrozumiałem zadanie”** jest w presecie dołączane do każdego schematu (notatka mówi „zawsze”); wyłączenie jest możliwe, z
podpowiedzią `open_field_off`, bo to główne źródło mapy niejednoznaczności.

Agregacja = akcja katalogu `(odpowiedzi torów, schemat) → wynik + węzły grafu`. **Preset: suma** (nie głosowanie):
unia elementów z deduplikacją semantyczną i pochodzeniem każdego elementu (który pseudonim, która runda, cytat).
**Inne przykłady**: tylko niezgodności (MR21); przecięcie; głosowanie z progiem; ranking przez sędziego-model; ocena
wielokryterialna z DUCH_GRAFU §4; ręczny wybór; łańcuch agregacji (najpierw suma, potem tylko różnice). Deduplikacja sama
jest metodą z wariantami (przykłady: dokładna, leksykalna, wektorowa, model, Jev — dotychczasowy lokalny mechanizm oceny z
projektu, nazwa robocza; lista otwarta) — wybór i próg to presety, a scalenie dwóch
elementów zostaje odwracalną propozycją z dowodem. Zasada z notatki „zgoda modeli to słaby sygnał (skorelowane błędy)”
jest daną kalibracji: zgoda podnosi pewność słabo (waga w `loom/data/policy/calibration.json`, do strojenia), a zgoda
modeli tej samej rodziny jeszcze słabiej; nigdy nie jest werdyktem.

### 3.6 Jak pokazać dostawcy innych uczestników (zdolność dostawcy w danych + preset)

Dostawcy mają role „swoje” (użytkownik/model, czasem system) i różnie umieją odróżnić kilku mówiących. Sposób
przedstawienia jest **otwartą wartością** z presetami, wybieraną per tor:
- `native_name` — natywne pole nazwy uczestnika przy wiadomości (gdzie dostawca je ma);
- `labelled_sections` — wypowiedzi innych uczestników w jednej roli z podziałem „Model A: … / Model B: …” (forma z poprawki
  właściciela; szablon nagłówka to dane);
- `quoted_transcript` — cały przebieg jako cytowany transkrypt w jednym bloku (rozszerzenie istniejącego trybu pakietu
  w `build_messages` — uwaga: tamtejszy „flow” to klasa formatu czatu, nie flow z obszaru „Przepływy”; precedens dotyczy tylko bloku w roli system), z instrukcją, kim jest adresat;
- `separate_turns` — każdy uczestnik jako osobna tura (z ryzykiem sklejenia przez dostawcę, patrz niżej);
- dowolny inny szablon zdefiniowany przez użytkownika.
Wybór domyślny wynika z wpisu zdolności dostawcy (`participants_presentation` — wpis do utworzenia w pakiecie W1, dziś go nie ma; tryby natywne, co dostawca robi z nieznanym
polem, czy skleja kolejne tury tej samej roli) i z presetu. Użytkownik może wymusić dowolny tryb także tam, gdzie dostawca
go nie wspiera natywnie — wtedy podpowiedź `presentation_not_native` mówi, co dostawca z tym zrobi (np. zignoruje pole,
sklei tury). Paragon „Co wysłaliśmy” pokazuje dokładnie wysłany kształt.

**Obserwacje datowane (2026-10-28) jako zasilenie wpisu zdolności — nie ograniczenia.** Ocena źródła: O = oficjalna
dokumentacja/kod SDK, F = forum/prasa.

| Dostawca / produkt | Co obserwujemy | Źródło (ocena) |
|---|---|---|
| OpenAI Chat Completions | opcjonalne pole `name` przy wiadomości: „An optional name for the participant. Provides the model information to differentiate between participants of the same role.” | openai-python, `chat_completion_user_message_param.py` (O) |
| OpenAI Responses API | wejściowe wiadomości bez `name`; wysłanie daje „Unknown parameter”; zalecane obejście: nazwa w treści | community.openai.com/t/1154818, /t/1365804 (F) |
| Anthropic Messages | role tylko `user`/`assistant`, system w osobnym polu `system`; brak pola nazwy; „Consecutive user or assistant turns in your request will be combined into a single turn.” | platform.claude.com/docs/en/api/messages (O) |
| Google Gemini | `Content.role`: „Must be either 'user' or 'model'.”; instrukcja systemowa w `systemInstruction`; brak pola nazwy | ai.google.dev/api/generate-content (O) |
| OpenRouter | `name?: string` w schemacie; „If "name" is included, it will be prepended like this for non-OpenAI models: `{name}: {content}`”; nieobsługiwane parametry są ignorowane | openrouter.ai/docs/api-reference/overview (O) |
| ChatGPT (produkt) | czaty grupowe do 20 osób z ChatGPT „wchodzącym w razie potrzeby” (globalnie od 2025-11-20); wzmianki `@GPT` w rozmowie (od 2024-01), w 2025/26 zgłaszane jako niedziałające z częścią modeli | techcrunch.com 2025-11-20; help.openai.com/en/articles/8554402; community.openai.com/t/1367275 (F) |

Wniosek dla presetów (przykład, do kalibracji testem): OpenAI Chat → `native_name`; Anthropic i Gemini →
`labelled_sections` w jednej turze użytkownika (sklejanie tur u Anthropic i tak by je połączyło); OpenRouter → pole `name`
działa jak `labelled_sections` przy modelach spoza OpenAI, więc system może wysłać `name` i wiedzieć, co dotrze.
Wpis ma okno ważności i ocenę źródła (rejestr R59); test na żywym dostawcy podnosi albo obniża ocenę. Źródła tabeli nie zostały ponownie otwarte w kontroli 2026-10-28 (cytaty przepisane z projektu; ocena O/F jak wyżej) — pierwszy test na żywym dostawcy w W6 je potwierdza albo obala. Obserwacja o
produktach (czaty grupowe, `@`) to inspiracja dla wejść w UI, nie wzorzec do kopiowania.

## 4. Geneza tekstu: pochodzenie i diff (mechanizm ogólny, poza R102 też)

### 4.1 Fragmenty i ich tożsamość

Tekst (wiadomość, dokument, wklejka, wynik toru, plik importu) jest dzielony na fragmenty na kilku poziomach: dokument →
akapit → zdanie → słowo (lista poziomów otwarta: linia, punkt listy, komórka tabeli, linia kodu). Podział to metoda z
presetami zależnymi od języka i formatu (zdania po polsku i angielsku, Markdown, kod). Fragment dostaje odcisk
znormalizowanej treści (normalizacja to dane: białe znaki, wielkość liter, interpunkcja) i węzeł w grafie dopiero, gdy
jest potrzebny (preset: leniwie; tryb „od razu dla wszystkiego” jest ustawieniem do włączenia, bez ograniczeń).

### 4.2 Wyrównanie wersji

Dwie wersje porównuje się wyrównaniem sekwencji fragmentów: istniejący Myers z `lineage.cpp` uogólniony z linii na
dowolne jednostki i rozszerzony tak, by zwracał **skrypt** (rodzaje operacji — przykłady, lista otwarta: zachowane, dodane, usunięte, zmienione, przeniesione), nie tylko
długość. Zdanie zmienione (a nie usunięte i dodane) rozpoznaje dopasowanie przybliżone (podobieństwo znakowe/słowne;
próg = preset; opcjonalnie wektorowe lub model jako dalsze warianty metody; preset: wyłączone, bo płatne — użytkownik włącza je jednym ustawieniem, także na stałe). Wewnątrz
zmienionego zdania — diff słów. Przeniesienie = fragment usunięty w jednym miejscu i dodany w innym z tym samym odciskiem.

### 4.3 Skąd biorą się wersje

- wersje wiadomości (`version_group_id`) i gałęzie rozmowy;
- **różne rozmowy**: ten sam fragment rozpoznany po odcisku w innej rozmowie łączy je krawędzią;
- **wklejki**: klient zapisuje zdarzenie wklejenia (czas, długość, miejsce w polu, opcjonalnie źródło, jeśli znane), a
  silnik lokalnie, bez modelu, dopasowuje wklejony tekst do znanych fragmentów (indeks odcisków) i zapisuje krawędź
  „wklejone z” — to pokazuje, z której wypowiedzi której rozmowy pochodzi i czego w nim brakuje względem źródła;
- wyniki torów i agregacji (każdy element sumy ma pochodzenie);
- importy (archiwa rozmów, dokumenty), artefakty i pliki projektu.

### 4.4 Usunięcie jako zdarzenie pierwszej klasy

Każda różnica jest węzłem w grafie, nie tylko kolorem w widoku: `usunięcie` (fragment, wersja przed, wersja po, gdzie —
rozmowa/wiadomość/wklejka, kiedy, kto lub co: użytkownik, model z pseudonimem, tor, agregacja), analogicznie `zastąpienie`
(z krawędzią do następcy), `przeniesienie`, `dodanie` (rodzaje zdarzeń to przykłady; nowe dodaje się przez użycie). Krawędzie: `derived_from`, `replaced_by`, `deleted_in`,
`pasted_from`, `moved_to` (nowe rodzaje krawędzi dodaje się przez samo użycie). Istotność usunięcia to osobna, opcjonalna
ocena (lokalne cechy: długość, liczby, nazwy własne, przeczenia; Jev albo model jako warianty) — preset: pokazuj wszystkie
usunięcia, istotne na górze; „cicho zniknęło” = usunięcie, którego nikt nie zaakceptował ani nie skomentował.

### 4.5 Zapytania

„Skąd to zdanie?”, „co zastąpiło to zdanie?”, „gdzie i kiedy zniknęło zdanie X?”, „czego brakuje w wersji B względem A?”,
„co dodał który model?” — to akcje katalogu nad węzłami fragmentów, dostępne z czatu, zdań i okna (§7). Wszystko liczy się
lokalnie i deterministycznie; koszt pieniężny tylko przy włączonych wariantach z modelem.

## 5. Widoki rozbieżności i niejednoznaczności (węzły grafu)

- **Rozbieżność** — węzeł `disagreement`: twierdzenie lub element, kto (pseudonim, runda) za, kto przeciw, kto milczy,
  cytaty, odpowiedzi użytkownika. Powstaje z agregacji „tylko niezgodności” albo z dowolnej innej (każda suma ma też
  listę elementów, które ma tylko jeden tor).
- **Niejednoznaczność** — węzeł `ambiguity`: fragment promptu użytkownika, możliwe odczytania, który tor przyjął które
  (z pola „jak zrozumiałem”), propozycja poprawki promptu. Przyjęcie poprawki tworzy nową wersję promptu, z genezą.
- Widoki to perspektywy grafu i okna (§7): „tylko to, w czym się nie zgadzają” jest filtrem-presetem nad tymi węzłami.
  Zgoda wszystkich torów jest pokazana jako słaby sygnał (oznaczenie, nie zielony znaczek „prawda”).
- Każdy bieg zostaje w grafie jako dowód dla metod i modeli (model modeli, MR09): które tory częściej wnoszą unikalne
  trafne elementy, które częściej gubią zdania — dane do przyszłych presetów, nie ranking narzucony użytkownikowi.

## 6. Koszt i potwierdzenie ×10

- Kosztorys biegu = suma po rundach i torach (rozszerzenie `estimate.cpp`, które już mnoży warianty). Rundy krzyżowe
  rosną szybciej niż liniowo (każdy tor czyta odpowiedzi pozostałych): kosztorys pokazuje to wprost.
- Strażnik ×10 działa jak dla każdej akcji: jeśli spodziewane zużycie biegu przekracza zwykłe zużycie o rząd wielkości
  (współczynnik z `usage_policy`, preset 10), bieg czeka na „tak” z paragonem. Hipoteza do zmierzenia kosztorysem, nie fakt: trzy tory × trzy rundy z
  krzyżowym kontekstem mogą przekroczyć ×10 względem pojedynczej wiadomości (wtedy pytamy); dwa tory w jednej ślepej
  rundzie raczej nie. Próg decyduje liczba z kosztorysu, nie ta uwaga. Limity wydatków użytkownika (dzień/tydzień/miesiąc) też działają; nic nie dodajemy od siebie.
- Bez nowych ograniczeń: liczba torów, rund i uczestników jest otwarta. Podpowiedź `many_tracks` (próg = preset) mówi o
  koszcie i, jako hipotezę do sprawdzenia na dowodach z §5, że kolejne tory tej samej rodziny modeli mogą wnosić mało — nie blokuje.
- Pamięć podręczna dostawców (caching) jest wykorzystywana: wspólny prefiks wiadomości jest taki sam dla wszystkich
  torów tego samego dostawcy (preset „te same słowa” temu sprzyja).

## 7. Prywatność: kto widzi co

- Każdy tor ma dostawcę; paragon „Co wysłaliśmy” jest **per tor i per runda** — widać, który dostawca dostał co.
- Runda krzyżowa wysyła odpowiedź toru A do dostawcy toru B. To przepływ danych, który istnieje tylko we flow
  wielotorowym, więc reguły prywatności (kategorie danych, dostawcy dozwoleni dla kategorii) sprawdzają go jak każdą
  wysyłkę: jeśli odpowiedź A cytuje dane kategorii niedozwolonej u B, wysyłka czeka na decyzję (regułą, nie kodem).
- Pseudonimy chronią przed uprzedzeniem modeli (nazwa dostawcy w treści to też dane; czyszczenie „jestem Claude/GPT”
  z odpowiedzi przed rundą krzyżową to preset, wyłączalny).
- Pierwsze wysłanie danych do dostawcy, którego użytkownik jeszcze nie używał dla danej kategorii, przechodzi przez
  istniejące potwierdzenie — flow wielotorowy nie omija tej zasady, także gdy tor został dodany automatycznie (tryb półautomatyczny lub automatyczny).
- Geneza tekstu i indeks odcisków są lokalne; nic z nich nie wychodzi bez osobnej, jawnej akcji.

## 8. Wejścia w UI

- **Czat**: w menu „+” przy polu wiadomości „Zapytaj kilka modeli” (otwiera wybór uczestników z presetami flow);
  `@pseudonim` / `@model` w polu wiadomości dodaje uczestnika albo adresuje wiadomość do jednego toru (inspiracja: wzmianki
  w produktach, §3.6); przy odpowiedzi „Powtórz innym torem” (MR21, drugi przykład) i „Porównaj z…” (inna wersja, inna
  rozmowa, wklejka). Przykład układu (nie wymóg): wiadomość wielotorowa jako jeden dymek z zakładkami/kolumnami torów i paskiem agregacji; kształt ustawia użytkownik.
- **Niedokończone = oznaczone**: wpisy menu, okna i zdania z tej listy pojawiają się w aplikacji dopiero z pakietem, który je realizuje; wcześniej są widoczne jako wyszarzone z powodem („planned”, jak w `window_kinds.json`), nigdy jako działające atrapy. Ograniczenia flow (np. warianty dziś po kolei, brak rund) użytkownik dostaje przed uruchomieniem.
- **Rodzaje okien** (nowe wpisy w `window_kinds.json`, lista otwarta): „Tory obok siebie” (kolumny, synchroniczne
  przewijanie, diff słów między kolumnami), „Rozbieżności i niejednoznaczności” (lista węzłów §5 z cytatami), „Geneza
  tekstu” (oś czasu fragmentu: powstanie, zmiany, wklejki, usunięcie; z przejściem do źródła).
- **Graf**: obszar „Przepływy” pokazuje flow konwersacyjne i ich presety; bieg i jego węzły (rozbieżności, usunięcia)
  w perspektywach; obszar „Eksperymenty” — wyniki torów jako dowody.
- **Zdania** (wpisy w `intent_routing.pack`, przykłady): „zapytaj trzy modele o to samo i pokaż, w czym się nie
  zgadzają”, „puść to jeszcze raz innym modelem”, „skąd to zdanie?”, „gdzie zniknęło zdanie o …?”, „czego brakuje w tej
  wersji względem tamtej rozmowy?”, „pokaż, jak każdy model zrozumiał moje pytanie”.

## 9. Mechanizm a presety

| Mechanizm (kod, ogólny) | Presety i przykłady (dane, do zmiany) |
|---|---|
| flow konwersacyjny jako akcja złożona, bieg trwały | flow „Zapytaj kilka modeli”, „Krytyk i obrońca”, „Redakcja łańcuchowa” |
| tryb wariantów jako otwarta wartość/predykat | `parallel` dla rund, liczba jednoczesnych wywołań = liczba torów |
| uczestnicy, role, pseudonimy jako dane | „Model A/B/C”, stałe w sesji, losowane między sesjami; użytkownik widzi tożsamość po kliknięciu |
| rundy + predykaty widoczności | ślepa pierwsza, krzyżowe kolejne; te same słowa dla wszystkich |
| tryby przedstawienia uczestników + wpis zdolności dostawcy | tabela §3.6, wnioski per dostawca |
| schematy jako wtyczki | wolny tekst, sloty pytań, pytania z cytatem, mapa niejednoznaczności; pole „czego brakuje” dołączane |
| agregacje jako wtyczki | suma z deduplikacją i pochodzeniem; „tylko niezgodności”; próg deduplikacji |
| fragmenty, odciski, wyrównanie, dopasowanie przybliżone | poziomy zdanie+słowo; próg podobieństwa; normalizacja |
| zdarzenia usunięcia/zastąpienia/wklejenia jako węzły | „pokaż wszystkie usunięcia, istotne na górze” |
| kosztorys po torach i rundach, ×10, limity | współczynnik 10; próg podpowiedzi `many_tracks` |
| podpowiedzi | `tracks_identical`, `presentation_not_native`, `open_field_off`, `many_tracks`, `cross_round_privacy` (informacja towarzysząca wstrzymaniu z §7, nie samodzielna blokada) |

## 10. Przypadek testowy jako test akceptacyjny (e2e)

Scenariusz: dokument redagowany w dwóch rozmowach z dwoma modelami przez przeklejanie; w jednej z przeklejek cicho znika
istotne zdanie. System ma pokazać **gdzie** (która rozmowa, która wiadomość, która wklejka) i **kiedy**.

Zasada oddania (~/.claude/CLAUDE.md, 2026-10-25): dostawca-atrapa i dane syntetyczne żyją wyłącznie w `loom/tests/`, `loom/web/e2e/` i w osobnych katalogach danych testów; w aplikacji, danych roboczych i eksportach właściciela nie zostaje po nich ślad. Właściciel dostaje to po przejściu Etapu B przez mnie, nie przed.

Etap A (deterministyczny, w CI): dane testowe w `loom/tests/fixtures/` i `loom/web/e2e/` — dokument i odpowiedzi modeli
są **syntetyczne** (dostawca-atrapa w fixture, tylko w testach; zgodnie z zasadą właściciela nie ma ich w aplikacji ani w
danych użytkownika). Kroki: (1) rozmowa 1: wklejka dokumentu D0 (7 zdań), model X zwraca D1 z poprawkami; (2) użytkownik
kopiuje D1 do rozmowy 2 (zdarzenie wklejki), model Y zwraca D2, w którym brak zdania nr 5 (z liczbą i nazwą własną) i
zmienione zdanie nr 2; (3) wklejka D2 z powrotem do rozmowy 1. Asercje: węzeł `usunięcie` dla zdania 5 z `deleted_in` =
odpowiedź Y w rozmowie 2, z czasem tej odpowiedzi; zdanie 2 jako `zastąpienie` (nie usunięcie + dodanie); łańcuch
`pasted_from` D0→D1→D2 kompletny; okno „Geneza tekstu” pokazuje zdanie 5 z osią czasu i przejściem do miejsca usunięcia;
zdanie „gdzie zniknęło zdanie o …?” znajduje je; żadne wywołanie modelu w krokach genezy (licznik wywołań = 0).
Testy własności: losowe edycje (usuń/wstaw/zmień/przenieś) na losowych tekstach — wyrównanie odtwarza dokładnie
wstrzyknięty skrypt; teksty puste, jednozdaniowe, identyczne, całkowicie różne; Unicode i polskie znaki.

Etap B (na prawdziwych modelach, raz przed oddaniem do testów właściciela): ten sam scenariusz z dwoma tanimi modelami
przez OpenRouter, kosztorys przed biegiem w granicach zgody właściciela na wydatki (CLAUDE.md „Spending authority” i „Credits are separate”: płatne opłaty ≤ 1 €/dzień i ≤ 3 €/tydzień; kredyty — np. Google — do 10 % każdego kredytu bez pytania (właściciel, 2026-10-27); powyżej — pytanie tak/nie); bieg w osobnym, tymczasowym katalogu danych (`CHATADHD_DATA`), nie w danych użytkownika; raport podaje, które dane
były syntetyczne (dokument D0) i które odpowiedzi prawdziwe. Do tego bieg flow „Zapytaj kilka modeli” (3 tory, ślepa +
krzyżowa) z asercjami: pseudonimy stałe w biegu, paragony per tor, suma z pochodzeniem, węzły rozbieżności.

## 11. Plan pakietów (każdy ≈ 50 wywołań narzędzi, commit + karta wznowienia)

1. **G1 Fragmenty i wyrównanie** — podział na zdania/słowa (dane per język), odciski, Myers uogólniony zwracający skrypt
   (z `lineage.cpp`), dopasowanie przybliżone, przeniesienia. Akceptacja: testy jednostkowe + własności (§10), ASan-czyste.
2. **G2 Geneza w grafie** — węzły fragmentów (leniwie), zdarzenia usunięcia/zastąpienia/przeniesienia, krawędzie;
   źródła: wersje wiadomości i rozmowy; akcje „skąd to zdanie / co zastąpiło / czego brakuje” w katalogu i C ABI.
   Akceptacja: testy obszaru, `test_abi_compat.py` zielony.
3. **G3 Wklejki** — zdarzenie wklejenia w polu wiadomości (web), lokalne dopasowanie do indeksu odcisków, krawędź
   `pasted_from`; importy jako źródło. Akceptacja: Etap A §10 (bez okna) zielony w ctest.
4. **G4 Okno „Geneza tekstu” + zdania** — rodzaj okna, oś czasu fragmentu, zdania intencji. Akceptacja: Etap A §10
   pełny jako Playwright e2e.
5. **W1 Uczestnicy i przedstawienie** — uczestnik/rola/pseudonim jako dane; wpis zdolności `participants_presentation`
   z obserwacjami §3.6; `build_messages` renderuje tryby `native_name`/`labelled_sections`/`quoted_transcript`; paragon
   RI-2 pokazuje kształt. Akceptacja: test dla każdego trybu × każdego adaptera (także wymuszonych, z podpowiedzią).
6. **W2 Interpreter: tory i rundy** — otwarty `variant_mode` (w tym równolegle), rundy, predykaty widoczności, bieg
   flow konwersacyjnego, kosztorys po torach × rundach, ×10. Akceptacja: testy interpretera na dostawcy-atrapie,
   wstrzymanie ×10 i wznowienie biegu.
7. **W3 Schematy** — schemat jako wtyczka (dopisek, struktura, parser, walidator), pole „czego brakuje”, presety §3.5.
   Akceptacja: każdy preset parsuje przykłady poprawne i odrzuca błędne z podpowiedzią, nie wyjątkiem.
8. **W4 Agregacje i węzły rozbieżności** — suma z deduplikacją (dokładna/leksykalna lokalnie, wektor/model jako
   warianty), „tylko niezgodności”, głosowanie; węzły `disagreement`/`ambiguity`; pytania w slotach rozsyłane raz.
   Akceptacja: testy na ustalonych odpowiedziach torów, pochodzenie każdego elementu.
9. **W5 UI wielotorowe** — menu „+”, `@` w polu, „Powtórz innym torem”, dymek z torami, okna „Tory obok siebie” i
   „Rozbieżności”, flow w obszarze „Przepływy”. Akceptacja: e2e na dostawcy-atrapie, widok 390 px i desktop.
10. **W6 Prywatność i prawdziwy bieg** — paragony per tor i rundę, reguła dla rundy krzyżowej, czyszczenie tożsamości;
    Etap B §10 na prawdziwych modelach z kosztorysem; raport ze źródłem każdego wyniku. Akceptacja: raport + zielona
    bramka.

Kolejność (decyzja koordynatora, preset; odwrócenie: zmiana kolejności pakietów, zapis w `docs/reports/INDEX.md`): G1→G4 dają przypadek testowy niezależnie od R102 (najwyższa wartość za najmniejszy koszt, bez wywołań
modeli); W1→W6 potem. Projekt przed pakietami przechodzi tani przegląd czterech antywzorców (CLAUDE.md, „Check before use”).

## 12. Pytania do właściciela

Brak. Sprawdzone wobec filozofii: liczba torów, rund, role, pseudonimy, tryby przedstawienia, schematy i agregacje są
otwartymi danymi z presetami (nic do wyboru za niego); nowi dostawcy nie są potrzebni (tory używają już skonfigurowanych);
koszt Etapu B ma mieścić się w zgodzie na wydatki (płatne ≤ 1 €/dzień, ≤ 3 €/tydzień; z kredytów do 10 % każdego bez pytania) — to sprawdza kosztorys przed biegiem, nie ta uwaga. Jedyne miejsce, gdzie decyzja należy do niego, zostaje w aplikacji:
potwierdzenie ×10 i pierwsze wysłanie kategorii danych do nowego dostawcy.

## Poprawki po kontroli (2026-10-28)

Kontrola read-only wg „Check before use”; kod sprawdzony na `origin/claude/m1-w1` a781156a (potwierdzone: warianty
sekwencyjne w run.cpp ~1116–1153, Myers po liniach tylko z długością w lineage.cpp ~71, klasy `flows`/`flow_step` i
obszar „Przepływy” w pakach, `growth_factor` 10 w `usage_policy.pack`, adaptery tylko `openai_chat` i `anthropic_messages`).
Blokujące (4): B1, B2, B3, B4. Redakcyjne (7): W1–W7.

- B1 (hand-off, §10, §8): Etap B szedł bez izolacji danych, a UI mogło pokazać niedokończone wpisy → osobny katalog danych,
  atrapy tylko w testach, niedokończone wpisy wyszarzone z powodem, ograniczenia flow przed uruchomieniem.
- B2 (§6): przykłady ×10 (3×3 przekracza, 2×1 nie) podane jak fakty → hipoteza do zmierzenia kosztorysem.
- B3 (§10, §12): „stała zgoda właściciela” jako fakt → cytat reguły z CLAUDE.md z zastrzeżeniem odczytu, kosztorys rozstrzyga.
- B4 (§3.3, §3.5, §4.2, §4.4): listy zamknięte (wiązania, metody deduplikacji, operacje skryptu, rodzaje zdarzeń) → przykłady, lista otwarta.
- W1 (§3.2): „variant_mode zamknięta lista” → dokładny opis kodu (rozpoznaje tylko `first_success`).
- W2 (§3.5): „Jev” nieobjaśnione → objaśnione jako nazwa robocza.
- W3 (§3.6): `participants_presentation` brzmiał jak istniejący → wpis do utworzenia w W1; „flow” w trybie pakietu to klasa formatu czatu.
- W4 (§3.6): tabela dostawców — zaznaczone, że źródeł nie otwierano ponownie w kontroli.
- W5 (§4.1, §4.2, §6): „leniwie”/„wyłączone”/„wnoszą mało” — uzupełnione jako presety i hipotezy, z opcją włączenia.
- W6 (§7, §9): zdanie o automacie półautomatycznym poprawione; `cross_round_privacy` opisany jako towarzyszący wstrzymaniu.
- W7 (§8, §11): układ dymka oznaczony jako przykład; kolejność pakietów jako decyzja koordynatora z drogą odwrócenia.
