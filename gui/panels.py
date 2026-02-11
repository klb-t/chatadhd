
from kivy.uix.popup import Popup
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label

class LogViewer(Popup):
    def __init__(self, text='', **kw):
        content = BoxLayout(orientation='vertical')
        content.add_widget(Label(text=text))
        super().__init__(title='Log', content=content, size_hint=(0.9,0.9))
