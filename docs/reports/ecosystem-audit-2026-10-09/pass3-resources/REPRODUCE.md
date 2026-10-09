# Uruchomienie indeksu i testów

Katalog narzędzi: `tools/ecosystem-audit-2026-10-09/pass3-resources/`. Skrypty przyjmują ścieżkę checkoutu i dokładne SHA; nie zastępują badanych konsumentów kopią implementacji.

```sh
python tools/ecosystem-audit-2026-10-09/pass3-resources/run_index.py --list
python tools/ecosystem-audit-2026-10-09/pass3-resources/run_index.py --validate
python tools/ecosystem-audit-2026-10-09/pass3-resources/run_index.py \
  --jobs /path/to/jobs.json --output /path/to/new-index-results
```

`jobs.json` jest niepustą tablicą `{ "suite": "python-import", "args": ["--repo", "/path/to/checkout", "--sha", "FULL_SHA", "--out", "/path/to/new-receipt.json"] }`. Argumenty przekazywane są bez powłoki. Dla pozostałych zestawów użyj opcji wypisanych przez `--list` oraz instrukcji modułu:

- `importers/REPRODUCE.md`: rzeczywisty Python importer/SQLite oraz Watchdog mapping/repositories/object store/SSR. Watchdog korzysta z zainstalowanych zależności jego lockfile.
- `native/README.md`: rzeczywisty C++ Runtime, katalog, import, extract, KnowledgeStore i profil sieciowy. Native runner weryfikuje źródła i pochodzenie linkowanych obiektów.
- `discovery/README.md`: rzeczywisty registry/ContextEngine/provider/import_unknown z publicznym packetem C2. Potrzebuje istniejącego native builda i osobnego checkoutu research.
- `web/REPRODUCE.md`: rzeczywiste moduły TypeScript, NativeGraphStore i dwie zapisane/odczytane rewizje profilu. Node i TypeScript są zależnościami hostowymi; tylko końcowy HTTP fetch jest przechwycony.

Wszystkie fixtures są syntetyczne. Nie przekazuj prywatnego archiwum ani prawdziwych kluczy. Native driver używa transportu testowego; web guard Node/Python nie jest sandboxem C++, a jego native część wywołuje wyłącznie lokalne GraphPacketStore z workerami wyłączonymi. Nie uruchamiaj płatnych modeli/CI.

Wynik runnera 0 nie oznacza automatycznie zgodności produktu, jeżeli wybrano reprodukcję. FAIL oraz BLOCKED zachowują niezerowy wynik zgodnie z instrukcją danego modułu. Indeks kontynuuje po niepowodzeniu i zachowuje log każdego zadania. Wykonany przykład znajduje się w `index-execution/`: reprodukcje mają PASS, lecz nierozwiązane BLOCKED pozostały jawne. Nie doliczamy tej demonstracji ponownie do 82 kryteriów.

Trzy BLOCKED web i inne missing-contract wpisy są świadomymi granicami istniejących konsumentów. Nie wykryją automatycznie przyszłego nowego API. Po implementacji B należy przypiąć do niego cienki adapter testowy, bez implementowania parsera/projekcji w teście; dotychczasowe dodatnie i ujemne bramki pozostają.
