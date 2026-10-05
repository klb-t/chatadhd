# W11 — aktualny kontrakt warstw i metod, 2026-10-05

Ten przyrost dotyczy danych profilu i odczytowego deskryptora. Nie tworzy nowej
bazy, resolvera warstw ani rejestru wykonawców. Pełne przyjęcie gałęzi pozostaje
bramką integratora; dowód izolowanego kontraktu nie zastępuje pełnego CTest.

| Źródło | Przypięty commit |
|---|---|
| W11 przed przyrostem source metadata | `fffa9fd557db97cae549d555959fedf3910d346c` |
| W3 selector / method registry | `03c670caa6ee3d8ac2c478186548114f8e83927f` |
| W4 packet / native method graph | `b302df25e1a65f20c395eadc5ad5ef065d26e33d` |
| W12 onboarding / DefaultLayers | `3c0bc36552ef9851f1174946cfb108549aae3228` |

Integrator przyjął W3/W4 na `main e4109df`; powyższe kotwice pinują konkretne
odczytane kontrakty. Źródła obecnego loadera i wygenerowanej tabeli są zachowane
osobno w dowodach, razem z hashami ich rzeczywistych bajtów.

## Istniejący most R40

W12 `loom/include/loom/onboarding_layers.h:12–38` wystawia
`DefaultLayers::create`, `resolve`, `dispatch`, `update_pack` oraz
`runtime_profile_values(definition,layers,bindings)`. Pack ma
`loom.default_layers_pack/1`, stabilne `id/key/area`, dodatnią rewizję i wartość.
Resolver zachowuje historię tożsamości, wykluczenia po aktualizacji oraz
propozycje nowych domyślnych w wykluczonym obszarze. Operacje zwracają nowe
snapshoty, a oryginalna instancja pozostaje niezmieniona.

Rzeczywisty `loom/src/onboarding/runtime_adapter.cpp:13–62` używa W11
`RuntimeProfile::from_definition` i dokładnego `with_values`. Binding jest mapą
niekorzeniowych RFC6901 pointerów na klucze warstw; nakładające się pointery są
konfliktem. Disabled/excluded/proposal/missing usuwa wartość przed natywną
walidacją. Usunięcie wymaganego pola kończy się jawnym błędem, bez przywrócenia
presetu. `from_definition` nie zna źródłowych plików i ma puste pochodzenie
źródeł; nie należy przedstawiać go jako pochodzenia warstwy użytkownika.

W11 `RuntimeProfile::inspection()` udostępnia teraz dokładne
`definition/defaults/values/value_schema/hash` i `source_provenance` konstrukcji
presetu. [Przyrost source metadata](implementation-runtime-profile-sources-2026-10-05.md)
utrzymuje te metadane poza dotychczasowym hashem definition+values. To odczyt
źródeł konstrukcji presetu, nie zapisany dziennik użytkownika ani wykonanie
metody. Istniejący resolver W12 pozostaje jedynym mechanizmem wykluczeń.

Najmniejszy dalszy binding produkcyjny W11/W12 powinien wskazywać stabilne
identyfikatory, obszary i pointery do kanonicznych deskryptorów w danych, bez
ręcznie utrzymywanej drugiej kopii defaultów. Elementów tablic nie należy
utożsamiać wyłącznie przez pozycję, jeśli ich trwałe wykluczenie ma przetrwać
zmianę kolejności. Zapis snapshotów/CAS i aktualne konsumery wymagają oddzielnego
połączenia z istniejącym `OnboardingStore`.

## Granica R41

W3 `loom/src/context/method_registry.h:75–82` już wystawia
`load/resolve/prepare/bind_results/accept`. `prepare` zwraca
`{packet,manifest,effective_recipe}`, a `bind_results` — `{packet,manifest}`;
żadne nie oznacza automatycznego przyjęcia. `selection.parameter_layers` jest
wymaganymi danymi wejściowymi, nie domyślną listą C++
(`method_registry.cpp:300–315`). W3 wiąże dokładny parameter-set z konkretną
wersją metody i runem (`:643–675,:755–767`). W4 wystawia `packet::execute`
(`loom/src/packet/packet.h:5–13`), istniejący
`GraphPacketStore::execute` (`loom/include/loom/graph_packet_store.h:12–18`) oraz
C ABI `loom_packet/loom_graph_packet_store` (`loom/include/loom/loom.h:350–352`).

W11 `selector.pack` opisuje TF-IDF/keyword/ranking/tiers i zamyka dodatkowe
właściwości. Nie jest profilem całego `method_registry`; obecne źródła nie
dostarczają takiego bindingu. Ewentualny bridge ma odwołać się do rzeczywistych
danych W3 i dokładnych consumed parameters. Hash RuntimeProfile nie zastępuje
method-version, run ani rzeczywistych krawędzi result→run/version. Sam seed
deskryptora nie może tworzyć fikcyjnego wykonania.

## Oceny i model scores

- Rzeczywiste pomiary retrieval W3 to osobne Entities z
  `result_type:retrieval_measurement`, target claim, score, instrument status,
  `confidence_scope:instrument_measurement` i
  `content_truth:not_established` (`method_execution.cpp:245–271`). Rzeczywiste
  krawędzie do runu/wersji dodaje `method_registry.cpp:1002–1011`.
- W4 fixture `method-graph-fixture.json:1897–1951` zachowuje osobne datowane
  twierdzenie o dokładnej wersji: dataset ID/hash, `evaluated_at`, model,
  judgement, score kind, measurement status oraz cytowaną Observation z hashem.
  Jego `score:0.6` jest oznaczone
  `model_judgement_not_measured_accuracy`, `measurement_status:unavailable` i
  `confidence_scope:unmeasured_model_judgement`. To syntetyczne zdanie o metodzie,
  nie zmierzona skuteczność modelu.
- Historyczne projekcje W3 zachowują model/version/provider/task/domain/context/
  recipe, datę `observed_on`, populację i metryki z licznikami, mianownikami,
  jednostką, metodą wyprowadzenia i źródłem (`loom/tools/eval/model_profiles.py:50–70`,
  `docs/contracts/model_profile.schema.json:220–434`). Nieznane pozostaje null;
  ocena nie dziedziczy się na inne wersje ani populacje.
- W11 `model_profile.h` normalizuje creation priors. Nie jest kalibracją ani
  walidacją dowodów modelu; nie wolno utożsamiać jego domyślnego confidence z
  powyższymi ocenami.

## Niezależny dowód mieszany

[Harness i odtwarzanie](evidence/default-layers-runtime-mixed/README.md) kompilują
ten sam runner i rzeczywiste źródła W12 z osobnymi, zgodnymi nagłówkami i dziewięcioma
modułami W11. Nie łączą nowych obiektów RuntimeProfile ze starymi fs/log ani ze
starą biblioteką. Przypięty wariant before ma 19 domen; aktualny embedding dodaje
`import/import_audit/import_formats`. Liczby zaliczenia pochodzą wyłącznie z
rzeczywistego `receipt.json`, nie z przygotowania kodu testującego.

Rzeczywisty [paired receipt](evidence/default-layers-runtime-mixed/actual-2026-10-05/receipt.json) ma `valid:true`, obie kompilacje i wykonania `exit0`. **Before: 19 domen, 162 grupy, 1 828 asercji; after: 22 domeny, 179 grup, 2 148 asercji.** Wszystkie 19 wspólnych kompletnych definicji, defaultów, hashy i bindingów są identyczne. Trzy dodatkowe domeny oceniono osobno. Zweryfikowano **23 niepuste rekordy source provenance** względem rzeczywistych bajtów źródła. Pełne before/after JSON, wejścia i logi są zachowane; oba compile/stderr logi są puste. To wyniki tego izolowanego kontraktu, nie liczniki pełnego CTest.

Bindingi w dowodzie są jawnie syntetycznymi, atomowymi grupami najwyższego poziomu.
Wartości pochodzą z natywnych deskryptorów. Dowód obejmuje zgodność defaultów i
hashy, kontrolowane override, niezmienność snapshotów, wykluczenia, aktualizację,
zniknięcie/powrót oraz raw-source hashes. Nie deklaruje produkcyjnych identyfikatorów
wewnątrz tablic, OnboardingStore, działającego okablowania wszystkich konsumentów,
grafu wykonania ani nowej jakości modeli. Nie ma bazy ani płatnych wywołań.

## Do wątku N

- **Do wątku 3/4:** Zachowaj oddzielne measurement/evaluation/prior i ich exact
  version/date/recipe/population/evidence. Do bindingu W11 użyj przyjętego
  kontraktu registry/packet; deskryptory i hash mogą być wejściem, ale nie
  zastępują rzeczywistego run/result.
- **Do wątku 10:** Rozróżniaj source provenance presetu, warstwę użytkownika oraz
  pochodzenie rzeczywistego wykonania. Suppression wymaganego pola jest widoczną
  niedostępnością; UI nie może go potajemnie włączyć przez preset. `is_builtin`
  oznacza wyłącznie równość wartości, a nie brak jawnej decyzji użytkownika.
- **Do wątku 12:** Użyj obecnego `DefaultLayers/runtime_profile_values` i dokładnych
  descriptorów W11. Source metadata są odczytowe; trwałe identyfikatory i bindings
  trzeba określić w danych, bez nowej bazy i bez przywracania wykluczonych wartości.
  Zapis i CAS przez istniejący OnboardingStore pozostają osobną bramką. Standalone
  proof nie oznacza zaliczenia wszystkich 22 domen w rzeczywistych konsumentach.
