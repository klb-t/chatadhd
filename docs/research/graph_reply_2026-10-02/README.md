# Odpowiedź API jako graf — ChatADHD / Loom

2026-10-02. Izolowany eksperyment na bazie `af3c81a5ddce70bdf4346a2c8c808ce6cc97b4d0`.
Implementacja opublikowana: `a9110e1561697d50826b03bccd7a578575d1b560` (identyczne źródła lokalnego checkpointu `7634446`). Nie jest to zmiana produkcyjnego czatu ani przejęcie integracji projektu.

## Ocena pomysłu

To użyteczny kierunek: **w wywołaniu API żądamy odpowiedzi będącej grafem**, z dotychczasowym grafem w kontekście. Model generuje całą wypowiedź i jej adresowalne części jednocześnie. ChatADHD może wyświetlać zwykłą rozmowę, a Loom zachowuje strukturę, odwołania i pochodzenie każdego fragmentu.

Najważniejszy potencjalny zysk to możliwość wskazania konkretnego fragmentu: rozwiń tę tezę, popraw tylko ten argument, wróć do tego pytania, kontynuuj od tej części. To propozycja zachowania interfejsu, nie funkcja UI wdrożona w tym eksperymencie. Nie wykazaliśmy jeszcze poprawy jakości odpowiedzi, obsługi ADHD ani oszczędności tokenów.

Jest też istotne ograniczenie: JSON Schema może kontrolować kształt danych, ale sens relacji pozostaje propozycją modelu. Poprawny `supports` nie dowodzi poprawnego wynikania; odwołanie do istniejącego ID nie dowodzi, że wskazano właściwą wypowiedź. W pilotażu wystąpił właśnie błąd zbyt zgrubnego przypisania źródeł.

## Co zostało zrobione

Kod znajduje się w `loom/tools/structure/graph_reply_v1/`:

| Plik | Funkcja |
|---|---|
| `prompt.py` | Buduje rzeczywisty payload Chat Completions z kontekstem grafu, instrukcją i `response_format: json_schema`, `strict: true`. |
| `reply.py` | Sprawdza graf odpowiedzi i deterministycznie przepisuje go do istniejącego `GraphPacketDiff`. |
| `transport.py` | Odczytuje zachowane bajty odpowiedzi HTTP; obsługuje odmowę, obcięcie, błędny JSON i wynik zamiast oczekiwanego grafu. |
| `experiment.py` | Ocenia pierwsze próbki, zachowanie poprzednich rekordów i wielkość reprezentacji. |
| `test_reply.py`, `test_api.py` | 37 testów kontraktu, API, Unicode, historii, konkurujących przyrostów i błędów transportu. |

`json_schema`, `json_object` i samo polecenie w prompcie są osobnymi ustawieniami. Nie ma cichego obniżenia wymagań po błędzie. Model, dostawca i parametry generacji należą do wywołującego. W wariancie OpenRouter ustawiane jest `provider.require_parameters: true`.

Źródła techniczne sprawdzone 2026-10-02:
[OpenRouter Structured Outputs](https://openrouter.ai/docs/guides/features/structured-outputs) oraz
[OpenAI Structured Outputs](https://developers.openai.com/api/docs/guides/structured-outputs).
Obsługa i siła wymuszania schematu zależą od endpointu. Przyszły test konkretnego modelu musi sprawdzić rzeczywiste wykonanie; sama dokumentacja i przygotowany payload tego nie potwierdzają.

## Format kompatybilny z Loom

Model zwraca niewielki graf, a kod wykonuje **graf → graf**, bez kolejnego wywołania ekstrakcyjnego. Pełne native Entity/Claim/Observation są tworzone przez deterministyczny adapter. Nie wymagamy od modelu powtarzania całej struktury Assessment, obliczania hashy ani nadawania skalibrowanej pewności.

Odpowiedź główna i każda część mają dokładnie ten sam schemat węzła:

```json
{
  "id": "r",
  "text": null,
  "role": "response",
  "children": ["p1", "p2"]
}
```

Cały wynik zawiera `schema`, `base_packet_sha256`, `response_id`, `nodes`, `links`.
`children` określa kolejność czytania i zawieranie części; `links` opisuje dodatkowe relacje, także pomiędzy nowymi fragmentami. Ich etykiety są otwarte. Graf relacji semantycznych może mieć cykle; ograniczenie drzewa dotyczy tylko struktury składania tekstu.

W `loom.graph_reply/1` węzeł główny zawiera pełny tekst, a części jego dokładne fragmenty. Każda lista dzieci musi składać się dokładnie do tekstu rodzica, z zachowaniem separatorów. Ten wariant sprawdza spójność, ale powtarza treść.

W `loom.graph_reply/2` węzeł złożony może mieć `text: null`. Tekst powstaje przez połączenie dzieci w podanej kolejności. Liście zawierają tekst tylko raz. Format węzła jest taki sam na każdym poziomie. Jest to preferowany kandydat do następnego pilotażu API; domyślny wariant prototypu pozostaje V1, żeby zachować pierwotny eksperyment.

Kompletny payload ze schematem i syntetycznym kontekstem: [request_example_v2.json](request_example_v2.json).
Pole `model` jest miejscem na wybór wywołującego, nie deklaracją przetestowanego endpointu.

Przykład użycia z katalogu repozytorium:

```python
from loom.tools.structure.graph_reply_v1.prompt import build_request
from loom.tools.structure.graph_reply_v1.transport import consume_completion

body = build_request(
    selected_packet, current_user_text,
    model=selected_model,
    wire_schema="loom.graph_reply/2",
    api="openrouter",
)
# body jest payloadem POST /chat/completions; wysłanie i rozliczenie
# pozostają w istniejącym runnerze dostawcy, z jego bieżącym budżetem.
result = consume_completion(
    selected_packet, saved_first_http_bytes,
    request_id=request_id, turn_id=turn_id, requested_model=selected_model,
)
```

## Inwarianty i znaczenie danych

| Reguła | Realizacja |
|---|---|
| Model nie regeneruje starego grafu. | Zwraca nowe węzły i linki; kompilator tworzy wyłącznie dodatki. |
| Format nie zmienia stanowiska użytkownika. | Role i linki modelu pozostają danymi draft; nie są automatycznie decyzjami użytkownika. |
| Tekst i podział muszą być zgodne. | Dokładna partycja lub kompozycja; spany UTF-8 liczy kod, również dla powtórzonych identycznych fragmentów. |
| Odwołania mają określoną bazę. | Weryfikacja istniejących entity IDs i hash snapshotu; stare/konkurujące przyrosty nie są przyjmowane do innej bazy. |
| Nieudana odpowiedź też jest wynikiem. | Zachowane pierwsze bajty i przyczyna odrzucenia, bez automatycznej naprawy albo podmiany próbki. |
| Przyjęcie struktury nie ustala prawdziwości. | Wyraźne `content_verification: unverified`; confidence=1 dotyczy tylko sprawdzonej tożsamości/struktury. |

W bazowej projekcji są osobne źródła: dosłowny wire JSON oraz tekst wyświetlanej odpowiedzi. To wirtualne, content-addressed człony źródłowe przechowywane w projekcji. Nie deklarują istniejących plików poza nią.

Kompilacja dodaje rekordy i relacje `has_response`, `contains_part`, opcjonalnie `reply_to_turn`. Znaczeniowe `links` modelu są zachowane jako drafty w atrybutach węzłów, z pochodzeniem i rozwiązanymi IDs; nie są udawanymi kanonicznymi twierdzeniami.

**Transformacja:** wejście to zapisane bajty grafu odpowiedzi; wyjście to istniejący GraphPacketDiff. Zachowane są wszystkie bajty pierwszej odpowiedzi, dosłowny tekst, kolejność, struktura, lokalne IDs i proponowane linki. Dodane są deterministyczne IDs Loom, spany, hashe oraz metadane hosta. Nie dodaje się nowych interpretacji semantycznych. Sam widok tekstowy pomija strukturę, ale źródłowy graf pozostaje dostępny. Replay kompilacji sprawdza dane zamiast ufać wyłącznie hashom.

## Wyniki i granice dowodu

Osiem syntetycznych przypadków DEV obejmuje korektę, rozgałęzienia, kontekst bez zobowiązania, negację warunku, cytat bez poparcia, nierozstrzygnięty konflikt, powtórzony Unicode i krótką odpowiedź bez wymuszonych relacji. Podano gotowe adnotacje zakresu, statusu i pochodzenia. Nie testowano ich ekstrakcji z surowego archiwum.

Dwa osobne zadania modeli w tej sesji przygotowały po jednej pierwszej odpowiedzi do każdego przypadku. Autorzy odpowiedzi dostali żądania bez kryteriów oceny. Grafy zostały przez te zadania zapisane przy użyciu serializacji JSON. To **pilotaż treści i struktury**, nie pomiar skuteczności constrained decoding ani surowej składni wyjścia konkretnego API. Recenzent był osobnym zadaniem modelu z tej samej rodziny środowiska; nie człowiekiem ani niezależnie wybranym modelem/dostawcą.

| Pomiar | Kontekst grafowy | Te same dane liniowo |
|---|---:|---:|
| Pierwsze próbki zgodne z kontraktem | 8/8 | 8/8 |
| Poprzednie rekordy, kolejność i provenance zachowane | 8/8 | 8/8 |
| Kryteria semantyczne | 23/24 | 23/24 |
| Przypadki spełniające wszystkie kryteria | 7/8 | 7/8 |

**Brak wykazanej przewagi grafowego wejścia.** Oba warianty zachowały właściwą treść odpowiedzi. Ten sam ścisły test źródeł nie przeszedł: fragment łączył cytat i stanowisko użytkownika, a odwołania nie rozdzielały dostatecznie ich źródeł. Źródło samego cytatu zawierało już ocenę „przykład złej praktyki”, więc wynik należy rozumieć jako problem kompletności/granularności przypisania według zamrożonej rubryki, nie dowód błędnego przypisania poparcia.

Przepisanie **tych samych** zapisanych grafów V1 do wariantu V2 zachowało tekst, spany i poprzedni graf w 16/16 przypadków. Znormalizowany JSON zmalał:

| Zbiór odpowiedzi | V1 | V2 | Różnica |
|---|---:|---:|---:|
| Wejście grafowe | 6247 B | 5288 B | −15,35% |
| Wejście liniowe | 6651 B | 5539 B | −16,72% |

To deterministyczne porównanie reprezentacji. Nie wygenerowano nowych odpowiedzi modelu V2, nie zmierzono tokenów, opóźnienia ani ceny. Bogaty native diff był znacznie większy niż wire modelu: 118296 B i 122929 B dla odpowiednich ośmiopróbkowych zbiorów. Te metadane tworzy host; wysyłanie całego native diff jako tekstu odpowiedzi modelu nie byłoby wykazaną oszczędnością.

Zewnętrzne wywołania API: **0**. W tej sesji nie był skonfigurowany klucz. Gotowy payload i testy transportu nie są dowodem, że dany endpoint przyjmie schemat lub poprawnie odpowie. Nie poniesiono nowych kosztów API.

## Weryfikacja i odtwarzanie

Nowe testy: **37/37**. Cała seria `research.structure` uruchomiona przez discovery: **880/880**, 33,493 s. To testy Pythona/kontraktów; nie nowy wynik pełnego natywnego CTest. Pierwsza seria miała błąd importu nowego pakietu w discovery; log zachowano, import poprawiono i przeprowadzono pełną serię ponownie.

```bash
python3 -m unittest loom.tools.structure.graph_reply_v1.test_reply loom.tools.structure.graph_reply_v1.test_api
python3 -m unittest discover -s loom/tools/structure -p 'test_*.py'
python3 docs/research/graph_reply_2026-10-02/reproduce.py --output /tmp/graph-reply-new-replay
```

Ostatni katalog musi być nowy. Replay nie wywołuje modeli, nie naprawia odpowiedzi i nie zapisuje canonical store. Pierwotne odpowiedzi, przygotowane żądania, kryteria oraz oceny pozostają osobnymi plikami. Aktualny replay ze źródłami tego commitu jest w `verification/final-replay/`; pierwotne wyniki pozostają historycznymi checkpointami.

## Następny zakres implementacji

Potrzebny jest mały rzeczywisty pilotaż na wybranym endpointcie ze sprawdzoną obsługą `json_schema`, potem adapter do natywnego ChatEngine. Przed włączeniem do normalnej rozmowy trzeba powiązać odpowiedź z konkretną rozmową, istniejącą turą użytkownika i snapshotem wysłanego żądania, oraz ustalić obsługę przerwanego streamingu.

Obecny prototyp przechowuje próbę odpowiedzi i hostowy `parent_turn_id`; tożsamość tury jest ograniczona parą `(request_id, turn_id)`. Nie sprawdza jeszcze `conversation_id`, pełnej naprzemiennej chronologii user→assistant ani atomowego zapisu wieloklientowego do native store. GraphPacket nadal jest projekcją, a nie drugim magazynem wiedzy. Nie ma migracji bazy ani zmiany ABI.
