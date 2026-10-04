# Niezależny dowód poprawki P1: replay przy rozliczeniu unresolved

2026-10-04, wątek 9 (INTEGRATOR). Oryginalny probe uruchomiono ponownie przez rzeczywiste współdzielone C ABI; wynik PASS, exit 0. Dwa identyczne polecenia `capabilities` z tym samym `operation_id`, `calls=1` i `gpu_seconds=null` wykonały operację tylko raz.

| Obserwacja | Wynik |
| --- | --- |
| Pierwsze wywołanie `executed` | `true` |
| Drugie wywołanie `executed` | `false` |
| Settlement pierwszego / drugiego | `unresolved` / `unresolved` |
| Ledger `actual.calls` | `1` |
| Ledger próbki `calls` | `[1.0]` |
| Ledger `remaining` | `{"gpu_seconds": null}` |
| Ledger status | `unresolved` |
| Kompilacje / provider calls / paid calls tego sprawdzenia | `0` / `0` / `0` |

## Pochodzenie

- W2: `5a73a360f44626333ce6510f26261c32239de73c`.
- W4: `1377e20c71f200b01416c7c87f06c3ffdafb02b6`.
- Użyta biblioteka: `/tmp/chatadhd-w3-native-golden-consumer/latest-w2-5a73a36/libloom_w3_w2_w4_actual.so`.
- SHA256 biblioteki: `57c1b549c260e14b4f31d21c36bec34690cdcbd16c47d4238079a9c0bb23be97` — identyczny z opublikowanym dowodem golden W3.
- SHA256 niezmienionego oryginalnego probe: `915b216738ef74100794d52824f61db006d6e0717d45695ca5e240453052152c`.

Przed uruchomieniem sprawdzono zgodność 10 zachowanych plików źródłowych z podanymi commitami i z nakładką użytej biblioteki. Sprawdzono też hashe biblioteki, archiwów i oryginalnego manifestu budowy względem opublikowanych danych W3. `manifest.json` zachowuje te powiązania, dokładny command, exit i hashe logów. `original_cabi_build_manifest.json` opisuje wcześniejszą budowę rzeczywistego C ABI; jej kompilacje nie należą do tego niezależnego sprawdzenia.

Dokładne pierwotne wywołanie:

```sh
python /workspace/scratch/2fbae43b23fa/packet_replay_repro.py /tmp/chatadhd-w3-native-golden-consumer/latest-w2-5a73a36/libloom_w3_w2_w4_actual.so --expect fixed
```

## Archiwum i odtworzenie

[evidence.zip](evidence.zip) zawiera oba manifesty, wszystkie 10 zachowanych źródeł, niezmieniony probe, pełny stdout/stderr i `SHA256.json` z hashami 15 plików wejściowych. Nie zawiera binariów ani baz danych. SHA256 nie obejmuje samego `SHA256.json`; hash całego ZIP jest poniżej.

- Rozmiar ZIP: **45030** bajtów.
- SHA256 ZIP: `36f419a580f3cb97b178421b92f10846bb84a9d05639fc4ae380bb42b99d3827`.

Po rozpakowaniu sprawdź hashe plików z katalogu archiwum:

```sh
unzip evidence.zip -d packet-replay-evidence
cd packet-replay-evidence
python3 - <<'PYVERIFY'
import hashlib, json
from pathlib import Path
for name, expected in json.loads(Path("SHA256.json").read_text()).items():
    assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == expected, name
print("SHA256: PASS")
PYVERIFY
python3 packet_replay_repro.py /absolute/path/to/compatible/libloom.so --expect fixed
```

Wymagane są Python 3 i zgodna rzeczywista biblioteka z eksportami `loom_init_ex`, `loom_packet`, `loom_free_string`, `loom_shutdown` oraz schematem ledger W2 (`usage_operations`, `usage_samples`). Dla dokładnego odtworzenia użyj biblioteki o wskazanym SHA256 albo odtwórz jej źródła i budowę z manifestów. Zgodna nowsza biblioteka pozwala ponownie sprawdzić regresję, ale ma własną proweniencję. Probe tworzy świeży tymczasowy katalog danych, wyłącza workers, czyta ledger przez SQLite w trybie read-only i usuwa katalog po zakończeniu; nie wykonuje wywołań modeli.

## Zakres dowodu

PASS zamyka konkretny wcześniejszy P1: ponowienie tego samego operation ID przy częściowo nierozliczonym zasobie nie daje drugiego wykonania z pojedynczym pomiarem `calls=1`. To sprawdzenie obejmuje dwa kolejne wywołania w jednym procesie; nie jest nowym testem współbieżności ani restartu.

Osobno pozostają kwestie ogólnego kontraktu W3/W4 dla aliasu modelu i zagnieżdżonych combinations, potwierdzenie wspólnego kontraktu i golden przez autora W4 oraz pełne bramki CTest/build web na mieszanej gałęzi integracyjnej. Ten dowód nie rozstrzyga ich i sam nie oznacza przyjęcia W4 na `main`.
