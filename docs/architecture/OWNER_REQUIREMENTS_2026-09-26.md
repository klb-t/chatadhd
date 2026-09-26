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
