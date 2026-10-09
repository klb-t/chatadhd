# Przyrost A: niezależna granica C → payer → packet → native store

Sprawdzana gałąź C: `gpt/thread7-real-2026-10-09`, SHA `69880859802267f1b38b82eeaecd6fdc5522a47a`. Baza produktu: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`. Nie zmieniono kodu ani artefaktów C/B. Historyczny audyt A i jego receipts pozostają zachowane.

## Ustalenia z wykonania

- **A2-C-001 — potwierdzona nieszczelność granicy eksportu dla rozszerzonych/wadliwych danych.** Rzeczywisty `build_public_artifact.build()` odrzuca nieznany klucz pierwszego poziomu receipt. Przepuszcza dodatkowe zagnieżdżone pole w `producer_sha256`, tekst niebędący hashem w `spec_sha256` oraz dodatkowe metadane planu/profilu. Syntetyczny canary przetrwał we wszystkich czterech wariantach. Dwa wycieki ujawnia dopiero odkodowanie źródeł base64. Raport zawiera hashe i statusy, bez canary. **Nie ustalono wycieku rzeczywistej prywatnej treści w opublikowanym snapshotcie.**
- **A2-C-002 — potwierdzone błędne powiązanie wystąpienia i wyniku.** Dwa legalne powtórzenia tego samego requestu mają ten sam hash body i różne `operation_id`. Wynik B utworzony przez `result_record(B,…)` zostaje przyjęty do slotu A, ponieważ `capture_first` sprawdza hash body, ale nie tożsamość wystąpienia. Test utrzymuje odrębne referencje rezerwacji i nie wywołuje payera.
- **Dopuszczalny mechanizm:** generyczny eksporter grafu zachowuje wszystkie zadeklarowane źródła i wersje. Base64 jest transportem, nie anonimizacją; naprawa dotyczy publicznego buildera, nie kasowania źródeł w generycznym storage. Klucze kontraktów, stany i diagnostyczne kody korzystają z uzasadnionych wyjątków R42.
- **Granica transportu:** rzeczywisty `OpenRouterTransport` dostaje dwa różne modele/temperatury i wysyła właściwe body do przechwyconego transportu. Nagłówek cache działa po jawnym wpisaniu `headers_by_method.POST`. Sama obecność `job.headers` w kolejce go nie podłącza. C opisuje to ograniczenie w `CACHE_AND_ORDER.md`; brak dispatcher/payer bridge nie został uznany za wykonaną naprawę runtime.
- **Kontrola błędów:** przechwycony transport rzucający wyjątek z syntetycznym canary zwraca stały kod `transport_failed`; treść wyjątku nie trafia do ujawnionego błędu. Nie uruchomiono sieci ani płatnego modelu.

`projection-receipt.json` rozdziela PASS reprodukcji od FAIL akceptacji produktu. Pięć porażek akceptacji reprezentuje dwa ustalenia, nie pięć niezależnych wad. Nie odtworzono ani nie dublowano pełnej autorskiej baterii testów kolejki C.

## Ocena przyrostu C

| Obszar | Klasyfikacja | Niezależne uzasadnienie |
|---|---|---|
| Reprezentacja wariantów i dokładne requesty | częściowa naprawa | Dane zmieniają rzeczywisty konsument/transport; nie powstał aplikacyjny dispatcher. |
| Trwała kolejka, referencja payera, wynik | częściowa naprawa | Wykonywalne API istnieje; A2-C-002 narusza identyfikację powtórzeń. Sam string referencji nie uwierzytelnia rezerwacji — C przyznaje to wprost. |
| Publiczny eksport | częściowa naprawa | Płaska allowlista i pierwszopoziomowa blokada działają; głębsze pola/źródła przepuszczają canary. |
| GraphPacket/schema/codec | naprawa w ograniczonej bramce reprezentacji | Opublikowany packet i jego sześć rekordów odtworzono istniejącym kodekiem. Odtworzone policy/program/protocol są dokładnie zgodne z publicznymi plikami przypiętego commita. |
| Natywne utrwalenie packetu C | częściowa naprawa | 9/9 nowych bramek native, w tym restart, zachowanie historycznej receptury i jawne odrzucenia. Kod natywny był już w main; to nowe potwierdzenie kompatybilności C, nie nowa implementacja. |
| Płatne wykonanie i uwierzytelnienie rezerwacji | nadal niezweryfikowane | Bez klucza/preflight, zgodnie z zakresem A. Brak testu na żywo nie jest równoważny błędowi ledgeru. |
| UI Basic/Advanced/Expert, adopcja ustawień | nadal niezweryfikowane | Research workflow nie wykonuje adopcji i nie ma aplikacyjnego wejścia UI; C nie twierdzi inaczej. |

**Bramka natywna 9/9 PASS** (`native-receipt.json`, rzeczywista biblioteka main `9e20f99`, SHA-256 `4ce4d06f2348c82f063893882a85ebc1c2efdec584e28f52dd90d87f64842771`): rzeczywisty zapis/odczyt/restart/replay i mutacje pakietów przeciw bibliotece z przypiętego main. Samo PASS tej bramki nie oznacza uruchomienia modeli, autentyczności rachunku, jakości wyników ani istnienia dispatchera w produkcie.

Natywnie utrwalono publiczny packet C: **15 Entity, 30 Claim, 12 Source**, potem odtworzono sześć wyników i wszystkie źródła po zamknięciu i utworzeniu nowego kontekstu. Syntetyczny pełny przepływ przez API zachował jawnie nieuwierzytelnioną referencję payera i null obserwowanego modelu. Bezpośredni C ABI odrzucił nieznane pole DTO, wersję `/999`, kolizję ID i brakującą referencję; przeliczono hashe wejścia, więc odrzucenie nie wynikało z samego uszkodzonego hasha. Druga gałąź ze starym CAS została odrzucona. Po nowej wersji spec/receptury stary wynik i jego oryginalna receptura pozostały odtwarzalne z native store po restarcie. To zamyka brakującą bramkę store dla artefaktu C.

Łącznie w tym pakiecie: **18 bramek — 13 PASS akceptacji, 5 FAIL**, zero blokad wykonania i zero płatnych wywołań. `coverage.json` podaje zakresy 10 plików (2 produktu, 8 narzędzi badań), z jawną listą nieprześledzonych wywołań; nie podnosi tego do przeglądu pełnego produktu.

## Pakiety odbioru

Szczegółowe, gotowe do odbioru pakiety znajdują się w `findings.jsonl`. Naprawy w research builderze/journalu pozostają własnością C. B powinien przed podłączeniem do aplikacji użyć tego samego zestawu akceptacyjnego i dopiąć: zweryfikowaną referencję payera, tożsamość wystąpienia, niezmienny raw reference/hash, ustawienie transportu oraz zgodę na publikację. Nie implementowano drugiego payera.

Niezamknięte wywołania: `research_programme_runner.run_stage`, `PrivateLedger` i provider preflight nie zostały uruchomione przez tę baterię; badanie nie odczytuje prywatnego checkpointu. Nie certyfikuje prywatności każdego dowolnego stringa opublikowanych materiałów względem nieprzeczytanych prywatnych źródeł. Kontrole pomocniczych pól są eksperymentem na syntetykach, nie analizą jakości modeli.
