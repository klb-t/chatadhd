# Preparacja kontekstu — wykonane transformacje offline

Producent: `loom/tools/structure/experiment_context_preparation_v1.py`,
rewizja `experiment_context_preparation_v1/2`. Narzędzie nie wysyła wywołań,
nie ocenia jakości modelu, nie zmienia ustawień ani nie tworzy silnika profili.
Instrukcje, parametry, osie i selekcja są danymi w `context-plan.json`,
`context-task-policy.json`, `context-profiles.json`.

Macierz ma 3 reprezentacje × 3 rozdzielczości × 3 formy odpowiedzi ×
3 tryby preferencji. Każda kombinacja jest rzeczywistą preparacją albo ma
jawną zależność `pending`; sam katalog nazw nie zastępuje requestu.
Generator jest leniwy. Eksport prywatny zapisuje źródła, dokładne widoki,
body requestów, wszystkie intencje macierzy i hashe plików przed oceną.

`exact_text` zachowuje dokładne ciągi i granice pól tekstowych z ich
wskaźnikami. Różniące się alternatywne pola tekstowe pozostają osobno;
identyczne bajtowo pole ma jawną relację aliasu. Pominięte części
nietekstowe i metadane są liczone/opisane. `explicit_fields` projektuje
wyłącznie wskazane jawne pola i oryginalny `native_message`.
`source_graph` zachowuje węzły i natywne relacje parent, wraz z granicami
podgrafu. Nie jest wynikiem semantycznej ekstrakcji. Całe źródła są
zachowane prywatnie również wtedy, gdy widok pomija fragmenty.

`full` obejmuje wszystkie odzyskane węzły. `checkpoint` jest deterministyczną
migawką ścieżki do zaobserwowanej granicy źródła; nie jest streszczeniem ani
deklarowanym checkpointem użytkownika. Gdy natywnej relacji parent nie ma,
mechanizm potrafi jawnie przygotować prefiks kolejności eksportu, bez
twierdzenia, że jest to chronologia lub ancestry. `selected_subgraph` stosuje
konfigurowalne okno przodków albo jawnie wskazane węzły. Nie deklaruje
semantycznej optymalności wyboru. Sześć zewnętrznych parent IDs w sześciu
rodzinach Anthropic pozostaje nierozwiązanymi referencjami; nie dopisano
korzeni i nie przypisano tym identyfikatorom znaczenia sentinela.

Zadanie nowego panelu to jawnie autorska retrospektywna inspekcja źródła:
cele, korekty, preferencje i stan wieloetapowej pracy z referencjami.
Nie przewiduje następnej historycznej odpowiedzi asystenta. Pełny widok może
zawierać późniejsze lub równoległe węzły; zadanie i analiza mają raportować
zakres dostępnych danych. Prowizoryczna selekcja walidacyjna pozostaje
prowizoryczna: nie uzyskano niezależnej adjudykacji ani nowego blind.

`none` nie dodaje personalizacji. `source_explicit` wymaga dokładnej
wypowiedzi roli user, zgodnego wskaźnika i SHA wiadomości oraz przypisanej
adnotacji i zakresu. W nowym panelu ręcznie sprawdzono dwie jawne negatywne
preferencje dotyczące wyniku w jednym rzeczywistym żądaniu. Inne leksykalne
tropy — cytowany transkrypt, wypowiedzi o innych osobach i niejednoznaczne
życzenia — nie zostały zamienione w deklarowane preferencje. Adnotacja ma
autora badawczego, status prowizoryczny i nie ma zatwierdzenia właściciela.
Pozostałe rodziny zachowują brakujące adnotacje jako `pending`.
`selected_profile` odwołuje się do dokładnego istniejącego eksperymentalnego
profilu 7B i jego wersji; wybór przypisano protokołowi badania. Historyczne
zdanie profilu o preferencji użytkownika jest danymi profilu, a nie dowodem
rzeczywistej deklaracji właściciela. Nie zmieniono jego ustawień.

Formy odpowiedzi są zgodne z protokołem analizy:

| Wariant | Rzeczywisty request / zależność |
|---|---|
| `graph_direct` | Jeden request, odpowiedź root `nodes`, `edges`, `evidence`, `limitations`. |
| `text_plus_structure` | Jeden request, odpowiedź `text` oraz `structure` zawierająca ten sam kształt grafu. |
| `text_then_structure` | Pierwszy request tekstowy; drugi pozostaje `pending` bez body do chwili otrzymania kompletnej zapisanej odpowiedzi modelu. |

Drugi request powstaje przez `materialize_extraction`: sprawdza tożsamość
pierwszego call, hash dokładnego tekstu, referencję niezmiennego capture,
status odpowiedzi i brak trafienia response cache. Wiąże dokładny tekst z
nowym body i własnym kosztem oczekującym; nie udaje wykonanej ekstrakcji.
Każdy request ma jawnie wyłączony response cache. Requested provider policy
jest danymi badania; `supported_parameters`, aktualna cena i admission
dostawcy pozostają unknown. Lokalna preparacja nie daje zgody dispatch.

Prywatny `jobs-index.jsonl` daje B/koordynatorowi pola `operation_id`,
`family_id`, `task_id`, `split`, `variant`, `phase`, `status`, `body_ready`,
`body_sha256`, `body_path`, `dependencies`, `matrix_ordinal`,
`source_sha256`, `plan_sha256`, `dispatch_ready:false`. Ścieżki są względne
wobec danego freeze. Różne intencje mogą mieć identyczne body; hash body nie
zastępuje ich tożsamości. Oczekujące intencje zachowują mianownik.

Pierwszy `expanded-v1` zachowano z manifestem
`07a2a1ba344e185fe4c40041f160e71cde255a5befc989790e60e4f4f24242ad`.
Review wykrył osiem wiadomości Anthropic z dostępnym polem top-level text,
które pierwsza projekcja pominęła przy nietekstowym content. Nie nadpisano
freeze: nowy `expanded-v2` zachowuje te pola i ich pochodzenie, dodaje
sprawdzoną adnotację i jawne requested provider policy. Sprawdzono wszystkie
1109 dostępnych top-level pól text Anthropic: zero pominięć w rewizji 2.
Niekompletną pierwszą próbę suplementu preferencji również zachowano z
kodem błędu i manifestem; nie wysłała żadnego requestu.

`context-build-release.py` odtwarza kolejną wersję z zamrożonego panelu oraz
prywatnych adnotacji. `--annotate-panel` zachowuje cały panel, alternatywa
bez tej flagi przygotowuje tylko osobny suplement rodzin z adnotacjami.
Każdy release wymaga nowego katalogu. Całość jest preparacją na rzeczywistych
źródłach; nowe eksperymenty modelowe i nowy koszt: 0 / 0 USD.
