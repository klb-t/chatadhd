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
