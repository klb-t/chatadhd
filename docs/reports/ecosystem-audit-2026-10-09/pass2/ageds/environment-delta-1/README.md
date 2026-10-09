# AGEDS — aktualizacja środowiska po poprawieniu proxy

Poprzedni `Network is unreachable` **nie jest już wystarczającym opisem blokady**. Jedna ograniczona próba trwała 39,91 s, przy budżecie 300 s.

1. Aktualny `HTTPS_PROXY` zastąpił stare `GRADLE_OPTS`; oddzielny snapshot i cache. Pobrano przypięty **Gradle 9.7.0**, a `./gradlew --version` zakończyło się **PASS**.
2. Następne wywołanie `tasks --all`, bez kompilacji, wykryło nadmiar katalogów w cache dystrybucji. Ponowne pobranie zakończyło się niezgodnością checksum: rzeczywisty hash był SHA-256 pustych bajtów. **Konfiguracja projektu nie została zweryfikowana.** To nie dowód błędnego pinu ani ingerencji w upstream.
3. Dostępny JDK17/SDK36.1 nie zastępuje wymaganych przez AGEDS JDK21/SDK37. Nie zmieniono pinów, nie pominięto walidacji checksum i nie uruchomiono emulatora.

Receipts/logi pierwszej próby pozostają nienaruszone. Nowy skrypt `check_gradle_environment.py` odtwarza tę ograniczoną bramkę; wykorzystuje bieżące proxy, wymaga nowego katalogu wyników i odcina całość po najwyżej 300 s.

Następny konkretny krok: czysty cache poprawnie pobranej i zweryfikowanej dystrybucji9.7.0 oraz JDK21/SDK37, następnie konfiguracja projektu, dopiero potem osobno bramki JVM/APK/SAF. Żaden test produktu nie otrzymał nowego PASS wskutek bootstrapu.
