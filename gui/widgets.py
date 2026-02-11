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
    """Przycisk z zaokrąglonym tłem"""
    def __init__(self, bg=None, **kw):
        kw.setdefault('halign', 'center')
        kw.setdefault('valign', 'middle')
        super().__init__(**kw)
        self.bg = bg or C['accent']
        self.bind(pos=self._draw, size=self._draw, text=self._update_text_size)
        Clock.schedule_once(lambda dt: self._draw())
    
    def _update_text_size(self, *a):
        self.text_size = (self.width - dp(8), None)
    
    def _draw(self, *a):
        self.canvas.before.clear()
        with self.canvas.before:
            Color(*self.bg)
            RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(6)])
        self._update_text_size()

class Card(BoxLayout):
    """Karta z tłem"""
    def __init__(self, bg=None, radius=10, **kw):
        super().__init__(**kw)
        self.bg = bg or C['card']
        self.radius = radius
        self.padding = dp(8)
        self.bind(pos=self._draw, size=self._draw)
        Clock.schedule_once(lambda dt: self._draw())
    
    def _draw(self, *a):
        self.canvas.before.clear()
        with self.canvas.before:
            Color(*self.bg)
            RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(self.radius)])

class Panel(BoxLayout):
    """Wysuwany panel boczny"""
    is_open = BooleanProperty(False)
    panel_width = NumericProperty(dp(280))
    
    def __init__(self, **kw):
        super().__init__(orientation='vertical', size_hint_x=None, width=0, **kw)
        self.padding = dp(8)
        self.spacing = dp(6)
        with self.canvas.before:
            Color(*C['card'])
            self._bg = RoundedRectangle(pos=self.pos, size=self.size, radius=[0])
        self.bind(pos=self._update_bg, size=self._update_bg)
    
    def _update_bg(self, *a):
        self._bg.pos = self.pos
        self._bg.size = self.size
    
    def toggle(self):
        self.is_open = not self.is_open
        Animation(width=self.panel_width if self.is_open else 0, d=0.2).start(self)
    
    def open(self):
        if not self.is_open: self.toggle()
    
    def close(self):
        if self.is_open: self.toggle()

class IconBtn(RBtn):
    """Mały przycisk z ikoną"""
    def __init__(self, icon_name: str, **kw):
        from .colors import icon
        kw.setdefault('text', icon(icon_name))
        kw.setdefault('size_hint_x', None)
        kw.setdefault('width', dp(44))
        kw.setdefault('bg', C['card'])
        super().__init__(**kw)
