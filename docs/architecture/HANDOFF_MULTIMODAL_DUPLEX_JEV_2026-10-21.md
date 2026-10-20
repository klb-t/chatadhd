# ChatADHD / Loom — transfer ustaleń: mimika, multimodalność, pełny dupleks, JEV, meta-rozumowanie

**Data kompilacji:** 2026-10-21  
**Status:** syntetyczny handoff projektowo-badawczy na podstawie bieżącej rozmowy, wcześniejszych omówionych wątków i dwóch lokalnych dokumentów źródłowych. To **nie jest raport o wdrożeniu**.  
**Zasada:** opisujemy hipotezy, wymagania i proponowane zadania. Odróżniamy potwierdzone historyczne pomysły od nowych interpretacji oraz od zweryfikowanego działania kodu.

## 0. TL;DR — co ma trafić do ChatADHD

1. Wspólny model wieloosobowej interakcji: stan latentny i graf dotyczące **uwagi, ciekawości, akceptacji kierunku, zaangażowania, rozumienia, celu, pilności, kontroli tury i konsekwencji działań**. Zmienne zależą od uczestnika, celu/tematu, perspektywy i czasu. Nie utożsamiać uwagi z aprobatą.
2. **Kontroler real-time full duplex**: oddzielne nasłuchiwanie, generowanie treści, synteza i odtwarzanie dźwięku. Możliwość mówienia jednocześnie, wchodzenia w słowo, ustępowania, utrzymywania głosu, zmiany tempa, mikroprzerw i natychmiastowego przerwania audio, również w środku fonemu.
3. **Wstępny plan interwencji generowany wraz z odpowiedzią LLM**, najlepiej w grafowym pakiecie: możliwe przerwania per segment/argument, skutki alternatywnych reakcji, zależności argumentów, istotne niewypowiedziane informacje. Plan i prognozy są hipotezami, a nie rozkazami blokowania użytkownika.
4. **Tani oceniający model JEV jako kandydat eksperymentalny**, plus lokalne DSP/VAP/GRU/TCN/state-space; JEV ma analizować sytuacje i warunki oraz warianty decyzji, nie sterować PCM przy każdym fonemie. JEV należy porównać z heurystykami i konkurentami, a nie zakładać przewagi.
5. **Planowanie warunkowe, wielohoryzontowe**: ustąp teraz, zachowaj pilną informację i możliwość wznowienia, po wypowiedzi użytkownika ponownie oceń zasadność przypomnienia głównego celu. Modeluje się też możliwość porzucenia celu, jeśli utracił ważność.
6. **Metadiagnostyka trajektorii decyzji od pierwszego zdarzenia**, gdy budżet i ustawienia na to pozwalają: wielokrotne odraczanie informacji może świadczyć o złej polityce ustępowania, złej konstrukcji odpowiedzi, zmianie potrzeb użytkownika lub konflikcie celów. Wykrywaj przyczyny, konkurencyjne hipotezy i eksperymenty naprawcze.
7. **Mimika jako jedna projekcja stanu multimodalnego**. Proceduralna twarz i optymalizacja parametrów anatomicznych na podstawie multimodalnych embeddingów; jednocześnie inne kanały: głos, gest, poza, kamera, światło, montaż, wirtualna reżyserka, awatary agentów.
8. **Graf zachowujący potencjał przyszłej rekonstrukcji**: źródła, identyfikatory, czasy, referencje, alternatywy, oceny, modele, transformacje, znane straty. Różne perspektywy i lokalne projekcje ciągłe/manifoldy są opcjonalne i odtwarzalne; nie każde źródło trzeba importować ani materializować.
9. **Nienarzucanie ontologicznych granic**: użytkownik jako rola/relacja względem dowolnego obiektu/referencji/konstruktu, także grupy, bytu fikcyjnego, hipotetycznego, nierozpoznanego, sprzecznego. Tożsamość, istnienie, spójność, członkostwo, uprawnienia, zgoda i wykonalność są odrębnymi ocenami.
10. **Semantyka niezależna od skali, wykonanie adaptowane do zasobów**: skalowanie liczby aktorów, wydarzeń, wymiarów, źródeł, horyzontów i budżetów o wiele rzędów wielkości, bez zaszytych list możliwych typów i arbitralnych progów uruchamiania analizy.

## 1. Wyraźny podział: rodzaje informacji

- **Wymagania użytkownika wyrażone w rozmowie:** uniwersalne struktury i operatory; przykłady są instancjami klas, nie wyliczeniem zamkniętym; suwak kosztu; zachowanie źródeł; brak niepotrzebnych strat; JEV jako wariant do zbadania; pełny dupleks konwersacyjny; multimodalna ekspresja; warianty działania w zależności od kontekstu.
- **Hipotezy do falsyfikacji:** multimodalny embedding przenosi wystarczająco subtelny sygnał emocjonalno-pragmatyczny; niewielki lokalny model może przewidywać skutki ustępowania; JEV trafnie ocenia konsekwencje strategii; części grafu nadają się do lokalnej rekonstrukcji rozmaitości.
- **Znane ograniczenia:** tekst nie daje pełnej obserwowalności uwagi, emocji i intencji; brak surowego audio ogranicza odtwarzanie prozodii; podobny obraz twarzy w embeddingu nie musi mieć równoważnej percepcji ludzkiej; wzrost budżetu nie gwarantuje lepszej jakości.

## 2. Dynamiczna reprezentacja komunikacji

### 2.1 Co obserwujemy i co wnioskujemy

Obserwacje: kolejność wypowiedzi; znaczniki audio; aktywność głosu; nakładanie się tur; intensywność, F0, rytm, pauzy, oddech, formanty i inne cechy DSP; słowa/argumenty; gesty i kamera, jeśli dozwolone; jawne prośby o zmianę tematu; faktyczne reakcje; kontekst historii.

Hipotezy modelu (nie pomiary bezpośrednie): uwaga danego uczestnika na konkretnym obiekcie; ciekawość; recepcja informacji; akceptacja kierunku; zrozumienie; ocena mówcy; tendencja do przerwania; receptywność; pilność celu. Odrębne przestrzenie dla każdego uczestnika / celu / perspektywy / horyzontu.

Reprezentacja przykładowa:

`state(actor_ref, object_ref, perspective_ref, time_range, estimate_method, uncertainty, observation_refs)`

Stan może być wektorem latentnym, rozkładem, modelem grafowym lub mieszanym. Nazwy cech są **opcjonalnymi projekcjami**, nie obowiązkowym zestawem osi. Uważaj na błędny skrót: człowiek może słuchać z ciekawością, mimo że nie akceptuje kierunku; przyjąć argument bez poparcia nadawcy; ustąpić mimo utrzymania uwagi grupy.

### 2.2 Kontrola tury ≠ uwaga ≠ skuteczność argumentu

Rozróżnić co najmniej: `floor_ownership`, `attention_distribution`, `content_reception`, `speaker_evaluation`, `argument_update`, `direction_acceptance`, `social_dynamics`. Sukces akustyczny może być porażką poznawczą, np. brutalne wejście w słowo przyciąga uwagę, lecz podkopuje przyjęcie argumentu.

### 2.3 Wielowymiarowy model decyzji

`Q(action, horizon, goal_ref, perspective_ref | state, constraints, budget)`

Akcje: kontynuuj / ustąp / przyspiesz / zmień kolejność / wyprzedź najważniejszą informację / wtrąć krótkie wyjaśnienie / wycisz / zawieś / wznów / odłóż / porzuć. Horyzonty: natychmiast, reszta tury, dalszy dialog, cel długoterminowy; bez wymogu dyskretnej zamkniętej listy — horyzont może być ciągły lub zadany zdarzeniem. Wymiary: recepcja, wartość informacji, pilność, koszt eskalacji, autonomia, zaufanie, szansa powrotu, niepewność, koszt wyliczenia. Nie redukować wszystkiego do jednego „maksymalizuj zgodę/uwagę”.

Nie materializować pełnego iloczynu aktor × obiekt × horyzont × akcja × wariant × metoda; zapytania i projekcje na żądanie oraz adaptacyjne próbkowanie.

## 3. Full duplex: rozdzielenie niezależnych mechanizmów

- **Techniczny full duplex:** mikrofon i głośnik równocześnie.
- **Konwersacyjny full duplex:** obaj rozmówcy mogą mówić naraz, nawzajem się przerywać, ustępować, obstawać przy głosie lub współprowadzić wątki; kontroler nie zakłada jedynego właściciela tury.
- **Lokalna pętla audio:** 10–20 ms ramka DSP jako ilustracja; VAD/diarization; aktywność, prozodia, overlap; szybka polityka kontroli dźwięku. Nie wszystkie cechy DSP są sensowne w 10 ms.
- **Średnioterminowa pętla:** przewidywanie dalszej aktywności, końca tury, backchannel, receptywności, prawdopodobnych przerwań; lokalny model rekurencyjny lub VAP/MaAI jako baseline.
- **Wolniejsza pętla semantyczna:** ASR, graf argumentów, plany LLM, pakiet prognoz i wartości informacji; JEV jako kandydat na ocenę warunków oraz skutków działań.
- **Kanały niezależne:** `generation`, `audio_synthesis`, `playback`, `capture/listening`, `conversation_memory`; z własnymi buforami, pozycjami i wersjami.
- **Pozycje:** `P_plan`, `P_generated`, `P_synthesized`, `P_played`, `P_heard_estimated`. Wygenerowany argument nie jest argumentem usłyszanym. `P_heard` to hipoteza.
- **Sterowanie:** `response.cancel`, buforowanie/wyciszanie i odtwarzanie, natychmiastowe ucięcie PCM z fade-out, prędkość lokalnego odtwarzania przez time-stretching, synchronizacja kontekstu z faktycznie odtworzonym czasem. Konkretne możliwości API należy zweryfikować dla docelowego providera i wersji; nie wszystkie wspierają zmianę prędkości syntezy w trakcie aktywnej tury.

## 4. Antycypacja: odpowiedź LLM + metadane / graf reakcji

LLM może emitować równocześnie widoczną treść i **metapakiet**:
- segmenty i zależności grafu argumentacyjnego;
- informację, co jest już przekazane, co dopiero planowane, co może zmienić interpretację całości;
- przewidywane rodzaje przerwań i prawdopodobne miejsca;
- alternatywy działań dla kontrolera i skutki w różnych horyzontach;
- otwarte warunki i powody rewizji;
- wskaźniki dowodów, confidence oraz pochodzenie oceny.

Sygnał „nie warto ustępować teraz” oznacza porównanie wartości strategii, **nie uprawnienie do niesłuchania użytkownika**. Plan musi podlegać rewizji, zwłaszcza przy nowym fakcie, korekcie lub wyraźnej zmianie zamiaru użytkownika. Brak przewidzianego przerwania po uprzedniej modyfikacji wypowiedzi nie dowodzi błędu prognozy: interwencja mogła zapobiec zdarzeniu (kontrfaktyczność / refleksyjność prognoz).

### 4.1 Schemat ilustracyjny, nie kanoniczny format Loom

```json
{
  "schema": "proposal.conversation_policy/0",
  "utterance_ref": "response:42",
  "segments": [
    {
      "id": "arg:3",
      "text_span_ref": "text:42#span=20:45",
      "argument_refs": ["claim:3", "premise:8"],
      "information_priority": {"value": 0.9, "basis": "proposal", "calibrated": false},
      "predicted_interventions": [
        {"kind": "premature_objection", "basis_refs": ["context:31"]}
      ]
    }
  ],
  "options": ["continue", "yield", "reorder", "summarize", "defer"],
  "deferred_intentions": [
    {
      "target_ref": "claim:3",
      "trigger": "user_turn_end",
      "conditions": ["still_relevant", "still_urgent", "user_did_not_cancel"],
      "reassess_before_execute": true
    }
  ]
}
```

**Nie uznawać tego za `loom_packet` ani produkcyjny kontrakt**; mapować przez istniejące semantyczne `Entity/Claim/Assessment/Decision/Observation` i operatory konstrukcji GraphPacket. Przed implementacją przeczytać aktualne `AGENTS.md`, `LOOM_CONCEPTUAL_MODEL.md`, `METHOD_GRAPH.md` i przetestować zgodność z pakietami grafu.

## 5. JEV: jak zweryfikować wartość

JEV ma być jednym z **estymatorów ocen**, nie jedynym sterownikiem polityki. Kandydat: zestaw prostych pytań o opłacalność ustąpienia versus utrzymania głosu w krótkim/średnim/długim horyzoncie, względem różnych celów i uczestników; oceny alternatyw, pilności i niepewności. Można użyć pakietowej oceny jednego stanu. Interpretator łączy oceny z jawnie wersjonowaną polityką.

Porównać co najmniej: deterministyczny baseline regułowy, lokalny model VAP/turn-taking, JEV na strukturze tekstowej, JEV + plan LLM, tani model wyuczony na śladach. Metryki: latency p50/p95, koszty całkowite, kalibracja, jakość decyzji z oceną ludzką, procent utraconych pilnych informacji, zgodność z preferencją użytkownika, reakcje na nowe fakty, OOD. Nie wnioskować o użyteczności tylko z ceny i płynności oceny.

## 6. Trwałe zobowiązania, a nie „zapamiętaj na potem”

Przykład: ważna informacja musi zostać odłożona ze względu na stan dialogu. Tworzymy **warunkowe zobowiązanie** powiązane z `goal_ref`, ze stanem `pending`, zdarzeniowym wyzwalaczem ponownej oceny, warunkami anulowania, terminami i relacją do rzeczywiście przekazanej treści. Ustąpienie nie równa się porzuceniu; zmiana priorytetów użytkownika może jednak uzasadnić porzucenie. Po ponownym dojściu do głosu kontroler **na nowo** ocenia ważność; nie wykonuje starego polecenia bez sprawdzenia warunków.

## 7. Samodiagnostyka od pierwszego wystąpienia

Sekwencja `zdarzenie → decyzja → alternatywy → rezultat → hipotezy → ewentualny test → rewizja polityki`.

Wielokrotne odroczenie tego samego argumentu jest przykładem ogólnej klasy **powtarzających się nieskuteczności działania**. Możliwe przyczyny: zły układ wypowiedzi prowokuje przerwanie, system nadmiernie ustępuje, priorytet argumentu był błędny, użytkownik zmienił cel, konflikt wartości, artefakt estymatora. Hipotezy powinny współistnieć z uzasadnieniami; nie wpisywać jednego „rozpoznania przyczyny” jako obserwacji.

**Nie wprowadzać progu 3 zdarzeń.** Nawet pierwsza nietypowa sytuacja może uzasadnić analizę, zależnie od budżetu, prywatności, ryzyka i oczekiwanej wartości informacji. Brak zasobów nie powinien kasować możliwości przyszłego zbadania. Mechanizm diagnozy ma być ogólny dla głosu, decyzji narzędziowych, utraty kontekstu, konfliktu preferencji, powtarzalnych błędów i kosztownych operacji.

## 8. Pole potencjału hipotez, „holograficzne cienie” i uciąglenie

- Nie wymuszać materializacji każdej możliwej hipotezy. Zachować **operatory ich generowania**, źródła, referencje, kontekst, transformacje, oceny, historię alternatyw, ślady strat.
- Idea może mieć bardzo mały *dodatni udział w potencjale budżetowym* bez wykonania atomowej operacji (np. symboliczna „jedna decylionowa dolara”); nie twierdzić, że sprzęt/API realizuje ułamkowy call. Możliwe księgowanie, akumulacja, pakietowanie i wspólne operacje.
- Rozdzielić: obecność hipotezy / wykonalność / wartość / zasoby / uprawnienia. Nieskończenie wiele wariantów wymaga miary lub generatora z normalizowalnym rozkładem, nie węzła i niezerowego budżetu operacyjnego dla każdej wyobrażalnej hipotezy.
- Lokalna geometria: z pewnych dyskretnych zdarzeń można budować przestrzenie ciągłe, manifoldu podobne struktury, grafy rzadkie, pola, rozkłady, przestrzenie mieszane; nie zakładać jednej globalnie gładkiej rozmaitości ani istnienia danych wystarczających do jednoznacznej rekonstrukcji.
- Wersjonować embeddingi, modele, metryki, perspektywy, transformacje i utraty informacji. Dane źródłowe mogą pozostać **zewnętrzne, referencjonowane i parsowane na żądanie**.

## 9. Model ekspresji twarzy: precyzyjna hipoteza badawcza

Rozważana metoda: tekst + kontekst + wskazana perspektywa → embedding docelowy `E_text(context)`; parametry twarzy `θ` (blendshapes, kości/mięśnie/aktywacje, tkanki, mimika, poza, oświetlenie, styl) → proceduralny renderer `R(θ)` → `E_image(R(θ))`. Minimalizacja kosinusowej odległości embeddingów z regularyzacją anatomii i ciągłości czasowej:

`L(θ) = 1 - cos(E_image(R(θ)), E_text(context)) + λA Ω_anatomy + λT Ω_temporal`.

Brak gradientów przez proprietary encoder nie zabrania przybliżenia jakobianu po parametrach generatora numerycznie; analizować `J`, `G = JᵀJ`, SVD, rząd efektywny, nieidentyfikowalność, kierunki niewidoczne, singularności i zmiany geometrii. Metody: central differences, SPSA, CMA-ES, surrogates, active sampling, wielowiernościowe rendery. Najpierw sprawdzić semantyczną czułość embeddingów na rzeczywiste różnice ekspresji; możliwy negatywny wynik. Porównać do prostych metod etykietowania emocji i bibliotek wzorcowych ekspresji; uzyskać ocenę ludzką.

**Kluczowe:** żadnego obowiązkowego przejścia przez klasy `happy/sad/angry`; potrzebna bogatsza reprezentacja komunikacyjno-perspektywiczna. Ekspresja oznacza wyrażanie **modelowanej interpretacji lub roli**, a nie dowód uczuć LLM czy niezawodną inferencję emocji użytkownika.

**Kompletny, odrębny plan eksperymentów, telemetrii i budżetu jest w lokalnym pliku:** `face_embedding_research_master_prompt_2026-10-19.md` (dołącz do przekazania, nie zastępuj go streszczeniem).

## 10. Wielomodalny „piczer”, wirtualna reżyserka i skala ogólna

W dawnych rozmowach o agencie prezentującym portfolio (grudzień 2025, czerwiec 2026; raport rekonstrukcyjny omawiany w tej rozmowie) pojawiły się: awatar pitchujący z grafu wiedzy, telewizyjna reżyserka / ściana monitorów, sceny 3D w Unreal Engine, osobne awatary i reprezentacje podagentów, widoczna degradacja złożoności awatara jako sposób pokazywania obciążenia, kamera rozmówcy za zgodą, reagowanie innych awatarów na jego wypowiedzi i kontekst, gesty i mimika, AR, celowo widoczna sztuczność postaci (bez przymusowego antropomorfizmu).

Już 17 czerwca 2026 użytkownik rozważał łączenie lipsync z kontrolą mięśniową / ręczną mimiką. Pierwszy piczer miał wspierać interfejs lub wywołania mimiki, nawet jeśli pełne wykonanie pozostanie późniejsze. Nie zamieniać tego we wtórny, odrębny engine: ekspresja twarzy, głos, gesty, kamera, światło, scena i procesy agentowe powinny być **projekcjami i mechanizmami wykonawczymi jednego bogatszego stanu**.

Dwukierunkowość: `kontekst → ekspresja` oraz `obserwacje rozmówcy → hipotezy o interakcji`; niezależna walidacja obu kierunków; rozpoznanie czyjejś ekspresji nie jest tym samym co jej generowanie. Jeśli przetwarzane są obrazy/biometria, uwzględnić jawne uprawnienia, przetwarzanie lokalne i brak nieuprawnionego przechowywania.

## 11. Głos jako funkcje czasu, nie tylko ASR

Archiwalne wypowiedzi z rozmowy Gemini 15 października: darmowe frameworki, kilkadziesiąt cech czasowych; preferencja jawnych pomiarów DSP; F0, jitter, shimmer, HNR, formanty, MFCC, rytm, pauzy, oddech i zależność od segmentacji tekstu; baseline osobniczy i sytuacyjny, ekspresja naturalna vs wyuczona, granice wnioskowania o stanie. Unikać pseudonaukowego „wykrywania kłamstw” z parametrów głosu.

**Osobny lokalny materiał historyczny:** `gemini_glos_wypowiedzi_i_wymagania_2026-10-15.md`.

## 12. Ontologia agenta i future-proofing

Abstrakcja `Actor`/`User` jako **rola relacyjna**, nie zamknięta lista typów `person | group | organization | AI | ...`. Dopuszczać nierozpoznane źródła i byty opisowe, fikcyjne, hipotetyczne, wewnętrznie sprzeczne; nie wymuszać rozstrzygnięcia istnienia jako warunku dołączenia do grafu. Obserwacja i opis nie są automatycznie przyzwoleniem na wykonywanie zadań w imieniu danego bytu; odrębne są auth/consent/permissions/legal scope. Sprzeczności należy przechować z kwalifikatorami/konkurencyjnymi perspektywami, nie propagować eksplozji logicznej.

Testować kilkurzędowe zmiany liczby uczestników i zdarzeń. `double` jest dobry dla części danych numerycznych, ale **nie** dla identyfikatorów i dokładnych dużych liczników (`uint64`, BigInt, arbitralna precyzja), pieniędzy (decimal/int), mikroskopijnych prawdopodobieństw (log-space), czasu (`int64` + skala); model semantyczny nie zakłada limitów typu `int32 participant_count`.

Jedno zdanie: **Scale-independent semantics, resource-adaptive computation.** Wzrost mocy powinien umożliwiać jakościowo nowe i dokładniejsze metody, nie jedynie szybsze powtarzanie uproszczeń. Wyjaśniać różnice metod i jakość projekcji.

## 13. Związek ze sprawdzonymi założeniami Loom

W repo `klb-t/chatadhd` sprawdzono (GitHub, aktualna gałąź główna przy sporządzaniu handoff):
- `docs/architecture/LOOM_CONCEPTUAL_MODEL.md`: kontrakt obserwacje → stan epistemiczny → generalizacje → decyzje/działania; `Entity`, `Claim`, siedmiopunktowy `Assessment`, `Principle`, `Operator`, `Expected Property`; dane źródłowe i pochodzenie, konflikt ≠ natychmiastowe scalenie; wartości nie są jednym skalarem; 14 ról uniwersalnych.
- `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md`: R12 celowy dobór kontekstu grafem; R13 uczenie generatora decyzji, nie historii; R14 bezstratna abstrakcja rozmowy; R15 wszystko jako graf; R16 wieloperspektywowe wizualizacje; R20 referencja/kopia wyborem użytkownika.
- `loom/src/packet/METHOD_GRAPH.md`: `loom.method_graph/1` jako **manifest konstrukcji grafu**, nie operacja dispatcher API; definicje, wersje, run trace, provenance. Używać istniejących konstruktorów GraphPacket; nie tworzyć osobnych, zahardkodowanych typów „conversation policy” w silniku.
- `docs/reports/graph-perspectives-2026-10-09/README.md`: wybieralne perspektywy i projekcje, źródła jako referencje, adaptory na żądanie; tamte gałęzie/raporty nie gwarantują, że całość jest na `main`.

Linki: https://github.com/klb-t/chatadhd/blob/main/docs/architecture/LOOM_CONCEPTUAL_MODEL.md ; https://github.com/klb-t/chatadhd/blob/main/docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md ; https://github.com/klb-t/chatadhd/blob/main/loom/src/packet/METHOD_GRAPH.md

## 14. Pakiet roboczy / kryteria odbioru — następny przyrost

### P0: kontrakt reprezentacyjny (bez wojny o implementację)
1. Zmapować `Actor`, `Goal`, `InteractionEvent`, `Decision`, `Alternative`, `Observation`, `Prediction`, `DeferredIntention`, `StateProjection`, `Transform`, `Assessment`, `Evidence`, `BudgetPolicy` na **istniejący model Loom**. Jeśli wymagana jest zmiana ontologii, opisać różnicę i uzasadnić, nie dopisywać bezrefleksyjnie nowej zamkniętej kategorii.
2. Ślad tury z `generated != played != heard_estimated`, przykładowo przerwanie w połowie słowa; idempotentny, weryfikowalny replay wydarzeń.
3. Graf decyzji i planów warunkowych: przechodzą testy przeplanowania po nowej informacji, unieważnienia nieaktualnego celu, powrotu do ważnej sprawy.
4. Mechanizm perspektywy dla dowolnych `actor_ref`, `goal_ref`, okien czasu i modalności; nie wymaga audio ani wideo.

### P1: działający pionowy przekrój
1. Pipeline `dialogue events/text → stan → kandydaci działań → oceny → polityka → ślad decyzji → wizualizacja`.
2. Trzy polityki/baseline'y: zawsze ustąp; jawna reguła; estymator dodatkowy (JEV, gdy dostęp i budżet) — raport mierzalnego wyniku, nie marketing.
3. Wykrywanie potencjalnie złej polityki od pierwszego zdarzenia, a potem na wielu przypadkach. Porównania dwóch działań i ślad uzasadnienia decyzji.
4. Grafowy ślad argumentu, który został wygenerowany, lecz nieprzekazany; późniejsze wznowienie albo jawne anulowanie.
5. Działający headless replay oraz prosty panel perspektyw; testy przy opóźnieniu, braku internetu, niedostępnym JEV, braku audio, prywatności local-only.

### P2: program badawczy twarz/głos/scena (równolegle)
1. Wykonać odrębny master prompt twarzy: test czułości embeddingu na kontrolowanych mimikach; wynik dodatni lub negatywny z telemetryką i hashami.
2. Biblioteka ekstrakcji cech mowy DSP + synchronizacja z transkrypcją; zachować surowy time base i znane ograniczenia próbek.
3. Minimalny proceduralny awatar: lipsync, blendshape, jawna polityka ekspresji, timeline, kontrola spójności czasowej; później transformacja tekst→embedding→parametry.
4. Scena/reżyserka jako kolejny renderer tej samej reprezentacji, nie osobna ontologia.

### P3: falsyfikacja i metryki
- Predykcja zmian tury, błędy detekcji overlap, opóźnienie decyzji.
- Retencja ważnych argumentów i brak niepożądanego upierania się.
- Trafność akceptacji kierunku i ciekawości względem jawnych ocen ludzi, z osobnymi poziomami niepewności.
- Odsetek źle interpretowanych intencji i skuteczność rewizji po nowym fakcie.
- Kalibracja ocen (ECE, Brier lub inne adekwatne miary), OOD.
- Koszt per sesja/per decyzja; runtime latency; zasobożerność; jakość degradacji.
- Dla twarzy: ocena ludzka, zgodność ekspresji, wpływ tożsamości/światła/pozy, cross-modal alignment, geometryczne właściwości `J`.
- Skalowanie po liczbie podmiotów, historii i hipotez; bez ukrytych progów typu trzy powtórzenia.

## 15. Zasady wykonawcze i handoff

- Najpierw `AGENTS.md`, stan aktualnego main/brancha, check istniejących kontraktów; nie zakładać, że opisane w raportach gałęzie są zintegrowane.
- Nie modyfikować cudzej gałęzi i nie traktować planu jako działającego modułu; osobna gałąź i raport `docs/reports/...`, testy i manifesty.
- Małe checkpointy, dowody, SHA, pomiar rzeczywistego kosztu. Podczas blokady dostępu do modeli wykonywać odblokowane zadania offline, lecz natychmiast zgłaszać brak credential/budżetu.
- Preferować realne wycinki eksportów do oceny metod; syntetyki do testów kontraktowych i kontrolnych, nie jako dowód skuteczności użytkowej. Nie uruchamiać płatnych eksperymentów bez autoryzacji.
- Każda hipoteza i ocena ma źródło, status oraz warunki weryfikacji. Możliwy wynik negatywny jest wartością naukową.
- Odpowiedzi na dane nieznane nie wymagają obowiązkowego importu; referencje, zewnętrzne źródła i projekcje on-demand są równorzędne.

## 16. Czego ten transfer nie obejmuje

Późniejszy wątek DG, tajemnego uścisku dłoni, wiedzy niewypowiadanej i hipotez o indywidualnej intencji DG **nie jest tu przedstawiany jako fakt o DG ani jako wymaganie do modelu twarzy**. Jego ogólne idee mogą być relewantne dla future research nad pragmatyką i wiedzą komunikacyjną: wysokorzędowe przekonania, pośrednie znaczenia, inferencje ze znaczących pominięć i kontrfaktyczne różnice reakcji. Nie należy jednak przenosić personalnych hipotez o DG do danych treningowych lub wniosków o konkretnym człowieku bez niezależnych podstaw.

## 17. Pliki towarzyszące i status dostępności

- `face_embedding_research_master_prompt_2026-10-19.md` — **istniejący lokalny plik**, pełny program badań nad parametryczną twarzą i kontrolą kosztu.
- `gemini_glos_wypowiedzi_i_wymagania_2026-10-15.md` — **istniejący lokalny plik**, rekonstrukcja 11 wypowiedzi użytkownika o analizie głosu.
- Rekonstrukcja rozmów o piczerze: została przywołana i omówiona w tej rozmowie, lecz **nie zweryfikowano tutaj nowego lokalnego pliku źródłowego**. Przed użyciem jako dowodu zakresu projektu ponownie pobrać oryginalny raport i rozmowy.

**Proponowana decyzja:** wprowadzić ogólny `InteractionTrajectory/DecisionAssessment/Projection` jako pakiet grafowy, dodać baseline strumieniowej polityki kontroli tury, a twarz i scenę potraktować jako wymienne modalne projekcje tego samego stanu. Zatwierdzenie pozostaje po stronie właściciela projektu.
