# Odtworzenie pakietu

Przeczytaj `test-index.json` i `RESUME.md`. Wszystkie suite entrypointy są w `tools/ecosystem-audit-2026-10-09/pass2/catalog.json`; `run_index.py --list` udostępnia wykonywalny indeks. Polecenia konsumentów wymagają checkoutu oraz SHA, a natywne/JVM także odpowiednich zależności lub buildu. Instalacja zależności odbywa się lokalnie, bez płatnego CI.

Raporty modułów rozdzielają: A reprodukcję, B akceptację, kontrolę fixture, blokadę środowiska i brak kontraktu. Błędy wcześniejszego harnessu mają zachowane receipts i rozstrzygnięcie; nie sumuj ich z końcowymi przypadkami. Nie używamy xfail do udawania naprawy.

Płatny transport jest wyłączony: Python sockets denied/in-process ASGI, rzeczywisty native ScriptedTransport, przechwycone TypeScript/Retrofit/OkHttp/JVM wywołania. Nigdy nie podstawiaj prawdziwych kluczy do syntetycznych fixtures.

Natywny test stemming odczytuje autentyczną paczkę z historycznego commita i tworzy osobnego kandydata migracji. Dla instrumentacji użyj `native/README.md` oraz manifestu 37 jednostek; wynik ASan/UBSan nie jest wynikiem LSan.

Kotlin source-slice, rzeczywisty bytecode, SQLite, Room/Robolectric oraz urządzenie mają osobne zakresy dowodów. Hostowy PASS nie oznacza Android/device ani browser E2E. Finalne per-module indeksy wskazują, które bramki zostały faktycznie wykonane.
