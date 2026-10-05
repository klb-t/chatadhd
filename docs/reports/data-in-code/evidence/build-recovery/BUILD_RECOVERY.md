# Wątek 11 — zachowane negatywne buildy i odzyskiwanie

To jest pełny dowód napotkanych błędów narzędzi/buildów, a nie wynik końcowego `ctest`. Pierwsze18 logów/binarnych migawek zachowano bez skracania (259329 bajtów w chwili przechwycenia). Później dodano2 pełne pliki negatywnego ctest; receipt rejestruje wszystkie20 snapshots i ich hashe. `receipt.json` zapisuje ich SHA256, czas przechwycenia oraz wszystkie 6 rzeczywistych poleceń compiler/linker występujących po `FAILED:`. Puste logi również zachowano. Najnowszy `current-build-sourcefreeze.log` przechwycono podczas pracy: nie należy uznawać jego końca za pomyślne zakończenie.

| Dowód | Obserwacja i granice wniosku | Odzyskiwanie |
|---|---|---|
| `baseline-build.log` | `cc1plus` został zabity podczas kompilacji `capi_knowledge.cpp`. Log nie zawiera kernelowego potwierdzenia OOM; brak zasobów jest hipotezą, nie osobnym pomiarem. | Właściciel potwierdził debug build z `-j3`, następnie retry z `-j2`. Dokładne historyczne top-level argumenty nie są zachowane. |
| `baseline-build-retry.log` | Linker zgłosił, że członek `capi_graph_packet_store.cpp.o` w `libloom_core.a` nie jest obiektem. Zachowano pełne polecenie i diagnostic. Nie przechwycono pierwotnego członka archiwum, więc sam log nie dowodzi jego długości0. | `baseline-build-final.log` pokazuje przebudowę tego TU, archiwizację i udany link rzeczywistego `libloom.so.0.1.0`, przed późniejszym błędem kompilacji testów. |
| `baseline-build-final.log` | Test `test_chat_active_task_history_aba.cpp` nie mógł zapisać assembly: `No space left on device`. Udany wcześniejszy link biblioteki nie oznacza ukończenia baseline testów. | Właściciel usunął wyłącznie baseline `.o` (raportowane około868MB), zachowując `.a` i faktyczną `.so`. Nie udajemy pomiaru odzyskanych bajtów. |
| `current-build.log` | `candidate_graph.cpp` nie mógł zapisać assembly z `No space left on device`. | Retry jest zachowany jako osobny pełny log. Nie zmieniano progów ani testów. |
| `current-build-complete.log` | Link testów zgłasza brak `KnowledgeConfig::builtin_priors_default/builtin_llm_default`. | Po zmianach źródeł wykonano pełny refresh. Rozbieżne/stare obiekty są wyjaśnieniem organizacyjnym, nie nowym błędem asercji. Zachowano pełne symbole i linker command. |
| `current-build-refresh.log` | Link serwera zgłasza brak metod `App`. Właściciel osobno zaobserwował `server/CMakeFiles/loom-server.dir/app.cpp.o` o długości0 i usunął wyłącznie ten plik. | Retry serwera z `-j1`. Oryginalny plik0 usunięto przed przechwyceniem; nie przypisujemy syntetycznej kopii do oryginalnego błędu. |
| `ninja-deps-before-recompact.bin`, `ninja-recompact.log` | Zachowano surowy `.ninja_deps`151244B i pełne ostrzeżenie `premature end of file; recovering`. | Właściciel wykonał dokładne polecenie `ninja -t recompact`, podane niżej. Suchy plan po odzyskaniu jest zachowany, lecz nie dowodzi wyniku pełnego buildu. |

Dokładne polecenia właściciela, zgłoszone w tej sesji (atrybucja w JSON):

```sh
/root/.local/bin/ninja -C /workspace/scratch/72fc60ad1cc5/current-build -t recompact > /workspace/scratch/72fc60ad1cc5/ninja-recompact.log 2>&1
/root/.local/bin/cmake --build /workspace/scratch/72fc60ad1cc5/current-build -j2 > /workspace/scratch/72fc60ad1cc5/current-build-sourcefreeze.log 2>&1
```

Pozostałe historyczne top-level polecenia podane opisowo są rekonstrukcjami. Pełne rzeczywiście wyemitowane compiler/linker commands zawiera `receipt.json`; deklarowane komendy z `compile_commands.json` są osobno w `declared-recovery-compilation-commands.json`, z takim oznaczeniem. Nie należy ich utożsamiać z wykonaniem.

## Deterministyczny replay obiektu0 — tylko kopie

`input-object.bin` jest zachowaną publiczną binarną kopią poprawnego obiektu vendored miniz ze zbudowanej bazy161. Jego pochodzenie, rozmiar i hash są zapisane. Replay czyta tę kopię, kopiuje ją do izolowanego katalogu tymczasowego, ucina wyłącznie kopię, sprawdza rozmiar0/brak ELF i zgodność bajtów źródła po operacji. Następnie usuwa pliki tymczasowe. Nie uruchamia kompilatora, linkera ani nie zmienia oryginalnych katalogów build. To odtwarza klasę uszkodzenia deterministycznie; nie jest przechwyconym oryginalnym `app.cpp.o`.

```sh
python docs/reports/data-in-code/evidence/build-recovery/zero_object_replay.py docs/reports/data-in-code/evidence/build-recovery/input-object.bin --receipt /tmp/zero-object-replay.json
```

Rzeczywiste wykonanie tego replay: PASS4 asercji, input356888B, input niezmieniony, kopia0B. Pełne stdout i receipt są zachowane. Spodziewana klasa problemu: pusty obiekt nie dostarcza symboli App i nie stanowi poprawnego członka archiwum obiektowego; nie wygenerowano sztucznego diagnostic linkera.

## Ujemny pełny ctest — brak execute bit

Po zakończeniu pierwszego pełnego runu final-validation JUnit pokazał122 wrappery i95 failures: wszystkie95 native unitów nie wystartowały z `permission denied`. Niezależny readonly stat potwierdził `loom_tests` jako ELF54783640B z mode0644 i `os.X_OK=False`; CLI i serwer miały755. Pełne `ctest-negative-no-execute.log/xml` oraz metadata zachowano przed podmianą finalnego dowodu. Nie wykonywano w tych95 przypadkach asercji; nie należy deklarować ich jako zielonych testów ani porażek implementacji. Właściciel został powiadomiony; późniejszy readonly snapshot już widzi mode0755 i execute_allowed=true. Metadata rozróżnia wcześniejszą obserwację0644 i późniejszą0755. Wciąż wymagany jest osobny dodatni wynik pełnego ctest. Child nie zmienił mode ani builddir.

## Do wątku N

- **Do wątku 8:** zachować te pełne negatywne dowody wraz z końcowymi dodatnimi wynikami. Nie używać logów syntax-only ani przerwanych buildów jako zielonego ctest.
- **Do wątku 9:** po finalnej kompilacji wymagany jest osobny receipt pełnego `ctest`, buildu web i dokładnej parity defaultów. Podczas capture tego dowodu bieżący build nadal pracował.
