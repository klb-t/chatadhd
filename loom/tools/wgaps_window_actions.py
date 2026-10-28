#!/usr/bin/env python3
"""W-gaps of OKNA_SYSTEM (one-off data edit, kept as the record of what was added and to re-run on a fresh tree; idempotent).

Adds, as catalogue actions carried out by the asking client (client.capability arranges_windows, web ASKING_CLIENT_ONLY):
- window.style / layout.style: the style (application profile) of a window or of the layout from a sentence (W9/W10 gap);
- artifact.open / artifact.copy / artifact.save: a block of an answer opened in a window, copied, saved (W8/W9 gap);
- the sentence „pokaż zapytanie” / „show the request” for window.open of the kind request_inspector (W8 gap 1).
Their pl+en sentences go to intent_routing.pack (before the W7 window sentences), the direct list, and chat_coverage.
Lists are examples (CLAUDE.md): a further style word, block word or format is data, not a new action.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from w7_window_actions import P, S, REASON, p, TARGET, cmd, load, save  # noqa: E402

STYLE = p('style', 'The style: a word naming an application profile known on the device (Claude, ChatGPT, Gemini, Loom ... - the client matches it against its profiles); a word of its data such as default clears the choice (inherit).', 'Styl: słowo nazywające profil aplikacji znany na urządzeniu (Claude, ChatGPT, Gemini, Loom ... - klient dopasowuje je do swoich profili); słowo z jego danych takie jak domyślny zdejmuje wybór (dziedziczenie).', True)
WHICH = p('which', 'Which block: a word such as last, previous, first (the client maps words through its data; empty = the last one).', 'Który blok: słowo takie jak ostatni, przedostatni, pierwszy (klient mapuje słowa przez swoje dane; puste = ostatni).')

# id: (title, description, why_pl, label_pl, phrases_en, phrases_pl, params, result)
ACTIONS = {
    'window.style': ('Set the style of a window', 'Give one window the style of an application profile (only its CSS variables change; the request to the model does not, UI17); a word such as default makes it inherit from the layout again. One journal entry, undoable.', 'Ustawia styl jednego okna.', 'Styl okna', ['window style'], ['styl okna'], [STYLE, TARGET],
                     {'en': 'The style of the window is set.', 'pl': 'Styl okna jest ustawiony.'}),
    'layout.style': ('Set the style of the layout', 'Give the whole layout the style of an application profile (windows without their own style inherit it); a word such as default makes it inherit from the person again. One journal entry, undoable.', 'Ustawia styl całego układu.', 'Styl układu', ['layout style'], ['styl układu', 'styl ukladu'], [STYLE],
                     {'en': 'The style of the layout is set.', 'pl': 'Styl układu jest ustawiony.'}),
    'artifact.open': ('Open a block of an answer in a window', 'Open a block of an answer (code, HTML, SVG, JSON ... - the formats are data of the client) in a window of the kind artifact; the same as „Otwórz w oknie” under the block.', 'Otwiera blok odpowiedzi w oknie.', 'Otwórz blok w oknie', ['open the block in a window'], ['otwórz blok w oknie'], [WHICH],
                      {'en': 'The block is open in a window.', 'pl': 'Blok jest otwarty w oknie.'}),
    'artifact.copy': ('Copy a block of an answer', 'Copy a block of an answer to the clipboard of the asking device; the same as „Kopiuj” under the block.', 'Kopiuje blok odpowiedzi do schowka.', 'Skopiuj blok', ['copy the block'], ['skopiuj blok'], [WHICH],
                      {'en': 'The block is copied.', 'pl': 'Blok jest skopiowany.'}),
    'artifact.save': ('Save a block of an answer', 'Save a block of an answer as a file on the asking device (name and extension from the data of the client); the same as „Zapisz” under the block.', 'Zapisuje blok odpowiedzi do pliku.', 'Zapisz blok', ['save the block'], ['zapisz blok'], [WHICH],
                      {'en': 'The block is saved.', 'pl': 'Blok jest zapisany.'}),
}

BLOCK_PL = r"(?:blok(?:\s+kodu)?|kod|artefakt|fragment(?:\s+kodu)?)"
BLOCK_EN = r"(?:code\s+block|block(?:\s+of\s+code)?|code|artifact|snippet)"
WHICH_PL = r"(ostatni|przedostatni|pierwszy|ten)"
WHICH_EN = r"(last|previous|first|this)"
COMMANDS = [
    # layout first: „przywróć domyślny styl układu” is the layout, „przywróć styl domyślny” the focused window
    cmd('layout.style.pl', 'layout.style', 'pl', r"(?:(?:ustaw|zmien|daj)\s+)?(?:styl\s+(?:calego\s+)?ukladu\s+(?:na\s+)?|(?:caly\s+)?uklad\s+(?:w\s+stylu|jak\s+w|jak)\s+)(.+?)", ['style']),
    cmd('layout.style.default.pl', 'layout.style', 'pl', r"przywroc\s+(?:domyslny\s+styl|styl\s+domyslny)\s+(?:calego\s+)?ukladu", [], [], {'style': 'default'}),
    cmd('layout.style.en', 'layout.style', 'en', r"(?:(?:set|change)\s+)?(?:the\s+)?(?:(?:whole\s+)?layout\s+style\s+to\s+|style\s+of\s+the\s+(?:whole\s+)?layout\s+to\s+|(?:make\s+)?the\s+(?:whole\s+)?layout\s+(?:look\s+)?like\s+|(?:the\s+)?layout\s+in\s+the\s+style\s+of\s+)(.+?)", ['style']),
    cmd('layout.style.default.en', 'layout.style', 'en', r"(?:restore|reset)\s+the\s+default\s+style\s+of\s+the\s+(?:whole\s+)?layout", [], [], {'style': 'default'}),
    cmd('window.style.pl', 'window.style', 'pl', r"(?:ustaw|zmien|daj)\s+styl\s+(?:tego\s+|tamtego\s+)?okna(?:\s+(.+?))?\s+na\s+(.+?)", ['target', 'style']),
    cmd('window.style.like.pl', 'window.style', 'pl', r"(?:ustaw|zrob|daj)\s+(?:to\s+|tamto\s+)?okno(?:\s+(.+?))?\s+(?:w\s+stylu|jak\s+w|jak)\s+(.+?)", ['target', 'style']),
    cmd('window.style.default.pl', 'window.style', 'pl', r"przywroc\s+(?:domyslny\s+styl|styl\s+domyslny)(?:\s+(?:tego\s+)?okna(?:\s+(.+?))?)?", ['target'], ['target'], {'style': 'default'}),
    cmd('window.style.en', 'window.style', 'en', r"(?:set|change)\s+the\s+style\s+of\s+(?:this|the)\s+(?:(.+?)\s+)?window\s+to\s+(.+?)", ['target', 'style']),
    cmd('window.style.like.en', 'window.style', 'en', r"make\s+(?:this|the)\s+(?:(.+?)\s+)?window\s+(?:look\s+)?like\s+(.+?)", ['target', 'style']),
    cmd('window.style.default.en', 'window.style', 'en', r"(?:restore|reset)\s+the\s+default\s+style(?:\s+of\s+(?:this|the)\s+(?:(.+?)\s+)?window)?", ['target'], ['target'], {'style': 'default'}),
    cmd('artifact.open.pl', 'artifact.open', 'pl', r"(?:otworz|pokaz)\s+" + WHICH_PL + r"\s+" + BLOCK_PL + r"\s+w\s+(?:osobnym\s+|nowym\s+|swoim\s+)?oknie", ['which']),
    cmd('artifact.open.en', 'artifact.open', 'en', r"(?:open|show)\s+(?:the\s+)?" + WHICH_EN + r"\s+" + BLOCK_EN + r"\s+in\s+(?:a|its\s+own|a\s+new|a\s+separate)\s+window", ['which']),
    cmd('artifact.copy.pl', 'artifact.copy', 'pl', r"skopiuj\s+" + WHICH_PL + r"\s+" + BLOCK_PL + r"(?:\s+do\s+schowka)?", ['which']),
    cmd('artifact.copy.en', 'artifact.copy', 'en', r"copy\s+(?:the\s+)?" + WHICH_EN + r"\s+" + BLOCK_EN + r"(?:\s+to\s+(?:the\s+)?clipboard)?", ['which']),
    cmd('artifact.save.pl', 'artifact.save', 'pl', r"(?:zapisz|pobierz)\s+" + WHICH_PL + r"\s+" + BLOCK_PL + r"(?:\s+(?:do\s+pliku|jako\s+plik|na\s+dysku?))?", ['which']),
    cmd('artifact.save.en', 'artifact.save', 'en', r"(?:save|download)\s+(?:the\s+)?" + WHICH_EN + r"\s+" + BLOCK_EN + r"(?:\s+(?:to\s+a\s+file|as\s+a\s+file))?", ['which']),
]
REQUEST = [
    cmd('window.open.request.pl', 'window.open', 'pl', r"(?:pokaz|otworz|wyswietl)\s+(?:mi\s+)?(?:ostatnie\s+|wyslane\s+)?(?:zapytanie|zadanie|request)(?:\s+do\s+(?:modelu|jadra))?", [], [], {'target': 'request_inspector'}),
    cmd('window.open.request.en', 'window.open', 'en', r"(?:show|open)\s+(?:me\s+)?(?:the\s+)?(?:last\s+|outgoing\s+|sent\s+)?request(?:\s+to\s+the\s+(?:model|kernel))?", [], [], {'target': 'request_inspector'}),
]
DIRECT = list(ACTIONS)

COVERAGE = [
    {
        'id': 'window_style', 'title': {'en': 'Set the style of a window or of the layout by a sentence', 'pl': 'Ustaw styl okna albo układu zdaniem'},
        'via': 'client', 'action': 'window.style', 'also': ['layout.style'], 'surfaces': {},
        'examples': {'pl': 'ustaw styl tego okna na Claude', 'en': 'set the style of this window to Claude'},
        'examples_more': {'pl': ['układ w stylu ChatGPT', 'przywróć styl domyślny', 'ustaw styl okna graf na Gemini', 'przywróć domyślny styl układu', 'zrób to okno jak w Claude'],
                          'en': ['make the layout look like ChatGPT', 'restore the default style', 'make this window look like Gemini', 'set the layout style to Loom']},
        'note': {'en': 'W-gaps of OKNA_SYSTEM: the style word is matched by the page against the application profiles it knows (built in and imported); the words that clear a choice are data of the page (styles.json clear_words). One journal entry; layout.undo takes it back.',
                 'pl': 'Luki W z OKNA_SYSTEM: słowo stylu strona dopasowuje do znanych sobie profili aplikacji (wbudowanych i zaimportowanych); słowa, które zdejmują wybór, to dane strony (styles.json clear_words). Jeden wpis dziennika; cofnij układ go zdejmuje.'},
    },
    {
        'id': 'artifact_actions', 'title': {'en': 'Open, copy or save a block of an answer by a sentence', 'pl': 'Otwórz, skopiuj albo zapisz blok odpowiedzi zdaniem'},
        'via': 'client', 'action': 'artifact.open', 'also': ['artifact.copy', 'artifact.save'], 'surfaces': {},
        'examples': {'pl': 'otwórz ostatni blok kodu w oknie', 'en': 'open the last code block in a window'},
        'examples_more': {'pl': ['skopiuj ostatni blok', 'zapisz ostatni blok', 'skopiuj przedostatni kod do schowka', 'pokaż pierwszy blok w osobnym oknie'],
                          'en': ['copy the last block', 'save the last block', 'download the previous code block']},
        'note': {'en': 'W-gaps of OKNA_SYSTEM: the page that asked finds the block among the answers of the conversation it shows (the words of which block are data, artifacts.json which_words) and does what the buttons under the block do.',
                 'pl': 'Luki W z OKNA_SYSTEM: strona, która pytała, znajduje blok wśród odpowiedzi pokazanej rozmowy (słowa „który blok” to dane, artifacts.json which_words) i robi to samo co przyciski pod blokiem.'},
    },
]
REQUEST_MORE = {'pl': ['pokaż zapytanie', 'pokaż ostatnie zapytanie do modelu'], 'en': ['show the request', 'show me the last request']}


def main():
    path, a, nl = load('actions.pack')
    acts = a['defaults']['actions']
    tpl = acts['window.open']
    for aid, (title, desc, why, label, ph_en, ph_pl, params, result) in ACTIONS.items():
        e = json.loads(json.dumps(tpl))
        e.update({'id': aid, 'planned_reason': REASON, 'title': title, 'description': desc, 'why': {'pl': why}, 'labels': {'pl': label},
                  'phrases': {'en': ph_en, 'pl': ph_pl}, 'params': params, 'result_text': result})
        e['effects'] = dict(e['effects'], irreversible=False)
        e['client'] = {'capability': 'arranges_windows', 'prompt': {'en': 'Do it on this device.', 'pl': 'Zrób to na tym urządzeniu.'}, 'expects': []}
        acts[aid] = e
    save(path, a, nl)

    path, ir, nl = load('intent_routing.pack')
    d = ir['defaults']
    mine = {c['id'] for c in COMMANDS + REQUEST}
    cmds = [c for c in d['heuristic']['commands'] if c['id'] not in mine]
    # before the W2 window.open sentences: „open the last code block in a window” must not be window.open of a kind „last code block”
    at = next(i for i, c in enumerate(cmds) if c['id'] == 'window.open.beside.pl')
    d['heuristic']['commands'] = cmds[:at] + COMMANDS + REQUEST + cmds[at:]
    direct = d['execution']['direct']
    for x in DIRECT:
        if x not in direct: direct.append(x)
    save(path, ir, nl)

    path, cov, nl = load('chat_coverage.pack')
    caps = cov['defaults']['capabilities']
    caps[:] = [c for c in caps if c['id'] not in {x['id'] for x in COVERAGE}]
    at = next(i for i, c in enumerate(caps) if c['id'] == 'window_coupling') + 1
    caps[at:at] = COVERAGE
    wo = next(c for c in caps if c.get('action') == 'window.open' or 'window.open' in c.get('also', []))
    for lang in ('pl', 'en'):
        more = wo.setdefault('examples_more', {}).setdefault(lang, [])
        for s in REQUEST_MORE[lang]:
            if s not in more: more.append(s)
    save(path, cov, nl)
    print('ok', len(ACTIONS), 'actions,', len(COMMANDS) + len(REQUEST), 'commands, window.open coverage:', wo['id'])


if __name__ == '__main__':
    sys.exit(main())
