# Owner requirements log — 2026-09-26

Requirements stated by the owner in conversation, recorded verbatim (voice
dictation, obvious transcription errors fixed in brackets) with a precise
restatement. They extend `MEGA_MASTER_2026-09-16.md` and drive the Loom
paradigm / self-discovery engine (`LOOM_PARADIGM_ENGINE.md`).

## R1 — Selective import of multi-GB archives
> "import selektywny trzeba umożliwić, bo mam archiwa wielo GB"

Catalog without importing; select; import only what matters (by locator).

## R2 — Self-discovery
> "żeby na przykład z zestawu eksportów znalazł wszystko na swój temat - skatalogował, zwersjonował określił status, forki, dopracował się wyekstrapolował w duchu naszej filozofii kodowania, ulepszył i będziemy dalej działać"

From a set of exports find everything about the system itself and about the
coding philosophy; catalog, version, status (implemented / partial / planned /
abandoned / superseded), forks; refine, extrapolate, improve, continue.

## R3 — Meaningful semantics
> "żadnego sensownego grafu nie widziałem, tylko porozrzucane słowa"

Graph of projects, components, versions, concepts, decisions, principles —
readable, hierarchical, with provenance — not regex word soup.

## R4 — Isomorphism search against predefined paradigms
> "wyszukiwania [izo]morfizmów na podstawie na przykład wstępnie zdefiniowanych paradygmatów typu aplikacja wieloplatformowa - [burze mózgów], specyfikację, kody, zapełnione danymi które są w archiwach"

## R5 — Visibly marked inferred data with an expected property
> "te które z dużą pewnością można uzupełnić to zaznaczone na przykład opacity jako jedna z wielu opcji może sygnalizować dane inferowalne z określoną oczekiwaną własnością, nawet dokładnie to określone. Ja ci podaję takie podstawowe parametry ale ty mądrzejsze wymyślisz."

## R6 — Extrapolation from the coding philosophy
> "z tych archiwów ma znaleźć wszystko o sobie i o filozofii kodowania i z danych które ma wyekstrapolować na podstawie filozofii resztę"

## R7 — Self-built abstraction model of applications
> "fajnie by było jakby system sam na podstawie danych, również swojej wiedzy swojego modelu, [...] powszechnej, zbudował sobie model abstrakcji aplikacji ogólnie, z czego się składa wiesz źródła danych interfejsy wejście wyjście protokoły takie rzeczy żeby to sobie sam zorganizował"

A meta-model (components, data sources, stores, interfaces, inputs/outputs,
protocols, events, actors, capabilities, deployment targets, policies) built
from the owner's data + general model knowledge (marked as its own evidence
source, below archive evidence) + induction across projects (candidate →
evaluation → owner approval).

## R8 — Project kinds are paradigms linked by homomorphisms
> "film to jest kolejna kategoria rodzaju projektu, który swoje [homo]morfizmy będzie miał [...] jak apka ma brainstormy, kod, dokumentację, moduły, to film będzie miał brainstorm, postaci, scenopis, powiązania fabułą... i zgodnie z filozofią kodowania mogłeś się na przykład domyślić że oprócz filmów również muzykę będziemy mogli tworzyć i w ogóle wszystko"

"Project" is the abstraction; software app, film, music (and any future kind)
are data-defined kinds sharing roles (intent, brainstorm, specification,
entities/modules, relations, source artifact, build/render, tests/consistency,
versions/forks, output) via explicit, data-defined morphisms that allow
knowledge transfer and inference across kinds. A new kind = a new data file.

## R9 — Second project: a multi-thread, multi-code legal/disciplinary case
> "jak już sam siebie [czat] spisze i udoskonali to drugim projektem będzie wielowątkowe wielokodeksowe postępowanie prawno-dyscyplinarne z kilkoma instytucjami - masa nagrań maili i innych dowodów do przeanalizowania i [burz mózgów] na temat strategii, to do tego Chat sam się zajmie wyszukiwaniem podstaw prawnych, pilnowaniem terminów, i upewnianiem się i takie rzeczy"

A `legal_case` project kind: several parallel proceedings across multiple legal
codes and institutions; evidence corpus (audio recordings → transcripts with
timestamps/speakers, e-mails, documents/scans) with chain-of-custody
(content hashes, immutable originals); strategy brainstorms; legal-basis
search with citations to authoritative sources; procedural deadline tracking
(rules as data, computed from dated events, conservative, each deadline tied
to its legal basis); verification ("upewnianie się": citation checks against
source text, cross-evidence contradiction detection, multi-model
cross-checks). Confidentiality is critical: local-first, encryption, explicit
control over what is sent to external model providers. The system assists; the
owner decides.

## R10 — Brainstorm is an artifact type (unstructured capture), processed automatically
> "burza mózgów to jest że tak powiem rodzaj artefaktu, albo typ opisu danych. zawiera różne informacje na temat na przykład dla aplikacji – funkcjonalności, implementację, zasady fundamentalne, źródła danych. Jednym słowem wszystko nie ustrukturyzowane tylko wymienione i również zakreślone obszary uogólnieniami i to wszystko ma być wiesz automatycznie przetwarzane"

A brainstorm is not a role parallel to specification/modules; it is an artifact
type — an unstructured capture that lists items of any kind (for an app:
features, implementation details, fundamental principles, data sources, …;
for a film: characters, scenes, plot ideas, style; for a legal case:
arguments, evidence, legal bases, strategies) and also outlines areas with
generalizations (umbrella statements that delimit a region, e.g. "everything
is data"). Brainstorms must be processed automatically: segment into items,
detect generalizations and the areas they delimit, classify each item against
the project kind's roles/meta-model, map items into paradigm slots with
provenance, merge/deduplicate across brainstorms and versions, and use
generalizations to infer unstated members (marked as inferred, with the
expected property derived from the generalization) and to flag gaps.

## R11 — Conversation → finished products that honour all user preferences
> "idealna dopracowana wersja Chata GPT to na podstawie samej rozmowy powinna wytworzyć gotowe zgodne ze wszystkimi preferencjami użytkownika produkty"

The conversation itself is the source: it is continuously compiled into
project state (paradigm instances fed by brainstorms, decisions, corrections)
and materialized into finished products of the project's kind (code + tests +
docs, film/music artifacts, legal letters, …). A user preference model —
explicit statements plus preferences inferred from history, each with
provenance, scope, confidence and conflict resolution — acts as enforced
constraints on every generated product, and each product is validated against
those preferences (preference violations are test failures, not style notes).
When the conversation changes, affected products are regenerated
incrementally.

## R12 — Graph structure exists to drive goal-directed context selection
> "właśnie dlatego grafy muszą być tak przemyślane i ustrukturyzowane żeby ułatwiły między innymi dobór węzłów do kontekstów odpowiedni dla celów danego prompta, to też było jedno z głównych założeń projektowych że struktura danych będzie pozwalać na inteligentne zarządzanie kontekstem które pozwoli na olanie [cache'owania] bo samą inteligencją więcej zaoszczędzi"

The graph's primary consumer is the context engine (MEGA MASTER §4.4
`ContextSet`). For each prompt: determine its goal/task type (e.g. implement
module X, write a pleading, extend a scene) → which project roles, paradigm
slots, principles/preferences, decisions and evidence are relevant → select
nodes under a token budget at the right resolution (each node available at
several granularities: label / summary / full / raw evidence), closing over
required dependencies, maximising relevance × authority × freshness ×
confidence with diversity, and explaining why each node was included.
Savings from intelligent selection are the primary cost lever; they must be
measured (tokens and answer quality vs naive full-history / flat retrieval
baselines). Caching is secondary and must never constrain selection; ordering
context stable → volatile lets providers' prefix caches help for free where
they exist.

## R13 — Learn the generator, not the history (GPT note, 2026-09-26)
Full text: `NOTATKA_GPT_2026-09-26.md`. Binding points:
- Context assembly order: stable prefix (constitution, invariants, core
  preferences) → slower project context → dynamic goal-specific tail;
  selection and prompt caching are complementary.
- One process behind every project: observations → epistemic state →
  generalizations → decisions/actions/products. Self-discovery must find the
  *generator of decisions*, not only features/decisions/components.
- One higher-order model across domains: Values → Epistemic principles →
  Action/design strategies; software, research, legal, film are instances.
  Common epistemic core per claim: what is it → how is it known → how certain →
  what does it depend on → what contradicts it → what follows → what is missing.
- Extract invariants, heuristics, defaults, meta-principles, conflict-resolution
  rules and transformation operators; principles are hypothesis-like objects
  (statement, scope, provenance, evidence_for, counterexamples, confidence,
  exceptions, derived_from, predicts, conflicts_with, supersedes,
  validation_status); competing models may coexist.
- Compression: store state + generators + exceptions + provenance, reconstruct
  consequences.
- Values as a model layer: claim → interpretation → affected values → actions →
  trade-offs (no scalar utility); explanations of the form "decision follows
  from principle X protecting value Y under constraint Z".
- Inference ≠ fact: every inferred element carries provenance, method,
  confidence, expected property, supporting principles, competing alternatives.
- Benchmark: temporal holdout / predictive reconstruction.
- Process: build one coherent conceptual model first; all agents share its
  semantics.
- Roles: LEM = epistemic representation; Loom = runtime/transformation;
  ChatADHD = interaction and model synchronisation.

## R14 — Conversation abstraction; lossless full import; one pipeline for live and archived (2026-09-27)
> "abstrakcje rozmowy też trzeba jakoś reprezentować bo będę chciał zaimportować docelowo cały archiwum i mieć co najmniej te dane które są w archiwach, bezstratnie wszystko. a analiza brainstormów to ten sam pipeline [...] dla archiwów jak dla rozmowy na żywo, bo ta rozmowa właśnie opisuje konstrukcje struktury która powinna być odzwierciedlona w grafie, sekwencyjnie. co nie znaczy, że na inny sposób ekstrakcja danych też nie powinna bardziej strukturalnie zachodzić. są różne podejścia i z żadnego nie rezygnujemy."

- The conversation itself is a first-class abstraction in the model (turns,
  branches, edits, attachments, tool calls, citations, provider metadata).
- Full import is **lossless**: every field of every export survives (at least
  what the source archive holds); unknown fields are kept verbatim.
- Live and archived conversations go through the **same** extraction pipeline;
  the sequence in which a conversation builds a structure is preserved in the
  graph (ordered construction steps), alongside other, more structural
  extraction approaches — none is dropped.

## R15 — Everything the application uses is in the graph (2026-09-27)
> "graf ma obejmować wszystkie informacje: profil użytkownika, abstrakcje projektów też powinny być w grafie, wspomnienia, konektory - to wszystko z czego aplikacja korzysta musi być w grafie opisane, krawędzie grafu też dosyć bogatą strukturę będą musiały mieć"

User profile, project abstractions (paradigms, roles, morphisms), memories,
connectors/providers, settings — all described as graph entities; edges carry a
rich structure (typed, qualified, time-scoped, with provenance and assessment).

## R16 — Visualisation beyond 2D/3D (2026-09-27)
> "wizualizacje grafu trzeba pomyśleć, jak wykorzystać perspektywę, kanały alfa i tak dalej żeby wybraną podstrukturę najbardziej uwidocznić. [...] dwa a nawet trzy wymiary to za mało [...] dużo suwaków będzie potrzebnych wskazujących szczegóły kontekstu [...] intuicyjne przełączanie widoków graf / lista."

Focus+context with perspective, alpha, depth, and many sliders (filters /
weights over the claim space) to bring a chosen substructure forward; seamless
graph ⇄ list switching.

## R17 — Memory isomorphism across providers; provider-inspired interface profiles as data (2026-09-27)
> "powinien być zachowany homo[mor]fizm czy izomorfizm ze wspomnieniami z innych eksportów. [...] można by jeszcze zrobić żeby importy wyglądały identycznie jak w źródłowej apce, całe profile interfejsów inspirowane oryginalnymi dostawcami jako możliwe widoki [...] w kodzie tak uogólnione żeby te interfejsy to były tylko dane [...] docelowo [...] opcja przeszukiwania wszystkich apek i nie tyle klonowania ale odwzorowywania interfejsów na tyle na ile prawa autorskie pozwalają."

Memories from different providers (ChatGPT memories, Claude memories/projects,
…) map onto one memory abstraction via morphisms. UI "profiles" inspired by the
source apps are data (layout + interaction logic, own graphics), selectable as
views; later, discovery of other apps' interface logic — mapping, not cloning,
within copyright limits.

## R18 — Import scope defaults and options (2026-09-27)
> "pełen import to definitywnie musi wszystkie dane z paczki wchłonąć, a selektywny [...] domyślnie powinien uwzględniać wszystkie rozmowy pełne zawierające informacje na temat, wszystko o projekcie jeśli to jest w projekcie, wszystkie załączniki i zalinkowane rozmowy, uwzględnione w rozumowaniu, wszystkie powiązane dane. [...] możliwe do zmiany przez użytkownika. na przykład [...] możliwe do odznaczenia pełne treści rozmów łącznie z niezwiązanymi tematami jeśli rozmowa wielowątkowa, wiadomości z innych rozmów z podobnego okna czasowego opcjonalnie dołączane, streszczenie kontekstu z podobnych okolic czasowych, i tak dalej."

Full import = everything. Selective import defaults: whole on-topic
conversations, everything inside a matching project, all attachments and
linked/referenced conversations, all related data used in reasoning. Every rule
is an owner-toggleable option (strip off-topic threads, add same-time-window
messages, add temporal-neighbourhood summaries, …).

## R19 — Privacy and threat model in the provider abstraction; "paranoid" mode (2026-09-27)
> "czy abstrakcja dostawców obejmuje szczegółowe ustawienia prywatności [...] z oceną jakich wysiłków wymagałoby zdobycie danych przez atakującego? [...] lokalny model na telefonie może być skompromitowany pegasusem. Jak to ma wojskowe standardy spełniać [...] dokładna informacja użytkownika na co nie mamy wpływu oraz czego nawet nie wiemy że może być, jak ktoś wybierze tryb paranoid to musi mieć pełną mapę ryzyk [...] model musi być tego świadom."

Every provider/connector/storage/device path carries a privacy profile: data
exposure (who can read what, where, retention, jurisdiction), attacker models
and the effort each needs (remote API compromise, legal compulsion, device
malware such as Pegasus-class spyware, physical/acoustic/optical side channels
— voice through open windows, shoulder-surfing optics), what Loom can control
vs cannot vs does not know. A "paranoid" mode shows the full risk map for the
current configuration and routes accordingly. The model is comprehensive even
where the UI keeps details in the background.

## R20 — Copy vs link is the owner's choice; watch and auto-export (2026-09-27)
> "czy w grafie będziemy powielać importy, czy tylko linkować do danych w plikach? [...] nie podejmujemy decyzji zostawiamy użytkownikowi [...] kod ma obsłużyć co sobie użytkownik zażyczy, łącznie z odwzorowaniem w grafie wszystkich danych i meta danych i pilnowaniem cyklicznym czy pliki nowe nie doszły, zautomatyzowanym eksportem ze źródeł, jak zawsze - wszystkie możliwe opcje"

Retention policy per source is an owner option (copy into blob store / link to
the original file by hash / both), with complete graph mapping of data and
metadata either way; periodic watching of sources for new files; automated
export from source services where they allow it.

## R21 — Complete export interpretation; coordinated multi-views; analysis-methods programme (2026-09-28)
> "system musi umieć wszystko z nich zinterpretować, powiązać załączniki, sprawdzać sam na internecie strukturę a w razie jakby nie znalazł to się domyślać po nazwach i powiązaniach [...] widoki interfejsu inspirowane oryginalnymi apkami dostawców, również archiwalnymi wersjami [...] ograniczanie użytkownika do alternatywy rozłącznej [nie] wchodzi w grę. użytkownik Jak będzie chciał to sobie będzie mógł odpalić pięć różnych widoków grafów, współzależnych od siebie [...] interfejs ma co do zasady dawać dostęp i wizualizować na wszystkie możliwe sposoby wszystkie istotne dane, połączenia, sterowanie"
> "fajnie by było mieć opracowaną w miarę uniwersalną i pojemną reprezentację struktury argumentu, myśli [...] znaczenia słów [...] z kilku różnych perspektyw [...] geometrycznie, topologicznie, porównać matematycznie, określając na przykład uniwersalność wzorca, szczegółowość która nie jest odwrotnością uniwersalności [...] dla każdego konkretnego analizowanego tematu przyda się określić jego uogólnienie, kategorię, przykładem czego on jest, jakiej relacji [...] kiedy ostatnio była podobna omawiana w szczególności w innym temacie żeby zwrócić uwagę na [...] wnioski które nie zostały wypowiedziane"

- OpenAI/Anthropic exports interpreted completely and losslessly (all files and
  fields, current and archival formats); unknown structure looked up online,
  else inferred from names and relations (marked inferred); attachments linked.
- Provider-inspired interface profiles (incl. archival app versions) as data;
  functionality replicated as far as possible.
- Views are never mutually exclusive: any number of coordinated views
  (graphs, lists, timelines…) linked for navigating a multi-dimensional space.
- A research programme on analysis methods (argument/thought/meaning
  representation from several perspectives; generalisation levels;
  universality and specificity as separate measures; cross-topic recurrence;
  unstated conclusions) — see `docs/HANDOFF_2026-09-28.md` §5.1.
- Scan the whole codebase for generalisations and for choices that should be
  owner options.

## R22 — Thought-structure generalisation, atomic elements, topic-change recognition (2026-09-28, to GPT)
Verbatim source: `docs/research/inputs/gpt-conversation-window-2026-09-28.md`
(turns 1–5; also quoted in `docs/research/PROGRAMME_2026-09-28.md`).
> "potrzebujemy jakiegoś uogólnienia rozmów, które by oddawało strukturę argumentu, myśli czy idei, żeby później to łatwo matematycznie porównać."
> "Chodzi mi o struktury na tyle ogólne jak na przykład syllogizmy jako podstawowe elementy tych struktur. [...] jak jest rozmowa na jakiś temat, ale struktura jest bardzo popularna w uogólnieniu w innych tematach, no to to uogólnienie trzeba wyłapać."
> "To jest przykład właśnie taki atomowy element. Innego rodzaju rzeczy to będą uogólnienia albo sprecyzowania, warunki rozgałęzienia i wiesz, takich atomowych rzeczy każdą prawie że strukturę myśli można opisać."
> "Dobrze by było też rozpoznać ogólnie zmianę tematu, żeby wyciągnąć z rozmów wielowątkowych, gdzie na przykład dopiero pod koniec się jakiś temat projektu omawia [...]. I także odwoływać się do umieszczonych w grafie danych. W ich kontekście umieszczać nowe, a w razie potrzeby updateować - rozbudowywać graf, czasem upraszczać, zawsze udoskonalać."

Atomic elements of thought structures (syllogisms, generalisations and
specialisations, branching conditions, ...) composable into any structure; the
same structure recognised across topics; topic changes inside multi-thread
conversations detected; new material placed in the context of, and updating,
the existing graph.

## R23 — Argument structure lives in the graph; multi-track experiments (2026-09-28, to GPT)
> "a ja zauważyłem że chyba niepotrzebnie mnożę byty bo przecież struktura argumentu, myśli, idei, to wszystko z założenia ma być reprezentowane w grafie więc szukanie powiązań to będzie szukanie podobnych struktur w grafie. Co o tym myślisz? Przemyśl jak to ugryźć. eksperymentuj wielotorowo, żeby z wyników skompilować w miarę optymalny flow. nie przerywaj pracy, o ile nie stwierdzisz że wszystko już zrobione idealnie"

No parallel store of "thought entities": structure of arguments, thoughts and
ideas is represented in the one knowledge graph; finding connections = finding
similar structures (sub-graphs) in it. Explore several paths in parallel and
compile the best flow from the results.

## R24 — The cheap second model for semantic and structural analysis (2026-09-28, to GPT)
> "a wiesz co jeszcze myślę że chyba nie jest wykorzystywane w odkrywanie struktury? a może jest, bo nawet nie patrzyłem, a Claude mówił, że wszystko odtworzył i sprawdził przy porcie. No ale znowuż przed portem też nie działało tak jak powinno być. bo jeszcze przed wyłonieniem loom w apce pytonowej był drugi model do ustalenia właśnie do analizy semantycznej i strukturalnej. żeby można było ustawić jakiś bardzo tani i zadawać mu proste pytania o ustrukturyzowanie, uogólnienie rozmowy i tak dalej"

The separately configured cheap `semantic_model` must actually drive semantic
and structural analysis (structuring, generalising conversations) — in Loom
and in the Python app. **Finding (2026-09-29):** Python 0.7.10 `Config.auto_upgrade`
silently reset any `semantic_model` containing `claude-haiku-4` (which includes
the recommended `anthropic/claude-haiku-4-5`) to `""`, disabling LLM semantic
analysis; the Codex branch removed the reset in Python and C++ (mirrored).
This is a plausible reason the owner "never saw meaningful results".

## R25 — Search by every method; words and regex are only complementary (2026-09-29)
> "żeby się nie sugerować słowami, że słowa kluczowe i regex to tylko metody uzupełniające, żeby na wszystkie sposoby szukać i znaleźć wszystko co jest potrzebne"

(Said to GPT earlier in the session, outside the saved MHT window; here in the
owner's words of 2026-09-29. GPT's paraphrase: "lexical/regex signals are
secondary omission diagnostics", `docs/research/JEV_USAGE_RULES_2026-09-28.md`.)
Retrieval and relevance use ALL methods together — lexical, regex, morphology,
vector/embedding similarity, graph structure, model judgements — and no single
channel (lexical or semantic) may define relevance or veto another. Recall
first (union of evidence, coverage-first), precision by verification; every
inclusion/exclusion explainable per channel.

## D1 — Python compatibility is no longer required (2026-09-29)
> "nie musi być zgodności z pytonem to był tylko plan minimum żeby chociaż to działało, a dużo już dalej poszliśmy"

The Python/C++ on-disk compatibility invariant (schema v4, CLAUDE.md, model I10)
was a minimum plan. It no longer constrains new work. Existing compat tests stay
green as regression sentinels until a deliberate schema migration replaces
them; schema changes are allowed when deliberate (migration + note in STATE.md).

## D2 — Develop linearly while there is no regression (2026-09-29)
> "Ja myślę że jedziemy liniowo tak długo jak żadnego regresu nie ma"

One development line (`claude/chataddhd-cpp-loom-core-IRGRN`); helper branches
are merged forward promptly; no long-lived divergence. Progress is a ratchet:
a change may not worsen any tracked test or quality metric (baseline in
`docs/STATE.md` §4/§5); if something regresses, stop and fix or revert before
continuing.

## D3 — Real data permitted and preferred; every available key (owner, 2026-10-12)
> "można użyć wszystkich kluczy jakie są dostępne jak żaden nie działa to poproś o
> następny i wszystko na danych rzeczywistych dane syntetyczne porzucamy to już było
> gdzieś ustalone nie znalazły się dokumentacji?"
>
> Correction a moment later: "sorry może powiedziałem zbyt ogólnie coś co było w
> jakimś szczególnym kontekście [...] traktuj to jako zezwolenie na korzystanie
> z prawdziwych danych a nie porzucenie syntetyków całkowite, tylko tam gdzie jest
> wybór to korzystanie z prawdziwych"

Answer to the two yes/no questions of `ACCEPTANCE_TESTS_2026-10-11.md` §7
(money, privacy) and a standing rule, as corrected:
1. Sessions may use **every API key available** to them (secrets, proxy-injected
   credentials, the thread-7 key); when none works, ask the owner for the next
   one. No per-task dollar cap beyond that; the ×10 confirmation rule
   (`CLAUDE.md`) and estimate-before / actual-after (`AGENTS.md`) stay; keys are
   never committed.
2. **Real data is permitted**, including paid model calls on the owner's real
   exports and sending their fragments to providers reachable through those keys,
   and **preferred wherever there is a choice** between real and synthetic input.
3. Synthetic corpora are **not abandoned**: they remain for what needs exact
   ground truth or must not touch private data (mechanism tests, scale padding,
   fixtures, `LOOM_CONCEPTUAL_MODEL.md` §7.3). Where both exist, the real run is
   the evidence and the synthetic run is the control.

The decision had not been recorded before this entry (checked 2026-10-12:
`OWNER_REQUIREMENTS`, `AGENTS.md`, `STATE.md`, `THREADS`, `LOOM_CONCEPTUAL_MODEL`;
only indirect traces: T12 "on real exports", STATE "nothing ran on real exports").
Unchanged: the real hold-out rule of `AGENTS.md` (`eval/real-holdout-key` stays
unread until the owner says otherwise).

## Requirements from the 2026-09-29 discussion (GPT note R26–R38)

Source: `NOTATKA_GPT_2026-09-29_KOMPILATOR_INTERAKCJI.md` (written by GPT from the
owner's conversation; §-references below). Only statements the note marks **[U]**
(owner) are recorded here as requirements; **[P]** proposals and **[H]**
hypotheses stay in the note, the conceptual model (v1.1 section) and
`ACCEPTANCE_TESTS_2026-09-29.md`. Where the note quotes the owner, the quote is
verbatim; otherwise it is GPT's paraphrase (marked "paraphrase"). New
requirements do not mark any existing feature as done.

## R26 — Five layers, never conflated (note §1, §2)
Conversation history · project/knowledge state · active task specification ·
query context · request/execution are different objects. History is a source;
request context is a compilation result. The owner corrected earlier
shorthand that equated "whole context" with history. Audit views: what happened
in the conversation / what currently holds / what is in the graph / what was
actually sent to the model. (paraphrase)

## R27 — Retrieval is semantic; words/regex are a shadow pipeline (note §1, §4, §20.1)
> "Ja mówiłem o wektorach, o odpytywaniu małego modelu i klasyfikacyjnego modelu"

Preferred retrieval: vector representations, the cheap model (interpretation,
generalisation), the Jev classifier (judgements of supplied hypotheses) and graph
structure. Keyword/regex matching complements them and additionally serves as an
independent *shadow pipeline that evaluates the main one*: material found by
lexical search but missed by the semantic pipeline is a diagnostic gap
(`C_lexical − C_semantic`); verified "lexical rescue" cases become regression
cases, "lexical noise" changes nothing. Extends R25. (paraphrase of §20.1)

## R28 — Scope and detail are two independent controls; a plan is a structural query (note §7)
How widely to reach for material (scope) and how much / in which representation
to take from each element (detail) are separate, adjustable by the user also
after an unsatisfying answer ("more detail at the same scope" vs "wider scope at
similar detail"); a token budget is a constraint, not their definition. If a
plan of the product exists (owner's example: a formal notification built on an
earlier agreed plan), material is selected from the graph *according to that
plan*, with per-thesis scope/detail, counter-evidence and gaps. (paraphrase)

## R29 — Composable workspace; any number of coupled views (note §8)
Independent UI elements and parent containers (fullscreen views, windows, docked
windows, tabs, splits, adaptive layouts) that the user composes. Any number of
simultaneous views, including several graphs with coupled selection/presentation
parameters, for moving through a multidimensional space; never "graph OR table".
Parameters may be shared, independent or coupled by a chosen dependency
(selection, time, branch/version, relations, evidence filters, scope/detail,
projection, scale/opacity). Five graphs was an example, not a maximum. (paraphrase; extends R16/R21)

## R30 — Provider profiles are detachable presets (note §9)
Conditional assignment by import source: Anthropic export → Claude-like view of a
chosen earlier app version (owner said "the second-to-last" — pin a concrete
reference/date when creating the preset); OpenAI export → web-app-like view
adapted to a phone. As full a reproduction of interaction and tools as possible,
not only colours; a simple/compatibility mode that assembles context/request as
the source app did within reproducible limits, alongside Loom's compilation, with
freedom to mix parts. Settings remain user-changeable and composable. Fidelity
boundary: an export is not a transmission record; missing memory/system
instructions/hidden state/history-truncation algorithms are never fabricated;
adapters report native / equivalent / limited / unavailable. (paraphrase; extends R17/R21)

## R31 — Interaction composer (note §10)
The user can combine, e.g., the ChatGPT interface, Loom's context selection,
tool functionality associated with Claude, n prompt variants, contexts composed
in several ways and i models. A preset may bundle these but nothing may depend on
another element without real necessity; changing a UI profile must not implicitly
change model, permissions, privacy or context selection. UI = composition of
components; execution = composition of transformations bound by data and events.
(paraphrase)

## R32 — A latent space of reasoning models emerges from the data (note §11, §12)
The owner sees a space hidden in the data/graph that can be explored and sampled,
with samples materialised into a chosen query element (example: select
`user → philosophical preferences`, set a sampling resolution, render to a
system prompt). What matters are reasoning models, assumption structures,
transformations, relations, exceptions and ways of updating a position; names of
schools/authors are only helper anchors. **Not** a hand-made global database of
`idealism/physicalism/scepticism` with arbitrary values, and not hand-designed
axes. Within a studied scope, admissible, non-trivial, unrefuted stances are
materialised systematically as *separate* configurations and their results
compared — never averaged into one personality. System prompt is one example of
a target; the same mechanism feeds many interaction elements. Sampling resolution,
source detail and output length are different parameters. (paraphrase)

## R33 — Automatic experiments under user budget and triggers (note §14)
The user sets a budget and triggers that launch chosen experiments, including
automatic selection of parameters and models during work and comparison of answer
quality. Trigger examples (open): user correction, poor rating, new task type,
model disagreement, cost/time overrun. The variant space (prompts, contexts,
detail, models, renderers, examples, tools, evaluators) is configurable; the
product P×C×M is not executed exhaustively. (paraphrase)

## R34 — Consolidate each refinement block into one current instruction (note §15)
> "każdy blok taki właśnie precyzowania przez użytkownika, dogadywania się z modelem, to trzeba do jednej tury streścić. Znaczy nie tyle streścić, co optymalnie i systematycznie wyłożyć."
> "nie z tym, że użytkownikowi się nie podoba, tylko z modyfikacją prompta."

The next request carries the *modified prompt*, not the history of dislike plus
rejected attempts. Primary goal: **semantic purity of the context**; token saving
is secondary; improved quality is the owner's expectation, not yet demonstrated.
Source history is kept; the compiled turn is a derived input with a map to
utterances and a version. (Quotes verbatim.)

## R35 — Entities: not everything is `same_as`; several true projections (note §21)
Historical "separate projects" may be spelling/ASR/alias variants of one entity,
successive lineage stages (predecessor, successor, fork, supersession, evolution),
independent approaches to parts of a larger project that later *converge*,
components/capabilities of a whole, or shared modules used by many projects.
Merge only real identity; keep evolution, convergence, composition and sharing as
rich time-scoped, provenance-carrying relations. Several simultaneous projections
of one graph (historical-design, architectural, ecosystem, generative); none is
"the" true one. Later recognition of a whole does not rewrite history. (paraphrase)

## R36 — Philosophy as generator; seeding the graph with extrapolated data (note §22)
Above the coding projects sits a layer of philosophy/way of thinking that shapes
their generation; the goal is to model the *way projects are generated* well
enough to reconstruct/extrapolate unsaid and unrealised elements from parts of
the archives and the existing ecosystem. Links run through concrete generative
relations/operators, and project practice may correct a principle. Graph seeding
= proposing missing nodes, candidate relations, qualifiers, exceptions,
abstraction levels, capability implementations, applications of shared modules,
generalisations/specialisations, consequences, conflicts and missing evidence —
always distinguishable from observation. Rich edges are required so such
dependencies can be described, compared and used formally; `related_to` is not
enough. If later sources confirm a seeded hypothesis, keep its genealogy and
prediction timestamp (ground truth for measuring the generator). (paraphrase)

## R37 — Model of models: the epistemics of cognitive instruments (note §23)
The system needs a model of the models it uses: biases, typical failure modes and
quality that depends on the kind of operation, because the system co-depends on
the models used to build it. Owner's observation (a task-class assessment, not a
benchmark): even the newest models currently generalise the owner's philosophy at
best "medium / medium-weak". This must lead to caution in promoting such
generalisations and/or an important compensating element of the request.
(paraphrase; observation recorded as the owner's)

## R38 — Owner-driven Jev exploration; Jev Lab (note §5, §6, §20.2)
The owner wants to learn the classifier's behaviour first-hand, experiment with
how questions are asked and give agents concrete hints (kept as annotated
knowledge with provenance, not as statistics). Three families of questions to the
classifier: relation relevance while building the graph, relevance of candidate
subgraphs during context selection, and needed level of detail. An application
for Pydroid over OpenRouter ("Jev Lab": query parameters GUI, contextual help,
files/parsing, experiment history, automatic statistics) was produced in the GPT
conversation (`JevLab_Pydroid.py`, `JevLab_sources.zip`, `README_PL.md`, test
report) — **not in this repository**. Also supplied: a Gemini document on
query optimisation for the classifier (research material, not a verified
TypeSafe specification). (paraphrase)

## R39 — Seeding a new user's graph; profile initialised as `unknown` (owner, 2026-10-04)
Every new user starts with a seeded graph, not an empty one: the built-in pack
contributes types, analysis methods, presets and the default working
assumptions of the application. If the user imports archives, extraction
methods seed projects, topics and preferences from them (marked as inferred,
never as observation). The user profile exists from the start with its fields
explicitly `unknown`. "Unknown" is a stated value, not a missing one, and has three states:
`unknown` (not known yet, may ask), `declined` (user chose not to answer, do not
ask again), `never` (never ask and never infer). Values are filled in by an
onboarding wizard and later by ordinary use. The wizard is primarily a conversation in which
the model actively interviews the user, summarises what it understood and asks for
confirmation. A structured form saves the same data. Every part can be skipped, paused,
resumed and repeated. The wizard covers who the user is, their work, how they want to be
addressed, concrete examples of what the application should and should not know
about them, and privacy choices: which categories of information may be stored
and at what level of detail and sensitivity, what may be sent to which model
provider, what may be inferred versus only recorded when stated explicitly, and
retention. Everything lands in the graph with provenance (`user_stated`,
`model_inferred` + confirmed/rejected, `form`). Privacy rules are graph nodes
that the selector and knowledge writer respect. Later, the model may propose new
preferences during ordinary chat; how they are stored (ask / candidate /
automatic) is itself a setting. A "what the app knows about me" view allows
editing and deletion with history. (paraphrase of owner's idea)

## R40 — Defaults are layered graph nodes the user can override, disable or exclude forever (owner, 2026-10-04)
A user who changes nothing still gets a working application: the default working
assumptions live in the graph as nodes from the built-in layer. Each default can
be overridden, disabled (kept but unused) or removed permanently when the user
never wants it referred to. Permanent removal leaves a persistent exclusion
marker, so that a later pack update cannot silently reintroduce it. Whether new
defaults touching an excluded area arrive as proposals or are applied directly
is a user setting. Resolution is explainable: for every effective value the system can
show which layer it came from and why it applies. One layering/exclusion
mechanism serves preferences, analysis methods, prompts, privacy consents and
application profiles alike. (paraphrase)

## R41 — Analysis methods are in the graph (owner, 2026-10-04)
Following "everything in the graph": methods of semantic and structural analysis
are graph entities. This covers lexical/regex, TF-IDF, BM25, vectors, Jev, cheap
and frontier models and graph structure, together with their recipes, prompts
(version + hash), parameters, presets and user-made combinations. Each result has a
"produced by" edge to the exact method version and parameters. Assessments of
methods (experiment results, model profiles, R37) are claims about these
entities with evidence and date. A model receiving context can therefore see which methods exist
and what is known about their strengths and weaknesses. The user may use any
method in any combination and tune its parameters. Default methods are pack data, not code.
(paraphrase)

## R42 — Which string literals may stay in code (owner, 2026-10-05)
Data does not live in code. The test for any literal is: **could someone want
to change it without changing the algorithm?** If yes, it is data (pack,
profile, setting or preset), never a literal. Allowed in product code (`loom/src`,
`server`, `cli`, `web/src`, `android`) are only literals whose change requires a
code change anyway:
1. **Names of the code↔data contract**: keys the code reads (`"schema"`,
   `"id"`), schema identifiers and versions (`loom.graph_packet/1`), file names
   inside the pack.
2. **Constants fixed by external standards**: HTTP methods/headers, SSE `data:`,
   JSON-RPC/MCP method names, MIME syntax, file signatures, algorithm names
   (`sha256`), grammars of standard formats (RFC 3339, JSON). This includes a
   regex that parses a standard. A regex that recognises domain content (e.g. a
   "decision" in a conversation) is data.
3. **The mechanism's own vocabulary**: lifecycle states, machine error codes,
   engine operation names. The user sees them only through translations in data.
4. **Serialization formats that are part of a contract** (date/number
   formatting required for compatibility).
5. **Minimal bootstrap needed to find the data**: data-dir env var name, data-dir
   sentinel file, pack location.
6. **Developer diagnostics**: logs and assertions, preferably as an event id
   plus parameters, with any human-readable text in data.

Tests and fixtures are not product code. Never allowed in code: user-visible
text (all UI is data, with language versions), prompts and recipes, addresses,
endpoints and model names, domain dictionaries/lists/patterns, thresholds,
weights and limits, categories, type/role names, scenarios. A missing or broken
pack yields an explicit error or the embedded pack *generated from data*, never
hand-written hidden defaults in code. Enforcement: an automated guard test scans
literals in product code and passes only these categories, through an allowlist
whose every entry carries its category and reason (owner: thread 11, building on
its data-in-code inventory). (owner's question answered by Claude and accepted
by the owner)

## Requirements from the 2026-10-10 discussion (owner to Claude, R43–R50)
Answer to the open choices of the data-graph-engine branch, then a design
improvisation the owner explicitly marked as one projection among many ("sprzeczności
nie są błędem tylko projekcją ewolucji"). The generalisation and populated
lists are in [GRAPH_ENTRY_PARTIES_ECONOMY_2026-10-10.md](GRAPH_ENTRY_PARTIES_ECONOMY_2026-10-10.md).

## R43 — Every block needs an initiator (owner, 2026-10-10)
> "Zablokowanie czegokolwiek co do zasady musi być zainicjowane przez użytkownika
> (lub inne okoliczności z solidnym uzasadnieniem)."

The engine and UI do not refuse, narrow or withhold anything on their own
initiative. A block is either initiated by the user (a setting, a consent, an
explicit choice at some scope) or rests on a solid, stated justification (data
integrity, security, an external constraint, a capability that does not exist).
Missing capability degrades the operation and says so; it does not forbid the
rest. (paraphrase)

## R44 — Unrecognised input enters the graph, annotated (owner, 2026-10-10)
> "Jeśli coś jest nierozpoznane to w taką adnotacją wchodzi do grafu, czyli
> korpusy wszystkich danych."

Nothing is skipped silently and nothing fails a whole import because one part is
unknown. Unrecognised data is kept verbatim in the graph with its recognition
status and source location; the graph holds the corpus of all data. (paraphrase)

## R45 — The graph's entry view: what a query without parameters returns (owner, 2026-10-10)
> "graf to nie jest drzewo żeby był korzeń, a chodzi o to, co dostajesz odpytując
> graf bez parametrów"

The null-context view shows the main areas, among them: the graph's and the
application's self-model (goals, philosophy, capabilities); technologies,
adapters, profiles and interfaces; settings and profiles linked to the
preferences that resolve them; users; methods. The list is open and evolves.
(paraphrase)

## R46 — Users are parties of many levels; the user is a node, not the centre (owner, 2026-10-10)
> "użytkownikiem może być organizacja, pracownik, pracownik pracujący nad
> projektem, grupa zespołów pracujących nad gałęzią projektu, rodzina analizująca
> za i przeciw w kontekście ważnej decyzji życiowej…"
>
> "użytkownik jest osadzony jako node sieci, najdokładniej opisany i wysokoważony
> przy wielu decyzjach ale formalnie obiekt typu węzeł sieci. nie będzie
> użytkownikocentrycznie"
>
> "preferencje, filozofie, zainteresowania, obszary czy to geograficzne czy
> zawodowe czy teleologiczne, to jest pakiet właściwości, klasa człowiek"

The user is not a precisely defined single entity; it can be seen through several
perspectives (hierarchy of beings, philosophies of beings and contexts). The
owner asked how node types/classes are solved in the graph. (paraphrase;
question answered in the design document §5)

## R47 — Every setting resolves across scopes with stated conflict rules (owner, 2026-10-10)
> "Dla każdego ustawienia masz na przykład preferencje aplikacji, użytkownika,
> rozmowy, i określone zasady rozwiązywania konfliktów między nimi?"

Extends R40 from two layers (built-in, user) to a chain of scopes, each a graph
node, with a per-setting conflict rule that is itself data. (paraphrase)

## R48 — Methods sit next to settings as research-backed presets; experiments at hand (owner, 2026-10-10)
Methods of semantic analysis, graph building and exploring several paths at once
(automatic, semi-automatic, manual, with full multi-criteria evaluation) are
offered as presets optimised for concrete uses: the best models, prompts and
approaches found in research (e.g. Jev asked for the relevance of a claim at a
node for an edge of a given kind), with the API requests and benchmarks they were
proposed from. When it follows from the user's profile, an experiment is at hand,
e.g. at import: run a fragment through five prompts and let the application
analyse the outcome. (paraphrase; extends R23, R33, R41)

## R49 — Context order: stable facts first, the question last (owner, 2026-10-10)
> "podane najpierw fakty na temat podstawowych encji, później kontekst konkretnego
> projektu, później szczegółowy kontekst odpowiedniego rodzaju – na przykład
> wyselekcjonowany kod do którego się odwołuje analizowana funkcja, na końcu pytanie"

Proposed for saving on prompt caching and for sense. (owner's proposal,
confirmed by Claude: stable-prefix-first ordering maximises cache reuse)

## R50 — Resource economy: duplicates allowed, flagged, deduplication proposed (owner, 2026-10-10)
> "Silnik oczywiście nie może zabraniać duplikatów ale powinien zwracać uwagę …
> że wystąpiło duplikowanie ale proponuje przypięcie niezmiennych danych do kilku
> węzłów"
>
> "każdy powtarzający się jakąś częstotliwością blok danych o jakiejś wielkości,
> oczywiście o ile użytkownik sobie życzy, powinien być analizowany pod kątem
> kosztów i potencjalnych korzyści deduplikacji i linkowania"

We avoid easily avoidable waste. Native compression/deduplication analysis is
opt-in, never forced. Beyond linking, a generator: a description of where to get
the data, smaller than the data, with a known computational cost. (paraphrase)

## R51 — Storage, encryption and security are a first-class, visible area (owner, 2026-10-10)
> "magazyn szyfrowanie bezpieczeństwo. oferujemy magazyn na serwerze na którym apka
> webowa chodzi, wszystkie chmury użytkownika oraz jego lokalny Storage, w pełni
> konfigurowalne do granic absurdu, zgodnie z filozofią. na przykład szyfrowanie
> kwantowo odporne musi być"

In the most basic graph view the user must see at a glance where their data lives,
who can reach it and how it is encrypted. Storages: the server the web app runs on,
every cloud of the user, local storage — any combination, per data category, with
sync and conflict rules, configurable "to the point of absurdity". Post-quantum
cryptography is required. Claude's note: AES-256-GCM (already used by the Python
app) stays safe under Grover; what breaks is key exchange/signatures (RSA, EC), so
ML-KEM (FIPS 203) and ML-DSA (FIPS 204), preferably hybrid with X25519, plus
Argon2id for passwords; the RSA-OAEP in `credential_handoff_v2` is to be replaced.
Algorithms, key custody, rotation and threat model are scoped settings (R47) with a
safe, non-blocking preset. (paraphrase + Claude's technical note)

## R52 — The graph starts seeded, never empty (owner, 2026-10-10)
> "nie wiem czy dawać jakiś zalążek zerowy, czy po prostu pusty plik zapełniany w
> trakcie onboardingu dopiero? o właśnie, przecież informacje o sobie będą,
> niezależnie od użytkownika."

Before onboarding the graph already holds what exists independently of the user:
the application's self-model (goals, owner philosophy, capabilities, limits, its
models and methods with costs), the ontology, methods with research presets,
default layers (R40) and the party node of whoever is starting, with every slot
`unknown` (R39). All of it is data the user can disable or remove. (paraphrase)

## R53 — The engine generates the space; the owner's examples are calibration points (owner, 2026-10-10)
> "funkcja silnika żeby wszystkie operacje które przyjdą do głowy wykonywały się
> naturalnie intuicyjnie przezroczyste albo z pełną kontrolą użytkownika"
>
> "rekonstruował całą przestrzeń na podstawie podanych przeze mnie kilku
> charakterystycznych przykładów … liczyłem na to, że … rozszerzysz i
> uszczegółowisz. do tego chyba trzeba jakiegoś pucusia typu Gemini, albo chociaż
> w miarę kreatywnego GPT"

The lists in the design document (areas, party levels, property packages, scope
chain, conflict rules) are examples, not closed sets, and their counts are not
design facts. Expansion and detailing is an engine method (an N-track run per R48:
several models given the same seed, compared for agreement, novelty and
contradiction) whose results enter the graph as proposals, never as facts. Each
such operation runs transparently or under full user control, as a setting.
(paraphrase; correction of Claude's earlier closed lists)

## R54 — Everything from the conversation window, at any level of abstraction and competence (owner, 2026-10-13)
> "jedna zasada, która mi się przypomniała nt. ChatADHD: wszystko użytkownik powinien być w stanie zrobić
> z okienka konwersacji, komunikując się na dowolnym poziomie abstrakcji i kompetencji"

Every action, setting, flow and storage operation is reachable by talking in the conversation window, whether the
user says it as a vague wish or as an exact technical command. Panels and buttons are additional views of the same
actions, never the only way. (paraphrase)

## R55 — Content vs its forms; groups of actions; the list is open and grows from conversations (owner, 2026-10-13)
> "nie tylko wiadomość — wideo, obraz, nawet muzyka mogą nieść jednakową treść, tylko w innych modalach. Takie coś
> można opisać, streścić, namalować, parodiować, zaśpiewać i zatańczyć … cała rozmowa też może być namalowana;
> kot w Pythonie albo arkusz Excela to już troszkę mocniej trzeba kombinować"
>
> "grupę flow, rutyny, pipeliny można zastosować, zoptymalizować, wariantować, eksperymentować … jak ci podaję
> kilka przykładów, to jest kilka oczywistych z bardzo długiej listy, którą masz uzupełnić i obsłużyć rozwijanie
> w przyszłości, w tym w szczególności emergentne na podstawie konversacji"

Groups such as "rich representations of multidimensional content" and "flows / routines / pipelines" carry their
own families of actions; the given examples are the start of a long list Claude completes, and new groups and
actions emerge from conversations as proposals. Design: `docs/architecture/GRAF_HUB_KOD_SILNIK_2026-10-13.md`
§5–§7. (paraphrase)

## R56 — The first screen is a standard chat app; windows and the workbench are configurable views of it (owner, 2026-10-14)
> "było dla mnie oczywiście w tym punkcie wyjścia, że jak uruchamiasz aplikację, to dostajesz widok albo ostatni zapisany, albo ostatniej rozmowy w okienku czatu, albo nowej rozmowy. Na Androidzie okienka (pływające, dokowalne, obsługujące wszystko) co do zasady powinny być możliwe do skonfigurowania tak jak na desktopie Czy w web apce, tyle że domyślny, wyjściowy widok, to pełnoekranowy interfejs standardowej apki chat bota, z flow, harnesem, stylem wyświetlania, itp itd, będąca fuzją wszystkich apek od kilku (5-10) najpopularniejszych llm-ów. Obowiązkowe jak ktoś się przesiada na naszą apkę, to można się spodziewać że najpierw chciałby przetestować czat, zanim swędzi godziny na konfiguracji"

Opening the app lands in a view: the last saved one, or the last conversation in the chat window, or a new
conversation (an ordered, configurable choice). On Android too, floating and dockable windows that handle
everything are, in principle, configurable as on the desktop or in the web app; only the default, starting view is
the full-screen interface of a standard chatbot app (flow, harness, display style), a fusion of the 5-10 most popular
LLM apps. Mandatory: a person switching to this app first wants to try the chat, before spending hours on
configuration. (paraphrase; the quoted words are verbatim)

## R57 — Imports as linked files in the graph (class lineage), a working cache node, format-preserving edits on request, replication of vendor workflows (owner, 2026-10-23)
> "pamiętam że zgłaszałem takie wymaganie żeby na przykład było możliwe, za pomocą rozmowy jak i w menu, ustalenie, że importy: nie kopiujemy, rozpakowujemy ZIP, z grafie linkujemy i opisujemy metadane, wyświetlamy w interfejsie wzorującym się na apce dostawców danego exportu. innego punktu widzenia, to będzie plik przecinek z takimi samymi metodami w grafie i standardowymi łączami, jak każdy inny plik kropka do tego charakterystyczne dla archiwum ZIP, jeszcze węższe: dla eksportu, jeszcze węższe: eksport danego dostawcy dana wersja. daj fejblowi do przemyślenia, co zrobimy z kontynuacją lub modyfikacją rozmów które mamy w zipie. czy wszystkie możliwości są łatwe w realizacji? na przykład: zachowanie oryginalnego formatu archiwum z edycją jedynie danych, na przykład w openai jak konwersacje są podzielone na ileś plików json, oraz chyba te same w jednym .html. to zachowanie zasad podziału, jeśli dobrze mówię że to samo, to też druga kopia w html, potwierdzone w sieci najlepiej w dokumentacji, fachowych źródłach, dopiero z braku wiarygodnych, to określenie z danych oraz mniej wiarygodnych źródeł, wszystko ważone z głową. ale praca wyłącznie na zippie to za dużo obliczeń, kompresji, rekompresji będzie ciągnąć, to tylko na wyraźne życzenie użytkownika. standardową myślę, że nawet jak nie chcemy kopiować całej zawartości eksportu, to – i tu by się przydał węzeł główny typu pamięć podręczna, gdzie byłyby aktualnie przetwarzane i edytowane elementy z eksportu. a w kwestii klonów aplikacji, które Gemini mi mówił że są na githubie – są takie które reprodukują oryginalne work flows Twoje, codexa, i in.? jeśli nie to na ile my możemy je zreplikować?"

Whether an import copies or links is a setting, switchable from the conversation and from the menu: the export is unpacked, linked in the graph and described by metadata, and shown in an interface modelled on the provider's app. The imported thing is a file with the standard graph methods and links of any file, plus what is characteristic for a ZIP archive, narrower for an export, narrower for an export of a given provider in a given version (a class lineage). Preserving the archive's original format while editing only the data (e.g. OpenAI: conversations split over several JSON files, probably repeated in one .html) is wanted, with the rules confirmed first in documentation and professional sources, then from data and weaker sources, weighted. Working only inside the ZIP is too expensive, so it happens on the user's explicit request; by default a root node of type "working cache" holds the elements of an export that are currently processed and edited. Question: do GitHub apps reproduce the original workflows of Claude, Codex and others (as Gemini told him), and if not, how far can we replicate them. Design: `IMPORT_JAKO_PLIKI_W_GRAFIE_2026-10-23.md` and `REPLIKACJA_PRZEPLYWOW_DOSTAWCOW_2026-10-23.md` (Fable, in progress). Related, already recorded: UI14 and RE31 in `SLOWA_WLASCICIELA_2026-10-16.md`, X-Port (P38). Not found as a requirement before 2026-10-23 (searched OWNER_REQUIREMENTS and SLOWA for "kopiuj/linkuj/rozpakow/pamięć podręczna"). (paraphrase; the quoted words are verbatim)

## R58 — Examples are examples; the engine handles every configuration; little usefulness is an advisory, not a refusal (owner, 2026-10-23)
> "widzę w tabelce wpisałeś przypisanie jako wymagania, co się kłóci z filozofią. unikaj zapisywania przykładów bez wyraźnego zaznaczenia że to są przykłady które silnik i abstrakcje mają obsłużyć we wszelkich możliwych konfiguracjach, mało użyteczne (typu przypisywanie wielu reprezentacjom tej samej wartości w tym samym kontekście z takimi samymi modyfikatorami, konkretny przykład: starsze wersje robią się przezroczyste, mniejsze, oddalone w perspektywie, bardziej czerwone, mniej zielone, niebieskie bardziej oddalone od 128, przy czym kierunek oddalenia jeszcze od czegoś innego wynika ) również. wyzwalając ewentualnie, zwracając uwagę na niewielką użyteczność i pytając czy to jest celowe i czy następnym razem nie pytać"

A concrete assignment (channel to measure, colour to meaning) is a labelled example or a default preset in data, never a requirement and never attributed to the owner. The view engine and the other abstractions handle every configuration, also the rarely useful ones: many representations of one value in one context under the same modifiers; a mapping whose direction of departure from a reference (his example: blue away from 128) is itself derived from another variable. Little usefulness raises a non-blocking advisory that asks whether it is intended and offers "don't ask next time". Trigger: the coordinator's plan table of 2026-10-23 gave "colour = kind, transparency = certainty, depth = time" as if they were requirements. Rule also in `CLAUDE.md` and in every agent brief (`tools/workflows/single-package.js`). Code state checked 2026-10-23: `loom/src/views/view_engine.cpp` already never rejects an encoding (status `promoted` or `allowed`, with reasons such as `interference_unmet`), but the scale is only `range`/`domain`/`type` with a static `invert` (lines ~595-622) - no reference point with a signed distance, no direction derived from a second measure - and no remembered acknowledgement ("is it intended / don't ask again") exists. (paraphrase; the quoted words are verbatim)

## R59 — A brief is a list of examples; examples are necessary, only the exhaustion of the possibilities is sufficient; how lists are filled; reliability of web sources (owner, 2026-10-23)
> "A napisałeś to tak żeby agenty też kumały i reinterpretowały jak ty im podasz wybory jako wymagania, to mają to rozumieć jako przykłady funkcjonalności silnika i abstrakcji. konieczne i niewystarczające, w pełni wystarczający jest tylko pełne wyczerpanie listy możliwości. to on zresztą też wzięło się jak tym listem zapełniać: od najpopularniejszych oczywistych przykładów, przez wyszukiwanie sieci przed budowaniem release, po obsługę nieistniejących uprzednio formatów przez agenta nieobsługiwanych formatów szukającego wszystkiego w sieci podczas runtime. najlepiej w dokumentacji jak nie to na wiarygodnych forach i tak dalej jak poprzednio mówiłem - w przypadku braku wiarygodnych informacji opieramy się na analizie danych wspomaganej analizą  tych mniej wiarygodnych źródeł i testowaniem zdobytych tam informacji. A tak w ogóle to testy informacji z sieci powinny wpływać na ocenę wiarygodności źródła, autora, jeśli mamy dużo danych to również w zależności od czasu bo takie rzeczy się zmieniają..."

Agents must read the choices the coordinator gives them as examples of the functionality of the engine and its abstractions (R58 applied to briefs). Examples are necessary and insufficient; only the full exhaustion of the possibilities suffices. Lists are filled in three stages: the most popular obvious examples; web research before building a release; at runtime an agent for unsupported formats that searches the web for everything about a format that did not exist before. Web information: documentation first, then reliable forums and so on; without reliable information, data analysis supported by the weaker sources and testing of what they say. Test outcomes feed the reliability rating of the source and the author and, when there is much data, depend on time. Recorded in `CLAUDE.md` and in `tools/workflows/single-package.js`; design inputs for `IMPORT_JAKO_PLIKI_W_GRAFIE_2026-10-23.md` (unsupported-format agent, reliability ledger extending `loom/data/policy/calibration.json`). (paraphrase; the quoted words are verbatim)

## R60 — Absence today is not absence forever; the ecosystem has other consumers; we build a new paradigm (owner, 2026-10-23)
> "odpowiedź do twojej poprzedniej odpowiedzi brak czegoś lub kogoś teraz nie oznacza że będzie tak zawsze. w ekosystemie mamy innych odbiorców, A my budujemy nowy paradigmat. ASR się wczuło w klimat, musiałem poprawić z paradygraf ;D"

Reply to the coordinator's argument that format-preserving writing deserves only an export on request because (to its knowledge) ChatGPT and Claude do not read their own export back. That argument was an unverified statement about the present, used as a design filter. Rule: facts about the present state of the ecosystem are dated observations (with validity window and source rating, R59), they may feed a non-blocking advisory (R58) and never limit an option. Trigger for the design documents of 2026-10-23: check that `IMPORT_JAKO_PLIKI_W_GRAFIE_2026-10-23.md` does not lean on "no consumer today", and consider our own export as something others may read. (paraphrase; the quoted words are verbatim)

## R61 — Expensive work starts after a paraphrase is approved; before asking, check the understanding against the philosophy (owner, 2026-10-23)
> "droga praca, to startujesz po upewnieniu się że dobrze zrozumiałeś, parafrazując wymagania i prosząc o ewentualne korekty i zatwierdzenie a upewniając się że dobrze rozumiesz to zanim zaczniesz mi zadawać pytania, to przeanalizuj twoje zrozumienie w kontekście filozofii Bo jeśli coś się nie zgadza i nie było wyraźnie zaznaczone to najpewniej źle rozumiesz"

Expensive work (Fable stages, multi-agent workflows, packages beyond a trivial fix) starts only after the coordinator has paraphrased the requirements, asked for corrections and received approval; the approved understanding is recorded. Before the coordinator asks the owner anything, it analyses its own understanding against the philosophy: a mismatch that was not marked explicitly most likely means a misunderstanding on the coordinator's side. Supersedes the coordinator's own preset of waiting about 15 minutes after a series (CLAUDE.md, "How the owner works"). Applied to `SPEC1`: the spec node gets an `understanding` (paraphrase, state proposed / approved / corrected). Note: the two Fable design runs of 2026-10-23 (`IMPORT_JAKO_PLIKI...`, `REPLIKACJA...`) were started on the owner's explicit order ("daj fejblowi do przemyślenia") before this rule and without an approved paraphrase; their documents are drafts for his corrections. (paraphrase; the quoted words are verbatim)

## R62 — Output tokens cost the owner's time; raise the weekly pace; act boldly while he is away (owner, 2026-10-23, ≈00:20 UTC)
> "o widzisz masz przykład co się dzieje z twoimi wyjściowymi tokenami które tutaj trafiają: musisz je powtarzać droższym modelem bo jednak mój czas który bym zmarnował na przewijanie i szukanie jest więcej wart, w szczególności o 2:12 w nocy. brzmi dobrze ale sprecyzuj Co znaczy ID – unikalny numer bez treści interpretowalnej? a jeszcze w ogóle plany zużycia tygodniowego to sobie podkręć trochę, bo prawdopodobnie przez kilkadziesiąt godzin się nie będę odzywał, więc jeśli wiesz z dużą dozą prawdopodobieństwa dobrze co robić żebym był zadowolony, to działaj śmiało"

Chat output is read by him and costs his time more than tokens; every status line must earn its place. SPEC1 "sounds good" with the clarification that an id is an opaque unique number (decision: `s_` + 12 hex, allocated once in `docs/spec/ids.json`; historical codes stay as aliases). Weekly pace plan raised (`tools/pace_plan.json`: 56 % Fri 20:00, 80 % Sat 08:00, 92 % Mon 08:00, 100 % reset; previous 50/90/100 kept in `_history`). He will be away for tens of hours: when the coordinator knows with high probability what would satisfy him, it acts without waiting (R61 approval is then recorded as `proposed`, for his later review). (paraphrase; the quoted words are verbatim)

## R63 — A better coordinator model may loosen the rules a little; the Sonnet rules were for Sonnet (owner, 2026-10-23)
> "a i dobrze by było z mojej strony jakbym zaznaczył, bo dla ciebie to nie tylko nieoczywiste, ale być może być i nawet protestował gdyby jakiś agent ci to powiedział a nie ja: jak zmieniam model na lepszy to domyślnie możesz poluzować troszkę wymagania. to co mówiłem sonetowi to było do soneta, tobie troszkę bardziej ufam, w twój gutt feeling, że tak tą relację zantropomorfizuję"

When he switches the coordinator to a stronger model, the procedural requirements he set for the weaker one (approval gates, pacing checks, spacing rules) may be loosened a little by default; the coordinator's judgement gets more room. Does not touch the philosophy itself (R58–R60, configurability, privacy, his money). Stated by him directly, because an agent could not grant this. (paraphrase; the quoted words are verbatim)

## R64 — Windows (panels): dockable, floating, maximizable; the standard interface is one maximized window; the union of functionality and the style alternatives of popular apps (owner, 2026-10-22)
> Ja mówiłem o panelach czy oknach Jak zwał tak zwał, dokowalnych, pływających, maksymalizowalnych, standardowy interfejs to zmaksymalizowany, inspirowany i zawierającymi sumę funkcjonalności oraz alternatywy stylistyczne popularnych apek

Okno jest jednostką interfejsu: każdy widok można zadokować, odpiąć i zmaksymalizować; interfejs standardowy to jedno zmaksymalizowane okno czatu, nie osobny tryb; okno czatu ma sumę funkcji popularnych aplikacji i wybór ich stylów. Dziś: layout_mode=windows oznaczone planned; „Warsztat” = stara siatka paneli (kolizja nazw).

## R65 — Graph screens after an import: attachments missing, chat sentences about the graph not understood, a moving cobweb of three nodes, no 3D/alpha/dimensions, texts that say nothing; are the guidelines collected? (owner, 2026-10-22)
> czyli onboarding głównie? przypomniała mi się uwaga: import nie pokazywał załączników w którejś rozmowy nie wiem skąd. próbowałem np. pokaż katalog główny grafu, pokaż import w grafie - nie kuma. widok grafu też bezużyteczny: ruszająca się pajęczyna. pokazuje na przykład trzy węzły, po imporcie tysięcy konwersacji. chociaż jakiś tam sens zaczyna się wyłaniać. nie za duży bo jak koło grupę na przykład pokazuje chyba wszystkie kwoty wymienione w imporcie. A gdzie 3+D, kanały alfa, wielowymiarowość...? co w ogóle ten interfejs grafu robi? w ogóle wszystkie teksty to nic nie mówią, mi - autorowi niby. masz zebrane wytyczne co do interfejsu i funkcjonalności?

Zgłoszenia z telefonu po APK v2: import bez załączników w jednej rozmowie; „pokaż katalog główny grafu” i „pokaż import w grafie” nierozumiane; stary widok grafu (pajęczyna) pokazuje 3 węzły po imporcie tysięcy rozmów, koło z kwotami; brak 3D/alfa/wielowymiarowości w głównym grafie; teksty nic nie mówią autorowi; pytanie o zebrane wytyczne. Audyt 22.10: przyczyny w QUEUE_S1 i zadaniach G1–G4.

## R66 — Was the workbench only for tests? Present a plan with perspectives: when the functionality, when the interface (owner, 2026-10-22)
> o ile dobrze pamiętam work band to był dla ciebie do testów tak? przedstaw plan z perspektywami, kiedy będą funkcjonalności, kiedy interfejs
> work bench

„Warsztat” (workbench) był w jego pamięci narzędziem testowym; prosi o plan z perspektywami i o rozdzielenie: kiedy funkcje, kiedy interfejs. Plan G1–G4 z 23.10 (daty ze zmierzonego tempa).

## R67 — No mock-ups, placeholders, fakes or temporary bridges - only full functionality; the context of an API call is inferred from the wide, multi-dimensional context (history, teleology, perspective...); depth and width are optional arguments, inferred by default, passable as absolute or modifying parameters (owner, 2026-10-23)
> o Tak mi się przypomniało co tam jeszcze jak tą apkę na Androida oglądałem mnie zasmuciło, co jest sprzeczne z filozofią bez mokapów, placeholderów, fakeów, tymczasowych mostków... tylko pełne funkcjonalności. a tam widzę żeby w kontekście wybrać boxy... 
> kontekst (api call) zależy od kontekstu szerokiego, wielowymiarowego- historia, teleologia,, perspektywa....głębokość i szerokość jako argumenty opcjonalne, domyślnie wnioskowane z kontekstu, możliwe do przekazania jako parametry bewzględne lub modyfikujące...

Na Androidzie zobaczył w „Kontekście” wybór z pól wyboru (checkboxy) — zasmuciło go to jako atrapa sprzeczna z filozofią (bez makiet, placeholderów, fałszywek, tymczasowych mostków). Kontekst wywołania API ma wynikać z szerokiego, wielowymiarowego kontekstu (historia, teleologia, perspektywa…); głębokość i szerokość to argumenty opcjonalne: domyślnie wnioskowane z kontekstu, przekazywalne jako parametry bezwzględne albo modyfikujące.

## R68 — Move the coordinator to GCP: code there and deploy at once; prepare for the move, possibly tomorrow (owner, 2026-10-23)
> właśnie a ja zapomniałem że miałem cię przenieść bezpośrednio na gcp tam byś sobie kodził i od razu deployował. trzeba będzie, nie? Przygotuj się może na przeprowadzkę? na jutro?

Właściciel przypomina, że miał przenieść sesję koordynatora na GCP (kodowanie i natychmiastowy deploy w jednym miejscu) i pyta, czy to konieczne i czy przygotować przeprowadzkę na jutro. Kontekst: dwa restarty kontenera przez dysk (23.10), CI 2 h, emulator w GH Actions.

## R69 — Depth and width as arguments of a function: a context selector that takes a graph node corresponding to ... (continuation of R67; the sentence breaks off) (owner, 2026-10-23)
> ja akurat o szerokości głębokości myślałem w kontekście argumentów  funkcji może jak dziadek gadam nie wiem tak to się Kiedyś mówiło ;)
> wiesz po staremu architektonicznie tak to widzę że jest funkcja kontekst selektor która bierze węzeł w grafie odpowiadający

Doprecyzowanie R67: głębokość i szerokość to argumenty funkcji. Architektonicznie: jest funkcja „kontekst selektor”, która bierze węzeł w grafie odpowiadający … (wypowiedź urwana — ciąg dalszy w następnej wiadomości).

## R70 — The context selector takes the node of the reply being generated (message dimension, position -1 to the last user prompt), sees what converges in all perspectives, minds the cache, appends modifications at the end, builds from the most general to the most specific (project > branch > module > function); the flow is called by the user, the agent or the experimenter (retry with wider/deeper context), and narrows on a topic change when no return is expected within the cache lifetime (owner, 2026-10-23)
> samo mi się wysłało odpowiadający generowany właśnie przez model odpowiedzi.  w wymiarze wiadomości na pozycji -1 na ostatni prąd użytkownika. Widzi we wszystkich perspektywach co się schodzi. kmini co jest scacheowane żeby jak najwięcej z tego skorzystać. o ile nie zaburzy to układu to modyfikacje dodaje na końcu, a kontekst jest zbudowany od najuniversalniej potrzebnych i ogólnych danych, przez bardziej szczegółowe – na przykład o projekcie później przez gałąź moduł funkcję jeśli koderkę robimy. No i flow, nie tylko na życzenie użytkownika, ale również agenta,eksperymentatora automatycznego półautomatycznego, choćby po niewystarczającej odpowiedziPonawia pytanie z szerszym i głębszym kontekstem. albo jeśli w trakcie rozmowy Właśnie skończyliśmy jedną funkcjonalność i przechodzimy do następnej No to typowy deterministyczny kontekst selektor będzie pewnie chciał trzymać ten duży kontekst,Chociażby przez ten bajas na cache,    to przy zmianie tematu na którymkolwiek poziomie może być koniecznośćzawężenia kontekstu dla oszczędności jak wiadomo że już nie będzie powrotu do tego samego przez czas trwania pamięci podręcznej

Ciąg dalszy R69 (odczyt ASR: „prąd” = prompt, „kmini” = pomni/uwzględnia, „bajas” = bias). Selektor bierze węzeł odpowiedzi generowanej właśnie przez model — w wymiarze wiadomości pozycja −1 względem ostatniego promptu użytkownika; widzi we wszystkich perspektywach, co się w nim schodzi; uwzględnia, co jest w cache, by jak najwięcej z niego skorzystać; jeśli nie zaburza układu, modyfikacje dokłada na końcu; kontekst budowany od najuniwersalniejszych i najogólniejszych danych do szczegółowych (projekt → gałąź → moduł → funkcja). Przepływ wołany nie tylko przez użytkownika, ale i agenta oraz eksperymentatora (auto/półauto), np. po niewystarczającej odpowiedzi ponawia pytanie z szerszym i głębszym kontekstem; przy zmianie tematu na dowolnym poziomie deterministyczny selektor trzymałby duży kontekst (bias na cache) — a wtedy trzeba zawęzić dla oszczędności, gdy wiadomo, że powrotu nie będzie w czasie życia cache.

## Decision — rebuild, don't recover
> "nic już nie szukam bo nawet jak coś było to ty i tak teraz lepiej zrobisz od nowa. bo co było to nawet nieprzetestowane"

Recovered versions (`history/chatadhd_v0.8.3`, `history/chatadhd_v0.9.0`) and
the historical report are sources of requirements, ideas and data assets
(e.g. `film_structure.json`), and real ground truth for self-discovery tests —
not code to merge. Everything is rebuilt in Loom with tests.

## UI reference
`workbench_mockup_2026-09-26.webp`: dockable/floating panels (conversation
list, active conversation, knowledge graph with active context window, project
structure with dimmed high-confidence auto-completable nodes, artifact views
for music/video with timeline + storyboard, video player, execution
environment with agent), multi-format ingestion, Loom SDK at the centre.
