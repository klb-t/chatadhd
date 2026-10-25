# Pokwitowania wpisów z VM_INBOX.md (sesja VM „gcp”)

- 2026-10-25 11:4x UTC — wpis 1: build+ctest zmierzone (`1a7df48b`): zimny build ≈ 12 min, ctest 1217 s, 242/242 z Pythonem 3.11.
- wpis 6: serwer chatadhd wdrożony 24.10 (`44e6fbdf`), AGEDS 25.10 (port 8090, dane `/var/lib/ageds`), opis `VM_DEPLOY_2026-10-23.md`.
- wpis 7: `claude-rc.service` zablokowany przez klasyfikator, czeka na jedno „tak” właściciela w sesji.
- wpis 8: skryptów z 2024 nie ma. Projekt `clever-stone-467803-g7` powstał 2026-09-22; metadane projektu mają tylko `ssh-keys`; w projekcie nie ma Functions/Run/Scheduler poza spot-guard; nie ma własnych obrazów; jedyna instancja to devbox; starsze projekty na tym koncie (`tempwin-440909`, `velvety-folder-322217`) nie mają włączonego Compute albo dostępu dla SA; `gh search code` po „preempt” w repo `klb-t` nic nie znajduje. Mogły być na innym koncie albo już usunięte.
- wpis 12a (pomiar 25.10): dysk `pd-balanced` 34 GB, zapis direct 158 MB/s; RAM użyty 13,7 GB (bez cache; największe procesy to evolution 1 GB, claude 0,5 GB, plasma). Zrzut RAM-u przy tej przepustowości ≈ 87 s, czyli mieści się w 120 s uprzedzenia, ale nie w 20 s. 20 s wymaga ≈ 700 MB/s (hyperdisk z provisioningiem albo większy pd-ssd); cena do policzenia przed decyzją. Secure Boot wyłączony (hibernacja jest możliwa od strony podpisów).
- wpis 13: poprawka g++15 `sha256.cpp` przeniesiona na `claude/m1-w1` (`9e0d2a94`), FF main po bramce.
- wpis 14: spot-guard wdrożony 24.10 (`74c448b9`), pierwszy przebieg „nothing to do (RUNNING, SPOT)”.
- wpis 16: pakiet „shardy asan” w toku (agent Sonnet na `~/wt/m1d1`).
