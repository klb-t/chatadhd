"""X-Port Commander — the two-pane face of the engine (Norton/Total Commander in a terminal).

``state.CommanderState`` holds everything the screen shows and turns key names into calls of the one
dispatcher (``api.Session``); it never imports curses, so a list of keys drives it headless
(``driver.py``) in tests and scripts.  ``view_curses.py`` only draws ``CommanderState.frame()`` and
translates terminal keys to key names; it is imported lazily, so a Python without curses still has
the CLI and the API.  Keys, actions, columns, colours and texts are data (``data/commander/*.json``,
``data/i18n``).
"""
