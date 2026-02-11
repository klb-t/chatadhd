"""
Graph Explorer - Force-directed visualization of knowledge/conversation graph
Based on Gemini's suggestion for Non-Linear Context Editor
"""
from kivy.uix.widget import Widget
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.popup import Popup
from kivy.uix.label import Label
from kivy.uix.slider import Slider
from kivy.graphics import Color, Line, Ellipse, Rectangle
from kivy.clock import Clock
from kivy.vector import Vector
from kivy.metrics import dp, sp
from random import random
import math

from gui.panels import C, RBtn, DarkInput, Card, Panel, show_toast

class GraphNode:
    """Node in the graph with physics properties."""
    def __init__(self, uid, label, n_type, x, y, data=None):
        self.id = uid
        self.label = label[:15]
        self.type = n_type  # 'chat', 'memory', 'file'
        self.pos = Vector(x, y)
        self.velocity = Vector(0, 0)
        self.data = data or {}
        self.pinned = False
        
        # Colors by type
        colors = {
            'chat': (0.3, 0.6, 1, 1),
            'memory': (0.8, 0.5, 0.2, 1),
            'file': (0.2, 0.8, 0.4, 1),
            'user': (0.3, 0.7, 0.5, 1),
            'assistant': (0.5, 0.4, 0.8, 1),
        }
        self.color = colors.get(n_type, (0.5, 0.5, 0.5, 1))
        
        # Size based on weight
        self.weight = data.get('weight', 1.0) if data else 1.0
        self.size = 15 + self.weight * 10


class GraphWidget(Widget):
    """Force-directed graph visualization widget."""
    
    def __init__(self, engine, memory, **kwargs):
        super().__init__(**kwargs)
        self.engine = engine
        self.memory = memory
        self.nodes = {}  # id -> GraphNode
        self.edges = []  # (src_id, dst_id, weight, type)
        
        # View state
        self.zoom = 1.0
        self.offset = Vector(0, 0)
        self.selected_node = None
        self.dragged_node = None
        
        # Physics parameters
        self.repulsion = 15000
        self.spring_length = 100
        self.spring_k = 0.04
        self.friction = 0.88
        self.gravity = 0.008
        
        self.running = False
        self.bind(size=self._on_resize, pos=self._on_resize)
    
    def start(self):
        """Start physics simulation."""
        if not self.running:
            self.running = True
            Clock.schedule_interval(self._update_physics, 1.0 / 30.0)
    
    def stop(self):
        """Stop physics simulation."""
        self.running = False
        Clock.unschedule(self._update_physics)
    
    def load_data(self):
        """Load graph data from engine and memory."""
        self.nodes.clear()
        self.edges.clear()
        
        cx, cy = self.width / 2, self.height / 2
        
        # 1. Load conversation messages
        if self.engine.conv and self.engine.db:
            msgs = self.engine.db.get_msgs(self.engine.conv['id'], include_all=True)
            for i, m in enumerate(msgs):
                angle = (i / max(len(msgs), 1)) * 2 * math.pi
                r = 150 + random() * 50
                x = cx + r * math.cos(angle)
                y = cy + r * math.sin(angle)
                
                n_type = 'user' if m['role'] == 'user' else 'assistant'
                self.nodes[m['id']] = GraphNode(m['id'], m['text'][:15], n_type, x, y, m)
                
                # Edge to parent
                if m.get('parent_id') and m['parent_id'] in self.nodes:
                    self.edges.append((m['parent_id'], m['id'], 1.0, 'reply'))
        
        # 2. Load memory nodes
        if self.memory:
            mem_data = self.memory.get_graph_data()
            for n in mem_data['nodes']:
                if n['id'] not in self.nodes:
                    x = cx + (random() - 0.5) * 300
                    y = cy + (random() - 0.5) * 300
                    self.nodes[n['id']] = GraphNode(n['id'], n['label'], 'memory', x, y, n)
            
            for e in mem_data['edges']:
                if e['src'] in self.nodes and e['dst'] in self.nodes:
                    self.edges.append((e['src'], e['dst'], e.get('weight', 1.0), e.get('type', 'link')))
        
        # 3. Load links from DB
        if self.engine.db:
            for link in self.engine.db.get_links():
                if link['src'] in self.nodes and link['dst'] in self.nodes:
                    self.edges.append((link['src'], link['dst'], link['weight'], link['link_type']))
        
        self.redraw()
    
    def _on_resize(self, *args):
        self.redraw()
    
    def _update_physics(self, dt):
        """Force-directed layout algorithm."""
        if not self.nodes:
            return
        
        ids = list(self.nodes.keys())
        center = Vector(self.width / 2, self.height / 2)
        
        # 1. Repulsion (Coulomb's Law)
        for i, id1 in enumerate(ids):
            n1 = self.nodes[id1]
            if n1.pinned:
                continue
            
            for id2 in ids[i+1:]:
                n2 = self.nodes[id2]
                dist_vec = n1.pos - n2.pos
                dist = max(dist_vec.length(), 1)
                
                force_mag = self.repulsion / (dist * dist)
                force = dist_vec.normalize() * force_mag
                
                if not n1.pinned:
                    n1.velocity += force
                if not n2.pinned:
                    n2.velocity -= force
        
        # 2. Attraction (Springs)
        for src_id, dst_id, weight, _ in self.edges:
            if src_id not in self.nodes or dst_id not in self.nodes:
                continue
            
            n1, n2 = self.nodes[src_id], self.nodes[dst_id]
            dist_vec = n2.pos - n1.pos
            dist = max(dist_vec.length(), 1)
            
            target_dist = self.spring_length / max(weight, 0.1)
            force_mag = (dist - target_dist) * self.spring_k
            force = dist_vec.normalize() * force_mag
            
            if not n1.pinned:
                n1.velocity += force
            if not n2.pinned:
                n2.velocity -= force
        
        # 3. Gravity to center + apply velocity
        for n in self.nodes.values():
            if n.pinned or n == self.dragged_node:
                continue
            
            # Gravity
            n.velocity += (center - n.pos) * self.gravity
            
            # Apply velocity with friction
            n.pos += n.velocity * dt * 60
            n.velocity *= self.friction
            
            # Keep in bounds
            n.pos.x = max(50, min(self.width - 50, n.pos.x))
            n.pos.y = max(50, min(self.height - 50, n.pos.y))
        
        self.redraw()
    
    def redraw(self, *args):
        """Redraw the graph."""
        self.canvas.clear()
        
        with self.canvas:
            # Background
            Color(*C['bg'])
            Rectangle(pos=self.pos, size=self.size)
            
            # Draw edges
            for src_id, dst_id, weight, link_type in self.edges:
                if src_id not in self.nodes or dst_id not in self.nodes:
                    continue
                
                n1, n2 = self.nodes[src_id], self.nodes[dst_id]
                p1 = self._transform(n1.pos)
                p2 = self._transform(n2.pos)
                
                # Color by type
                if link_type == 'reply':
                    Color(0.5, 0.5, 0.6, 0.6)
                elif link_type == 'child':
                    Color(0.6, 0.4, 0.2, 0.6)
                else:
                    Color(0.4, 0.6, 0.4, 0.6)
                
                Line(points=[p1.x, p1.y, p2.x, p2.y], width=1 + weight * 0.5)
            
            # Draw nodes
            for n in self.nodes.values():
                pos = self._transform(n.pos)
                size = n.size * self.zoom
                
                # Selection highlight
                if n == self.selected_node:
                    Color(1, 1, 0, 0.8)
                    Line(circle=(pos.x, pos.y, size + 5), width=2)
                
                # Node
                Color(*n.color)
                Ellipse(pos=(pos.x - size/2, pos.y - size/2), size=(size, size))
                
                # Label (if zoomed in)
                if self.zoom > 0.5:
                    Color(*C['text'])
                    # Simple text rendering via Label texture
                    lbl = Label(text=n.label, font_size=9 * self.zoom)
                    lbl.texture_update()
                    if lbl.texture:
                        Rectangle(
                            texture=lbl.texture,
                            pos=(pos.x - lbl.texture.width/2, pos.y + size/2 + 2),
                            size=lbl.texture.size
                        )
    
    def _transform(self, pos):
        """Apply zoom and offset to position."""
        return (pos - Vector(self.width/2, self.height/2)) * self.zoom + self.offset + Vector(self.width/2, self.height/2)
    
    def _inverse_transform(self, pos):
        """Inverse transform from screen to graph coordinates."""
        return (Vector(*pos) - self.offset - Vector(self.width/2, self.height/2)) / self.zoom + Vector(self.width/2, self.height/2)
    
    def on_touch_down(self, touch):
        if not self.collide_point(*touch.pos):
            return False
        
        graph_pos = self._inverse_transform(touch.pos)
        
        # Check node collision
        for n in self.nodes.values():
            if graph_pos.distance(n.pos) < n.size + 5:
                self.selected_node = n
                self.dragged_node = n
                touch.grab(self)
                self.redraw()
                return True
        
        # Pan start
        self.selected_node = None
        touch.grab(self)
        return True
    
    def on_touch_move(self, touch):
        if touch.grab_current != self:
            return False
        
        if self.dragged_node:
            self.dragged_node.pos = self._inverse_transform(touch.pos)
        else:
            # Pan
            self.offset += Vector(touch.dx, touch.dy)
        
        self.redraw()
        return True
    
    def on_touch_up(self, touch):
        if touch.grab_current == self:
            touch.ungrab(self)
            
            # If node was clicked (not dragged much), open editor
            if self.selected_node and self.dragged_node:
                if hasattr(touch, 'time_end') and hasattr(touch, 'time_start'):
                    if touch.time_end - touch.time_start < 0.3:
                        self._open_node_editor(self.selected_node)
            
            self.dragged_node = None
        return True
    
    def _open_node_editor(self, node):
        """Open popup to edit node."""
        NodeEditorPopup(node, self.engine, self.memory, on_update=self.load_data).open()


class NodeEditorPopup(Popup):
    """Editor for graph nodes - edit content, weight, links."""
    
    def __init__(self, node, engine, memory, on_update=None, **kwargs):
        self.node = node
        self.engine = engine
        self.memory = memory
        self.on_update = on_update
        
        content = BoxLayout(orientation='vertical', padding=dp(8), spacing=dp(4))
        
        # Type and ID
        header = BoxLayout(size_hint_y=None, height=dp(24))
        header.add_widget(Label(text=f"[{node.type}] {node.id[:8]}", font_size=sp(9), color=C['dim']))
        content.add_widget(header)
        
        # Content
        content.add_widget(Label(text="Content:", color=C['text'], size_hint_y=None, height=dp(16), halign='left'))
        self.content_input = DarkInput(text=node.data.get('text', node.label), multiline=True, size_hint_y=0.35)
        content.add_widget(self.content_input)
        
        # Weight slider
        weight_row = BoxLayout(size_hint_y=None, height=dp(36))
        weight_row.add_widget(Label(text="Weight:", color=C['text'], size_hint_x=0.25))
        self.weight_slider = Slider(min=0.1, max=2.0, value=node.weight, size_hint_x=0.55)
        weight_row.add_widget(self.weight_slider)
        self.weight_label = Label(text=f"{node.weight:.1f}", color=C['text'], size_hint_x=0.2)
        self.weight_slider.bind(value=lambda s, v: setattr(self.weight_label, 'text', f"{v:.1f}"))
        weight_row.add_widget(self.weight_label)
        content.add_widget(weight_row)
        
        # Pin toggle
        pin_row = BoxLayout(size_hint_y=None, height=dp(32))
        pin_row.add_widget(Label(text="Pin position:", color=C['text'], size_hint_x=0.5))
        self.pin_btn = RBtn(text="PINNED" if node.pinned else "FREE", 
                           bg=C['warn'] if node.pinned else C['card'],
                           size_hint_x=0.5, on_press=self._toggle_pin)
        pin_row.add_widget(self.pin_btn)
        content.add_widget(pin_row)
        
        # Versions (if message)
        if node.type in ('user', 'assistant', 'chat') and node.data.get('version_group_id'):
            vg_id = node.data['version_group_id']
            versions = self.engine.db.get_versions(vg_id)
            if len(versions) > 1:
                content.add_widget(Label(text=f"Versions: {len(versions)}", color=C['dim'], 
                                        size_hint_y=None, height=dp(20), font_size=sp(9)))
                ver_row = BoxLayout(size_hint_y=None, height=dp(28), spacing=dp(2))
                for v in versions[-5:]:  # Last 5 versions
                    is_active = v['status'] == 'active'
                    vbtn = RBtn(text=f"v{v['version_num']}", font_size=sp(8),
                               bg=C['accent'] if is_active else C['card'])
                    vbtn.version_id = v['id']
                    vbtn.bind(on_press=self._restore_version)
                    ver_row.add_widget(vbtn)
                content.add_widget(ver_row)
        
        # Actions
        btns = BoxLayout(size_hint_y=None, height=dp(38), spacing=dp(4))
        
        if node.type in ('user', 'assistant', 'chat'):
            btns.add_widget(RBtn(text="Edit (new ver)", bg=C['warn'], font_size=sp(9), on_press=self._edit_version))
        
        btns.add_widget(RBtn(text="Save", bg=C['accent'], font_size=sp(9), on_press=self._save))
        btns.add_widget(RBtn(text="Close", bg=C['card'], font_size=sp(9), on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)
        
        super().__init__(title=f"Edit: {node.label}", content=content, size_hint=(0.92, 0.65), **kwargs)
    
    def _toggle_pin(self, *args):
        self.node.pinned = not self.node.pinned
        self.pin_btn.text = "PINNED" if self.node.pinned else "FREE"
        self.pin_btn.background_color = C['warn'] if self.node.pinned else C['card']
    
    def _save(self, *args):
        """Save weight and metadata (not content - that's versioned)."""
        new_weight = self.weight_slider.value
        self.node.weight = new_weight
        
        # Update in database
        if self.node.type in ('user', 'assistant', 'chat'):
            self.engine.db.update_msg(self.node.id, weight=new_weight)
        elif self.node.type == 'memory' and self.memory:
            self.memory.update_node(self.node.id, weight=new_weight)
        
        show_toast("Saved")
        self.dismiss()
        if self.on_update:
            self.on_update()
    
    def _edit_version(self, *args):
        """Create new version with edited content."""
        new_text = self.content_input.text.strip()
        if new_text and new_text != self.node.data.get('text', ''):
            new_id = self.engine.db.edit_msg(self.node.id, new_text)
            if new_id:
                show_toast("New version created")
                self.dismiss()
                if self.on_update:
                    self.on_update()
    
    def _restore_version(self, btn):
        """Restore specific version."""
        if self.engine.db.restore_version(btn.version_id):
            show_toast(f"Restored v{btn.text}")
            self.dismiss()
            if self.on_update:
                self.on_update()


class GraphExplorerPanel(Panel):
    """Panel containing the graph explorer."""
    
    def __init__(self, engine, memory, **kwargs):
        super().__init__(**kwargs)
        self.engine = engine
        self.memory = memory
        self.panel_width = dp(320)
        
        # Header
        header = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(2))
        header.add_widget(Label(text="Graph Explorer", font_size=sp(11), color=C['text'], bold=True))
        header.add_widget(RBtn(text="X", size_hint_x=None, width=dp(36), bg=C['card'],
                              on_press=lambda *a: self.close()))
        self.add_widget(header)
        
        # Controls
        ctrl = BoxLayout(size_hint_y=None, height=dp(32), spacing=dp(2))
        ctrl.add_widget(RBtn(text="Refresh", bg=C['accent'], font_size=sp(9), on_press=lambda *a: self.refresh()))
        ctrl.add_widget(RBtn(text="Reset", bg=C['card'], font_size=sp(9), on_press=self._reset_view))
        ctrl.add_widget(RBtn(text="+", bg=C['ok'], font_size=sp(12), size_hint_x=0.2, on_press=self._zoom_in))
        ctrl.add_widget(RBtn(text="-", bg=C['card'], font_size=sp(12), size_hint_x=0.2, on_press=self._zoom_out))
        self.add_widget(ctrl)
        
        # Graph widget
        self.graph = GraphWidget(engine, memory)
        self.add_widget(self.graph)
        
        # Legend
        legend = BoxLayout(size_hint_y=None, height=dp(24), spacing=dp(4))
        for name, color in [("User", (0.3, 0.7, 0.5)), ("AI", (0.5, 0.4, 0.8)), ("Mem", (0.8, 0.5, 0.2))]:
            lbl = Label(text=f"● {name}", font_size=sp(8), color=(*color, 1))
            legend.add_widget(lbl)
        self.add_widget(legend)
    
    def refresh(self):
        self.graph.load_data()
        self.graph.start()
    
    def close(self):
        self.graph.stop()
        super().close()
    
    def _reset_view(self, *args):
        self.graph.zoom = 1.0
        self.graph.offset = Vector(0, 0)
        self.graph.redraw()
    
    def _zoom_in(self, *args):
        self.graph.zoom = min(3.0, self.graph.zoom * 1.2)
        self.graph.redraw()
    
    def _zoom_out(self, *args):
        self.graph.zoom = max(0.3, self.graph.zoom / 1.2)
        self.graph.redraw()
