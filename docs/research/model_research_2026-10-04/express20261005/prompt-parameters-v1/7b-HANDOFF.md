# 7B — częściowy protokół oczekujący na opublikowane źródła

Stan 2026-10-05: **NOT DISPATCH READY**. Dokładna baza: `edcb32fb87116ac497f8eb3d59d53ecea0cfee3f`. Właściciel wymaga kontynuacji istniejącego przygotowania, więc nie utworzono drugiego płatnego wykonawcy ani zastępczego korpusu.

`7b-design.pending.json` zachowuje dwa proponowane prompty, preferencje, wspólny schemat i wszystkie 16 konfiguracji. Hashe dotyczą proponowanych tekstów systemowych, nie całych żądań z brakującymi źródłami. `7b-rubric.pending.json` zapisuje reguły oceny przed otrzymaniem odpowiedzi. Pliki mają jawny status częściowy; nie są kompletną prerejestracją.

`7b-source-audit.json` dokumentuje brak wskazanego ZIP/panelu w pobranych referencjach. Nie jest twierdzeniem o nieistnieniu nieopublikowanych checkpointów. `7b-cost-plan.pending.json` oblicza wyłącznie komponent wyjściowy; całkowita wycena i rezerwacja pozostają null. `7b-artifact-hashes.json` wiąże projekty danych; nie wiąże niedostępnych źródeł.

Wznowienie:

1. Prowadzący 7A publikuje oryginalny `stage5/UNEXECUTED_PREPARATIONS_20261005.zip`, zaufany SHA-256 i instrukcję/manifest oraz rozpoczęty `express20261005/prompt-parameters-v1`; przekazuje commit.
2. Fetch tej publikacji. Archiwum rozpakować do nowego pustego katalogu, sprawdzić zewnętrzny SHA, CRC, ścieżki, rozmiary i SHA wszystkich wpisów. Nie nadpisywać oryginałów ani istniejących przygotowań.
3. Odczytać rzeczywisty adapter panelu i kontynuować go. Zweryfikować wszystkie cztery oryginalne pytania każdej rodziny `new_label_001` oraz `new_label_013`, pełne źródła i aktualny gold. Bez wymyślonych query_id i bez przenoszenia gold do promptu.
4. Uzgodnić projekt z odzyskanym schematem/metodami i zamrozić **przed pierwszą odpowiedzią** dokładne źródła, gold, schemat, prompty, kolejność 128 operacji, parametry, hashe i scorer. Jeżeli nowy kontrakt wymaga zmiany projektu, zapisać ją jawnie w nowej wersji przed collection. Nie nadpisywać historycznych projektów.
5. Wykorzystać `loom.research_programme_manifest/1` i istniejący `research_programme_runner.py`. Zweryfikować manifest i zmieniony scorer testami bez rozluźniania progów. Policzyć pełną górną rezerwację z dokładnych jednostek i aktualnej oferty przypiętego endpointu, z wszystkimi komponentami; przydział 0,4–0,5 USD nie jest tu potwierdzony.
6. Przekazać gotowy commit i manifest 7A. Wyłącznie 7A może dopuścić panel w jednym prywatnym rejestrze wspólnego klucza. 7B nie odczytuje klucza ani nie uruchamia drugiego wykonawcy.
7. 7A publikuje niezmienione pierwsze odpowiedzi/envelopes i rachunki z identyfikatorami/hashami. 7B ocenia je zamrożonym scorerem/rubryką; braków nie naprawia powtórkami. Twierdzenia powiązać z dokładnymi wersjami metod w istniejącym `loom.model_profiles/1` i produced_by; nie deklarować zapisu native.
8. Wszystkie negatywne odpowiedzi zachować w pełni na archiwalnej gałęzi. Zestawić osiem pytań osobno dla każdego z 16 wariantów, z brakami, cytatami, ucięciem, kosztami i obserwowalną preferencją. To autorski mały panel, bez ślepego holdoutu czy jakości produkcyjnej.

Nie czytano `eval/real-holdout-key`. Nie zmieniano STATE, głównego README, UI ani zakresów innych wątków. Nie zmieniano wykonawcy ani scorera.
