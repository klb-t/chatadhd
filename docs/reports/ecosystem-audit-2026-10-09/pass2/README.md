# Zadanie A — przebieg 2, 2026-10-09

Wykonanie trwa. Historyczny raport i receipts przebiegu1 pozostają niezmienione na commit baa9e30c12a29ab7f14fc060a13676b1bd35b036. Nowe dowody są wyłącznie w tym podkatalogu oraz tools/ecosystem-audit-2026-10-09/pass2.

Aktualne main mają te same SHA, więc używamy istniejącego inwentarza zamiast generować ponownie kandydatów. B@ddcaeaf7 i C@69880859 są dostępne i mają odrębne checkouty tylko do odczytu. Baselines i kolejka ryzyka są obok.

Test A odtwarza błąd; test B żąda poprawnego zachowania i zwraca rzeczywistą porażkę przy naruszeniu. Host/JVM/schema/native/browser mają osobne scope. Root publikuje checkpointy po modułach i kontynuuje następne zadania. Brak wcześniejszego kompilatora nie jest uznawany za stałą blokadę: CMake/Ninja i compiler Kotlin pobrano do środowiska roboczego. Produkt, schematy, main i cudze gałęzie nie są zmieniane.
