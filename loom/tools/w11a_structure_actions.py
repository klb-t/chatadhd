#!/usr/bin/env python3
"""W11a-1 (window „Struktura projektu”): one-off data edit, kept as the record of what was added and to re-run on a fresh tree; idempotent.

- intent_routing: the sentences „pokaż strukturę projektu X” / „show the structure of project X” reach window.open with target
  structure and node = the words that name the root (the page passes them to graph.structure, which resolves a name to an address);
- chat_coverage: the capability project_structure (via client, window.open) and graph.structure's own surfaces as display
  (the window draws it; a sentence opens the window).
The sentences are examples (CLAUDE.md); another wording is data.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from w7_window_actions import cmd, load, save  # noqa: E402

COMMANDS = [
    cmd('window.open.structure.pl', 'window.open', 'pl',
        r"(?:pokaz|otworz|wyswietl)\s+(?:mi\s+)?(?:strukture|drzewo)(?:\s+(?:projektu|jednostki|wezla))?(?:\s+(.+?))?", ['node'], None, {'target': 'structure'}),
    cmd('window.open.structure.en', 'window.open', 'en',
        r"(?:show|open)\s+(?:me\s+)?(?:the\s+)?(?:project\s+)?(?:structure|tree)(?:\s+of)?(?:\s+(?:the\s+)?(?:project|unit|node))?(?:\s+(.+?))?", ['node'], None, {'target': 'structure'}),
]
COVERAGE = [{
    'id': 'project_structure',
    'title': {'en': 'The structure of a project or another node: what is there and what is expected', 'pl': 'Struktura projektu albo innego węzła: co jest i czego się spodziewamy'},
    'via': 'client', 'action': 'window.open', 'surfaces': {},
    'examples': {'pl': 'pokaż strukturę projektu Alfa', 'en': 'show the structure of project Alfa'},
    'examples_more': {'pl': ['pokaż strukturę projektu', 'otwórz drzewo jednostki Alfa'], 'en': ['show the project structure', 'open the tree of unit Alfa']},
    'note': {'en': 'W11a-1: the sentence opens the window project_structure (panel structure) with the root the words name; the window reads graph.structure (read only).',
             'pl': 'W11a-1: zdanie otwiera okno „Struktura projektu” z korzeniem, który nazywają słowa; okno czyta graph.structure (tylko odczyt).'},
}]
DISPLAY = {
    'id': 'graph_structure', 'category': 'display',
    'reason': {'en': 'What the window „Struktura projektu” draws: the structure of a node is the content of that window; a sentence opens the window (capability project_structure), not this read.',
               'pl': 'To, co rysuje okno „Struktura projektu”: struktura węzła jest treścią tego okna; zdanie otwiera okno (zdolność project_structure), a nie ten odczyt.'},
    'surfaces': {'http': ['POST /api/graph/structure'], 'invoke': ['actions.graph.structure']},
}


def main():
    path, ir, nl = load('intent_routing.pack')
    d = ir['defaults']
    mine = {c['id'] for c in COMMANDS}
    cmds = [c for c in d['heuristic']['commands'] if c['id'] not in mine]
    at = next(i for i, c in enumerate(cmds) if c['id'] == 'window.open.request.en') + 1
    d['heuristic']['commands'] = cmds[:at] + COMMANDS + cmds[at:]
    save(path, ir, nl)

    path, cov, nl = load('chat_coverage.pack')
    caps = cov['defaults']['capabilities']
    caps[:] = [c for c in caps if c['id'] not in {x['id'] for x in COVERAGE}]
    at = next(i for i, c in enumerate(caps) if c['id'] == 'windows') + 1
    caps[at:at] = COVERAGE
    nfc = cov['defaults']['not_from_chat']
    nfc[:] = [c for c in nfc if c['id'] != DISPLAY['id']]
    at = next(i for i, c in enumerate(nfc) if c['id'] == 'conversation_view') + 1
    nfc[at:at] = [DISPLAY]
    save(path, cov, nl)
    print('ok', len(COMMANDS), 'commands, coverage', [c['id'] for c in COVERAGE], DISPLAY['id'])


if __name__ == '__main__':
    sys.exit(main())
