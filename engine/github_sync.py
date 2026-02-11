"""
ChatADHD v0.07.01 - GitHub Sync
Bidirectional sync with GitHub repositories.
"""
import os
import json
import base64
import logging
import requests
from pathlib import Path
from datetime import datetime
from typing import Optional, List, Dict, Any
from dataclasses import dataclass, field

log = logging.getLogger('github_sync')


@dataclass
class GitHubFile:
    """Represents a file in GitHub repo."""
    path: str
    sha: str
    size: int
    content: Optional[str] = None
    url: str = ""
    local_path: Optional[str] = None
    status: str = "unknown"  # synced, modified_local, modified_remote, new_local, new_remote, conflict


@dataclass 
class SyncConfig:
    """GitHub sync configuration."""
    repo: str  # owner/repo
    branch: str = "main"
    local_path: str = ""
    token: str = ""
    sync_direction: str = "bidirectional"  # bidirectional, push_only, pull_only
    auto_sync: bool = False
    include_patterns: List[str] = field(default_factory=lambda: ["*.py", "*.md", "*.json", "*.txt"])
    exclude_patterns: List[str] = field(default_factory=lambda: ["__pycache__/*", ".git/*", "*.pyc"])


class GitHubSync:
    """GitHub synchronization handler."""
    
    BASE_URL = "https://api.github.com"
    
    def __init__(self, config: SyncConfig):
        self.config = config
        self.headers = {
            "Accept": "application/vnd.github.v3+json",
            "User-Agent": "ChatADHD-Sync"
        }
        if config.token:
            self.headers["Authorization"] = f"token {config.token}"
    
    def test_connection(self) -> bool:
        """Test GitHub API connection."""
        try:
            resp = requests.get(
                f"{self.BASE_URL}/repos/{self.config.repo}",
                headers=self.headers,
                timeout=10
            )
            return resp.status_code == 200
        except Exception:
            return False
    
    def list_remote_files(self, path: str = "") -> List[GitHubFile]:
        """List files in remote repository."""
        files = []
        
        try:
            url = f"{self.BASE_URL}/repos/{self.config.repo}/contents/{path}"
            params = {"ref": self.config.branch}
            
            resp = requests.get(url, headers=self.headers, params=params, timeout=30)
            resp.raise_for_status()
            
            items = resp.json()
            if not isinstance(items, list):
                items = [items]
            
            for item in items:
                if item["type"] == "file":
                    if self._should_include(item["path"]):
                        files.append(GitHubFile(
                            path=item["path"],
                            sha=item["sha"],
                            size=item["size"],
                            url=item.get("download_url", ""),
                        ))
                elif item["type"] == "dir":
                    # Recurse into directories
                    files.extend(self.list_remote_files(item["path"]))
                    
        except requests.exceptions.RequestException as e:
            log.error(f"Failed to list remote files: {e}")
        
        return files
    
    def list_local_files(self) -> List[GitHubFile]:
        """List local files for sync."""
        files = []
        local_path = Path(self.config.local_path)
        
        if not local_path.exists():
            return files
        
        for fp in local_path.rglob("*"):
            if fp.is_file():
                rel_path = str(fp.relative_to(local_path))
                
                if self._should_include(rel_path):
                    files.append(GitHubFile(
                        path=rel_path,
                        sha="",  # Calculate later if needed
                        size=fp.stat().st_size,
                        local_path=str(fp),
                    ))
        
        return files
    
    def _should_include(self, path: str) -> bool:
        """Check if file should be included in sync."""
        import fnmatch
        
        # Check excludes first
        for pattern in self.config.exclude_patterns:
            if fnmatch.fnmatch(path, pattern):
                return False
        
        # Check includes
        for pattern in self.config.include_patterns:
            if fnmatch.fnmatch(path, pattern):
                return True
        
        # If no include patterns, include all (except excluded)
        if not self.config.include_patterns:
            return True
            
        return False
    
    def get_sync_status(self) -> List[GitHubFile]:
        """Compare local and remote, return files with status."""
        remote_files = {f.path: f for f in self.list_remote_files()}
        local_files = {f.path: f for f in self.list_local_files()}
        
        all_paths = set(remote_files.keys()) | set(local_files.keys())
        result = []
        
        for path in sorted(all_paths):
            remote = remote_files.get(path)
            local = local_files.get(path)
            
            if remote and local:
                # Both exist - check for modifications
                # Simple size check (proper impl would compare content hashes)
                if remote.size != local.size:
                    # Need to determine which is newer
                    # For now, mark as potential conflict
                    f = GitHubFile(
                        path=path,
                        sha=remote.sha,
                        size=remote.size,
                        local_path=local.local_path,
                        status="modified"
                    )
                else:
                    f = GitHubFile(
                        path=path,
                        sha=remote.sha,
                        size=remote.size,
                        local_path=local.local_path,
                        status="synced"
                    )
            elif remote and not local:
                f = GitHubFile(
                    path=path,
                    sha=remote.sha,
                    size=remote.size,
                    url=remote.url,
                    status="new_remote"
                )
            else:  # local and not remote
                f = GitHubFile(
                    path=path,
                    sha="",
                    size=local.size,
                    local_path=local.local_path,
                    status="new_local"
                )
            
            result.append(f)
        
        return result
    
    def fetch_file_content(self, file: GitHubFile) -> str:
        """Fetch content of a remote file."""
        try:
            if file.url:
                resp = requests.get(file.url, timeout=30)
                resp.raise_for_status()
                return resp.text
            else:
                # Use contents API
                url = f"{self.BASE_URL}/repos/{self.config.repo}/contents/{file.path}"
                params = {"ref": self.config.branch}
                
                resp = requests.get(url, headers=self.headers, params=params, timeout=30)
                resp.raise_for_status()
                
                data = resp.json()
                if data.get("encoding") == "base64":
                    return base64.b64decode(data["content"]).decode("utf-8")
                return data.get("content", "")
                
        except Exception as e:
            log.error(f"Failed to fetch {file.path}: {e}")
            return ""
    
    def pull_file(self, file: GitHubFile) -> bool:
        """Download file from GitHub to local."""
        if self.config.sync_direction == "push_only":
            log.warning("Pull disabled in push_only mode")
            return False
        
        content = self.fetch_file_content(file)
        if not content and file.size > 0:
            return False
        
        local_path = Path(self.config.local_path) / file.path
        local_path.parent.mkdir(parents=True, exist_ok=True)
        
        try:
            local_path.write_text(content, encoding="utf-8")
            log.info(f"Pulled: {file.path}")
            return True
        except Exception as e:
            log.error(f"Failed to write {file.path}: {e}")
            return False
    
    def push_file(self, file: GitHubFile, message: str = None) -> bool:
        """Upload local file to GitHub."""
        if self.config.sync_direction == "pull_only":
            log.warning("Push disabled in pull_only mode")
            return False
        
        if not file.local_path:
            return False
        
        try:
            content = Path(file.local_path).read_text(encoding="utf-8")
            content_b64 = base64.b64encode(content.encode("utf-8")).decode("utf-8")
        except Exception as e:
            log.error(f"Failed to read {file.local_path}: {e}")
            return False
        
        url = f"{self.BASE_URL}/repos/{self.config.repo}/contents/{file.path}"
        
        data = {
            "message": message or f"Update {file.path} via ChatADHD",
            "content": content_b64,
            "branch": self.config.branch,
        }
        
        # If updating existing file, need SHA
        if file.sha:
            data["sha"] = file.sha
        
        try:
            resp = requests.put(url, headers=self.headers, json=data, timeout=30)
            resp.raise_for_status()
            log.info(f"Pushed: {file.path}")
            return True
        except requests.exceptions.RequestException as e:
            log.error(f"Failed to push {file.path}: {e}")
            return False
    
    def pull_all(self, files: List[GitHubFile] = None) -> Dict[str, int]:
        """Pull all files (or specified list)."""
        if files is None:
            files = [f for f in self.get_sync_status() 
                    if f.status in ("new_remote", "modified")]
        
        results = {"success": 0, "failed": 0}
        
        for f in files:
            if self.pull_file(f):
                results["success"] += 1
            else:
                results["failed"] += 1
        
        return results
    
    def push_all(self, files: List[GitHubFile] = None) -> Dict[str, int]:
        """Push all files (or specified list)."""
        if files is None:
            files = [f for f in self.get_sync_status() 
                    if f.status in ("new_local", "modified")]
        
        results = {"success": 0, "failed": 0}
        
        for f in files:
            if self.push_file(f):
                results["success"] += 1
            else:
                results["failed"] += 1
        
        return results
    
    def sync(self, files: List[GitHubFile] = None) -> Dict[str, Any]:
        """Perform bidirectional sync."""
        if files is None:
            files = self.get_sync_status()
        
        results = {
            "pulled": {"success": 0, "failed": 0},
            "pushed": {"success": 0, "failed": 0},
            "conflicts": [],
        }
        
        for f in files:
            if f.status == "new_remote":
                if self.pull_file(f):
                    results["pulled"]["success"] += 1
                else:
                    results["pulled"]["failed"] += 1
                    
            elif f.status == "new_local":
                if self.push_file(f):
                    results["pushed"]["success"] += 1
                else:
                    results["pushed"]["failed"] += 1
                    
            elif f.status == "modified":
                # Conflict - need manual resolution
                results["conflicts"].append(f.path)
        
        return results


class GitHubSyncManager:
    """Manages GitHub sync configuration and operations."""
    
    def __init__(self, config_path: str, secrets):
        self.config_path = Path(config_path)
        self.secrets = secrets
        self.configs: Dict[str, SyncConfig] = {}
        self._load()
    
    def _load(self):
        """Load sync configurations."""
        if self.config_path.exists():
            try:
                data = json.loads(self.config_path.read_text())
                for name, cfg in data.items():
                    self.configs[name] = SyncConfig(**cfg)
            except Exception:
                pass
    
    def _save(self):
        """Save sync configurations."""
        data = {}
        for name, cfg in self.configs.items():
            data[name] = {
                "repo": cfg.repo,
                "branch": cfg.branch,
                "local_path": cfg.local_path,
                "sync_direction": cfg.sync_direction,
                "auto_sync": cfg.auto_sync,
                "include_patterns": cfg.include_patterns,
                "exclude_patterns": cfg.exclude_patterns,
            }
        self.config_path.write_text(json.dumps(data, indent=2))
    
    def add_config(self, name: str, repo: str, local_path: str, 
                   branch: str = "main", direction: str = "bidirectional") -> SyncConfig:
        """Add a new sync configuration."""
        token = self.secrets.get("github_token", "")
        
        cfg = SyncConfig(
            repo=repo,
            branch=branch,
            local_path=local_path,
            token=token,
            sync_direction=direction,
        )
        
        self.configs[name] = cfg
        self._save()
        return cfg
    
    def remove_config(self, name: str):
        """Remove a sync configuration."""
        if name in self.configs:
            del self.configs[name]
            self._save()
    
    def get_syncer(self, name: str) -> Optional[GitHubSync]:
        """Get GitHubSync instance for a configuration."""
        if name not in self.configs:
            return None
        
        cfg = self.configs[name]
        cfg.token = self.secrets.get("github_token", "")
        return GitHubSync(cfg)
    
    def list_configs(self) -> List[str]:
        """List all sync configuration names."""
        return list(self.configs.keys())
