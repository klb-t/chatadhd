from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.behaviors import ButtonBehavior
from kivy.metrics import dp
from kivy.graphics import Color, RoundedRectangle
from kivy.properties import BooleanProperty, NumericProperty
from kivy.animation import Animation
from kivy.clock import Clock
from .colors import C

class RBtn(ButtonBehavior, Label):
    def __init__(self, bg=None, **kw):
        super().__init__(**kw)
        self.bg = bg or C['accent']
        self.bind(pos=self._draw, size=self._draw)
        Clock.schedule_once(lambda dt: self._draw())
    
    def _draw(self, *a):
        self.canvas.before.clear()
        with self.canvas.before:
            Color(*self.bg)
            RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(6)])

class Card(BoxLayout):
    def __init__(self, bg=None, **kw):
        super().__init__(**kw)
        self.bg = bg or C['card']
        self.padding = dp(8)
        self.bind(pos=self._draw, size=self._draw)
        Clock.schedule_once(lambda dt: self._draw())
    
    def _draw(self, *a):
        self.canvas.before.clear()
        with self.canvas.before:
            Color(*self.bg)
            RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(8)])

class Panel(BoxLayout):
    is_open = BooleanProperty(False)
    panel_width = NumericProperty(dp(280))
    
    def __init__(self, **kw):
        super().__init__(orientation='vertical', size_hint_x=None, width=0, **kw)
        self.padding = dp(8)
        self.spacing = dp(6)
    
    def toggle(self):
        self.is_open = not self.is_open
        Animation(width=self.panel_width if self.is_open else 0, d=0.2).start(self)
    
    def open(self):
        if not self.is_open: self.toggle()
    
    def close(self):
        if self.is_open: self.toggle()
