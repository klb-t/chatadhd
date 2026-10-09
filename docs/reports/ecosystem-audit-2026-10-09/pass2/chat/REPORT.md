# Chatadhd — etap 2 audytu A

Baza `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`; porównanie B `ddcaeaf7a25b70f0d4dc2e0eed2466016233eeb8`. Raport pierwszego przebiegu pozostaje niezmieniony.

Cztery nowe potwierdzone ustalenia: utrata rekordów/rozszerzeń pamięci, zaszyta kolejność ASR, pewność 0.9 przy braku danych providera, fałszywe synced przy równej długości różnej treści. Szczegóły, zakresy, wpływ, alternatywy, migracje i kryteria znajdują się w findings.jsonl oraz packages-for-B.json. Dla ASR nie ustalono aktywnego GUI konsumenta; nie przypisujemy niezaobserwowanych wysyłek użytkownikowi.

Niezależne testy wywołują istniejące klasy i zapisują rzeczywiste pliki/SQLite; zastąpione są tylko zewnętrzny transport oraz zależność LLM w próbie timeout. Native korzysta z Runtime i istniejącego ScriptedTransport. Reprodukcja błędu PASS jest osobną kategorią od akceptacji produktu. Każda porażka lub brak kontraktu daje niezerowy kod zakończenia.

CH-004/005: wykonano różnicę rzeczywistych ścieżek, ale pełna akceptacja pozostaje BLOCKED_MISSING_CONTRACT. Nie udajemy API mapującego user/revision/category ani operacji/estymaty zwykłego czatu. Ręczne metadane pamięci nie są dowodem wycieku danych z OnboardingStore. UsagePolicy nie ma pola budget=0; initial baseline=0 oznacza potwierdzenie wzrostu. Zbadano też wszystkie wiersze ledgeru, nie tylko sztuczny cohort.

CH-006: rzeczywista materializacja relacji bez provenance; minimalna bramka metadanych nie dowodzi poprawności referencji ani admission. Pełny candidate/ask jest osobną brakującą bramką. CH-011: timeout daje trwały done, pending=0 także po ponownym otwarciu SQLite. Poprawki analizatora B mają oddzielny niezależny raport c-review/B-semantic, w tym nową regresję.

Potwierdzone mechanizmy: jawne odrzucenie braku kryptografii, nieznanego providera i push przy pull_only; dwa snapshoty Config rzeczywiście zmieniają body HTTP. Nie utożsamiamy tego z pełnym roundtrip Basic/Advanced/Expert.

Pokrycie jest zakresowe w coverage.json. Browser E2E blokuje faktyczny start Chromium (socket EPERM), nie brak pobrania ani kompilatora. Peer review narzędzi jest zachowany w ../watchdog/chat-peer-review.json; poprawiono słabe bramki, zamiast raportować pozorny PASS.
