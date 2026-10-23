# R89 — badanie tokenów, ścieżka „nauk”: nauka i eksperymenty

**Ścieżka:** kompresja kontekstu, pamięć agentów, routing i kaskady modeli, współdzielenie KV-cache, długi kontekst vs wyszukiwanie, koszt systemów wieloagentowych.
**Data:** 2026-10-23. **Autor:** agent badawczy R89 (jedna z pięciu ścieżek). Każde znalezisko ma źródło, datę, typ źródła i ocenę wiarygodności (R59); fakty o stanie obecnym są obserwacjami z datą, nie ograniczeniami (R60); liczby podane są tylko tam, gdzie je przeczytałem (abstrakt lub treść pracy).

## Jak szukałem

- 12 zapytań `WebSearch` (standardowe; jedno rozszerzone), zgodnie z kolejnością: dokumentacja/prace źródłowe → omówienia → słabsze źródła oznaczone jako takie.
- Czytanie prac: `arxiv.org`, `aclanthology.org`, `proceedings.neurips.cc`, `lmsys.org`, `sky.cs.berkeley.edu`, `research.trychroma.com`, `docs.anthropic.com`, `huggingface.co` były **zablokowane przez proxy sesji** (403 / brak DNS). Działały: `anthropic.com/engineering` i `github.com` (WebFetch) oraz **lustro prac arXiv w Hugging Face (narzędzie MCP `hf_fs`, `hf://papers/<id>/paper.md`)** — stamtąd przeczytałem abstrakty i fragmenty 22 prac (pełne teksty 5 prac przeszukałem lokalnie po słowach kluczowych).
- Łącznie: 12 wyszukiwań, 2 udane odczyty stron, ~25 odczytów fragmentów prac przez lustro HF. Zatrzymałem się, gdy trzy ostatnie źródła (TRACE, PACT, TokenDance) dawały już tylko warianty dwóch mechanizmów opisanych wcześniej (kompresuj to, co przechodzi przez granicę agenta; współdziel identyczne prefiksy).

Skala „kalibru”: mały / średni / duży / naukowy / eksperymentalny. „Wysiłek” — dla nas, przy obecnym jądrze (loom, przepływy jako dane, profil ekonomii tokenów z P38).

**Nasze ustawienie, do którego odnoszę każde znalezisko:** koordynator z kontekstem ~1M tokenów, tani pracownicy (Haiku/Sonnet) na pakietach ~50 wywołań, specyfikacja i przepływy jako dane, pamięć jako graf + pliki, dostęp do modeli przez API (nie hostujemy ich sami), telefon jako klient.

---

## A. Kompresja promptu i kontekstu

### nauk-01 — LLMLingua: kompresja promptu do 20× z małą stratą
- **Źródło:** Jiang i in., *LLMLingua: Compressing Prompts for Accelerated Inference of LLMs*, EMNLP 2023, arXiv 2310.05736 — https://arxiv.org/abs/2310.05736 (czytane przez lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic, **wysoka** — konferencja główna, kod otwarty, metoda szeroko używana; liczby są jednak pomiarami autorów (Microsoft).
- **Twierdzenie (z treści pracy):** na GSM8K (GPT-3.5-Turbo) spadek EM o 1,44 pkt przy 14× i 1,52 pkt przy 20× kompresji; na BBH −8,5 pkt (5×) i −13,2 pkt (7×). Wyraźny spadek przy 25–30× na GSM8K (Limitations). Na ShareGPT/Arxiv przy 2–4× BERTScore-F1 ~89–90 (bez istotnej straty). Skompresowany prompt skraca też odpowiedź modelu.
- **Mechanizm:** mały LM (Alpaca-7B lub GPT2-small) mierzy perplexity tokenów; kontroler budżetu rozdziela stopień kompresji między instrukcję, demonstracje i pytanie; kompresja iteracyjna na poziomie tokenów; dopasowanie rozkładu małego LM do dużego przez instruction tuning. Wariant z GPT2-small jest gorszy o 1–2 pkt EM.
- **U nas:** dotyczy pracowników (wyjścia narzędzi, logi, demonstracje) bardziej niż koordynatora. Różnica: wymaga lokalnego małego LM — 7B na telefonie nie wchodzi w grę, GPT2-small tak (ale gorzej); przy cache'owaniu prefiksu u dostawcy kompresja stabilnego prefiksu daje mało — opłaca się kompresować część zmienną. Zysk jest największy przy wielokrotnym użyciu tego samego długiego kontekstu bez cache'a.
- **Kaliber:** naukowy. **Wysiłek:** średni (integracja jako krok przepływu „compress” z wyborem małego modelu jako preset).
- **Zastrzeżenia:** benchmarki rozumowania, nie długie rozmowy; „20×” to szczyt na jednym zbiorze.

### nauk-02 — LongLLMLingua: 4× mniej tokenów i +21,4% jakości w długim kontekście
- **Źródło:** Jiang i in., *LongLLMLingua*, ACL 2024, arXiv 2310.06839 — https://arxiv.org/abs/2310.06839 (abstrakt, lustro HF, 2026-10-23); strona projektu Microsoft: https://www.microsoft.com/en-us/research/project/llmlingua/
- **Typ / wiarygodność:** academic, **wysoka** (ACL 2024); liczby własne autorów.
- **Twierdzenie (abstrakt):** NaturalQuestions z GPT-3.5-Turbo: do +21,4% skuteczności przy ~4× mniej tokenów; 94,0% redukcji kosztu na LooGLE; dla promptów ~10k tokenów przy 2–6× kompresji opóźnienie end-to-end mniejsze 1,4–2,6×.
- **Mechanizm:** kompresja świadoma pytania (gruba — ranking dokumentów względem pytania; drobna — kontrastowa perplexity), przestawianie dokumentów przeciw efektowi „lost in the middle”, odtwarzanie podciągów w odpowiedzi.
- **U nas:** to dokładnie zadanie selektora kontekstu (K3): wybór i kolejność fragmentów względem bieżącego pytania. Różnica: poprawa jakości bierze się częściowo z usunięcia dystraktorów (nauk-12), więc u koordynatora z 1M kontekstu kompresja ma sens nawet, gdy wszystko „się mieści”.
- **Kaliber:** naukowy. **Wysiłek:** średni.
- **Zastrzeżenia:** modele z 2023; sam autor obu prac (bez niezależnej replikacji w moim zasięgu).

### nauk-03 — Selective Context: −50% kontekstu kosztem ~0,02 BERTScore
- **Źródło:** Li i in., *Compressing Context to Enhance Inference Efficiency of LLMs*, EMNLP 2023, arXiv 2310.06201 — https://arxiv.org/abs/2310.06201 (abstrakt, lustro HF, 2026-10-23); kod: https://github.com/liyucheng09/Selective_Context
- **Typ / wiarygodność:** academic, **wysoka** (EMNLP main); liczby własne.
- **Twierdzenie (abstrakt):** 50% redukcji kosztu kontekstu → −36% pamięci inferencji, −32% czasu; spadek BERTScore o 0,023 i wierności o 0,038 na czterech zadaniach (streszczanie, QA, generowanie odpowiedzi; arXiv, newsy, długie rozmowy).
- **Mechanizm:** self-information (zaskoczenie wg małego LM) jednostek leksykalnych; usuwa mało informatywne frazy/zdania bez treningu.
- **U nas:** tani, deterministyczny filtr wstępny dla importowanych archiwów przed analizą modelem; dla rozumowania wieloetapowego gorszy — LLMLingua pokazuje (nauk-01) 33,10 pkt różnicy na GSM8K przy 20× na niekorzyść Selective Context.
- **Kaliber:** średni. **Wysiłek:** mały (biblioteka `selective-context` w Pythonie; port do C++ to perplexity z małego modelu — średni).

### nauk-04 — Gist tokens: 26× kompresja promptu przez trening modelu
- **Źródło:** Mu, Li, Goodman, *Learning to Compress Prompts with Gist Tokens*, NeurIPS 2023, arXiv 2304.08467 — https://proceedings.neurips.cc/paper_files/paper/2023/hash/3d77c6dcc7f143aa2154e7f4d5e22d68-Abstract.html (liczby z abstraktu wg streszczenia wyszukiwarki; wstęp pracy czytany przez lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic, **wysoka** jako wenue; liczby przeczytane pośrednio (abstrakt w streszczeniu wyszukiwarki, zgodny ze wstępem pracy).
- **Twierdzenie:** na LLaMA-7B i FLAN-T5-XXL do 26× kompresji promptu, do 40% mniej FLOPs, 4,2% szybciej (wall-clock), oszczędność pamięci cache.
- **Mechanizm:** maska uwagi w treningu zmusza model do „streszczenia” promptu w aktywacjach kilku tokenów-gistów, które można cache'ować i reużywać; meta-uczenie — nowe instrukcje bez dodatkowego treningu.
- **U nas:** wymaga treningu modelu — nie dotyczy modeli przez API; ma sens tylko dla własnego małego modelu (np. lokalny klasyfikator). Obserwacja z datą: dostawcy API realizują tę samą potrzebę przez cache prefiksu (patrz nauk-10).
- **Kaliber:** naukowy. **Wysiłek:** duży. **Zastrzeżenia:** zysk FLOPs ≠ zysk kosztu w API.

## B. Architektury pamięci agentów

### nauk-05 — MemGPT: pamięć jak system operacyjny (stronicowanie kontekstu)
- **Źródło:** Packer i in., *MemGPT: Towards LLMs as Operating Systems*, arXiv 2310.08560 (2023) — https://arxiv.org/abs/2310.08560 (abstrakt, lustro HF, 2026-10-23); dziś produkt Letta.
- **Typ / wiarygodność:** academic (preprint, bardzo cytowany), **średnio-wysoka**; liczb nie czytałem — tylko mechanizm.
- **Twierdzenie (abstrakt):** „virtual context management”: model sam stronicuje między kontekstem głównym a pamięcią zewnętrzną przez wywołania funkcji; umożliwia analizę dokumentów dłuższych niż okno i czat wielosesyjny z pamięcią.
- **Mechanizm:** hierarchia pamięci (kontekst roboczy, archiwum, odwołania), przerwania/zdarzenia, funkcje `memory_search`/`memory_write` wołane przez model.
- **U nas:** nasz graf + drzewo pamięci to już „archiwum”; brakuje przepływu, w którym model sam decyduje, co wczytać/zapisać (narzędzia pamięci jako działania w grafie). Różnica: u nas koordynator ma 1M okna — stronicowanie ma sens dla pracowników i dla telefonu.
- **Kaliber:** duży. **Wysiłek:** średni.

### nauk-06 — A-Mem: pamięć Zettelkasten, ~2,5k tokenów na pytanie zamiast ~17k
- **Źródło:** Xu i in., *A-Mem: Agentic Memory for LLM Agents*, arXiv 2502.12110 (2025; w tekście lista kontrolna NeurIPS) — https://arxiv.org/abs/2502.12110 (treść pracy, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic, **średnio-wysoka** — liczby z Tabeli 1 pracy; autorzy nie podają przedziałów błędu („wielokrotne wywołania API są drogie”).
- **Twierdzenie (Tabela 1, LoCoMo, GPT-4o-mini):** A-Mem 2 520 tokenów na pytanie vs MemGPT 16 977 i LoCoMo (pełny kontekst) 16 910 — autorzy: „85–93% redukcji”, „ok. 1 200–2 500 tokenów na operację pamięci”. F1 w kategorii czasowej 45,85 vs 25,52 (MemGPT); multi-hop 27,02 vs 26,65; adwersaryjne 50,03 vs 43,29; średnia pozycja 1,2. Ablacja: bez generowania linków i ewolucji pamięci F1 multi-hop spada z 27,02 do 9,65.
- **Mechanizm:** każda nowa pamięć = notatka ze słowami kluczowymi, tagami, opisem kontekstu; agent dopisuje linki do podobnych notatek i **aktualizuje stare notatki** (ewolucja); odczyt = top-k (domyślnie 10) po embeddingach all-MiniLM-L6-v2.
- **U nas:** to niemal nasz graf (węzły, linki, tagi, `derived_from`); brakuje kroku „ewolucji” (nowy węzeł może zmienić opis starych) oraz top-k zamiast całej historii. Różnica: nasz koszt zapisu (generowanie notatki modelem) trzeba liczyć osobno — tabela mierzy tokeny odczytu.
- **Kaliber:** naukowy. **Wysiłek:** średni (ewolucja węzłów jako przepływ; top-k już jest w selektorze grafu).

### nauk-07 — Generative Agents: ocena pamięci = α·świeżość + α·ważność + α·trafność
- **Źródło:** Park i in., *Generative Agents: Interactive Simulacra of Human Behavior*, UIST 2023, arXiv 2304.03442 — https://arxiv.org/abs/2304.03442 (treść pracy, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic, **wysoka** (UIST 2023); to nie jest praca o kosztach tokenów.
- **Twierdzenie (treść):** strumień pamięci; funkcja odczytu `score = α_recency·recency + α_importance·importance + α_relevance·relevance` po normalizacji min-max; świeżość = zanik wykładniczy 0,995 na godzinę gry od ostatniego odczytu; ważność 1–10 nadawana przez model **raz, przy zapisie**; refleksje generowane, gdy suma ważności ostatnich zdarzeń przekroczy próg (150) — 2–3 razy dziennie. Ablacje (bez obserwacji / refleksji / planowania) obniżają wiarygodność w ocenie ludzi (liczb nie cytuję).
- **Mechanizm:** mała liczba jawnych, konfigurowalnych wag; drogie oceny (ważność) liczone przy zapisie, tanie (świeżość) przy odczycie.
- **U nas:** wagi selektora K3 jako dane (α jako preset, nie reguła); ważność liczona raz przy zapisie węzła — koszt stały, nie na każde zapytanie; refleksja = nasza „karta wznowienia”.
- **Kaliber:** duży. **Wysiłek:** mały.

### nauk-08 — Reflexion: pamięć epizodyczna z refleksji słownych, 91% pass@1 HumanEval
- **Źródło:** Shinn i in., *Reflexion: Language Agents with Verbal Reinforcement Learning*, NeurIPS 2023, arXiv 2303.11366 — https://proceedings.neurips.cc/paper_files/paper/2023/hash/1b44b878bb782e6954cd888628510e90-Abstract.html (abstrakt wg streszczenia wyszukiwarki, spójny z arXiv/NeurIPS; spis treści pracy czytany przez lustro HF).
- **Typ / wiarygodność:** academic, **wysoka** (NeurIPS 2023); koszt tokenów **nie** jest mierzony.
- **Twierdzenie:** 91% pass@1 na HumanEval vs 80% poprzedniego SOTA GPT-4; bez zmiany wag — agent zapisuje słowną refleksję po nieudanej próbie do bufora pamięci epizodycznej i czyta ją w następnej próbie.
- **Mechanizm:** aktor → ewaluator (testy, heurystyka) → samorefleksja → bufor; kolejne próby.
- **U nas:** nasza praktyka „poprawka jako addendum po agencie” to Reflexion w wersji ręcznej; refleksja (co nie wyszło i dlaczego) jako dane w karcie wznowienia. Różnica: każda próba mnoży tokeny — zysk jakości trzeba zestawić z kosztem prób (tu nie zmierzonym).
- **Kaliber:** naukowy. **Wysiłek:** mały.

### nauk-09 — AutoMEM: agent sam zarządza plikami pamięci; tryb jednoprzebiegowy −66–72% tokenów
- **Źródło:** Chen, Gu, Yin i in. (MSU/GMU/Purdue), *Exploring Cross-Scenario Generality of Agentic Memory Systems*, arXiv 2606.04315 (2026-06) — https://arxiv.org/abs/2606.04315 (treść, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic (preprint), **średnia** — świeży, bez recenzji; duże porównanie (8 systemów + harness, 5 scenariuszy).
- **Twierdzenie (treść):** harness, który sam zarządza płaskim magazynem plików tekstowych przez wywołania narzędzi (`rg`, `read`, `index_query`), ma najlepszy ranking międzyzadaniowy; AutoMEM na LoCoMo 67,3 vs długi kontekst 61,5 vs Mem-T 66,5. Wariant jednoprzebiegowy (K_max=1, bez pętli sędzia–replan) obcina tokeny zapytania o 66–72% na trzech benchmarkach QA przy −3,8 pp na LoCoMo; zwykle tańszy niż długi kontekst.
- **Mechanizm:** aktywna kontrola agenta nad zapisem/odczytem zamiast pasywnego potoku; liczba rund jako pokrętło koszt–jakość.
- **U nas:** wprost: „import jako pliki w grafie” (R57) + narzędzia agenta; liczba rund jako preset w przepływie. Różnica: u nas magazyn to graf + pliki na telefonie; `rg` nad plikami już mamy w narzędziach.
- **Kaliber:** naukowy. **Wysiłek:** mały–średni.

## C. Współdzielenie KV-cache i prefiksów

### nauk-10 — SGLang / RadixAttention: do 6,4× przepustowości dzięki reużyciu KV-cache
- **Źródło:** Zheng i in., *SGLang: Efficient Execution of Structured Language Model Programs*, NeurIPS 2024, arXiv 2312.07104 — https://arxiv.org/abs/2312.07104 (abstrakt, lustro HF, 2026-10-23); blog LMSYS 2024-01-17: https://lmsys.org/blog/2024-01-17-sglang (niedostępny z tej sesji — wskaźniki trafień cache z blogu nie są tu cytowane).
- **Typ / wiarygodność:** academic + open source, **wysoka** (NeurIPS 2024, szeroko wdrożony).
- **Twierdzenie (abstrakt):** do 6,4× wyższa przepustowość niż SOTA systemy inferencji na zadaniach: sterowanie agentem, rozumowanie, few-shot, JSON, RAG, czat wieloturowy.
- **Mechanizm:** KV-cache zakończonych żądań trzymany w drzewie radix; wyszukiwanie wspólnych prefiksów, LRU, planowanie świadome cache'a; skompresowane automaty dla wyjścia strukturalnego.
- **U nas:** dotyczy tylko hostowania własnego; u dostawców API odpowiednikiem jest cache promptu po prefiksie — wniosek konstrukcyjny ten sam: **stabilna część (specyfikacja jako dane, reguły, narzędzia) bajt w bajt identyczna na początku, część zmienna na końcu**; każdy pakiet agenta zaczyna od tego samego prefiksu. Nasza miara z 2026-10-21 (98% odczytów cache w pięciu długich agentach) pokazuje, że to już jest nasz główny koszt.
- **Kaliber:** naukowy. **Wysiłek:** mały (dyscyplina układu promptu) / duży (własne serwowanie).

### nauk-11 — TokenDance: w systemach wieloagentowych All-Gather daje 11–17× redundancji KV
- **Źródło:** *TokenDance: Scaling Multi-Agent LLM Serving via Collective KV Cache Sharing*, arXiv 2604.03143 (2026-04) — https://arxiv.org/abs/2604.03143 (abstrakt, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic (preprint), **średnia**.
- **Twierdzenie (abstrakt):** wzorzec „zbierz wyjścia wszystkich agentów i rozdaj wszystkim” powoduje, że prompt każdego agenta zawiera te same bloki; kodowanie cache'ów rodzeństwa jako różnic do kopii głównej daje 11–17× kompresji; do 2,7× więcej agentów równolegle niż vLLM z cache'em prefiksu (GenerativeAgents, AgentSociety), do 17,5× mniej pamięci KV na agenta, 1,9× szybszy prefill.
- **Mechanizm:** reużycie w jednym kroku zbiorczym na rundę; diff blokowo-rzadki.
- **U nas:** hostowanie własne — nie dotyczy dziś; wniosek przenośny: współdzielony kontekst pracowników ma być **identycznym blokiem** (ten sam tekst, ta sama kolejność), żeby cache dostawcy go rozpoznał.
- **Kaliber:** eksperymentalny. **Wysiłek:** mały (dyscyplina) .

## D. Długi kontekst vs wyszukiwanie

### nauk-12 — Lost in the Middle: >20% spadku, gdy odpowiedź jest w środku; więcej dokumentów ≈ +1,5%
- **Źródło:** Liu i in., *Lost in the Middle: How Language Models Use Long Contexts*, TACL 2024, arXiv 2307.03172 — https://arxiv.org/abs/2307.03172 (treść, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic, **wysoka** (TACL, wielokrotnie replikowane); modele z 2023 (GPT-3.5-Turbo, Claude-1.3, MPT-30B, LongChat-13B).
- **Twierdzenie (treść):** krzywa U — najlepiej na początku i końcu kontekstu; GPT-3.5-Turbo w QA wielodokumentowym traci >20%, a w ustawieniach 20–30 dokumentów wypada gorzej niż **bez żadnego dokumentu** (56,1%). Modele z rozszerzonym oknem nie używają kontekstu lepiej (krzywe się pokrywają). W open-domain QA 50 zamiast 20 dokumentów daje tylko ~+1,5% (GPT-3.5) / ~+1% (Claude-1.3) przy znacznie większym koszcie. Pytanie przed i po danych naprawia syntetyczny odczyt klucz–wartość (do 100%), ale nie QA.
- **Mechanizm:** pozycyjny bias (primacy/recency); nasycenie czytelnika długo przed nasyceniem recall wyszukiwarki.
- **U nas:** kolejność bloków w prompcie to zmienna ekonomiczna: najważniejsze na początku i na końcu; obcinanie listy rankingowej (ile fragmentów) jako preset; „mieści się w 1M” nie oznacza „zostanie użyte”. Obserwacja z datą: nowsze modele mają mniejszy, ale nadal mierzalny efekt (nauk-14).
- **Kaliber:** naukowy. **Wysiłek:** mały.

### nauk-13 — Self-Route: RAG najpierw, pełny kontekst tylko gdy model mówi „nie da się” — −65% kosztu
- **Źródło:** Li, Li, Zhang, Mei, Bendersky (Google DeepMind), *Retrieval Augmented Generation or Long-Context LLMs? A Comprehensive Study and Hybrid Approach*, EMNLP 2024 Industry, arXiv 2407.16833 — https://aclanthology.org/2024.emnlp-industry.66/ (treść przez lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic, **wysoka** (EMNLP 2024, trzy modele, publiczne zbiory); modele z 2024.
- **Twierdzenie (treść):** przy wystarczających zasobach długi kontekst (LC) średnio wygrywa z RAG, ale RAG jest znacznie tańszy; dla 63% zapytań predykcje LC i RAG są identyczne (70% różnią się o <10 pkt). Self-Route: krok RAG z opcją „nieodpowiadalne” rozwiązuje większość zapytań (82% dla Gemini-1.5-Pro), reszta idzie do LC; koszt −65% (Gemini-1.5-Pro) i −39% (GPT-4o) przy jakości porównywalnej z LC; krok RAG to ~1,5k tokenów wobec 10k–100k pełnego kontekstu. Ostrzeżenie: w zbiorach syntetycznych artefakty budowy zbioru odwracają wynik (RAG 4,58% vs LC 69,32% dla „ukrytego tokenu”).
- **Mechanizm:** samoocena modelu jako router; eskalacja kosztu tylko tam, gdzie tanie podejście zgłasza brak.
- **U nas:** wprost dla selektora kontekstu: tani przebieg po wybranych fragmentach z jawną opcją „brakuje danych” → eskalacja do szerszego kontekstu (albo do koordynatora z 1M); próg i to, co liczy się za „brak”, jako preset. Różnica: u nas „pełny kontekst” to często cały profil/archiwum użytkownika — eskalacja ma koszt rzędu 10–100× kroku RAG, więc odsetek eskalacji jest kluczową miarą do profilu tokenów.
- **Kaliber:** naukowy. **Wysiłek:** mały–średni.

### nauk-14 — Context Rot (Chroma): jakość spada z długością wejścia nawet w prostych zadaniach
- **Źródło:** Hong, Troynikov, Huber, *Context Rot: How Increasing Input Tokens Impacts LLM Performance*, raport techniczny Chroma, 2025-07-14 — https://research.trychroma.com/context-rot (niedostępny z tej sesji); repozytorium: https://github.com/chroma-core/context-rot (README czytany 2026-10-23: brak liczb, tylko „model performance varies significantly as input length changes, even on simple tasks”).
- **Typ / wiarygodność:** vendor technical report, **średnia** (nie recenzowany; 18 modeli wg omówień; liczby z blogów pochodnych — 30%/60% spadków — **niezweryfikowane, nie cytuję**).
- **Twierdzenie (co potwierdzone):** przy stałej trudności zadania i rosnącej długości wejścia niezawodność spada (GPT-4.1, Claude 4, Gemini 2.5, Qwen3 wg omówień); spadek szybszy przy podobieństwie semantycznym (nie leksykalnym) igły i pytania; nawet jeden dystraktor obniża trafność.
- **U nas:** argument, że selekcja kontekstu opłaca się także jakościowo przy 1M okna; mierzyć u siebie (nasz zbiór, nasze modele) zamiast przenosić liczby.
- **Kaliber:** eksperymentalny. **Wysiłek:** mały (powtórka testu na naszych danych).

## E. Routing i kaskady modeli

### nauk-15 — FrugalGPT: kaskada modeli — do −98% kosztu przy jakości GPT-4
- **Źródło:** Chen, Zaharia, Zou (Stanford), *FrugalGPT*, arXiv 2305.05176 (2023) — https://arxiv.org/abs/2305.05176 (abstrakt, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic (preprint, bardzo cytowany), **średnio-wysoka**; „do 98%” to szczyt na konkretnych zbiorach klasyfikacyjnych z cenami z 2023; przegląd z 2025 (arXiv 2503.13505, nieczytany w całości) podaje ~80% przy +1,5% trafności.
- **Twierdzenie (abstrakt):** ceny API różnią się o dwa rzędy wielkości; trzy strategie: adaptacja promptu, aproksymacja LLM (cache, dostrajanie), kaskada LLM; FrugalGPT dorównuje najlepszemu modelowi (GPT-4) przy do 98% niższym koszcie albo poprawia trafność o 4% przy tym samym koszcie.
- **Mechanizm:** uczony wybór kolejności modeli i progów akceptacji odpowiedzi (scorer).
- **U nas:** kaskada jako przepływ w danych (lista modeli, scorer, progi); nasz etap `rule_command` przed modelem routującym (P34) to już pierwszy stopień kaskady. Różnica: ceny z 2023 nieaktualne — liczyć z profilu tokenów z datowanymi cenami.
- **Kaliber:** naukowy. **Wysiłek:** średni.

### nauk-16 — Hybrid LLM: router trudności — do 40% mniej wywołań dużego modelu bez spadku jakości
- **Źródło:** Ding i in., *Hybrid LLM: Cost-Efficient and Quality-Aware Query Routing*, ICLR 2024, arXiv 2404.14618 — https://arxiv.org/abs/2404.14618 (abstrakt, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic, **wysoka** jako wenue; RouteLLM wskazuje ograniczenia: etykiety syntetyczne (BARTScore na MixInstruct), jeden router BERT, ocena tylko w dziedzinie treningu.
- **Twierdzenie (abstrakt):** router przewiduje trudność zapytania i kieruje do małego/dużego modelu; pożądany poziom jakości ustawiany **dynamicznie w czasie testu**; do 40% mniej wywołań dużego modelu bez spadku jakości.
- **Mechanizm:** klasyfikator trudności + próg jakości.
- **U nas:** próg jakości to pokrętło użytkownika (zgodne z filozofią: suwak koszt↔jakość, nie decyzja za niego); nasz klasyfikator routera (P31) może dostać taki próg jako dane.
- **Kaliber:** naukowy. **Wysiłek:** mały.

### nauk-17 — RouteLLM: routery z danych preferencji — >2× taniej; etykiety LLM-sędzi za ~700 USD
- **Źródło:** Ong, Almahairi i in. (UC Berkeley/Anyscale), *RouteLLM: Learning to Route LLMs with Preference Data*, arXiv 2406.18665 (2024) — https://arxiv.org/abs/2406.18665 (treść, lustro HF, 2026-10-23); strona projektu: https://sky.cs.berkeley.edu/project/routellm/ (niedostępna; liczby 85%/45%/35% ze streszczenia wyszukiwarki — oznaczone jako pośrednie).
- **Typ / wiarygodność:** academic + open source, **wysoka** (publiczne benchmarki, kod); liczby per benchmark pośrednie.
- **Twierdzenie (treść):** routery uczone na 80k bitew Chatbot Arena (65k par po czyszczeniu, 64 modele) zmniejszają koszt >2× bez istotnej utraty jakości i generalizują na pary modeli nieobecne w treningu; cztery architektury (ranking ważony podobieństwem, faktoryzacja macierzy, BERT, Llama-3-8B); próg α przelicza prawdopodobieństwo wygranej silnego modelu na decyzję; metryki CPT (odsetek wywołań silnego modelu dla x% odzyskanej luki) i APGR. Augmentacja: etykiety złote (MMLU) i **GPT-4 jako sędzia na ~120k próbek Nectar za ~700 USD** — raz, offline. Pośrednio (strona projektu): −85% kosztu na MT Bench, −45% MMLU, −35% GSM8K przy ~95% jakości GPT-4. Różnica cen małego/dużego modelu wg autorów do 50× (Haiku 1,25 vs Opus 75 USD/M tokenów wyjścia, 2024).
- **Mechanizm:** sędzia-LLM używany do **etykietowania danych treningowych**, nie przy każdym zapytaniu — koszt jednorazowy.
- **U nas:** router jako dane (α jako preset per klasa zadań); nasze własne pary (Haiku/Sonnet/Fable) wymagają własnych etykiet — można je zbierać z ocen użytkownika i z porównań w profilu tokenów. Różnica: my routujemy całe pakiety agentów, nie pojedyncze zapytania — jednostka decyzji większa, ale mechanizm ten sam.
- **Kaliber:** naukowy. **Wysiłek:** średni.

### nauk-18 — Kaskada z mieszanką reprezentacji myśli: jakość GPT-4 za 40% kosztu
- **Źródło:** Yue i in., *Large Language Model Cascades with Mixture of Thoughts Representations for Cost-efficient Reasoning*, ICLR 2024, arXiv 2310.03094 — https://arxiv.org/abs/2310.03094 (abstrakt, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic, **wysoka** (ICLR 2024).
- **Twierdzenie (abstrakt):** kaskada GPT-3.5-turbo → GPT-4 na sześciu zbiorach rozumowania osiąga wynik porównywalny z samym GPT-4 przy 40% jego kosztu.
- **Mechanizm:** spójność odpowiedzi słabszego modelu (próbkowanie Chain-of-Thought i Program-of-Thought) jako sygnał trudności; niespójne → eskalacja.
- **U nas:** eskalacja po spójności wymaga kilku tanich próbek — opłaca się, gdy stosunek cen jest duży (jak Haiku↔Fable); parametry (liczba próbek, próg spójności) jako presety.
- **Kaliber:** naukowy. **Wysiłek:** mały–średni.

## F. Koszt systemów wieloagentowych

### nauk-19 — Anthropic: agenci ~4×, systemy wieloagentowe ~15× tokenów czatu; 80% wariancji to tokeny
- **Źródło:** Anthropic Engineering, *How we built our multi-agent research system*, 2025-06-13 — https://www.anthropic.com/engineering/multi-agent-research-system (czytane 2026-10-23).
- **Typ / wiarygodność:** vendor blog, **średnia** (wewnętrzna ewaluacja, brak publicznego benchmarku; liczby spójne w kilku omówieniach).
- **Twierdzenie (cytaty):** „token usage by itself explains 80% of the variance” (BrowseComp); „agents typically use about 4× more tokens than chat interactions”; „multi-agent systems use about 15× more tokens than chats”; Opus 4 jako lider + Sonnet 4 jako podagenci „outperformed single-agent Claude Opus 4 by 90.2% on our internal research eval”; „multi-agent systems require tasks where the value of the task is high enough to pay for the increased performance”; zadania z wieloma zależnościami między agentami i większość kodowania — nie.
- **Mechanizm:** równoległe osobne okna kontekstu = więcej pojemności rozumowania; koszt rośnie z liczbą wywołań narzędzi i wyborem modelu.
- **U nas:** zgodne z naszą miarą (koszt rośnie z kwadratem długości agenta; 98% odczytów cache). Reguła decyzyjna: fan-out dla badań równoległych (jak R89), jeden tor dla kodu sprzężonego.
- **Kaliber:** duży. **Wysiłek:** mały (reguła doboru w koordynatorze, jako preset).

### nauk-20 — „Towards a Science of Scaling Agent Systems”: 180 konfiguracji — kiedy więcej agentów szkodzi
- **Źródło:** Kim, Gu, Park i in. (Google Research/DeepMind, MIT), arXiv 2512.08296 (2025-12) — https://arxiv.org/abs/2512.08296 (abstrakt + wstęp, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic (preprint), **średnio-wysoka** — kontrolowane porównanie (te same narzędzia, prompty i budżety tokenów), 3 rodziny modeli, 4 benchmarki agentowe; bez recenzji; walidacja poza próbą na GPT-5.2.
- **Twierdzenie (abstrakt/wstęp):** (1) kompromis narzędzia–koordynacja: przy stałym budżecie zadania z wieloma narzędziami cierpią od narzutu wieloagentowego (β=−0,267, p<0,001); (2) **nasycenie zdolności**: gdy pojedynczy agent ma już >~45% skuteczności, dodawanie agentów daje zwrot malejący lub ujemny (β=−0,404, p<0,001); (3) wzmocnienie błędów: agenci niezależni 17,2×, koordynacja scentralizowana 4,4×. Scentralizowana +80,8% na równoległym rozumowaniu finansowym; zdecentralizowana +9,2% na nawigacji web; na planowaniu sekwencyjnym **każdy** wariant wieloagentowy −39…−70%. Model przewiduje najlepszą architekturę dla 87% konfiguracji testowych (R²=0,524). Cytują też: MaAS osiąga porównywalną jakość przy 6–45% kosztu przez dobór architektury per zapytanie.
- **Mechanizm:** fragmentacja budżetu tokenów między agentów; „podatek koordynacyjny” — kontekst globalny musi być stratnie skompresowany do wiadomości między agentami; orkiestrator jako wąskie gardło weryfikacji ogranicza kaskady błędów.
- **U nas:** najmocniejsze pojedyncze znalezisko dla koordynatora: wybór architektury per zadanie z mierzalnych cech (dekomponowalność, liczba narzędzi, sekwencyjność) zamiast stałej liczby agentów; scentralizowany orkiestrator (nasz model) jest w tej pracy najbezpieczniejszy; gdy silny pojedynczy model radzi sobie dobrze, pakiet wieloagentowy kosztuje więcej, niż zwraca. Różnica: nasz koordynator ma 1M okna — „podatek koordynacyjny” u nas to głównie koszt kart wznowienia i powtórnego czytania.
- **Kaliber:** naukowy. **Wysiłek:** średni (cechy zadania jako dane + reguła doboru jako preset).

### nauk-21 — Efficient Agents: metryka cost-of-pass; 96,7% jakości OWL za 0,228 zamiast 0,398 USD
- **Źródło:** *Efficient Agents: Building Effective Agents While Reducing Cost*, arXiv 2508.02694 (2025-08) — https://arxiv.org/abs/2508.02694 (abstrakt, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic (preprint), **średnia** — jeden benchmark (GAIA).
- **Twierdzenie (abstrakt):** pytania: ile złożoności zadania naprawdę wymagają, kiedy kolejne moduły dają malejący zwrot; metryka **cost-of-pass**; Efficient Agents zachowuje 96,7% wyniku OWL przy koszcie 0,398 → 0,228 USD na zadanie (+28,4% cost-of-pass).
- **Mechanizm:** dobór złożoności frameworku do wymagań zadania; pomiar kosztu na udane zadanie, nie na wywołanie.
- **U nas:** cost-of-pass jako miara w profilu ekonomii tokenów (P38 slice 4): koszt dzielony przez zaliczone pakiety, nie przez wywołania.
- **Kaliber:** średni. **Wysiłek:** mały.

### nauk-22 — MAST: dlaczego systemy wieloagentowe zawodzą — 14 trybów awarii z 1600+ śladów
- **Źródło:** Cemri, Pan, Yang i in. (UC Berkeley), *Why Do Multi-Agent LLM Systems Fail?*, arXiv 2503.13657 (v3 2025-10-26) — https://arxiv.org/abs/2503.13657 (abstrakt, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic (preprint, duży zbiór danych), **średnio-wysoka**; κ=0,88 między anotatorami.
- **Twierdzenie (abstrakt):** „performance gains on popular benchmarks are often minimal”; 1600+ anotowanych śladów z 7 frameworków; 14 trybów awarii w 3 kategoriach: błędy projektu systemu, niezgodność między agentami, weryfikacja zadania; potok LLM-sędzi zgodny z ludźmi.
- **Mechanizm:** awarie = tokeny wydane bez wyniku; brak weryfikacji i niezgodność agentów jako główne źródła.
- **U nas:** nasza praktyka „jeden recenzent proporcjonalny do poprawki” i kontrola przed użyciem projektu (Haiku) to „weryfikacja zadania”; tryby awarii jako lista kontrolna do zapisu w kartach wznowienia.
- **Kaliber:** naukowy. **Wysiłek:** mały.

### nauk-23 — PACT: wiadomości między agentami jako rekordy akcja–stan — o połowę mniej tokenów wejścia
- **Źródło:** Huang, Wu, Zhang (SUTD), *What Should Agents Say? Action-state Communication for Efficient Multi-Agent Systems*, arXiv 2606.05304 (2026-06) — https://arxiv.org/abs/2606.05304 (abstrakt, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic (preprint, świeży), **średnia**.
- **Twierdzenie (abstrakt):** swobodny język między agentami szybko rozdyma tokeny i zajmuje wspólne okno; z 5 strategii komunikacji × 2 topologie żadna nie jest zawsze najlepsza; skuteczne wiadomości zachowują informację o działaniach potrzebną dalej. PACT rzutuje każde wyjście agenta na zwarty rekord akcja–stan przed wejściem do wspólnej historii: OpenHands — wyższy odsetek rozwiązanych przy −10% tokenów na rozwiązane; SWE-agent — bez zmiany skuteczności przy **połowie tokenów wejścia**.
- **Mechanizm:** komunikacja jako aktualizacja publicznego stanu, nie proza.
- **U nas:** karty wznowienia i raporty agentów do koordynatora jako rekordy strukturalne (dane), nie narracja; nasz „status lines only” (R62) ma to samo uzasadnienie, teraz z pomiarem.
- **Kaliber:** eksperymentalny. **Wysiłek:** mały.

### nauk-24 — ACON: kompresja obserwacji i historii agenta — szczyt tokenów −26…−54%
- **Źródło:** Kang i in. (Microsoft), *ACON: Optimizing Context Compression for Long-horizon LLM Agents*, arXiv 2510.00615 (2025-10, v3) — https://arxiv.org/abs/2510.00615 (abstrakt, lustro HF, 2026-10-23); kod: https://github.com/microsoft/acon
- **Typ / wiarygodność:** academic (preprint), **średnia**; niezależna (maszynowa) recenzja wskazuje spadki 8–12 pkt na najtrudniejszych zadaniach AppWorld — **niezweryfikowane tutaj**.
- **Twierdzenie (abstrakt):** AppWorld, OfficeBench, Multi-objective QA: zużycie pamięci (szczyt tokenów) −26…−54% przy „w dużej mierze zachowanej” skuteczności; destylowany mały kompresor zachowuje >95%; małe LM jako agenci długohoryzontowi zyskują do +46%.
- **Mechanizm:** wytyczna kompresji optymalizowana **w języku naturalnym**: pary trajektorii (pełny kontekst — sukces, skompresowany — porażka) → silny model analizuje przyczynę → aktualizuje wytyczną; potem destylacja do małego modelu.
- **U nas:** wytyczna kompresji jako edytowalny tekst w danych (nie kod); krok „compress” między pakietami z kompresorem na tanim modelu; miara: szczyt tokenów na pakiet.
- **Kaliber:** naukowy. **Wysiłek:** średni.

### nauk-25 — TRACE: kompresja cykliczna destabilizuje wykonanie — oceniaj każde zdarzenie kompaktowania
- **Źródło:** Nokia Applied Research, *Toward Reliable Context Compression for Long-Horizon Agents: An Empirical Study of Execution Instability*, arXiv 2608.06503 (2026-08) — https://arxiv.org/abs/2608.06503 (abstrakt, lustro HF, 2026-10-23).
- **Typ / wiarygodność:** academic (preprint, „preliminary”), **nisko-średnia**.
- **Twierdzenie (abstrakt):** cykliczna kompresja osłabia wpływ ostatnich interakcji → więcej zablokowanych akcji, powtórnej eksploracji, niestabilności między przebiegami; TRACE ocenia pojedyncze zdarzenia kompaktowania przez sparowane kontynuacje z tego samego stanu i optymalizuje prompt kompresji preferencjami streszczeń (modele zamrożone); wstępne wyniki na AppWorld lepsze od baz (liczb nie czytałem).
- **Mechanizm:** ocena kompresji po skutku wykonania, nie po liczbie tokenów.
- **U nas:** kompaktowanie kontekstu w długich agentach jako eksperyment z weryfikatorem (czy po kompresji agent robi to samo?), a nie tylko licznik zaoszczędzonych tokenów.
- **Kaliber:** eksperymentalny. **Wysiłek:** średni.

### nauk-26 — AgentTaxo: „podatek komunikacyjny” — zduplikowane tokeny w systemach wieloagentowych
- **Źródło:** Wang i in., *AgentTaxo: Dissecting and Benchmarking Token Distribution of LLM Multi-Agent Systems*, warsztat ICLR 2025 (FM-Wild) — https://mlanthology.org/iclrw/2025/wang2025iclrw-agenttaxo (tylko streszczenie wyszukiwarki; pracy nie udało się odczytać).
- **Typ / wiarygodność:** academic (warsztat), **nisko-średnia** — bez dostępu do treści; liczb nie cytuję.
- **Twierdzenie (wg streszczenia):** systemy wieloagentowe mają znacznie wyższe opóźnienia i koszty tokenów przez powtarzane wywołania; głównym źródłem są tokeny zduplikowane między agentami („communication tax”).
- **U nas:** zgodne z nauk-11/nauk-23: liczyć w profilu tokenów udział tokenów powtórzonych (ten sam blok w wielu promptach) — to kandydat na cache lub na współdzielony plik zamiast wklejania.
- **Kaliber:** mały. **Wysiłek:** mały.

---

## Czego nie znalazłem (i dlaczego)

- **Oficjalna dokumentacja cache'owania promptu u dostawców** (Anthropic prompt caching, vLLM automatic prefix caching) — hosty zablokowane w tej sesji; to domena ścieżki „dokumentacja oficjalna”, tu tylko wniosek konstrukcyjny z nauk-10/11.
- **Liczby raportu Chroma „Context Rot”** — raport i README repozytorium bez liczb; omówienia podają 30–60% spadków, ale nie przeniosłem ich (niezweryfikowane).
- **AgentTaxo** — treść niedostępna (brak na lustrze HF, OpenReview zablokowany). **„Scaling LLM-Driven Multi-Agent Systems”** (arXiv 2607.27942, streszczenie wyszukiwarki: koszt rośnie ~liniowo ze złożonością architektury) — brak na lustrze HF, nie cytuję jako znaleziska.
- **Niezależna replikacja** liczb LLMLingua/LongLLMLingua (obie prace z Microsoftu) i A-Mem (bez przedziałów błędu) — nie znalazłem w budżecie.
- **Koszt tokenów Letta/MemGPT na LoCoMo wyodrębniony** — tylko strony dostawców (benchmark własny), nie użyte.
- **Badanie dokładnie naszego układu** (koordynator ~1M okna + tani pracownicy + specyfikacja jako dane): żadne z powyższych nie mierzy tego układu; najbliżej są nauk-20 (orkiestrator scentralizowany, budżety tokenów), nauk-19 (15×) i nauk-23 (rekordy akcja–stan). Pozostaje nasz własny pomiar (profil tokenów P38).
- Poza zakresem celowo: kompresja KV-cache wewnątrz modelu (SnapKV, H2O, StreamingLLM) — niedostępna użytkownikom API.

## Gdzie się zatrzymałem

Budżet wyczerpany zgodnie z planem (12 wyszukiwań; strony i prace czytane przez dostępne kanały). Trzy ostatnie dodane źródła (TRACE, PACT, TokenDance) powtarzały dwa mechanizmy znane z wcześniejszych znalezisk — uznałem to za sygnał nasycenia. Niezbadane kierunki na następną rundę: niezależne replikacje kompresji promptu na modelach 2025–2026; porównanie cache'owania prefiksu u różnych dostawców (ścieżka oficjalna); systematyczne prace o „kompaktowaniu” kontekstu w agentach kodujących (2026) poza ACON/TRACE.
