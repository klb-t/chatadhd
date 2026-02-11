
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.behaviors import ButtonBehavior
from kivy.metrics import dp
from kivy.graphics import Color, RoundedRectangle

class RBtn(ButtonBehavior, Label):
    def __init__(self, bg=(0.3,0.3,0.3,1), **kw):
        super().__init__(**kw)
        self.bg = bg
        self.bind(pos=self._draw, size=self._draw)
    def _draw(self, *a):
        self.canvas.before.clear()
        with self.canvas.before:
            Color(*self.bg)
            RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(6)])

class Card(BoxLayout):
    def __init__(self, **kw):
        super().__init__(**kw)
