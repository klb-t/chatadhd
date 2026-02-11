"""
ChatADHD v0.07.01 - GitHub Sync Popup

UI for configuring and executing GitHub synchronisation.
"""
import logging

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.metrics import dp, sp

from gui.base import C, RBtn, DarkInput, show_toast

log = logging.getLogger(__name__)


class GitHubSyncPopup(Popup):
    """Configure and run GitHub sync."""

    def __init__(self, engine, config, secrets, on_sync=None, **kw):
        self.engine = engine
        self.config = config
        self.secrets = secrets
        self.on_sync = on_sync

        content = BoxLayout(orientation="vertical", padding=dp(8), spacing=dp(4))

        content.add_widget(Label(text="GitHub Sync", color=C["text"],
                                 font_size=sp(12), size_hint_y=None, height=dp(24)))

        # Repo
        content.add_widget(Label(text="Repository (owner/repo):", color=C["text"],
                                 size_hint_y=None, height=dp(16), font_size=sp(9)))
        self.repo_input = DarkInput(
            text=config.get("github_repo", ""),
            size_hint_y=None, height=dp(30),
        )
        content.add_widget(self.repo_input)

        # Status
        self.status = Label(text="Configure repo and token in Settings",
                            color=C["dim"], font_size=sp(9),
                            size_hint_y=None, height=dp(20))
        content.add_widget(self.status)

        # Buttons
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Test", bg=C["ok"], on_press=self._test))
        btns.add_widget(RBtn(text="Push", bg=C["accent"], on_press=self._push))
        btns.add_widget(RBtn(text="Pull", bg=C["warn"], on_press=self._pull))
        btns.add_widget(RBtn(text="Close", bg=C["card"],
                             on_press=lambda *_: self.dismiss()))
        content.add_widget(btns)

        super().__init__(title="GitHub Sync", content=content,
                         size_hint=(0.95, 0.5), **kw)

    def _get_syncer(self):
        from engine.github_sync import GitHubSync, SyncConfig
        token = self.secrets.get("github_token", "")
        repo = self.repo_input.text.strip()
        if not repo or not token:
            self.status.text = "Need repo and token"
            return None
        self.config.set("github_repo", repo)
        self.config.save()
        cfg = SyncConfig(repo=repo, token=token,
                         local_path=str(self.config._path.parent))
        return GitHubSync(cfg)

    def _test(self, *_):
        syncer = self._get_syncer()
        if not syncer:
            return
        ok = syncer.test_connection()
        self.status.text = "Connected!" if ok else "Connection failed"

    def _push(self, *_):
        syncer = self._get_syncer()
        if not syncer:
            return
        self.status.text = "Pushing..."
        results = syncer.push_all()
        self.status.text = f"Pushed: {results['success']} ok, {results['failed']} failed"
        if self.on_sync:
            self.on_sync()

    def _pull(self, *_):
        syncer = self._get_syncer()
        if not syncer:
            return
        self.status.text = "Pulling..."
        results = syncer.pull_all()
        self.status.text = f"Pulled: {results['success']} ok, {results['failed']} failed"
        if self.on_sync:
            self.on_sync()
