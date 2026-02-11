
import logging, sys
logging.basicConfig(level=logging.INFO)
log = logging.getLogger('main')

try:
    import kivy
    HAVE_KIVY = True
except Exception:
    HAVE_KIVY = False

if HAVE_KIVY:
    try:
        from kivy.app import App
        from kivy.uix.label import Label
        class StubApp(App):
            def build(self):
                return Label(text='ChatADHD modular prototype - GUI placeholder (Kivy installed)')
        StubApp().run()
    except Exception as e:
        log.error('Kivy present but GUI failed to load: %s', e)
        print('GUI failed to initialize. Try running cli.py')
        sys.exit(1)
else:
    print('Kivy not installed. Run `python3 cli.py` to start CLI demo.')
