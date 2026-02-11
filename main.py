#!/usr/bin/env python3
"""
ChatADHD - Multi-Model AI Chat Client
=====================================
For minds that branch, jump, and never quite finish the previous thought.

External files: config.json, models.json, presets.json, data/client.db
Install: pip install kivy requests openai
"""

import os, sys, json, sqlite3, threading, re, uuid
from datetime import datetime, timedelta
from pathlib import Path
from typing import Optional, List, Dict, Any
from dataclasses import dataclass
from enum import Enum

APP_DIR = Path(__file__).parent.resolve()
CONFIG_FILE, MODELS_FILE, PRESETS_FILE = APP_DIR/"config.json", APP_DIR/"models.json", APP_DIR/"presets.json"
DATA_DIR, DB_FILE = APP_DIR/"data", APP_DIR/"data"/"client.db"
DATA_DIR.mkdir(exist_ok=True)

os.environ['KIVY_NO_CONSOLELOG'] = '1'
from kivy.app import App
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.scrollview import ScrollView
from kivy.uix.textinput import TextInput
from kivy.uix.button import Button
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.uix.spinner import Spinner
from kivy.uix.slider import Slider
from kivy.uix.checkbox import CheckBox
from kivy.uix.tabbedpanel import TabbedPanel, TabbedPanelItem
from kivy.clock import Clock
from kivy.core.window import Window
from kivy.metrics import dp
Window.softinput_mode = 'below_target'

class ConfigManager:
    DEFAULTS = {"api_key":"","base_url":"https://openrouter.ai/api/v1","default_model":"anthropic/claude-sonnet-4",
                "temperature":0.7,"max_tokens":4096,"auto_memory":True,"system_preset":"epistemic",
                "auto_update_models":True,"models_update_interval_hours":24}
    def __init__(self):
        self.data = {**self.DEFAULTS, **(json.load(open(CONFIG_FILE)) if CONFIG_FILE.exists() else {})}
    def save(self): json.dump(self.data, open(CONFIG_FILE,'w'), indent=2)
    def get(self, k, d=None): return self.data.get(k, d)
    def set(self, k, v): self.data[k]=v; self.save()

class ModelsManager:
    def __init__(self, cfg):
        self.cfg = cfg
        self.data = json.load(open(MODELS_FILE)) if MODELS_FILE.exists() else {"_metadata":{},"models":{},"favorites":[],"hidden":[]}
    def save(self): json.dump(self.data, open(MODELS_FILE,'w'), indent=2)
    def get_models(self): return {k:v for k,v in self.data.get("models",{}).items() if k not in self.data.get("hidden",[])}
    def get_model_name(self, mid): return self.data.get("models",{}).get(mid,{}).get("name", mid)
    def should_update(self):
        if not self.cfg.get("auto_update_models"): return False
        lu = self.data.get("_metadata",{}).get("last_updated")
        if not lu: return True
        try: return datetime.now() - datetime.fromisoformat(lu) > timedelta(hours=self.cfg.get("models_update_interval_hours",24))
        except: return True
    def update_from_api(self, key):
        if not key: return False
        try:
            import requests
            r = requests.get(f"{self.cfg.get('base_url')}/models", headers={"Authorization":f"Bearer {key}"}, timeout=10)
            r.raise_for_status()
            ex = self.data.get("models",{})
            for m in r.json().get("data",[]):
                mid = m.get("id","")
                if not mid: continue
                pr = m.get("pricing",{})
                ex[mid] = {"name":m.get("name",mid.split("/")[-1]),"provider":mid.split("/")[0].title() if "/" in mid else "?",
                          "context_length":m.get("context_length",4096),"pricing":{"prompt":round(float(pr.get("prompt",0))*1e6,2),"completion":round(float(pr.get("completion",0))*1e6,2)}}
            self.data["models"], self.data["_metadata"] = ex, {"last_updated":datetime.now().isoformat(),"count":len(ex)}
            self.save(); return True
        except: return False

class PresetsManager:
    def __init__(self):
        self.data = json.load(open(PRESETS_FILE)) if PRESETS_FILE.exists() else {"default":{"name":"Default","prompt":"You are helpful."}}
    def get_presets(self): return {k:v for k,v in self.data.items() if not k.startswith("_")}
    def get_preset(self, k): return self.data.get(k, {"name":k,"prompt":""})

class MessageRole(Enum): SYSTEM="system"; USER="user"; ASSISTANT="assistant"
class MessageStatus(Enum): ACTIVE="active"; EXCLUDED="excluded"; DELETED="deleted"
class MemoryType(Enum): FACT="fact"; GUIDELINE="guideline"; ERROR="error"

@dataclass
class Message:
    id:str; conversation_id:str; parent_id:Optional[str]; role:MessageRole; content:str
    model:Optional[str]; status:MessageStatus; created_at:datetime; metadata:Dict
    def to_api(self): return {"role":self.role.value,"content":self.content}

@dataclass
class Conversation:
    id:str; title:str; created_at:datetime; updated_at:datetime
    default_model:str; system_prompt:Optional[str]; system_preset:str

@dataclass
class Memory:
    id:str; type:MemoryType; content:str; context:Optional[str]; created_at:datetime; active:bool=True

@dataclass
class UserProfile:
    name:Optional[str]=None; description:Optional[str]=None; custom_instructions:Optional[str]=None

class Database:
    def __init__(self):
        self.conn = sqlite3.connect(str(DB_FILE), check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        c = self.conn.cursor()
        c.execute("CREATE TABLE IF NOT EXISTS conversations(id TEXT PRIMARY KEY,title TEXT,created_at TIMESTAMP,updated_at TIMESTAMP,default_model TEXT,system_prompt TEXT,system_preset TEXT)")
        c.execute("CREATE TABLE IF NOT EXISTS messages(id TEXT PRIMARY KEY,conversation_id TEXT,parent_id TEXT,role TEXT,content TEXT,model TEXT,status TEXT DEFAULT 'active',created_at TIMESTAMP,metadata TEXT)")
        c.execute("CREATE TABLE IF NOT EXISTS memories(id TEXT PRIMARY KEY,type TEXT,content TEXT,context TEXT,created_at TIMESTAMP,active INTEGER DEFAULT 1)")
        c.execute("CREATE TABLE IF NOT EXISTS user_profile(id INTEGER PRIMARY KEY CHECK(id=1),name TEXT,description TEXT,custom_instructions TEXT)")
        c.execute("INSERT OR IGNORE INTO user_profile(id)VALUES(1)")
        self.conn.commit()
    
    def get_profile(self):
        r = self.conn.execute("SELECT * FROM user_profile WHERE id=1").fetchone()
        return UserProfile(r['name'],r['description'],r['custom_instructions']) if r else UserProfile()
    def set_profile(self, p):
        self.conn.execute("UPDATE user_profile SET name=?,description=?,custom_instructions=? WHERE id=1",(p.name,p.description,p.custom_instructions))
        self.conn.commit()
    
    def create_conv(self, title, model, prompt=None, preset="default"):
        cid, now = str(uuid.uuid4()), datetime.now()
        self.conn.execute("INSERT INTO conversations VALUES(?,?,?,?,?,?,?)",(cid,title,now,now,model,prompt,preset))
        self.conn.commit()
        return Conversation(cid,title,now,now,model,prompt,preset)
    def get_conv(self, cid):
        r = self.conn.execute("SELECT * FROM conversations WHERE id=?",(cid,)).fetchone()
        return Conversation(r['id'],r['title'],datetime.fromisoformat(r['created_at']),datetime.fromisoformat(r['updated_at']),r['default_model'],r['system_prompt'],r['system_preset'] or 'default') if r else None
    def list_convs(self, lim=50):
        return [Conversation(r['id'],r['title'],datetime.fromisoformat(r['created_at']),datetime.fromisoformat(r['updated_at']),r['default_model'],r['system_prompt'],r['system_preset'] or 'default') for r in self.conn.execute("SELECT * FROM conversations ORDER BY updated_at DESC LIMIT ?",(lim,))]
    def update_conv(self, c):
        self.conn.execute("UPDATE conversations SET title=?,updated_at=?,default_model=?,system_prompt=?,system_preset=? WHERE id=?",(c.title,datetime.now(),c.default_model,c.system_prompt,c.system_preset,c.id))
        self.conn.commit()
    def delete_conv(self, cid):
        self.conn.execute("DELETE FROM messages WHERE conversation_id=?",(cid,))
        self.conn.execute("DELETE FROM conversations WHERE id=?",(cid,))
        self.conn.commit()
    
    def create_msg(self, cid, role, content, pid=None, model=None):
        mid, now = str(uuid.uuid4()), datetime.now()
        self.conn.execute("INSERT INTO messages VALUES(?,?,?,?,?,?,?,?,?)",(mid,cid,pid,role.value,content,model,'active',now,'{}'))
        self.conn.commit()
        return Message(mid,cid,pid,role,content,model,MessageStatus.ACTIVE,now,{})
    def get_msgs(self, cid, inc_excl=False):
        sf = "AND status!='deleted'" if inc_excl else "AND status='active'"
        return [Message(r['id'],r['conversation_id'],r['parent_id'],MessageRole(r['role']),r['content'],r['model'],MessageStatus(r['status']),datetime.fromisoformat(r['created_at']),json.loads(r['metadata'] or '{}')) for r in self.conn.execute(f"SELECT * FROM messages WHERE conversation_id=? {sf} ORDER BY created_at",(cid,))]
    def set_msg_status(self, mid, st):
        self.conn.execute("UPDATE messages SET status=? WHERE id=?",(st.value,mid)); self.conn.commit()
    
    def add_mem(self, t, content, ctx=None):
        mid, now = str(uuid.uuid4()), datetime.now()
        self.conn.execute("INSERT INTO memories VALUES(?,?,?,?,?,1)",(mid,t.value,content,ctx,now))
        self.conn.commit()
        return Memory(mid,t,content,ctx,now,True)
    def get_mems(self, t=None, active=True):
        q, p = "SELECT * FROM memories WHERE 1=1", []
        if t: q+=" AND type=?"; p.append(t.value)
        if active: q+=" AND active=1"
        return [Memory(r['id'],MemoryType(r['type']),r['content'],r['context'],datetime.fromisoformat(r['created_at']),bool(r['active'])) for r in self.conn.execute(q+" ORDER BY created_at DESC",p)]
    def del_mem(self, mid):
        self.conn.execute("UPDATE memories SET active=0 WHERE id=?",(mid,)); self.conn.commit()

class APIClient:
    def __init__(self, key, url): self.key, self.url = key, url
    def chat(self, msgs, model, temp=0.7, maxt=4096, stream_cb=None):
        try:
            from openai import OpenAI
            c = OpenAI(api_key=self.key, base_url=self.url)
            r = c.chat.completions.create(model=model, messages=msgs, temperature=temp, max_tokens=maxt, stream=stream_cb is not None)
            if stream_cb:
                full = ""
                for ch in r:
                    if ch.choices[0].delta.content: t=ch.choices[0].delta.content; full+=t; stream_cb(t)
                return full
            return r.choices[0].message.content
        except ImportError:
            import requests
            return requests.post(f"{self.url}/chat/completions",headers={"Authorization":f"Bearer {self.key}","Content-Type":"application/json"},json={"model":model,"messages":msgs,"temperature":temp,"max_tokens":maxt},timeout=120).json()['choices'][0]['message']['content']

class ChatEngine:
    def __init__(self, cfg, mdl, pre, db):
        self.cfg, self.models, self.presets, self.db = cfg, mdl, pre, db
        self.client = APIClient(cfg.get('api_key'), cfg.get('base_url')) if cfg.get('api_key') else None
        self.conv = None
        if self.client and self.models.should_update(): threading.Thread(target=lambda:self.models.update_from_api(cfg.get('api_key'))).start()
    
    def set_key(self, k):
        self.cfg.set('api_key',k); self.client = APIClient(k, self.cfg.get('base_url'))
        threading.Thread(target=lambda:self.models.update_from_api(k)).start()
    
    def new_conv(self, title=None, preset=None):
        preset = preset or self.cfg.get('system_preset','default')
        title = title or f"Chat {datetime.now().strftime('%Y-%m-%d %H:%M')}"
        self.conv = self.db.create_conv(title, self.cfg.get('default_model'), self.presets.get_preset(preset).get('prompt',''), preset)
        return self.conv
    
    def load_conv(self, cid): self.conv = self.db.get_conv(cid); return self.conv
    
    def _context(self):
        c, msgs, sp = self.conv, [], []
        if c.system_prompt: sp.append(c.system_prompt)
        pr = self.db.get_profile()
        if pr.name or pr.description:
            t = "\n## User\n"
            if pr.name: t+=f"Name: {pr.name}\n"
            if pr.description: t+=f"About: {pr.description}\n"
            if pr.custom_instructions: t+=f"Instructions: {pr.custom_instructions}\n"
            sp.append(t)
        facts = self.db.get_mems(MemoryType.FACT)
        if facts: sp.append("\n## Facts\n"+"".join(f"- {m.content}\n" for m in facts[:20]))
        gl, er = self.db.get_mems(MemoryType.GUIDELINE), self.db.get_mems(MemoryType.ERROR)
        if gl or er:
            t = "\n## Guidelines\n"+"".join(f"- {m.content}\n" for m in gl[:10])
            t += "".join(f"- ERROR: {m.content}"+(f" (Ex: {m.context})" if m.context else "")+"\n" for m in er[:10])
            sp.append(t)
        if sp: msgs.append({"role":"system","content":"\n".join(sp)})
        msgs.extend(m.to_api() for m in self.db.get_msgs(c.id))
        return msgs
    
    def send(self, txt, stream_cb=None):
        if not self.client: raise ValueError("No API key")
        if not self.conv: self.new_conv()
        c, ms = self.conv, self.db.get_msgs(self.conv.id)
        pid = ms[-1].id if ms else None
        um = self.db.create_msg(c.id, MessageRole.USER, txt, pid)
        resp = self.client.chat(self._context(), c.default_model, self.cfg.get('temperature',0.7), self.cfg.get('max_tokens',4096), stream_cb)
        am = self.db.create_msg(c.id, MessageRole.ASSISTANT, resp, um.id, c.default_model)
        self.db.update_conv(c)
        if self.cfg.get('auto_memory'): self._extract(txt)
        return am
    
    def _extract(self, txt):
        for pat, tpl in [(r"(?:jestem|I am|I'm)\s+(.+?)(?:\.|,|$)","User: {}"),(r"(?:pracuję|I work)\s+(?:w|at)\s+(.+?)(?:\.|,|$)","Works: {}"),(r"(?:mieszkam|I live)\s+(?:w|in)\s+(.+?)(?:\.|,|$)","Lives: {}")]:
            m = re.search(pat, txt, re.I)
            if m:
                f = tpl.format(m.group(1).strip())
                if not any(f.lower() in e.content.lower() for e in self.db.get_mems(MemoryType.FACT)): self.db.add_mem(MemoryType.FACT, f)

# === GUI ===
class MsgWidget(BoxLayout):
    def __init__(self, m, mdl, on_ex=None, on_in=None, **kw):
        super().__init__(orientation='vertical', size_hint_y=None, padding=dp(4), spacing=dp(2), **kw)
        role = "📝 You" if m.role==MessageRole.USER else f"🤖 {mdl.get_model_name(m.model) if m.model else 'AI'}"
        op = 0.5 if m.status==MessageStatus.EXCLUDED else 1.0
        hd = BoxLayout(size_hint_y=None, height=dp(26))
        hd.add_widget(Label(text=role, size_hint_x=0.75, halign='left', opacity=op))
        btn = Button(text="Hide" if m.status==MessageStatus.ACTIVE else "Show", size_hint_x=0.25, font_size=dp(10))
        btn.bind(on_press=lambda x: (on_ex(m.id) if m.status==MessageStatus.ACTIVE else on_in(m.id)) if (on_ex and on_in) else None)
        hd.add_widget(btn)
        self.add_widget(hd)
        ct = Label(text=m.content, size_hint_y=None, text_size=(Window.width-dp(25),None), halign='left', valign='top', opacity=op)
        ct.bind(texture_size=lambda *x: setattr(ct,'height',ct.texture_size[1]+dp(6)))
        self.add_widget(ct)
        self.bind(minimum_height=self.setter('height'))

class ChatView(BoxLayout):
    def __init__(self, eng, **kw):
        super().__init__(orientation='vertical', **kw)
        self.eng, self.sending = eng, False
        self._build()
    
    def _build(self):
        top = BoxLayout(size_hint_y=None, height=dp(46), spacing=dp(2))
        ms = self.eng.models.get_models()
        ns = [v.get('name',k) for k,v in ms.items()] or ['No models']
        self.mspin = Spinner(text=self.eng.models.get_model_name(self.eng.cfg.get('default_model')), values=ns, size_hint_x=0.55)
        self.mspin.bind(text=self._mchg)
        top.add_widget(self.mspin)
        for t,f in [("⚙️",self._set),("🔄",self._ref),("➕",self._new)]:
            b=Button(text=t,size_hint_x=0.15); b.bind(on_press=f); top.add_widget(b)
        self.add_widget(top)
        
        self.scr = ScrollView(size_hint_y=0.75)
        self.mlay = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(6), padding=dp(6))
        self.mlay.bind(minimum_height=self.mlay.setter('height'))
        self.scr.add_widget(self.mlay)
        self.add_widget(self.scr)
        
        ib = BoxLayout(size_hint_y=None, height=dp(85), spacing=dp(2))
        self.inp = TextInput(hint_text="Message...", multiline=True, size_hint_x=0.85)
        ib.add_widget(self.inp)
        sb = Button(text="Send", size_hint_x=0.15); sb.bind(on_press=self._send); ib.add_widget(sb)
        self.add_widget(ib)
        
        self.stat = Label(text="Ready", size_hint_y=None, height=dp(20))
        self.add_widget(self.stat)
    
    def _mchg(self, sp, txt):
        for mid, info in self.eng.models.get_models().items():
            if info.get('name')==txt:
                self.eng.cfg.set('default_model', mid)
                if self.eng.conv: self.eng.conv.default_model=mid; self.eng.db.update_conv(self.eng.conv)
                pr=info.get('pricing',{}); self.stat.text=f"${pr.get('prompt','?')}/{pr.get('completion','?')} /1M"
                break
    
    def _ref(self, *a):
        self.stat.text="Updating..."
        def up():
            ok=self.eng.models.update_from_api(self.eng.cfg.get('api_key'))
            Clock.schedule_once(lambda dt:self._refd(ok))
        threading.Thread(target=up).start()
    def _refd(self, ok):
        if ok: m=self.eng.models.get_models(); self.mspin.values=[v.get('name',k) for k,v in m.items()]; self.stat.text=f"{len(m)} models"
        else: self.stat.text="Failed"
    
    def _new(self, *a): self.eng.new_conv(); self.refresh(); self.stat.text="New chat"
    
    def _send(self, *a):
        if self.sending: return
        txt = self.inp.text.strip()
        if not txt: return
        if not self.eng.cfg.get('api_key'): self._keypop(); return
        self.sending=True; self.inp.text=""; self.stat.text="Sending..."
        threading.Thread(target=self._sendt, args=(txt,)).start()
    def _sendt(self, txt):
        try: self.eng.send(txt); Clock.schedule_once(lambda dt:self._sentok())
        except Exception as e: Clock.schedule_once(lambda dt:self._senterr(str(e)))
    def _sentok(self): self.sending=False; self.refresh(); self.stat.text="Ready"
    def _senterr(self, e): self.sending=False; self.stat.text=f"Error: {e[:35]}"
    
    def refresh(self):
        self.mlay.clear_widgets()
        if not self.eng.conv: return
        for m in self.eng.db.get_msgs(self.eng.conv.id, True):
            self.mlay.add_widget(MsgWidget(m, self.eng.models, self._ex, self._inc))
        Clock.schedule_once(lambda dt:setattr(self.scr,'scroll_y',0))
    def _ex(self, mid): self.eng.db.set_msg_status(mid, MessageStatus.EXCLUDED); self.refresh()
    def _inc(self, mid): self.eng.db.set_msg_status(mid, MessageStatus.ACTIVE); self.refresh()
    
    def _set(self, *a): SettingsPopup(self.eng).open()
    def _keypop(self):
        c=BoxLayout(orientation='vertical',padding=dp(8),spacing=dp(8))
        c.add_widget(Label(text="API Key:"))
        inp=TextInput(hint_text="sk-or-v1-...",multiline=False)
        c.add_widget(inp)
        def sv(*a):
            if inp.text.strip(): self.eng.set_key(inp.text.strip()); self.stat.text="Saved"; p.dismiss()
        c.add_widget(Button(text="Save",on_press=sv))
        p=Popup(title="API Key",content=c,size_hint=(0.9,0.32)); p.open()

class SettingsPopup(Popup):
    def __init__(self, eng, **kw):
        self.eng = eng
        tabs = TabbedPanel(do_default_tab=False)
        for nm, bd in [("General",self._gen),("System",self._sys),("Memory",self._mem),("Profile",self._prof)]:
            t=TabbedPanelItem(text=nm); t.add_widget(bd()); tabs.add_widget(t)
        super().__init__(title="Settings", content=tabs, size_hint=(0.95,0.9), **kw)
    
    def _gen(self):
        ly=BoxLayout(orientation='vertical',padding=dp(8),spacing=dp(6))
        ly.add_widget(Label(text="API Key:",size_hint_y=None,height=dp(22)))
        self.api=TextInput(text=self.eng.cfg.get('api_key',''),password=True,multiline=False,size_hint_y=None,height=dp(36))
        ly.add_widget(self.api)
        tb=BoxLayout(size_hint_y=None,height=dp(42))
        tb.add_widget(Label(text="Temp:"))
        self.tsl=Slider(min=0,max=2,value=self.eng.cfg.get('temperature',0.7))
        self.tlb=Label(text=f"{self.tsl.value:.2f}",size_hint_x=0.2)
        self.tsl.bind(value=lambda *x:setattr(self.tlb,'text',f"{self.tsl.value:.2f}"))
        tb.add_widget(self.tsl); tb.add_widget(self.tlb)
        ly.add_widget(tb)
        for txt,k,at in [("Auto memories","auto_memory","am"),("Auto update models","auto_update_models","au")]:
            bx=BoxLayout(size_hint_y=None,height=dp(32)); bx.add_widget(Label(text=txt))
            cb=CheckBox(active=self.eng.cfg.get(k,True)); setattr(self,at,cb); bx.add_widget(cb); ly.add_widget(bx)
        sv=Button(text="Save",size_hint_y=None,height=dp(42)); sv.bind(on_press=self._saveg); ly.add_widget(sv)
        ly.add_widget(BoxLayout()); return ly
    def _saveg(self,*a):
        if self.api.text.strip(): self.eng.set_key(self.api.text.strip())
        self.eng.cfg.set('temperature',self.tsl.value); self.eng.cfg.set('auto_memory',self.am.active); self.eng.cfg.set('auto_update_models',self.au.active)
        self.dismiss()
    
    def _sys(self):
        ly=BoxLayout(orientation='vertical',padding=dp(8),spacing=dp(6))
        pb=BoxLayout(size_hint_y=None,height=dp(42))
        pb.add_widget(Label(text="Preset:",size_hint_x=0.3))
        ps=self.eng.presets.get_presets(); ns=[p.get('name',k) for k,p in ps.items()]
        cur=self.eng.presets.get_preset(self.eng.cfg.get('system_preset','default')).get('name','Default')
        self.psp=Spinner(text=cur,values=ns); self.psp.bind(text=self._pchg); pb.add_widget(self.psp)
        ly.add_widget(pb)
        ly.add_widget(Label(text="System Prompt:",size_hint_y=None,height=dp(22)))
        pr = self.eng.conv.system_prompt if self.eng.conv else self.eng.presets.get_preset(self.eng.cfg.get('system_preset','default')).get('prompt','')
        self.pinp=TextInput(text=pr or '',multiline=True,size_hint_y=0.6); ly.add_widget(self.pinp)
        sv=Button(text="Save",size_hint_y=None,height=dp(42)); sv.bind(on_press=self._saves); ly.add_widget(sv)
        return ly
    def _pchg(self,sp,txt):
        for k,p in self.eng.presets.get_presets().items():
            if p.get('name')==txt: self.pinp.text=p.get('prompt',''); self.eng.cfg.set('system_preset',k); break
    def _saves(self,*a):
        if self.eng.conv: self.eng.conv.system_prompt=self.pinp.text; self.eng.db.update_conv(self.eng.conv)
    
    def _mem(self):
        ly=BoxLayout(orientation='vertical',padding=dp(8),spacing=dp(6))
        ly.add_widget(Label(text="Add:",size_hint_y=None,height=dp(22)))
        ab=BoxLayout(size_hint_y=None,height=dp(36))
        self.minp=TextInput(hint_text="Content",multiline=False,size_hint_x=0.65); ab.add_widget(self.minp)
        self.mtyp=Spinner(text="Fact",values=["Fact","Guideline","Error"],size_hint_x=0.35); ab.add_widget(self.mtyp)
        ly.add_widget(ab)
        ad=Button(text="Add",size_hint_y=None,height=dp(36)); ad.bind(on_press=self._addm); ly.add_widget(ad)
        ly.add_widget(Label(text="Memories:",size_hint_y=None,height=dp(22)))
        sc=ScrollView(size_hint_y=0.5)
        self.mlst=BoxLayout(orientation='vertical',size_hint_y=None,spacing=dp(3))
        self.mlst.bind(minimum_height=self.mlst.setter('height'))
        sc.add_widget(self.mlst); ly.add_widget(sc)
        self._refm(); return ly
    def _addm(self,*a):
        txt=self.minp.text.strip()
        if not txt: return
        tm={"Fact":MemoryType.FACT,"Guideline":MemoryType.GUIDELINE,"Error":MemoryType.ERROR}
        self.eng.db.add_mem(tm.get(self.mtyp.text,MemoryType.FACT),txt); self.minp.text=""; self._refm()
    def _refm(self):
        self.mlst.clear_widgets()
        for m in self.eng.db.get_mems()[:30]:
            em={"fact":"📌","guideline":"📋","error":"⚠️"}.get(m.type.value,"•")
            it=BoxLayout(size_hint_y=None,height=dp(30))
            it.add_widget(Label(text=f"{em} {m.content[:32]}...",size_hint_x=0.85,halign='left'))
            db=Button(text="🗑️",size_hint_x=0.15); db.bind(on_press=lambda x,mid=m.id:self._delm(mid)); it.add_widget(db)
            self.mlst.add_widget(it)
    def _delm(self,mid): self.eng.db.del_mem(mid); self._refm()
    
    def _prof(self):
        ly=BoxLayout(orientation='vertical',padding=dp(8),spacing=dp(6))
        pr=self.eng.db.get_profile()
        ly.add_widget(Label(text="Name:",size_hint_y=None,height=dp(22)))
        self.pn=TextInput(text=pr.name or '',multiline=False,size_hint_y=None,height=dp(36)); ly.add_widget(self.pn)
        ly.add_widget(Label(text="About:",size_hint_y=None,height=dp(22)))
        self.pd=TextInput(text=pr.description or '',multiline=True,size_hint_y=0.25); ly.add_widget(self.pd)
        ly.add_widget(Label(text="Instructions:",size_hint_y=None,height=dp(22)))
        self.pi=TextInput(text=pr.custom_instructions or '',multiline=True,size_hint_y=0.25); ly.add_widget(self.pi)
        sv=Button(text="Save",size_hint_y=None,height=dp(42)); sv.bind(on_press=self._savep); ly.add_widget(sv)
        return ly
    def _savep(self,*a):
        self.eng.db.set_profile(UserProfile(self.pn.text.strip() or None,self.pd.text.strip() or None,self.pi.text.strip() or None))
        self.dismiss()

class ConvsPopup(Popup):
    def __init__(self, eng, on_sel=None, **kw):
        self.eng, self.on_sel = eng, on_sel
        c=BoxLayout(orientation='vertical',padding=dp(6))
        sc=ScrollView()
        self.lst=BoxLayout(orientation='vertical',size_hint_y=None,spacing=dp(3))
        self.lst.bind(minimum_height=self.lst.setter('height'))
        sc.add_widget(self.lst); c.add_widget(sc)
        self._ld()
        super().__init__(title="Chats",content=c,size_hint=(0.9,0.8),**kw)
    def _ld(self):
        self.lst.clear_widgets()
        for cv in self.eng.db.list_convs():
            it=BoxLayout(size_hint_y=None,height=dp(46))
            b=Button(text=f"{cv.title}\n{cv.updated_at.strftime('%m/%d %H:%M')}",halign='left',size_hint_x=0.8)
            b.bind(on_press=lambda x,c=cv:self._sel(c)); it.add_widget(b)
            d=Button(text="🗑️",size_hint_x=0.2); d.bind(on_press=lambda x,c=cv:self._del(c.id)); it.add_widget(d)
            self.lst.add_widget(it)
    def _sel(self,cv): self.eng.load_conv(cv.id); self.on_sel() if self.on_sel else None; self.dismiss()
    def _del(self,cid): self.eng.db.delete_conv(cid); self._ld()

class MainWidget(BoxLayout):
    def __init__(self,**kw):
        super().__init__(orientation='vertical',**kw)
        self.cfg, self.mdl, self.pre, self.db = ConfigManager(), None, PresetsManager(), Database()
        self.mdl = ModelsManager(self.cfg)
        self.eng = ChatEngine(self.cfg, self.mdl, self.pre, self.db)
        
        nav=BoxLayout(size_hint_y=None,height=dp(46))
        cb=Button(text="📚 Chats",size_hint_x=0.4); cb.bind(on_press=self._cvs); nav.add_widget(cb)
        self.tlb=Label(text="ChatADHD",size_hint_x=0.6); nav.add_widget(self.tlb)
        self.add_widget(nav)
        
        self.chat=ChatView(self.eng); self.add_widget(self.chat)
        
        cs=self.db.list_convs(1)
        if cs: self.eng.load_conv(cs[0].id)
        else: self.eng.new_conv()
        self.chat.refresh()
    
    def _cvs(self,*a): ConvsPopup(self.eng,self._csel).open()
    def _csel(self): self.chat.refresh(); self.tlb.text=self.eng.conv.title[:20] if self.eng.conv else "Chat"

class ChatADHDApp(App):
    def build(self): return MainWidget()

if __name__=="__main__": ChatADHDApp().run()
