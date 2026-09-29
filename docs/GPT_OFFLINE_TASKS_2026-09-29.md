# Zadania offline dla ChatGPT (czat podstawowy) — 2026-09-29

Dla: ChatGPT w zwykłym czacie, **bez trybu agenta, bez sieci, bez kluczy API**.
Może: czytać repo (konektor GitHub albo ZIP wgrany przez właściciela), uruchamiać
Pythona w swoim sandboxie, pisać dokumenty, schematy JSON, korpusy i skrypty.
Nie może: wywoływać OpenRouter/Jev, GitHub Actions ani niczego w sieci.

## Zasady (obowiązkowe)

1. Najpierw przeczytaj: `docs/STATE.md`, `AGENTS.md`,
   `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` (R1–R38, D1–D2),
   `docs/architecture/LOOM_CONCEPTUAL_MODEL.md` (zwłaszcza §11 v1.1),
   `docs/architecture/ACCEPTANCE_TESTS_2026-09-29.md`, swoją notatkę
   `NOTATKA_GPT_2026-09-29_KOMPILATOR_INTERAKCJI.md`.
2. **Nie czytaj** gałęzi `eval/real-holdout-key` ani `wip/worktree-agent-a342fccb481c4116c`
   (ślepy korpus walidacyjny — musi zostać nieznany metodom).
3. **Nie zmieniaj** plików, nad którymi trwa praca Claude'a (gałęzie `wip/worktree-agent-*`):
   `loom/src/catalog/**`, `loom/src/import/**`, `loom/src/archive/ingest*`,
   `loom/src/resolve/**`, `loom/src/generalize/**`, `loom/src/extract/**`,
   `loom/tools/eval/knowledge_eval.py`, `docs/exports/**`.
4. Wyniki zapisuj jako nowe pliki w ścieżkach podanych niżej. Jeśli konektor pozwala
   pisać — na gałąź `gpt/offline-2026-09-29` (od `claude/chataddhd-cpp-loom-core-IRGRN`),
   commity z `[skip ci]`. Jeśli nie — pliki do pobrania dla właściciela.
5. Oznaczaj [U]/[P]/[H] jak w swojej notatce. Liczby z mianownikami i pochodzeniem
   danych. Nic nie jest „gotowe”, dopóki nie przejdzie testów Claude'a w natywnym buildzie.
6. Kolejność = priorytet. Rób kompletne, małe przyrosty; po każdym zadaniu krótki
   wpis w `docs/research/GPT_OFFLINE_LOG_2026-09-29.md` (co zrobione, pliki, ograniczenia).

## Zadania

### T1. Kontrakty danych pięciu warstw (R26, R30, R34, R38)
JSON Schemas (draft 2020-12) + przykłady poprawne/niepoprawne:
`HistoryEvent` (wiadomość, edycja, gałąź, załącznik, tool call/result, metadane),
`ActiveTaskSpec` (cel, wymagane informacje, format, styl, zakazy, wyjątki, rozstrzygnięte
alternatywy, otwarte kwestie, mapa do wypowiedzi, wersja), `RequestSnapshot`
(z `request_provenance ∈ {recorded, reconstructed, unknown}`), `EvaluationPacket`.
Zgodne z §11 modelu pojęciowego; rozszerzają istniejące obiekty, nie tworzą równoległego magazynu.
Pliki: `docs/contracts/` + `loom/tools/contracts/validate.py` z testami (pytest/unittest, offline).

### T2. Konsolidacja refinementu (R34) — korpus + ewaluator
≥ 40 bloków doprecyzowań (PL/EN, różne domeny: pismo, kod, tekst, analiza),
każdy z ręcznie zapisanym złotym `ActiveTaskSpec` i etykietami tur: nowy wymóg /
zmiana / wyjątek / odrzucenie / doprecyzowanie zakresu / pytanie / kontekst tylko
dla wykonawcy / niezadowolenie bez treści poprawki. Przypadki trudne: konflikt
bez „ostatnia wygrywa”, emocje niosące informację, dwa równoległe zadania.
Referencyjny ewaluator w Pythonie (zachowanie wyjątków, brak powrotu odrzuconych
wariantów, brak zmyślonych poprawek). Zamroź połowę jako walidację (sha256 w README).
Pliki: `loom/tests/fixtures/eval/refinement_v1/`, `loom/tools/eval/refinement_eval.py`.

### T3. Receptury pytań do Jev — eksperymenty do przyszłego uruchomienia (R38, R37)
Z audytu `JEV_OFFLINE_POLICY_AUDIT` i dokumentu Gemini: zaprojektuj **zamrożone**
eksperymenty (bez wykonywania): (a) rozdzielenie „relacja wyrażona w źródle” vs
„sensowna wywnioskowana abstrakcja”; (b) wrażliwość na nazwy kluczy JSON (znaczące /
neutralne / bezsensowne przy tej samej treści); (c) rubryka string vs strukturalna;
(d) Choice płaski vs hierarchiczny, liczba opcji; (e) trzy rodziny pytań: relewancja
relacji, relewancja subgrafu (wieloetykietowo), potrzebna szczegółowość.
Nowy, niezależnie napisany korpus PL/EN (nie te 64 teksty), request JSON-y gotowe
do uruchomienia przez Claude'a/Jev Lab, plan analizy spisany przed wynikami.
Pliki: `loom/tests/fixtures/eval/jev_recipes_v1/`, `docs/research/JEV_RECIPES_PLAN_2026-09-29.md`.

### T4. Model modeli — schemat profilu i wypełnienie z istniejących danych (R37)
Schemat `ModelProfile` (model/wersja × operacja × domena × kształt kontekstu ×
receptura → wyniki, failure modes, kalibracja, koszt, dowody, okres ważności) oraz
retrospektywne wypełnienie z artefaktów już w repo (`docs/research/inputs/jev-*`,
`openrouter-native-*`, wyniki natywne). Profil per pytanie q01…q12, nie jedna liczba.
Szkic „instrukcji kompensującej” dla uogólniania filozofii jako testowalny produkt
(z planem ablacji, test #17). Pliki: `docs/research/MODEL_PROFILES_2026-09-29.md` +
`loom/tests/fixtures/model_profiles_v1.json`.

### T5. Workspace UI-IR ze sprzężeniami (R29–R31)
Schemat `Workspace → Container(kind…) → View(query, projection, renderer, interactions)`
i model sprzężeń parametrów (kierunek, zakres, propagacja, odłączenie, zamrożenie,
ochrona przed cyklami). Referencyjna implementacja propagacji w Pythonie z testami:
kilka grafów + tabela + karty, różne sprzężenia, panel referencyjny odłączony
(test #5); preset profilu dostawcy rozkładalny na części (test #6).
Pliki: `docs/contracts/workspace.schema.json`, `loom/tools/contracts/workspace_ref.py`.

### T6. Struktury myśli na prawdziwym tekście (R22–R24, badania z 2026-09-28)
Najsłabszy wynik: 0 struktur z 877 jednostek w trzech dokumentach repo. Offline,
w `loom/tools/structure/` (czysty Python): rozszerz parser/rejestr operacji tak, by
pokrycie na tych dokumentach wzrosło, **bez** dopasowywania do nich (najpierw
napisz nowy, niezależny zestaw zdań PL/EN i zamroź go; mierz na nim i osobno na
dokumentach repo). Raport pokrycia per rodzina operacji, z przykładami porażek.
Uruchom istniejące zestawy testów badawczych w sandboxie i zapisz wynik.
Pliki: zmiany w `loom/tools/structure/`, `docs/research/STRUCTURE_ROUND3_2026-09-29.md`.

### T7. Zasiewanie grafu i transfer strukturalny — protokół + prototyp (R36)
Na `loom/tests/fixtures/eval/synthetic_dev/ground_truth.json`: prototyp w Pythonie
proponujący brakujące elementy przez częściowe odwzorowania podgrafów
(leave-one-project-out, ukrywanie modułów/capabilities), z `predicted_at`,
operatorem, expected properties i alternatywami. Porównanie z baseline'ami
(losowy, najczęstszy element). Uczciwie: to dane deweloperskie.
Pliki: `loom/tools/seeding/`, `docs/research/SEEDING_PROTOCOL_2026-09-29.md`.

### T8. Przegląd kodu pod kątem opcji użytkownika (R21, D1)
Przeczytaj `loom/src/context/`, `loom/src/chat/`, `loom/server/`, `loom/web/src/`
(nie pliki z zasady 3) i spisz wszystkie zaszyte wybory, które powinny być danymi
lub opcjami właściciela (progi, kolejności, domyślne modele, formaty), z propozycją
klucza w paczce danych. Tylko raport, bez zmian w kodzie.
Plik: `docs/research/OPTIONS_AUDIT_2026-09-29.md`.

## Czego nie robić
Nie uruchamiać niczego z kluczem, nie symulować wyników Jev/OpenRouter jako
prawdziwych, nie zmieniać progów testów, nie przepisywać `STATE.md` (to robi Claude
po weryfikacji), nie tworzyć drugiego magazynu wiedzy obok grafu.

## Runda 2 (dopisane 2026-09-29, po T1–T8)

Te same zasady co wyżej. Gałęzie `wip/*` możesz **czytać** (nie zmieniać), chyba że napisano inaczej.

### T9. Recenzja dokumentacji formatów eksportów
Przeczytaj `docs/exports/OPENAI_ANTHROPIC_EXPORT_FORMATS.md` na gałęzi
`wip/worktree-agent-ab76c2dece93103a1` i porównaj ze swoją wiedzą o eksportach
ChatGPT (mapping/current_node, content_type: text, code, multimodal_text,
thoughts, reasoning_recap, execution_output, tether_*, file-service:// / sediment://,
sharded conversations-NNN.json, user.json, shared_conversations.json,
message_feedback.json, chat.html) i Claude (conversations.json, projects.json,
users.json, memories, content blocks thinking/tool_use/tool_result/…).
Wynik: lista braków, błędów i pytań „tylko prawdziwy eksport rozstrzygnie”.
Plik: `docs/research/EXPORT_FORMATS_REVIEW_2026-09-29.md`.

### T10. Przewodnik dla właściciela: pierwszy przebieg na prawdziwych eksportach
Krótki, praktyczny (PL): jak zrobić eksport w ChatGPT i Claude, gdzie wrzucić ZIP,
jakie polecenia `loom` uruchomić (odczytaj z `loom/cli/main.cpp`, `docs/selfhost/v2/README.md`),
co sprawdzić w wyniku (raport kompletności importu, katalog, dossier), jak zgłosić
problem bez ujawniania prywatnych treści. Plik: `docs/OWNER_REAL_EXPORT_GUIDE.md`.

### T11. Rodzaj projektu `legal_case` — szczegółowa specyfikacja danych (R9)
Na bazie `loom/data/project_kinds/legal_case.json` i modelu pojęciowego:
dziedzinowe rodzaje (postępowania, instytucje, strony, dowody, podstawy prawne,
terminy, pisma), reguły terminów jako dane (doręczenie + N dni, dni wolne,
reguły liczenia per kodeks — jako szablon z polami, bez twierdzeń prawnych
podanych jako pewne), weryfikacja cytatów, łańcuch dowodowy. Tylko projekt
danych + przykłady fikcyjne. Plik: `docs/research/LEGAL_CASE_KIND_2026-09-29.md`.

### T12. Model zagrożeń i tryb paranoid (R19)
Katalog napastników z wymaganym wysiłkiem (kompromitacja API, przymus prawny,
spyware klasy Pegasus, dostęp fizyczny, kanały akustyczne/optyczne), strefy
„kontrolujemy / nie kontrolujemy / nie wiemy”, profil prywatności każdej ścieżki
danych Looma (lokalna baza, blob store, dostawcy LLM, OCR/ASR, GitHub sync),
propozycja schematu danych i co pokazuje tryb paranoid. Plik:
`docs/research/THREAT_MODEL_2026-09-29.md`.

### T13. Kolejka dla Claude'a
Na koniec każdej sesji dopisz do `docs/research/GPT_OFFLINE_LOG_2026-09-29.md`
sekcję „Do sprawdzenia przez Claude'a w natywnym buildzie” — konkretne pliki,
komendy i oczekiwane wyniki, żeby weryfikacja była tania.

## Status i wskazówki (aktualizacja 2026-09-29 wieczór)

- Scalone do `claude/chataddhd-cpp-loom-core-IRGRN`: **T1** (74/74 testów po dopisaniu
  `rfc3339-validator` do requirements — bez niego format `date-time` nie jest sprawdzany),
  **T2** (81/81), **T3** (wersja z `4ea4e74`: 188 body + 16 warunkowych).
- **T4 nie dotarło** — nie ma go na gałęzi ani w plikach. Jeśli konektor nie pozwala pisać,
  oddaj właścicielowi ZIP z `increment.patch` + `repo_increment/` (jak dla T1–T2);
  jeśli T3 w paczce jest nowsze niż `4ea4e74` (README mówi o 208 body), dołącz je też.
- Każdy nowy przyrost zaczynaj od **aktualnego czubka** `claude/chataddhd-cpp-loom-core-IRGRN`
  (nie od `gpt/offline-2026-09-29`), żeby łatki nakładały się bez konfliktów.
- Kolejne: T5 (workspace), T6 (struktury myśli), T7 (zasiewanie), T8 (audyt opcji),
  T9 (recenzja formatów eksportów — plik jest na `wip/worktree-agent-ab76c2dece93103a1`,
  wkrótce `wip/export-finish`), T10 (przewodnik), T11 (legal_case), T12 (zagrożenia), T13.
- Nowe dane dla T6/T7: recall katalogu po rundzie 2 to 21/45 (kanały TF-IDF: rdzenie słów +
  4-gramy znakowe); niezłapane rozmowy to dobry materiał do analizy „dlaczego semantyka nie trafiła”
  (`LOOM_CATALOG_EVAL_VERBOSE=1 loom/build/dev/loom_tests --test-suite=catalog_eval` wypisuje je z cechami).
