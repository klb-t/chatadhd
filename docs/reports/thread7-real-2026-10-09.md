# Zadanie C — wątek 7, rzeczywiste dane i workflow

Gałąź `gpt/thread7-real-2026-10-09`, od main
`9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`. Zakres zapisu: narzędzia structure,
własne badania i ten raport. STATE/INDEX, produkcyjny runtime i UI niezmienione.

## Etap 1: odzyskanie i replay

Odzyskano bajty trzech prywatnych artefaktów: freeze źródeł, checkpoint promptów
i checkpoint wykonawcy. Freeze SHA-256:
`5f976bbf9cee15dfe232e329dc2c85ebc40cae981af7ceedc56fc8e08719a832`.
Checkpoint promptów: `45272501536ee49c448bc1f541e68b30a1ea5dc0d9a60720a6a3862633bdfdbe`.
Checkpoint kampanii: `fa3ae1497cf16af6d7db80b424b04d1c99e88208c061fb9c0cda23093684a5ec`.

Zweryfikowano 173/173 plików checkpointu i 146/146 plików zamrożonej
preparacji. Niezmieniony następca intake z `ffe6443e` weryfikuje freeze,
44 wiadomości i 47 węzłów trzech wybranych rozmów OpenAI. Preparacja odtworzyła
128 dokładnych requestów: 16 konfiguracji × 8 zależnych pytań. Zbiór nie jest
reprezentatywny, ślepy, niezależny ani próbką Anthropic. Reference gold jest
prowizoryczne, autorstwa asystenta; sześć nietekstowych części pozostaje
niezinterpretowanych. Bajty dostępnych źródeł i metadane są prywatne.

Odzyskany intake i preparacja: **37 + 43 testy PASS**. Odtworzono byte-for-byte
trzy historyczne grafy: etap 1, etapy 3/4 oraz 192 powtórki etykiet.
To replay zapisanych dowodów, nie nowe wykonanie modeli.

## Korekta rachunku kampanii

Raport main: 708 prób / 0,703661900 USD. Odzyskany późniejszy checkpoint:
**720 prób / 0,873216500 USD**. Istniejący `PrivateLedger` odtworzył i zweryfikował
wszystkie 720 skutecznych wierszy i ich zapisane dowody; archiwalne nierozstrzygnięte
rezerwacje: 0 USD. Dodatkowe 12 prób należą do stage5-old-stage3 (8) i
stage5-old-stage4 (4). Nie skasowano pierwotnych pending/uncertain rekordów:
ich append-only dowody dają zakończony stan efektywny.

Archiwalna pozostałość nieodnawialnego limitu 5 USD: **4,126783500 USD**.
Klucz nie występuje w środowisku ani odzyskanym checkpointcie; checkpoint
jawnie `credential_included=false`. Szyfrowane materiały przekazania bez
działającej referencji do klucza nie dają dostępu. Aktualna tożsamość klucza,
usage i rezerwacje dostawcy są **unknown**, nie 0. Nie użyto salda konta,
nie odnowiono limitu. Nowe płatne wywołania/koszt: **0 / 0 USD**.

Dowody: `docs/research/thread7_real_2026-10-09/source-receipt.json`,
`historical-replay.json`, `campaign-replay.json`, pinned upstream/import.json.
Pełne source/request/response/ledger pozostają prywatne.

## Następny krok

Domknięcie testów workflow i kontraktów oraz prywatny checkpoint kolejki.
Płatne fazy wymagają bieżącego preflight istniejącego wykonawcy, tego samego
klucza/kampanii i zachowania wszystkich rezerwacji. Nie powtarzać 720 prób.
Brak danych jakości nowych modeli pozostaje null; żadnego presetu nie adoptowano.
