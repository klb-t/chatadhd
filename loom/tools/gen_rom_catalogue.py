#!/usr/bin/env python3
"""Generate data/graph/rom.pack: the ROM catalogue (GRAF_HUB_KOD_SILNIK section 3, 13 items).

ROM = the read-only memory shipped with the program, mounted first. This catalogue lists, for each
of the 13 items, the real program files / data packs that implement it by path (hashes are computed on demand, not pinned). Files are
resolved from the working tree (globs allowed); an item with no file is listed as "missing", an item
with a known hole is "partial" and says which hole. Nothing is invented: to claim a file exists, it must.
Usage: gen_rom_catalogue.py [--check]   (--check fails when the pack is stale)
The pack does not hash itself; its own pin is the entry in data/data_domains.pack.
"""
import hashlib
import json
import sys
from pathlib import Path

LOOM = Path(__file__).resolve().parents[1]
REPO = LOOM.parent
OUT = LOOM / 'data/graph/rom.pack'

# id -> (section-3 number, label en/pl, why en/pl (what the item is for), files (repo-relative, globs ok), gaps)
ITEMS = [
    ('boot', 1, {'en': 'Boot', 'pl': 'Rozruch'},
     {'en': 'Finds and opens memories (data-dir resolution order, code != data) before any memory can be read.',
      'pl': 'Znajduje i otwiera pamięci (kolejność szukania katalogu danych, kod ≠ dane), zanim jakąkolwiek pamięć da się przeczytać.'},
     ['loom/include/loom/config.h', 'loom/src/core/config.cpp', 'loom/src/util/fs.cpp',
      'loom/data/runtime/runtime_paths.pack', 'loom/data/pack.json'],
     ['Memory mounting beyond the data directory is not wired into Runtime yet (see rom.h MountRegistry; Runtime owns the data dir).']),
    ('meta_ontology', 2, {'en': 'Meta-ontology', 'pl': 'Meta-ontologia'},
     {'en': 'What a node, edge, action, field, provenance and version are; base types the engine must read in any graph.',
      'pl': 'Czym jest węzeł, krawędź, działanie, pole, pochodzenie i wersja; podstawowe typy, które silnik musi umieć przeczytać w każdym grafie.'},
     ['loom/data/graph/axes.pack', 'loom/data/graph/classes.pack', 'loom/data/graph/properties.pack',
      'loom/data/graph/parties.pack', 'loom/data/graph/scopes.pack', 'loom/data/schema/types.json',
      'loom/data/runtime/graph_views.pack', 'loom/include/loom/effective_view.h', 'loom/src/views/effective_view.cpp',
      'loom/src/views/perspectives.cpp', 'loom/src/views/scope_resolver.cpp',
      'loom/include/loom/view_engine.h', 'loom/src/views/view_engine.cpp',
      'loom/include/loom/interaction.h', 'loom/src/views/interaction.cpp'],
     []),
    ('action_interpreter', 3, {'en': 'Action interpreter', 'pl': 'Interpreter akcji'},
     {'en': 'The universal engine: apply the action named in the graph to the given nodes.',
      'pl': 'Uniwersalny silnik: wykonuje działanie nazwane w grafie na podanych węzłach.'},
     ['loom/src/actions/*.cpp', 'loom/include/loom/action_rules.h', 'loom/include/loom/action_table.h',
      'loom/data/runtime/actions.pack', 'loom/data/runtime/action_rules.pack'],
     []),
    ('drivers', 4, {'en': 'Drivers and adapters', 'pl': 'Sterowniki i adaptery'},
     {'en': 'Executable adapters: provider APIs, storage backends, file formats, devices, cryptography.',
      'pl': 'Wykonywalne adaptery: API dostawców, magazyny danych, formaty plików, urządzenia, kryptografia.'},
     ['loom/src/llm/adapters.cpp', 'loom/src/llm/use_model.cpp', 'loom/src/providers/providers.cpp',
      'loom/src/media/media_providers.cpp', 'loom/src/storage/*_backend.cpp', 'loom/src/github/*.cpp',
      'loom/src/crypto/crypto.cpp', 'loom/src/crypto/backend.cpp'],
     ['Input/output devices (microphone, camera, screen) are not drivers in the core; they live in clients.']),
    ('factory_presets', 5, {'en': 'Factory presets', 'pl': 'Ustawienia fabryczne'},
     {'en': 'Starting point and "restore defaults": preset packs and default runtime profiles/flows.',
      'pl': 'Punkt startowy i „przywróć ustawienia domyślne”: paczki presetów oraz domyślne profile i przepływy działania.'},
     ['loom/data/presets/*.pack', 'loom/data/runtime/*.pack', 'loom/data/context/runtime_preset_layers.pack',
      'loom/data/policy/*.json', 'loom/data/policy/*.pack'],
     ['runtime/graph_entry.pack embeds this catalogue and is therefore not hashed here (circular); it is pinned in data_domains.pack.']),
    ('self_model', 6, {'en': 'Self-model', 'pl': 'Model samego siebie'},
     {'en': 'What the application knows about itself regardless of the user: philosophy, requirements, state, costs (R52).',
      'pl': 'Co aplikacja wie o sobie niezależnie od użytkownika: filozofia, wymagania, stan, koszty (R52).'},
     ['loom/data/philosophy/*.json', 'loom/data/profiles/self.json', 'loom/data/runtime/economy.pack',
      'docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md', 'docs/STATE.md'],
     ['Version/capabilities come from loom_version at run time; there is no pinned machine-readable capability/limits manifest.',
      'R1-R53 exist as prose in docs, not as a data pack.']),
    ('safety_fuses', 7, {'en': 'Safety fuses', 'pl': 'Bezpieczniki'},
     {'en': 'Confirmation at x10 growth in usage and other guards that must not be switched off by accident.',
      'pl': 'Potwierdzenie przy dziesięciokrotnym (×10) wzroście zużycia i inne zabezpieczenia, których nie da się wyłączyć przez przypadek.'},
     ['loom/src/policy/usage_policy.cpp', 'loom/src/policy/usage_policy_preset.inc',
      'loom/data/policy/usage_policy.pack', 'loom/data/runtime/usage_policy.pack'],
     ['The x10 guard is a configurable preset (overlay can change it), not pinned in code; the owner rule makes this intentional.',
      'No dedicated "never overwrite an unread file" guard module: only point checks in storage backends and importers.']),
    ('validators_migrations', 8, {'en': 'Validators and migrations', 'pl': 'Walidatory i migracje'},
     {'en': 'How to read old formats and move data between versions.',
      'pl': 'Jak czytać stare formaty i przenosić dane między wersjami.'},
     ['loom/src/db/db.cpp', 'loom/src/db/schema_sql.inc', 'loom/src/model/runtime_profile.cpp',
      'loom/data/validation/product_literals.pack',
      'loom/src/util/product_literal_guard.py'],
     []),
    ('rescue_mode', 9, {'en': 'Rescue mode', 'pl': 'Tryb ratunkowy'},
     {'en': 'Minimal interface to settings and memories when everything else failed.',
      'pl': 'Minimalny dostęp do ustawień i pamięci, gdy wszystko inne zawiodło.'},
     [],
     ['No rescue mode exists: no minimal settings/memory interface that works with a broken configuration or memory.']),
    ('roots_of_trust', 10, {'en': 'Roots of trust', 'pl': 'Korzenie zaufania'},
     {'en': 'Cryptographic algorithms and keys that verify updates and signatures; must not come from the data they verify.',
      'pl': 'Algorytmy i klucze kryptograficzne, które sprawdzają aktualizacje i podpisy; nie mogą pochodzić z danych, które sprawdzają.'},
     ['loom/include/loom/crypto.h', 'loom/include/loom/crypto_suite.h', 'loom/src/crypto/envelope.cpp',
      'loom/src/crypto/sealed.cpp', 'loom/src/crypto/sig_hybrid.cpp', 'loom/src/crypto/kem_xwing.cpp',
      'loom/src/crypto/hash_portable.cpp', 'loom/src/util/sha256.cpp'],
     ['Algorithms only: there is no embedded update/signature verification key and no update verifier.']),
    ('id_time', 11, {'en': 'Identity and time generators', 'pl': 'Generatory tożsamości i czasu'},
     {'en': 'Identifier format, clock and versioning, so addresses stay consistent across the ecosystem.',
      'pl': 'Format identyfikatorów, zegar i wersjonowanie, aby adresy były spójne w całym ekosystemie.'},
     ['loom/include/loom/util/ids.h', 'loom/include/loom/util/time.h', 'loom/src/util/random_ids_time.cpp'],
     []),
    ('ui_texts', 12, {'en': 'Base UI texts', 'pl': 'Podstawowe teksty interfejsu'},
     {'en': 'Texts (pl, en) the application needs before any memory is loaded.',
      'pl': 'Teksty (pl, en), których aplikacja potrzebuje, zanim wczyta jakąkolwiek pamięć.'},
     ['loom/data/onboarding/ui.pack', 'loom/data/runtime/cli.pack', 'loom/data/runtime/settings_fields.pack'],
     ['Web UI strings are compiled into the web bundle (loom/web/src), not a shared data pack.']),
    ('catalogue', 13, {'en': 'ROM catalogue', 'pl': 'Katalog ROM'},
     {'en': 'The list of all items above with graph addresses; makes the ROM visible in the graph like any memory.',
      'pl': 'Lista wszystkich pozycji powyżej z adresami w grafie; dzięki niej ROM widać w grafie jak każdą pamięć.'},
     ['loom/tools/gen_rom_catalogue.py', 'loom/include/loom/rom.h', 'loom/src/graph/rom.cpp', 'loom/tests/test_rom.cpp'],
     ['The catalogue pack is pinned in data_domains.pack.']),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# The generated profile embeds this catalogue, so hashing it here would be circular.
EXCLUDE = {'loom/data/runtime/graph_entry.pack', 'loom/src/model/runtime_profiles_embedded.inc'}


def resolve(patterns):
    found = {}
    for pattern in patterns:
        for p in sorted(REPO.glob(pattern)):
            if p.is_file() and p.relative_to(REPO).as_posix() not in EXCLUDE:
                found[p.relative_to(REPO).as_posix()] = p
    return [{'path': k} for k in sorted(found)]


def build():
    items = {}
    for key, num, label, why, patterns, gaps in ITEMS:
        files = resolve(patterns)
        missing_declared = [p for p in patterns if not any(c in p for c in '*?[') and not (REPO / p).is_file()]
        gaps = gaps + [f'declared file absent: {p}' for p in missing_declared]
        status = 'missing' if not files else ('partial' if gaps else 'present')
        if not isinstance(why, dict) or not why.get('en') or not why.get('pl'):
            raise SystemExit(f'ROM item {key}: why needs en and pl')
        items[key] = {'number': num, 'label': label, 'why': why, 'address': f'rom:{key}', 'status': status,
                      'files': files, 'gaps': gaps}
    return {
        'schema': 'loom.rom_catalogue/1',
        'revision': 1,
        'description': 'ROM catalogue (GRAF_HUB_KOD_SILNIK section 3): the 13 read-only items shipped with the program, '
                       'generated by tools/gen_rom_catalogue.py from the working tree. Do not edit by hand. status: '
                       'present = files found and no known gap; partial = files found with a stated gap; missing = nothing '
                       'implements it. A user may override an item in the writable memory through the graph_entry overlay '
                       '(rom.items.<id>); the ROM itself is never written.',
        'mount': {'name': 'rom', 'order': 0, 'writable': False},
        'items': items,
    }


def main():
    if '--hashes' in sys.argv:
        out = Path(sys.argv[sys.argv.index('--hashes') + 1])
        doc = build()
        for item in doc['items'].values():
            for f in item['files']:
                f['sha256'] = sha(REPO / f['path'])
        out.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
        return 0
    text = json.dumps(build(), indent=2, ensure_ascii=False) + '\n'
    if '--check' in sys.argv:
        if not OUT.exists() or OUT.read_text(encoding='utf-8') != text:
            print('stale: run loom/tools/gen_rom_catalogue.py', file=sys.stderr)
            return 1
        return 0
    OUT.write_text(text, encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main())
