# Pokrycie data → graph → runtime → UI

Pełne, przypięte do SHA przebiegi są w `data-graph-runtime-ui.json` i plikach `flow-map.json` repozytoriów. Poniższy wykaz obejmuje wszystkie domeny wskazane w zleceniu, lecz nie deklaruje, że zostały kompletnie zaimplementowane lub prześledzone.

| Domena | Obiekt danych/grafu | Dowody | Stan połączenia / test granicy |
|---|---|---|---|
| Profil użytkownika, ustawienia i dziedziczenie | Onboarding/RuntimeProfile/DefaultLayers, CKP Settings | CH-004/013; CKP-A-004/006/010; EA-AGEDS-007/011 | Częściowe. Zachować działający resolver i projekcję; sprawdzić zwykły send/worker i trwałe exclusion. |
| Interfejsy i workflowy | Presentation/config profile, checkpoint/snapshot | CH-008/009; WD-004/006; CKP-A-004 | Teksty i checkpointy mają ścieżki pozagrafowe; definicja profilu nie dowodzi grafowej instancji wykonania. |
| Metody, modele i prompty | MethodSpec/MethodGraph, ModelProfile, task/recipe | CH-002/003/012; WD-001/003/005; CKP-A-001/002/008; LEM-001/002/004 | Rzeczywiste konsumowanie miesza się z martwymi polami i ręcznymi fallbackami; testować zmianę A/B danych na przechwyconym request. |
| Konstrukcja kontekstu, filtrowanie i straty | Context recipe, strategy parameters, provenance | CH-007/010/013; WD-002/009; CKP-A-005 | Prefix, ranking, missing i dedup muszą mieć jawny opis skutecznej metody/straty; raw źródło nie zawsze jest utracone. |
| Eksperymenty i wyzwalacze | Experiment/UsagePolicy/TaskEngine, instrument checks | CH-005/015; WD-008/010; LEM-001/008 | Opt-in nie dowiedziony globalnie; scoped guard/assistant consent działa tylko na swoich ścieżkach. |
| Kolejki, wolumen i budżet | Queue policy + immutable execution snapshot | CH-005/011; WD-010; EA-AGEDS-002/005; LOOM-001 | Pinned recipe, eligibility, retry i missed-slot strategy potrzebują konsumentów; standalone loom oddzielnie. |
| Wyniki, wnioski i dowody | Result/Assessment/Evidence/provenance | CH-006; WD-011; EA-AGEDS-006; LEM-003/007 | PROPOSED w Watchdog działa; legacy ingest nie ma tej samej bramki; wynik nie powinien udawać raw evidence. |
| Basic / Advanced / Expert | Ten sam kompletny preset i jawne manual/semiauto override | CH-004/013/015; CKP-A-010 | Test helpera nie dowodzi pełnego UI→graph→request roundtrip; pełny test pozostaje w backlogu. |

Nazwy docelowych obiektów w findingach są rekomendacją audytora do odwzorowania na istniejące kontrakty. Nie oznaczają nowego wspólnego store dla każdego projektu.
