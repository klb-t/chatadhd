# Profil zewnętrzny — przegląd konsumentów web/native

main `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` i B2 `384c5e1686cd3a58a7d89a8a6813a18c764f697d`: po **9 PASS / 0 FAIL / 3 BLOCKED**, w tym 1 PASS reprodukcji i 8 PASS komponentowych akceptacji. To nie PASS pełnego pionu. Wyniki nie zmieniły się między tymi SHA.

Sprawdzony łańcuch: syntetyczny plik JSON → rzeczywisty parser i rejestr profilu → rzeczywisty GraphPacket serializer → istniejący NativeGraphStore → SQLite → zamknięcie kontekstu → nowy kontekst → read/replay → rzeczywiste odtworzenie profilu → workflow → istniejący adapter chat.create → rzeczywisty encoder LoomHttpApi → przechwycony fetch. Różne wersje profilu zmieniają tytuł wysłany przez adapter. Stara receptura pozostaje niezmienna. Nie uruchamiano modeli ani sieci. Node host nie oznacza zamontowanego React/browser E2E; fetch przechwycono przed serwerem, więc test nie dowodzi stworzenia natywnej rozmowy.

**A3-WEB-001:** profil i dokładne źródło są zachowane, lecz wnętrze profilu jest odtwarzane z `raw_source`, bez projekcji pól do grafu. `sourceRef` jest tutaj pochodzeniem, nie resolverem źródła. Pełna akceptacja graph-field→runtime, nieznane pola/niepewne mapowanie i zmiana/niedostępność źródła pozostają BLOCKED z powodu brakujących kontraktów. Nie symulowano brakującego silnika w testach.

**A3-WEB-002/003:** gotowe do wykorzystania są dokładny source/receipt roundtrip, deklaratywne payloady workflowów, jawna dostępność adapterów i dodatkowa projekcja capability. Ścisły profil jawnie odrzuca nieznane pola — to prawidłowa bramka wykonania, ale jeszcze nie zachowanie nieznanych pól źródła jako nieaktywnych danych grafu.

Biblioteki native pochodzą z przypiętych buildów main/B ddca. Dla B2 porównano źródła jednostek GraphPacket i zapisano hash biblioteki; nie deklaruje się pełnego rebuild B2. Pełny zakres, funkcje i nieprześledzone wywołania: `coverage.json`. UI import/restore przejrzano semantycznie; nie wykonano mounted browser testu.
