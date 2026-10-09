# Watchdog — drugi przebieg, checkpoint 1

Baza main `58a0c93bd0135e3715dcbc4d92fb80e61bd31215`. Produkt niezmieniony.

`receipt-core.json`: 14 testów realnych konsumentów; reprodukcje 5 PASS, akceptacja 4 PASS / 5 FAIL. Wynik procesu **1** jest prawidłowym czerwonym wynikiem akceptacji, a nie usterką harnessu. PASS reprodukcji nie jest PASS produktu. Sieć zablokowana, transport wstrzyknięty, wyłącznie syntetyczne dane. Pierwsze uruchomienie harnessu ujawniło błędny lokalizator `object_store.ts`; poprawiono go do rzeczywistego `object_store/index.ts` przed zapisanym przebiegiem.

WD-001/002/003/011 pozostają odtworzone. Nowe A2-WD-001 ma reprodukcję i czerwony test akceptacyjny. Dwie różne konfiguracje provider.parameters zmieniają rzeczywisty request; to nie naprawia ignorowanych request.params.

SQLite jest dostępny w bieżącym środowisku: wykonano rzeczywiste migracje, zamknięcie i ponowne otwarcie pliku DB. Zapisane wyłączenie asystenta i budżet zero przetrwały restart i zmianę defaultów profilu. Nieznane ustawienia oraz uszkodzony JSON są odrzucane, bez cichego resetu. Timeout transportu zapisuje FAILED_RESERVED, zachowuje rezerwację, nie retryuje sam i pozostawia hashe ustawień/profilu/catalogu. Te zielone testy nie obejmują wszystkich alternatywnych API ani UI.

Następny zamykany moduł: source-access/public HTTP, workbench export/import i historyczna receptura po zmianie profilu.
