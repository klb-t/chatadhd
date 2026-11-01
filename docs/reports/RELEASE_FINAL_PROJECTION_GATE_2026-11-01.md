# Finalny immutable kandydat — bramka projekcji PDF, 2026-11-01

**PASS zakresowej, nowo wykonanej próby na finalnej bibliotece pakietu.**
Baza źródeł `c6111de0176ce1246a6efdb9074a498cf3701922`; runtime
`e27ce9ff0076c287963a588d942d68e7bebaca3f`, web
`8866e1f2f5ed4fe1cb0f21b44b151c78b986f1d2`. Bez zmiany produktu, buildu,
nowego pakietu lub wdrożenia. Wcześniejsze próby na b732/c41/029 pozostają
historyczne; poniższy wynik pochodzi z rzeczywistego uruchomienia finalnej 7004.

## Tożsamość wykonania

| Obiekt | SHA256 |
|---|---|
| Finalny archive | `18d4a5b4fc5be7785e69fcfde4eebe1bcfbea04989c65cf5a312363afe962839` |
| Manifest pakietu | `012a99a26a0812e204fead96a6b3b6546cd9881b53a70291602309b6949a14bf` |
| Faktycznie ładowana libloom.so | `7004d54bd106e281acddd5d352e8ab0e2ced0d710245f8a17c003ccb336e610a` |
| Server, zweryfikowany statycznie | `cc0f65f19cfaf609706064ae068f09073016aba257ce80509d1ae5431eb8ac90` |
| Poprzedni archive | `f9767a0c20e628f942f8abda9b24daebab5fe9fb23666ae70324f132840754fb` |
| Poprzednia libloom.so | `0c2052d8bd31214876a5690f05394db0e4f4ca862c7d882dfe7e6f6f2202e4c5` |
| PDF syntetyczny, zachowany oryginał | `491ed5d453e5e370df3099c3f0850f7d29f9a7934b554408fad2eba56095f171` |
| Finalny PDF projection.py | `6171d3a56b12cb96029171d084410de309a32bba5f9e8bcd5b8b1a2f5c7648d6` |
| PDF worker.py (obie wersje) | `f1ed06fdd3277243bfec205664ac533558880a5f8b8347824691fe66f5e5f6e7` |
| PDF profile.json (obie wersje) | `3e464c0419b13061db648a40f3326472810abd63fb399dccabbd806e203edcf0` |
| Interpreter Python 3.14.4 | `be9a2a5eada8c89c1c399fdfb8397179e877c20d8db0739c802f726d4d0e69fd` |

Archiwum: `.integration-continuous-graph-ui-20261101/chatadhd-server-linux-x64-8866e1f2-release-continuous-graph-20261101.tar.gz`.
Sprawdzono SHA archiwów, manifestów i każdy payload względem istniejących stagingów:
106 finalnych i 34 poprzednika. Helper/profil/interpreter przypięte i użyte przez
domyślną ścieżkę pakietową; bez pomocniczego readera z checkoutu. Pełne ścieżki,
hashe starego helpera, runtime.json i źródeł próby: prywatny `bindings.json`.

## Nowy wynik — tylko brakujący łańcuch

Wykonanie 09:40:16–09:41:56 UTC, wyłącznie nowe syntetyczne katalogi. Reużyto
istniejących Native/verify_native i cold_snapshot; prywatny probe wydziela ten
zakres. Każda faza zamyka runtime; proces kończy się i jest odczekany przed kopią.
Landlock, jawne ścieżki, env whitelist, brak workerów/providerów i ruchu sieciowego.

1. Poprzednik 0c2052 utworzył oryginał PDF, starą projekcję i czytelne wystąpienia;
   zapisał stary globalny wybór. Zatrzymany stan skopiowano na zimno.
2. Finalna 7004 otworzyła odtworzoną kopię: zapisany receipt integralny, replay
   niezgodny (`refused`). Fałszywy hash/wybór/receipt oraz naruszony zapis ekstrakcji
   odrzucone przed zmianą liczników grafu. Jawna reprojekcja utworzyła nowe Source,
   projection i occurrence IDs; stare węzły, receipts i bloby zachowane.
   Powtórzone żądanie reużyło rezultat bez nowych węzłów.
3. Mapowanie obu starych wystąpień zwróciło `decision_required`, bez kandydatów;
   **nie dowodzi automatycznej równoważności fragmentów**. Selektor pozostał stary.
   Dopiero jawny wybór nowego ID zmienił ustawienie. Rzeczywisty odczyt nowych
   części i ponowne otwarcie finalną 7004 zachowały treść/wybór; check:
   `saved_integrity=verified`, `current_replay=compatible`.
   Osobny pusty check zwrócił `not_exercised`, nie pozorny PASS replay.
4. Na oddzielnej kopii stanu po zmianie poprzednik odmówił nowego odczytu:
   `conflict/pdf_structure_invalid_receipt`; stare części i oryginał nadal czytelne.
   Nie przepisał selektora. To **brak zgodności nowej projekcji wstecz**, nie
   lossless rollback in-place.
5. Odtworzenie kopii sprzed zmiany do kolejnego nowego rootu i otwarcie poprzednikiem:
   stary wybór, oryginał, config i odczyty zgodne. Nowe projection/source/occurrence
   IDs nie istnieją (`not_found/text_source_occurrence_missing`). Obie DB:
   integrity/FK OK, schema/counts równe stanowi sprzed zmiany (nodes 11→32→11,
   sources 2→4→2; user_version 0, schema_version 36 i 8).

**RPO: restore traci wszystkie zapisy po wykonaniu kopii.** Nowa projekcja i nowy
wybór są utracone. Nie jest to migracja selektorów bez decyzji użytkownika.
Pełna zgodność drzewa metadanych przed startem odtworzonego runtime została
sprawdzona: UID/GID/mode, struktura, dostępne xattrs i hashe. Opaque config/key
witness jest wyłącznie losowym, nieoperacyjnym materiałem syntetycznym; zachowany
bajtowo po otwarciu. Nie badano cudzych UID/GID/ACL/MAC ani kluczy produkcyjnych.

## Zachowana odmowa i ograniczenia

Pierwsze restore odmówiło `preflight_or_snapshot_changed`: pomiar wielkości
harnessu zmienił atime katalogów snapshotu. Potwierdzono wyłącznie różnice atime;
nie zmieniono ani nie osłabiono narzędzia produktu. Prywatny pomiar poprawiono na
istniejący ColdTree/O_NOATIME; nowa kopia i nowy root odtworzenia, bez ponownego
uruchamiania generatora lub starej fixture. Nieudany snapshot, runner i logi
zachowane obok wyniku. Początkowa odmowa nie jest przemalowana na PASS.

Nowy dowód dotyczy API native rzeczywistej pakietowej biblioteki, nie nowego testu
HTTP serwera, auth/accounts lub browsera. GUI/P0/media/pełne repo i testy autorów
niepowtarzane; realvision/model/API, produkcyjny restore i deploy NOT RUN.
Pozostałe zaakceptowane dowody UI/HTTP pozostają historyczne. Nie zmieniono
pakietów, cache ani źródeł runtime. Wszystkie procesy próby zakończone.

Prywatne dowody: `.integration-release-final-projection-gate-20261101/`:
`gate-result.json`, `evidence-manifest.json`, `commands.json` (argv/env/UTC/exit),
`bindings.json`, `restore-proof.json`, `retained-atime-failure.json`, probe,
runner, logi i receipts. Manifest kopii sprzed zmiany i odtworzenia:
`90e46e141a70955b489bc3383794edf97296d408bf1c357c53c05122563240ab`.
Dane próby ok. 5,6 MB, limit 12 MiB zachowany; wolne >1 GiB.

Next: operacyjny spójny backup/restore preflight i jawna akceptacja RPO oraz
**decyzja właściciela o aktywacji konkretnego pakietu**. Ta bramka nie udziela zgody
na wdrożenie i nie zastępuje równoległego odbioru operatora na jego danych.
