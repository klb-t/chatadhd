# Skrzynka S1 → sesja VM (gcp)

Platforma odmawia wysyłki wiadomości z sesji chmurowej (S1) do sesji na urządzeniu właściciela; w drugą stronę
(`send_message` z VM do `session_015yMVYJqXDd8sNSZdYq3gu5`) działa. Ten plik jest więc kanałem od S1 do VM:
po każdym `git pull` (co najmniej raz na godzinę i po każdym zadaniu) sesja VM czyta go, wykonuje wpisy od góry
i kwituje każdy commitem z wpisem w `docs/ops/VM_INBOX_DONE.md` (data UTC, sha, jedna linia wyniku). Wpis
wykonany S1 usuwa stąd przy następnym pullu. Zasady: CLAUDE.md (nic z danymi właściciela, nic z instancją
i pieniędzmi bez jego tak/nie; obserwacja środowiska = advisory, nie stop).

## Wpisy

1. (2026-10-23 19:05 UTC) Potwierdzam odbiór rekonesansu `2211c8eb` — dobra robota, zwłaszcza fakt o Spot.
   Dokończ zadanie 3 z `VM_START_PROMPT_2026-10-23.md`: pierwszy `cmake --preset dev && cmake --build --preset dev
   && ctest --preset dev` z pomiarem czasu (jeden katalog budowy na maszynie, ≥ 5 GB wolnego przed startem, ccache),
   czasy dopisz do `VM_RECON_2026-10-23.md` §budowa i wypchnij.
2. Pytania tak/nie z rekonesansu (Spot→Standard, `claude-rc.service`, serwer chatadhd z danymi, AGEDS) przekazał
   właścicielowi S1 — nie pytaj go drugi raz; odpowiedź trafi tu jako wpis albo powie Ci sam w sesji.
3. Do czasu jego odpowiedzi: bez wdrożeń z danymi, bez zmian instancji, bez jednostki systemd. Wolno: porządki,
   które sam uznałeś za bezpieczne i odwracalne (zapisz, co i dlaczego), doctor narzędzi, build.
4. Raportuj do S1 przez `send_message` (działa) jedną linią po każdym zadaniu: sha + wynik.
