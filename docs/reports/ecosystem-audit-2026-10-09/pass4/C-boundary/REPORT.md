# PASS4 — C: niezależny odbiór granic

Przypięto C4 `21ffc5e0769d53e4dbc507a2c57d8385e638b3f1` względem C2 `b9b503f62bb8e8e00c94ab7401e1cd9579129af3`. Natywny odbiór wykonał B4 `387afe08587f179d47c013a2ea518ff4b68e36bc`. Wyniki końcowe: **28 bramek: 22 PASS, 6 FAIL**, przy czym **7 PASS to reprodukcje błędu**, a nie sukces produktu. Kategorie: 1 potwierdzenie naprawy, 9 testów kontraktu, 5 integracyjnych, 7 reprodukcji oraz 6 nieprzechodzących kryteriów akceptacji. Odrębnie, wewnątrz jednej bramki naprawy, wykonano wskazane przez właściciela **10/10 testów credential autora**, bez pominięć. Nie dodajemy ich ponownie do 28.

Autorytatywne receipts: `receipt-verified.json` oraz `registry-receipt-final.json`. Wcześniejsze próby harnessu są zachowane, z wyjaśnieniem w `harness-history.json`. Brak płatnych wywołań, rzeczywistej wysyłki i odczytu prywatnych archiwów lub kluczy. Zastosowano publiczne dane C i syntetyczne fixtures mechaniki; nie są one rozmowami użytkownika ani pomiarem jakości modelu.

## Wyniki odbioru

| Zakres | Wynik i rzeczywisty konsument |
|---|---|
| `test_credential_handoff_v2`, poprawka `a6481d11` | **Naprawione** w badanym zakresie. Rzeczywiste `TMPDIR`, wyzerowany cache `tempfile`, 10/10 testów z Python crypto i Node WebCrypto. Stary setup jawnie żądający `/tmp` odtworzono przez skierowanie tylko tego żądania do osobnego syntetycznego root Git; prawdziwy guard odrzucił lokalizację. Niezależne `.git` jako katalog, plik oraz symlink nadal odrzucane przed zapisaniem materiału. Nie zmieniono metadanych platformy ani guardu. |
| Nowe prepare → queue → payer manifest | **Częściowo**. Dokładne body, operation ID między kolejką i manifestem, kolejność oraz jawne `prepared/no dispatch/no reservation` przechodzą. **A4-C-001:** dwie poprawnie zamrożone wersje tej samej rodziny mogą dać inną wersję źródła w spec/kolejce i w analysis-binding. |
| Niezmienność planu | **A4-C-002, nadal wadliwe.** Canonical reload tego samego planu przestawia klucze obiektu wariantu. Connector generuje z kolejności kluczy listę `$match.fields`, więc zmienia digest spec i operation ID. Prawdziwa Queue słusznie odrzuca re-add `queue_spec_identity_reused`, pozostawiając dwa pierwotne joby. Nie wykonano duplikatu ani wywołania modelu. |
| Nowy final public builder | **Nadal wadliwe A2-C-001/004** na nowym wejściu. Syntetyczny marker pomocniczego protokołu/projekcji trafia do artifactu i odzyskanych źródeł; marker odrzuconej prezentacji trafia do rzeczywistego renderowania nieprzechwyconego błędu. Brak dowodu ujawnienia rzeczywistej prywatnej treści w opublikowanym artefakcie C. |
| Finalny packet C4 → B4 native store | **PASS kompatybilności i trwałości**: 16 entities, 31 claims, 12 sources, 7 odzyskanych rekordów. Rzeczywisty zapis, zamknięcie kontekstu, nowy Runtime, read/replay; identyczny packet, source bytes i wyniki, brak driftu. Zapis nie ustala prawdziwości wyniku. |
| Store → `MethodRegistry.load(profile, receipt_ids)` | **PASS rzeczywistego połączenia.** Istniejąca ścieżka przyjmuje finalny packet przez receipt i czyta rekordy z KnowledgeStore, bez normalizacji przez caller. To istniejący mechanizm, nie drugi magazyn ani nowy resolver. |
| Surowy profile → `MethodRegistry.load` | **Nadal wadliwe A3-DISC-001**: te same DTO są odrzucane przez porównanie zależne od serializacji. Diagnostyczny roundtrip przez rzeczywiste DTO pozwala na load; nie jest poprawką produktu. Oddzielny receipt-backed PASS zawęża problem do konkretnego wejścia, nie zamyka surowego wejścia. |
| Dostępność wykonawcy C | **Poprawny brak wykonawcy**, nie usterka: registry zwraca `execution_capability_not_advertised`. Zapis metody/dowodu nie instaluje kodu ani nie nadaje zgody; transporty natywne 0. Bramka odrzucania nieznanych pól DTO nadal PASS. |
| Stare A2-C-002 i A2-C-003 | Konsumenci bajtowo identyczni z C2; wcześniejsze dowody pozostają aktualne jako carry-forward. Nie powtarzano zakończonego testowania starej kolejki i nie przypisano C napraw, których nie zawiera diff. |

## Rozstrzygnięte transformacje i oracle

Zmiana kolejności kluczy/whitespace w wrapperach manifestu, indexu i panelu, przy zachowaniu dokładnych request bytes, zachowała operation ID, digest body i ordinale. Relokacja **tych samych** zamrożonych wejść przy tym samym planie także przeszła. Oddzielna zmiana serializacji **planu** ujawniła A4-C-002. Nie przestawiano wiadomości ani list wariantów, nie zakładano ogólnej zamienności `1` i `1.0`.

Źródłem oracle jest kontrakt JSON obiektu oraz wymaganie PASS4 niezmienności znaczenia. W badanym `render.$match` lista pól służy koniunkcji równości (`all`), a nie priorytetowi. To connector przekształca nieistotną kolejność kluczy w znaczącą dla hasha kolejność listy. Poprawka nie powinna normalizować wszystkich list ani osłabiać strażnika tożsamości kolejki. Historyczne specyfikacje/identyfikatory trzeba zachować lub jawnie wersjonować migrację producenta.

Wymuszono przerwanie rzeczywistego zapisu po utworzeniu Queue, przed manifestem payera. Po otwarciu SQLite joby pozostawały `prepared`, bez wyników i dispatch; kompletny receipt nie powstał. Ponowne użycie tego outputu daje jawne `new_private_output_required`. To poprawne odróżnienie przerwania od wykonania; **nie jest dowodem resume in-place**, którego connector nie udostępnia.

Pierwszy test nazwany relokacją dodatkowo odczytał kanoniczny `PLAN.json`, dlatego mieszał dwie transformacje. Ten receipt zachowano. Końcowe testy izolują oba warunki; nie raportujemy wadliwego traktowania samej lokalizacji.

## Zakres i ograniczenia

Diff C2→C4 obejmuje **32 pliki**, w tym **5 nowych wykonawczych plików Python bez testów** i 3 pliki testowe. Nazwane zakresy przejrzano w 3/5 nowych plików wykonawczych; 2 wykonano na rzeczywistych konsumentach. `check_public_projection.py` oceniono źródłowo jako ograniczony scanner skończonych canaries, nie jako dowód nieobecności dowolnych sekretów. Nie czytano prywatnych źródeł autora, aby go uruchomić. `seal_checkpoint.py` i `retrieval-task-validate.py` pozostają poza tym pakietem semantycznym. Pełny wykaz, klasy plików, funkcje, hashe i nieprześledzone wywołania: `coverage.json`.

Te skrypty badawcze nie zwiększają mianownika ani licznika przejrzanych aktywnych plików produktu. Native MethodRegistry to wcześniej nazwany zakres z nowym wejściem i nową próbą granicy. B4 połączono z C4 przez istniejące API/formaty; nie tworzono merge produktu, nie było konfliktów worktree, nie zapisano produktu w gałęzi A. Build B4 ma 9 przypiętych obiektów przed niezmienionym bazowym archive; receipt zapisuje manifest/hashe. Nie deklarujemy pełnego ponownego CMake/CTest ani aplikacyjnego UI E2E.

R15/R41 i konkretne wymagania PASS4 uzasadniają zgodność wersji/pochodzenia, a polecenie ochrony publicznej projekcji obejmuje również błędy i pomocnicze role. Nie rozszerzamy intencji właściciela. R42 nie uzasadnia usuwania wszystkich stringów: klucze/schema, SHA/format, machine errors, bootstrap i diagnostyka mają właściwe wyjątki; diagnostyka nie nadaje uprawnień do ujawnienia wejścia. Nie wymaga się osadzania wszystkich bajtów w grafie ani traktowania nieobecnego executora jako błędu.

## Pakiety i wznowienie

`findings.jsonl`, `packages-for-owners.json`, `acceptance-queue.json` i `test-index.json` zawierają ID → małą reprodukcję → oczekiwany wynik → konsumentów → komendę → migrację → zależności. A4-C-001/002 oraz granica publiczna należą do narzędzi C; A3-DISC-001 do produktu B. Publikacja tych plików nie oznacza wysłania wiadomości sesjom B/C.

Następna konkretna czynność po zmianie C: uruchomić `run.py` na nowym checkout/SHA, zachować dodatnie kontrole transportu/tożsamości/relokacji i domknąć pięć acceptance FAIL. Po zmianie B: `run_registry.py` z tym samym packetem C4 oraz rzeczywistym buildem B; zachować receipt-backed PASS, poprawne unavailable i odrzucanie nieznanych pól, domknąć surowe wejście. Bez nowych SHA nie powtarzać tych przebiegów.
