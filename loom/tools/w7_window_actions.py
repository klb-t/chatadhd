#!/usr/bin/env python3
"""W7 of OKNA_SYSTEM (one-off data edit, kept as the record of what was added and to re-run on a fresh tree).

Adds the window and layout operations of §6.1 as catalogue actions carried out by the asking client (client.capability
arranges_windows), their pl+en sentences in intent_routing.pack (heuristic.commands, after layout.reset.en), the direct
execution list (layout.delete is NOT direct: a proposal and „tak”), and the chat_coverage entries. Idempotent: an id already
present is replaced by the same content. Lists are examples (CLAUDE.md): any further operation is one more entry here.
"""
import json
import sys
from pathlib import Path

RT = Path(__file__).resolve().parent.parent / 'data/runtime'
P = r"^[\s,]*(?:prosze\s+|please\s+|can\s+you\s+|could\s+you\s+|moglbys\s+|mozesz\s+)?"
S = r"\s*[.!?]*\s*$"

REASON = {
    'en': 'A window of the application belongs to the client that draws it (the web page or the Android shell); this runtime arranges no windows itself. Runnable for a client that declares arranges_windows (W7 of OKNA_SYSTEM); only the client that asked carries it out.',
    'pl': 'Okno aplikacji należy do klienta, który je rysuje (strony albo powłoki Androida); ten runtime sam nie układa okien. Wykonywalna dla klienta, który deklaruje arranges_windows (W7 z OKNA_SYSTEM); wykonuje ją tylko klient, który pytał.',
}

def p(name, en, pl, required=False):
    return {'name': name, 'type': 'string', 'required': required, 'description': {'en': en, 'pl': pl}}

TARGET = p('target', 'The window: a kind or a word naming it (graph, settings, chat ...); empty = the focused window.', 'Okno: rodzaj albo słowo, które go nazywa (graf, ustawienia, czat ...); puste = okno z fokusem.')

# id: (title, description, why_pl, label_pl, phrases_en, phrases_pl, params, irreversible)
ACTIONS = {
    'window.maximize': ('Maximize a window', 'Show a window full-screen over the layout (the others wait in the switcher); a window not open yet is opened first.', 'Pokazuje okno na całym ekranie.', 'Zmaksymalizuj okno', ['maximize', 'maximise'], ['zmaksymalizuj'], [TARGET], False),
    'window.restore': ('Restore a window', 'Bring a maximized or minimized window back to its place in the layout (its leaf, or floating).', 'Przywraca okno na jego miejsce w układzie.', 'Przywróć okno', ['restore the window'], ['przywróć okno', 'przywroc okno'], [TARGET], False),
    'window.minimize': ('Minimize a window', 'Collapse a window to a handle on the shelf; it stays in the switcher.', 'Zwija okno do uchwytu.', 'Zminimalizuj okno', ['minimize', 'minimise'], ['zminimalizuj', 'zwiń'], [TARGET], False),
    'window.dock': ('Dock a window', 'Dock a window to a side (left, right, top, bottom - an open word the client maps through its data) of the window it opened over or of the focused one; a window not open yet is opened first.', 'Dokuje okno do krawędzi.', 'Zadokuj okno', ['dock'], ['zadokuj'], [TARGET, p('side', 'The side: a word such as left, right, top, bottom (the client maps words through its data).', 'Strona: słowo takie jak lewa, prawa, góra, dół (klient mapuje słowa przez swoje dane).')], False),
    'window.float': ('Float a window', 'Detach a window from the dock tree into a floating window (rect from the preset of the client).', 'Odpina okno do pływającego.', 'Odepnij okno', ['float', 'undock'], ['odepnij', 'w pływającym oknie'], [TARGET], False),
    'window.tab': ('Put a window in a tab', 'Dock a window as a tab of the leaf of the window it opened over or of the focused one.', 'Wstawia okno jako zakładkę.', 'Okno jako zakładka', ['as a tab'], ['jako zakładkę'], [TARGET], False),
    'window.move': ('Move a window', 'Move a floating window by a step in a direction (a docked one floats first); the step is a preset of the client, a number in the sentence overrides it.', 'Przesuwa pływające okno.', 'Przesuń okno', ['move the window'], ['przesuń okno', 'przesun okno'], [TARGET, p('direction', 'A word such as left, right, up, down.', 'Słowo takie jak lewo, prawo, góra, dół.'), p('amount', 'Pixels (empty = the preset step).', 'Piksele (puste = krok z presetu).')], False),
    'window.resize': ('Resize a window', 'Give a floating window a size (width x height in px) or grow / shrink it by the preset factor of the client (a docked one floats first).', 'Zmienia rozmiar okna.', 'Zmień rozmiar okna', ['resize'], ['zmień rozmiar', 'zmien rozmiar'], [TARGET, p('w', 'Width in px.', 'Szerokość w px.'), p('h', 'Height in px.', 'Wysokość w px.'), p('how', 'A word such as grow, shrink, widen, narrow.', 'Słowo takie jak powiększ, zmniejsz, poszerz, zwęź.')], False),
    'window.bind': ('Bind a parameter of windows', 'Couple a parameter (zoom, pan ... - a word the client maps through its coupling data) of every open window of a kind through a shared channel: changing it in one changes it in the others.', 'Wiąże parametr okien jednego rodzaju.', 'Powiąż parametr okien', ['link the', 'couple the'], ['powiąż', 'powiaz', 'połącz', 'polacz'], [p('param', 'The parameter: a word such as zoom, pan.', 'Parametr: słowo takie jak przybliżenie, przesunięcie.', True), TARGET], False),
    'window.unbind': ('Unbind a parameter of windows', 'Make a parameter of every open window of a kind independent again; each keeps the value it shows now.', 'Rozwiązuje wiązanie parametru.', 'Rozwiąż parametr okien', ['unlink the', 'uncouple the'], ['rozwiąż', 'rozwiaz', 'rozłącz', 'rozlacz'], [p('param', 'The parameter: a word such as zoom, pan.', 'Parametr: słowo takie jak przybliżenie, przesunięcie.', True), TARGET], False),
    'layout.rename': ('Rename a layout', 'Give a named layout of the person another name (a name already taken is answered, nothing is overwritten).', 'Zmienia nazwę zapisanego układu.', 'Zmień nazwę układu', ['rename the layout'], ['zmień nazwę układu', 'zmien nazwe ukladu'], [p('name', 'The layout now.', 'Obecna nazwa układu.', True), p('label', 'The new name.', 'Nowa nazwa.', True)], False),
    'layout.delete': ('Delete a layout', 'Delete a named layout of the person (the shipped layouts stay). Asks first: a proposal and a yes.', 'Usuwa zapisany układ.', 'Usuń układ', ['delete the layout'], ['usuń układ', 'usun uklad'], [p('name', 'The name of the layout.', 'Nazwa układu.', True)], True),
}

def cmd(cid, action, lang, body, args, original=None, setv=None):
    c = {'id': cid, 'action': action, 'lang': lang, 'regex': P + body + S, 'args': args, 'original': original if original is not None else list(args)}
    if setv: c['set'] = setv
    c['takes_scope'] = False
    return c

SIDE_PL = r"(lewej|prawej|lewa|prawa|lewo|prawo|gorze|gory|gore|dole|dolu|dol)"
KIN = r"(?:obu|obydwu|tych|wszystkich|dwoch|moich)"
COMMANDS = [
    cmd('window.maximize.pl', 'window.maximize', 'pl', r"(?:zmaksymalizuj|powieksz\s+na\s+caly\s+ekran)(?:\s+(?:okno\s+)?(.+?))?", ['target']),
    cmd('window.maximize.full.pl', 'window.maximize', 'pl', r"pokaz\s+(?:okno\s+)?(.+?)\s+na\s+calym\s+ekranie", ['target']),
    cmd('window.maximize.en', 'window.maximize', 'en', r"(?:maximi[sz]e|make\s+full\s*screen)(?:\s+(?:the\s+)?(.+?))?(?:\s+window)?", ['target']),
    cmd('window.restore.pl', 'window.restore', 'pl', r"(?:przywroc|odmaksymalizuj)\s+okno(?:\s+(.+?))?", ['target']),
    cmd('window.restore.en', 'window.restore', 'en', r"(?:restore|unmaximi[sz]e)\s+(?:the\s+)?(?:(.+?)\s+)?window", ['target']),
    cmd('window.minimize.pl', 'window.minimize', 'pl', r"(?:zminimalizuj|zwin)\s+(?:okno\s+)?(.+?)", ['target']),
    cmd('window.minimize.en', 'window.minimize', 'en', r"(?:minimi[sz]e|collapse)\s+(?:the\s+)?(.+?)(?:\s+window)?", ['target']),
    cmd('window.dock.pl', 'window.dock', 'pl', r"(?:zadokuj|przypnij|dosun|przyklej)\s+(?:okno\s+)?(.+?)\s+(?:po|do|na|u)\s+" + SIDE_PL + r"(?:\s+(?:stronie|strony|krawedzi))?", ['target', 'side']),
    cmd('window.dock.en', 'window.dock', 'en', r"(?:dock|pin|snap)\s+(?:the\s+)?(.+?)(?:\s+window)?\s+(?:on|to|at)\s+(?:the\s+)?(left|right|top|bottom)(?:\s+(?:side|edge))?", ['target', 'side']),
    cmd('window.float.pl', 'window.float', 'pl', r"(?:odepnij|odlacz)\s+(?:okno\s+)?(.+?)", ['target']),
    cmd('window.float.window.pl', 'window.float', 'pl', r"(?:pokaz|otworz)\s+(?:okno\s+)?(.+?)\s+w\s+plywajacym\s+oknie", ['target']),
    cmd('window.float.en', 'window.float', 'en', r"(?:float|undock|detach)\s+(?:the\s+)?(.+?)(?:\s+window)?", ['target']),
    cmd('window.float.window.en', 'window.float', 'en', r"(?:show|open)\s+(?:the\s+)?(.+?)\s+in\s+a\s+floating\s+window", ['target']),
    cmd('window.tab.pl', 'window.tab', 'pl', r"(?:dodaj|wstaw|daj|przenies|otworz)\s+(?:okno\s+)?(.+?)\s+(?:jako|do|w)\s+zakladk\w*", ['target']),
    cmd('window.tab.en', 'window.tab', 'en', r"(?:put|move|open|add)\s+(?:the\s+)?(.+?)(?:\s+window)?\s+(?:in|into|as)\s+a\s+tab", ['target']),
    cmd('window.move.pl', 'window.move', 'pl', r"przesun\s+(?:okno\s+)?(.+?)\s+(?:w|na|do)\s+(lewo|prawo|gore|dol)(?:\s+o\s+(\d+)(?:\s*(?:px|pikseli|piksele|piksel))?)?", ['target', 'direction', 'amount']),
    cmd('window.move.en', 'window.move', 'en', r"move\s+(?:the\s+)?(.+?)(?:\s+window)?\s+(left|right|up|down)(?:\s+by\s+(\d+)(?:\s*(?:px|pixels))?)?", ['target', 'direction', 'amount']),
    cmd('window.resize.pl', 'window.resize', 'pl', r"zmien\s+rozmiar\s+(?:okna\s+)?(.+?)\s+na\s+(\d+)\s*(?:x|na|×)\s*(\d+)(?:\s*px)?", ['target', 'w', 'h']),
    cmd('window.resize.how.pl', 'window.resize', 'pl', r"(powieksz|zmniejsz|poszerz|zwez)\s+okno\s+(.+?)", ['how', 'target']),
    cmd('window.resize.en', 'window.resize', 'en', r"resize\s+(?:the\s+)?(.+?)(?:\s+window)?\s+to\s+(\d+)\s*(?:x|by|×)\s*(\d+)(?:\s*px)?", ['target', 'w', 'h']),
    cmd('window.resize.how.en', 'window.resize', 'en', r"(enlarge|shrink|grow|widen|narrow)\s+(?:the\s+)?(.+?)\s+window", ['how', 'target']),
    cmd('window.bind.pl', 'window.bind', 'pl', r"(?:powiaz|polacz|zwiaz|sprzez|zsynchronizuj)\s+(.+?)\s+" + KIN + r"\s+(.+?)", ['param', 'target']),
    cmd('window.bind.en', 'window.bind', 'en', r"(?:link|couple|bind|sync|synchroni[sz]e)\s+(?:the\s+)?(.+?)\s+of\s+(?:both|the|these|all|all\s+the|all\s+of\s+the)\s+(.+?)", ['param', 'target']),
    cmd('window.unbind.pl', 'window.unbind', 'pl', r"(?:rozwiaz|rozlacz|odwiaz|odlacz)\s+(.+?)\s+" + KIN + r"\s+(.+?)", ['param', 'target']),
    cmd('window.unbind.en', 'window.unbind', 'en', r"(?:unlink|uncouple|unbind|unsync)\s+(?:the\s+)?(.+?)\s+of\s+(?:both|the|these|all|all\s+the|all\s+of\s+the)\s+(.+?)", ['param', 'target']),
    cmd('layout.rename.pl', 'layout.rename', 'pl', r"zmien\s+nazwe\s+(?:zapisanego\s+)?ukladu(?:\s+okien)?\s+(.+?)\s+na\s+(.+?)", ['name', 'label']),
    cmd('layout.rename.en', 'layout.rename', 'en', r"rename\s+(?:the\s+)?(?:saved\s+)?(?:window\s+)?layout\s+(.+?)\s+(?:to|as)\s+(.+?)", ['name', 'label']),
    cmd('layout.delete.pl', 'layout.delete', 'pl', r"(?:usun|skasuj|wyrzuc)\s+(?:zapisany\s+)?uklad(?:\s+okien)?\s+(.+?)", ['name']),
    cmd('layout.delete.en', 'layout.delete', 'en', r"(?:delete|remove|forget)\s+(?:the\s+)?(?:saved\s+)?(?:window\s+)?layout\s+(.+?)", ['name']),
]
# The windows' unbind regex must win over the float one for „odłącz przybliżenie obu grafów”: commands are tried in order,
# so the coupling sentences go first.
ORDER = ['window.bind', 'window.unbind']
COMMANDS.sort(key=lambda c: 0 if c['action'] in ORDER else 1)

DIRECT = ['window.maximize', 'window.restore', 'window.minimize', 'window.dock', 'window.float', 'window.tab', 'window.move', 'window.resize', 'window.bind', 'window.unbind', 'layout.rename']

COVERAGE = [
    {
        'id': 'window_arrange', 'title': {'en': 'Dock, float, maximize, minimize, move and resize windows', 'pl': 'Dokuj, odpinaj, maksymalizuj, zwijaj, przesuwaj i zmieniaj rozmiar okien'},
        'via': 'client', 'action': 'window.dock',
        'also': ['window.maximize', 'window.restore', 'window.minimize', 'window.float', 'window.tab', 'window.move', 'window.resize'],
        'surfaces': {},
        'examples': {'pl': 'zadokuj graf po lewej', 'en': 'dock the graph on the left'},
        'examples_more': {
            'pl': ['zmaksymalizuj ustawienia', 'zmaksymalizuj', 'przywróć okno graf', 'zminimalizuj graf', 'odepnij graf', 'pokaż ustawienia w pływającym oknie', 'przypnij graf do prawej', 'dodaj graf jako zakładkę', 'przesuń graf w prawo o 100', 'zmień rozmiar okna graf na 600x400', 'powiększ okno graf'],
            'en': ['maximize the settings', 'restore the graph window', 'minimize the graph', 'float the graph', 'show the settings in a floating window', 'pin the graph to the right', 'put the graph in a tab', 'move the graph right by 100', 'resize the graph to 600x400', 'enlarge the graph window'],
        },
        'note': {'en': 'W7 of OKNA_SYSTEM: the page (or the Android shell) declares arranges_windows; the sentence runs at once (intent_routing execution.direct) as a client step only the asking client carries out, one journal entry each (layout.undo takes it back). The words of sides and directions are data of the page (layouts.json window_sides).',
                 'pl': 'W7 z OKNA_SYSTEM: strona (albo powłoka Androida) deklaruje arranges_windows; zdanie rusza od razu (intent_routing execution.direct) jako krok klienta, który wykonuje tylko klient pytający, każdy jednym wpisem dziennika (cofnij układ go zdejmuje). Słowa stron i kierunków to dane strony (layouts.json window_sides).'},
    },
    {
        'id': 'window_coupling', 'title': {'en': 'Bind and unbind parameters of windows (zoom, pan ...)', 'pl': 'Wiąż i rozwiązuj parametry okien (przybliżenie, przesunięcie ...)'},
        'via': 'client', 'action': 'window.bind', 'also': ['window.unbind'], 'surfaces': {},
        'examples': {'pl': 'powiąż przybliżenie obu grafów', 'en': 'link the zoom of both graphs'},
        'examples_more': {'pl': ['rozwiąż przybliżenie obu grafów', 'połącz przesunięcie tych grafów'], 'en': ['unlink the zoom of both graphs', 'couple the pan of the graphs']},
        'note': {'en': 'W7 of OKNA_SYSTEM: a shared channel named after the parameter for every open window of the kind (the menu ⋯ of a window offers the linked forms with a dependency). The parameter words are data of the page (coupling.json params).',
                 'pl': 'W7 z OKNA_SYSTEM: wspólny kanał nazwany jak parametr dla każdego otwartego okna danego rodzaju (menu ⋯ okna daje formy powiązane z zależnością). Słowa parametrów to dane strony (coupling.json params).'},
    },
]
LAYOUT_MORE = {'pl': ['zmień nazwę układu praca na dom', 'usuń układ praca'], 'en': ['rename the layout work to home', 'delete the layout work']}


def load(name):
    path = RT / name
    text = path.read_text(encoding='utf-8')
    return path, json.loads(text), text.endswith('\n')


def save(path, obj, newline):
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + ('\n' if newline else ''), encoding='utf-8')


def main():
    path, a, nl = load('actions.pack')
    acts = a['defaults']['actions']
    tpl = acts['window.open']
    for aid, (title, desc, why, label, ph_en, ph_pl, params, irrev) in ACTIONS.items():
        e = json.loads(json.dumps(tpl))
        e.pop('result_text', None)
        e.update({'id': aid, 'planned_reason': REASON, 'title': title, 'description': desc, 'why': {'pl': why}, 'labels': {'pl': label},
                  'phrases': {'en': ph_en, 'pl': ph_pl}, 'params': params})
        e['effects'] = dict(e['effects'], irreversible=irrev)
        e['client'] = {'capability': 'arranges_windows', 'prompt': {'en': 'Arrange the windows on this device.', 'pl': 'Ułóż okna na tym urządzeniu.'}, 'expects': []}
        acts[aid] = e
    save(path, a, nl)

    path, ir, nl = load('intent_routing.pack')
    d = ir['defaults']
    cmds = [c for c in d['heuristic']['commands'] if c['id'] not in {c2['id'] for c2 in COMMANDS}]
    at = next(i for i, c in enumerate(cmds) if c['id'] == 'layout.reset.en') + 1
    d['heuristic']['commands'] = cmds[:at] + COMMANDS + cmds[at:]
    direct = d['execution']['direct']
    for x in DIRECT:
        if x not in direct: direct.append(x)
    save(path, ir, nl)

    path, cov, nl = load('chat_coverage.pack')
    caps = cov['defaults']['capabilities']
    caps[:] = [c for c in caps if c['id'] not in {x['id'] for x in COVERAGE}]
    at = next(i for i, c in enumerate(caps) if c['id'] == 'window_layouts') + 1
    caps[at:at] = COVERAGE
    wl = caps[at - 1]
    for x in ('layout.rename', 'layout.delete'):
        if x not in wl['also']: wl['also'].append(x)
    for lang in ('pl', 'en'):
        for s in LAYOUT_MORE[lang]:
            if s not in wl['examples_more'][lang]: wl['examples_more'][lang].append(s)
    save(path, cov, nl)
    print('ok', len(ACTIONS), 'actions,', len(COMMANDS), 'commands')


if __name__ == '__main__':
    sys.exit(main())
