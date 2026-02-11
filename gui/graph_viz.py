"""
ChatADHD v0.5.0 - Graph Explorer
Force-directed visualization with BIGGER nodes and READABLE labels
"""
from kivy.uix.widget import Widget
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.popup import Popup
from kivy.uix.label import Label
from kivy.uix.slider import Slider
from kivy.uix.scrollview import ScrollView
from kivy.graphics import Color, Line, Ellipse, Rectangle, RoundedRectangle
from kivy.clock import Clock
from kivy.core.window import Window
from kivy.vector import Vector
from kivy.metrics import dp, sp
from random import random
import math


class GraphNode:
    """Node in the graph with physics properties."""
    def __init__(self, uid, label, n_type, x, y, data=None):
        self.id = uid
        self.label = label[:25]  # Longer labels
        self.full_label = label
        self.type = n_type
        self.pos = Vector(x, y)
        self.velocity = Vector(0, 0)
        self.data = data or {}
        self.pinned = False
        
        # Colors by type - more vibrant
        colors = {
            'user': (0.2, 0.75, 0.5, 1),      # Green
            'assistant': (0.4, 0.5, 0.95, 1),  # Blue
            'chat': (0.5, 0.6, 0.9, 1),
            'memory': (0.95, 0.6, 0.2, 1),     # Orange
            'file': (0.3, 0.85, 0.5, 1),       # Bright green
            'folder': (0.9, 0.8, 0.2, 1),      # Yellow
        }
        self.color = colors.get(n_type, (0.6, 0.6, 0.6, 1))
        
        # Size based on type and weight
        base_sizes = {'user': 35, 'assistant': 40, 'memory': 30, 'file': 25, 'folder': 35}
        self.base_size = base_sizes.get(n_type, 30)
        self.weight = data.get('weight', 1.0) if data else 1.0
        self.size = self.base_size + self.weight * 10


class GraphWidget(Widget):
    """Force-directed graph with big readable nodes."""
    
    def __init__(self, engine, memory, **kwargs):
        super().__init__(**kwargs)
        self.engine = engine
        self.memory = memory
        self.nodes = {}
        self.edges = []
        
        self.zoom = 1.0
        self.offset = Vector(0, 0)
        self.selected_node = None
        self.dragged_node = None
        
        # Physics - more spread out
        self.repulsion = 25000
        self.spring_length = 180
        self.spring_k = 0.03
        self.friction = 0.85
        self.gravity = 0.005
        
        self.running = False
        self.bind(size=self._on_resize, pos=self._on_resize)
    
    def start(self):
        if not self.running:
            self.running = True
            Clock.schedule_interval(self._update_physics, 1.0 / 25.0)
    
    def stop(self):
        self.running = False
        Clock.unschedule(self._update_physics)
    
    def load_data(self):
        self.nodes.clear()
        self.edges.clear()
        
        cx, cy = self.width / 2, self.height / 2
        if cx == 0: cx = 200
        if cy == 0: cy = 300
        
        # Load conversation
        if self.engine.conv and self.engine.db:
            msgs = self.engine.db.get_msgs(self.engine.conv['id'], include_all=True)
            for i, m in enumerate(msgs):
                angle = (i / max(len(msgs), 1)) * 2 * math.pi
                r = 200 + random() * 100
                x = cx + r * math.cos(angle)
                y = cy + r * math.sin(angle)
                
                n_type = 'user' if m['role'] == 'user' else 'assistant'
                self.nodes[m['id']] = GraphNode(m['id'], m['text'][:30], n_type, x, y, m)
                
                if m.get('parent_id') and m['parent_id'] in self.nodes:
                    self.edges.append((m['parent_id'], m['id'], 1.0, 'reply'))
        
        # Load memory
        if self.memory:
            mem_data = self.memory.get_graph_data()
            for n in mem_data['nodes']:
                if n['id'] not in self.nodes:
                    x = cx + (random() - 0.5) * 400
                    y = cy + (random() - 0.5) * 400
                    ntype = n.get('node_type', 'memory')
                    self.nodes[n['id']] = GraphNode(n['id'], n['label'], ntype, x, y, n)
            
            for e in mem_data['edges']:
                if e['src'] in self.nodes and e['dst'] in self.nodes:
                    self.edges.append((e['src'], e['dst'], e.get('weight', 1.0), 'child'))
        
        self.redraw()
    
    def _on_resize(self, *args):
        self.redraw()
    
    def _update_physics(self, dt):
        if not self.nodes:
            return
        
        ids = list(self.nodes.keys())
        center = Vector(self.width / 2 if self.width else 200, 
                       self.height / 2 if self.height else 300)
        
        # Repulsion
        for i, id1 in enumerate(ids):
            n1 = self.nodes[id1]
            if n1.pinned:
                continue
            
            for id2 in ids[i+1:]:
                n2 = self.nodes[id2]
                dist_vec = n1.pos - n2.pos
                dist = max(dist_vec.length(), 1)
                
                force_mag = self.repulsion / (dist * dist)
                force = dist_vec.normalize() * min(force_mag, 50)
                
                if not n1.pinned:
                    n1.velocity += force
                if not n2.pinned:
                    n2.velocity -= force
        
        # Springs
        for src_id, dst_id, weight, _ in self.edges:
            if src_id not in self.nodes or dst_id not in self.nodes:
                continue
            
            n1, n2 = self.nodes[src_id], self.nodes[dst_id]
            dist_vec = n2.pos - n1.pos
            dist = max(dist_vec.length(), 1)
            
            target = self.spring_length / max(weight, 0.1)
            force_mag = (dist - target) * self.spring_k
            force = dist_vec.normalize() * force_mag
            
            if not n1.pinned:
                n1.velocity += force
            if not n2.pinned:
                n2.velocity -= force
        
        # Apply
        for n in self.nodes.values():
            if n.pinned or n == self.dragged_node:
                continue
            
            n.velocity += (center - n.pos) * self.gravity
            n.pos += n.velocity * dt * 50
            n.velocity *= self.friction
            
            margin = 80
            n.pos.x = max(margin, min(self.width - margin if self.width else 400, n.pos.x))
            n.pos.y = max(margin, min(self.height - margin if self.height else 600, n.pos.y))
        
        self.redraw()
    
    def redraw(self, *args):
        self.canvas.clear()
        
        with self.canvas:
            # Background
            Color(0.06, 0.06, 0.08, 1)
            Rectangle(pos=self.pos, size=self.size)
            
            # Edges
            for src_id, dst_id, weight, link_type in self.edges:
                if src_id not in self.nodes or dst_id not in self.nodes:
                    continue
                
                n1, n2 = self.nodes[src_id], self.nodes[dst_id]
                p1 = self._transform(n1.pos)
                p2 = self._transform(n2.pos)
                
                if link_type == 'reply':
                    Color(0.4, 0.5, 0.7, 0.7)
                elif link_type == 'child':
                    Color(0.7, 0.5, 0.3, 0.7)
                else:
                    Color(0.5, 0.5, 0.5, 0.5)
                
                Line(points=[p1.x, p1.y, p2.x, p2.y], width=1.5 + weight)
            
            # Nodes
            for n in self.nodes.values():
                pos = self._transform(n.pos)
                size = n.size * self.zoom
                
                # Selection glow
                if n == self.selected_node:
                    Color(1, 1, 0.3, 0.8)
                    Line(circle=(pos.x, pos.y, size/2 + 8), width=3)
                
                # Node circle
                Color(*n.color)
                Ellipse(pos=(pos.x - size/2, pos.y - size/2), size=(size, size))
                
                # Label background
                if self.zoom > 0.4:
                    lbl_text = n.label if self.zoom > 0.7 else n.label[:12]
                    font_sz = max(10, int(12 * self.zoom))
                    
                    # Create label to get texture
                    lbl = Label(text=lbl_text, font_size=font_sz, bold=True)
                    lbl.texture_update()
                    
                    if lbl.texture:
                        tw, th = lbl.texture.size
                        lx = pos.x - tw/2
                        ly = pos.y - size/2 - th - 4
                        
                        # Background box
                        Color(0, 0, 0, 0.7)
                        Rectangle(pos=(lx - 3, ly - 2), size=(tw + 6, th + 4))
                        
                        # Text
                        Color(1, 1, 1, 1)
                        Rectangle(texture=lbl.texture, pos=(lx, ly), size=(tw, th))
    
    def _transform(self, pos):
        center = Vector(self.width/2 if self.width else 200, self.height/2 if self.height else 300)
        return (pos - center) * self.zoom + self.offset + center
    
    def _inverse_transform(self, pos):
        center = Vector(self.width/2 if self.width else 200, self.height/2 if self.height else 300)
        return (Vector(*pos) - self.offset - center) / self.zoom + center
    
    def on_touch_down(self, touch):
        if not self.collide_point(*touch.pos):
            return False
        
        graph_pos = self._inverse_transform(touch.pos)
        
        for n in self.nodes.values():
            if graph_pos.distance(n.pos) < n.size + 10:
                self.selected_node = n
                self.dragged_node = n
                touch.grab(self)
                self.redraw()
                return True
        
        self.selected_node = None
        touch.grab(self)
        return True
    
    def on_touch_move(self, touch):
        if touch.grab_current != self:
            return False
        
        if self.dragged_node:
            self.dragged_node.pos = self._inverse_transform(touch.pos)
        else:
            self.offset += Vector(touch.dx, touch.dy)
        
        self.redraw()
        return True
    
    def on_touch_up(self, touch):
        if touch.grab_current == self:
            touch.ungrab(self)
            
            if self.selected_node and self.dragged_node:
                if hasattr(touch, 'time_end') and hasattr(touch, 'time_start'):
                    if touch.time_end - touch.time_start < 0.3:
                        self._open_editor(self.selected_node)
            
            self.dragged_node = None
        return True
    
    def _open_editor(self, node):
        from gui.panels import NodeEditorPopup
        NodeEditorPopup(node, self.engine, self.memory, on_update=self.load_data).open()


class GraphExplorerPanel(BoxLayout):
    """Slide-out panel with graph explorer."""
    
    def __init__(self, engine, memory, **kwargs):
        super().__init__(orientation='vertical', **kwargs)
        self.engine = engine
        self.memory = memory
        self.size_hint_x = None
        self.width = 0
        self._visible = False
        self.panel_width = dp(340)
        
        from gui.panels import C, RBtn
        
        # Background
        with self.canvas.before:
            Color(*C['bg'])
            self.bg = Rectangle(pos=self.pos, size=self.size)
        self.bind(pos=lambda *a: setattr(self.bg, 'pos', self.pos),
                  size=lambda *a: setattr(self.bg, 'size', self.size))
        
        # Header
        header = BoxLayout(size_hint_y=None, height=dp(40), spacing=dp(2), padding=dp(4))
        header.add_widget(Label(text="Graph Explorer", font_size=sp(13), color=C['text'], bold=True))
        header.add_widget(RBtn(text="X", size_hint_x=None, width=dp(40), bg=C['err'],
                              on_press=lambda *a: self.close()))
        self.add_widget(header)
        
        # Controls
        ctrl = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(3), padding=dp(4))
        ctrl.add_widget(RBtn(text="Refresh", bg=C['accent'], font_size=sp(10), 
                            on_press=lambda *a: self.refresh()))
        ctrl.add_widget(RBtn(text="Center", bg=C['card'], font_size=sp(10), 
                            on_press=self._reset))
        ctrl.add_widget(RBtn(text="+", bg=C['ok'], font_size=sp(14), size_hint_x=0.2,
                            on_press=self._zoom_in))
        ctrl.add_widget(RBtn(text="-", bg=C['card'], font_size=sp(14), size_hint_x=0.2,
                            on_press=self._zoom_out))
        self.add_widget(ctrl)
        
        # Graph
        self.graph = GraphWidget(engine, memory)
        self.add_widget(self.graph)
        
        # Legend
        legend = BoxLayout(size_hint_y=None, height=dp(28), spacing=dp(6), padding=dp(4))
        for name, color in [("You", (0.2, 0.75, 0.5)), ("AI", (0.4, 0.5, 0.95)), 
                           ("Memory", (0.95, 0.6, 0.2))]:
            row = BoxLayout(size_hint_x=None, width=dp(55))
            row.add_widget(Label(text=f"● {name}", font_size=sp(9), color=(*color, 1)))
            legend.add_widget(row)
        self.add_widget(legend)
    
    def toggle(self):
        self._visible = not self._visible
        self.width = self.panel_width if self._visible else 0
    
    def open(self):
        self._visible = True
        self.width = self.panel_width
    
    def close(self):
        self._visible = False
        self.width = 0
        self.graph.stop()
    
    def refresh(self):
        self.graph.load_data()
        self.graph.start()
    
    def _reset(self, *args):
        self.graph.zoom = 1.0
        self.graph.offset = Vector(0, 0)
        self.graph.redraw()
    
    def _zoom_in(self, *args):
        self.graph.zoom = min(3.0, self.graph.zoom * 1.25)
        self.graph.redraw()
    
    def _zoom_out(self, *args):
        self.graph.zoom = max(0.3, self.graph.zoom / 1.25)
        self.graph.redraw()
