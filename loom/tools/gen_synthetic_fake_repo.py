#!/usr/bin/env python3
"""Build the tiny fictional NoteFlow git repository: the code/"self" side of
the synthetic eval corpus (Ola Testowa, fictional -- see
gen_synthetic_eval_corpus.py and tests/fixtures/eval/synthetic_dev/README.md).

This script is committed; ITS OUTPUT IS NOT. Run it at test/eval time to get
a fresh repo whose commit history mirrors gen_synthetic_eval_corpus.py's
NoteFlow version/fork/lost-restored timeline exactly (imported directly from
that module, so the two can never drift apart): 0.1.0 -> 0.6.0 on `main`,
then a fork at 0.6.0 into `python-quick` (3 commits, abandoned, never
merged) and `cpp-core` (which becomes the new `main`), through to 1.2.0.
Two versions (1.0.1, 1.0.2) exist ONLY in this repo -- no conversation ever
mentions them -- by design, to test code-lineage-only evidence.

Usage:
    python3 loom/tools/gen_synthetic_fake_repo.py [--out DIR]

With no --out, a fresh directory under the system temp dir is created and
its path printed on stdout (nothing under the chatadhd working tree is
touched). Pass --out to build into an explicit directory instead (it must
not already exist, unless --force is given).

Determinism: fixed author identity, fixed GIT_AUTHOR_DATE/GIT_COMMITTER_DATE
per commit (taken from the same version timeline as the corpus), fixed file
contents and commit order. Git commit objects carry no hostname or clock, so
this produces byte-identical commit hashes on any machine, any time.
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_synthetic_eval_corpus import PERSONA, P_NF, VERSIONS  # noqa: E402

AUTHOR = PERSONA["git_author"]
NF_VERSIONS = {v["version"]: v for v in VERSIONS[P_NF]["versions"]}


def _date_of(version: str) -> str:
    return NF_VERSIONS[version]["date"] + "T12:00:00"


def run(cwd: pathlib.Path, *args: str, env_extra: dict | None = None):
    import os
    env = dict(os.environ)
    env.pop("GIT_DIR", None)
    env.pop("GIT_WORK_TREE", None)
    if env_extra:
        env.update(env_extra)
    subprocess.run(["git", *args], cwd=cwd, check=True, env=env,
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def commit(repo: pathlib.Path, version: str, branch: str, message: str, files: dict):
    """files: {relative_path: content_str_or_None}. None deletes the file."""
    for rel, content in files.items():
        p = repo / rel
        if content is None:
            if p.exists():
                p.unlink()
        else:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(content, encoding="utf-8")
    run(repo, "add", "-A")
    date = _date_of(version)
    env = {"GIT_AUTHOR_NAME": "Ola Testowa", "GIT_AUTHOR_EMAIL": "ola.testowa@example.invalid",
           "GIT_AUTHOR_DATE": date, "GIT_COMMITTER_NAME": "Ola Testowa",
           "GIT_COMMITTER_EMAIL": "ola.testowa@example.invalid", "GIT_COMMITTER_DATE": date}
    full_message = f"v{version}: {message}\n\nFictional commit for the synthetic eval corpus (Ola Testowa)."
    run(repo, "commit", "-q", "--allow-empty", "-m", full_message, "--no-verify", env_extra=env)
    run(repo, "tag", f"v{version}")


# ── file bodies (small, deterministic, purely fictional) ─────────────
README_0_1_0 = """# NoteFlow

Notatnik + czat z modelem w jednym miejscu. Offline-first.

Fictional project, part of a synthetic eval corpus. Not real software.
"""

MAIN_PY_0_1_0 = 'print("NoteFlow 0.1.0 - notatnik + czat, offline-first")\n'

STORAGE_PY_0_1_2 = '''"""Storage layer: SQLite for structured notes, a blobs/ dir for attachments.

Decision: see dec.nf.storage in the eval corpus ground truth.
"""
import sqlite3

SCHEMA = """
CREATE TABLE IF NOT EXISTS notes (id TEXT PRIMARY KEY, title TEXT, body TEXT, created TEXT);
"""


def open_db(path):
    conn = sqlite3.connect(path)
    conn.executescript(SCHEMA)
    return conn
'''

CRYPTO_PY_0_2_0 = '''"""At-rest encryption -- PLANNED, not implemented yet.

TODO: pick an AEAD cipher once the MVP ships. See dec.nf.encryption_defer.
"""
'''

SYNC_GIT_PY_0_3_0 = '''"""Sync v1: piggy-back on git. One commit per note change.

Decision: dec.nf.sync_v1. Superseded later by sync_rest.py (dec.nf.sync_v2).
"""


def sync():
    raise NotImplementedError("git-based sync: commit + push/pull the notes repo")
'''

TRANSCRIPTION_BRIDGE_0_4_0 = '''"""Voice memo transcription, behind a small provider interface.

Decision: dec.nf.transcription. Provider: cloud Whisper-style API.
feat.nf.voice_transcription
"""


class TranscriptionProvider:
    def transcribe(self, audio_path: str) -> str:
        raise NotImplementedError


class CloudWhisperProvider(TranscriptionProvider):
    def transcribe(self, audio_path: str) -> str:
        return "(cloud transcription stub)"
'''

NOTES_PY_0_5_0 = '''"""Note entity. Checklists are a DATA-shaped variant, not a new class.

Decision: dec.nf.checklist_as_data. feat.nf.checklist_notes
"""
from dataclasses import dataclass, field


@dataclass
class Note:
    id: str
    title: str
    body: str
    items: list = field(default_factory=list)  # non-empty => rendered as a checklist
'''

# ── cpp-core rewrite (0.7.0+) ─────────────────────────────────────────
CORE_ENGINE_CPP_0_7_0 = '''// core/engine.cpp -- the C++ core, one kernel for every platform.
// Decision: dec.nf.core_language (fork resolved: cpp-core wins over
// python-quick, which is abandoned on its own branch, not deleted).
#include <string>

namespace noteflow {
struct Engine {
    std::string version = "0.7.0";
};
}  // namespace noteflow
'''

DIFF_UTIL_CPP_0_7_1 = '''// core/diff_util.cpp -- library-based diff for note version history.
// Implementation detail only: dec.nf.diff_algo_swap. Not an architecture
// decision, no external contract changes.
#include <string>
#include <vector>

namespace noteflow {
std::vector<std::string> diff_lines(const std::string& a, const std::string& b);
}  // namespace noteflow
'''

GRAPH_MEMORY_CPP_0_8_0 = '''// core/graph_memory.cpp -- graph memory v1 (partial).
// feat.nf.graph_memory. Superseded at 1.0.0 by knowledge_graph_v2.cpp.
namespace noteflow {
struct GraphMemoryV1 {};
}  // namespace noteflow
'''

UPLOAD_GATE_CPP_0_9_0 = '''// core/upload_gate.cpp -- confirm-before-large-upload gate.
// Decision: dec.nf.upload_gate. feat.nf.upload_gate
namespace noteflow {
struct UploadGate {
    bool requires_confirmation(long long estimated_bytes) { return estimated_bytes > 5'000'000; }
};
}  // namespace noteflow
'''

SYNC_REST_CPP_1_0_0 = '''// core/sync_rest.cpp -- sync v2: REST-based custom sync, chunked uploads.
// Decision: dec.nf.sync_v2 (explicitly supersedes dec.nf.sync_v1 / sync_git.py).
namespace noteflow {
struct RestSync {};
}  // namespace noteflow
'''

KNOWLEDGE_GRAPH_V2_CPP_1_0_0 = '''// core/knowledge_graph_v2.cpp -- supersedes graph_memory.cpp (0.8.0).
// feat.nf.knowledge_graph_v2. Works offline with a bundled local embedding
// model (never named as its own "module" in conversation -- see
// ground_truth.json projects[proj.noteflow].universal_roles.part.inferable).
namespace noteflow {
struct KnowledgeGraphV2 {};
}  // namespace noteflow
'''

ONDEVICE_FALLBACK_CPP_1_1_0 = '''// core/transcription_ondevice.cpp -- on-device fallback transcription
// provider, added behind the SAME TranscriptionProvider interface as the
// cloud one (core/transcription_bridge.py from 0.4.0).
// Decision: dec.nf.transcription_fallback. feat.nf.voice_transcription: restored.
namespace noteflow {
struct OnDeviceTranscriptionProvider {};
}  // namespace noteflow
'''

CHACHA_PROVIDER_CPP_1_2_0 = '''// core/crypto_chacha.cpp -- at-rest encryption, ChaCha20-Poly1305.
// Decision: dec.nf.encryption (NOT the same cipher the real ChatADHD/Loom
// chose -- see ground_truth.json unpredictable_post_T_decisions).
namespace noteflow {
struct ChaChaCipherProvider {};
}  // namespace noteflow
'''


def build(repo: pathlib.Path):
    repo.mkdir(parents=True, exist_ok=False)
    run(repo, "init", "-q", "-b", "main")
    run(repo, "config", "user.name", "Ola Testowa")
    run(repo, "config", "user.email", "ola.testowa@example.invalid")

    commit(repo, "0.1.0", "main", "poczatek: notatnik + czat, offline-first",
           {"README.md": README_0_1_0, "main.py": MAIN_PY_0_1_0})
    commit(repo, "0.1.2", "main", "storage: SQLite dla danych, blobs/ dla zalacznikow",
           {"storage.py": STORAGE_PY_0_1_2})
    commit(repo, "0.2.0", "main", "planowanie szyfrowania (jeszcze nie implementowane)",
           {"crypto.py": CRYPTO_PY_0_2_0})
    commit(repo, "0.3.0", "main", "sync v1: po gicie",
           {"sync_git.py": SYNC_GIT_PY_0_3_0})
    commit(repo, "0.4.0", "main", "transkrypcja glosowa (cloud Whisper-style provider)",
           {"transcription_bridge.py": TRANSCRIPTION_BRIDGE_0_4_0})
    commit(repo, "0.5.0", "main", "checklisty jako dane + odlozone szyfrowanie",
           {"notes.py": NOTES_PY_0_5_0, "crypto.py": CRYPTO_PY_0_2_0 + "\n# abandoned for MVP, see dec.nf.encryption_defer\n"})
    commit(repo, "0.6.0", "main", "fork point: python-quick vs cpp-core", {})

    # python-quick: forks from 0.6.0, three small patches, then abandoned (never merged).
    run(repo, "branch", "python-quick")
    run(repo, "checkout", "-q", "python-quick")
    commit(repo, "0.6.1", "python-quick", "python-quick: drobna poprawka 1", {"main.py": MAIN_PY_0_1_0 + "# patch 1\n"})
    commit(repo, "0.6.2", "python-quick", "python-quick: drobna poprawka 2", {"main.py": MAIN_PY_0_1_0 + "# patch 1\n# patch 2\n"})
    commit(repo, "0.6.3", "python-quick", "python-quick: ostatnia lata przed porzuceniem",
           {"main.py": MAIN_PY_0_1_0 + "# patch 1\n# patch 2\n# patch 3 (last one on this branch)\n"})
    run(repo, "checkout", "-q", "main")

    # cpp-core: also forks from 0.6.0, becomes the new main line.
    commit(repo, "0.7.0", "main", "adopt C++ core (branch cpp-core); python-quick porzucona, nie skasowana",
           {"core/engine.cpp": CORE_ENGINE_CPP_0_7_0, "main.py": None})
    commit(repo, "0.7.1", "main", "wewnetrzna zmiana: diff biblioteczny zamiast recznego (bez ADR)",
           {"core/diff_util.cpp": DIFF_UTIL_CPP_0_7_1})
    commit(repo, "0.8.0", "main", "graph memory v1 (partial)",
           {"core/graph_memory.cpp": GRAPH_MEMORY_CPP_0_8_0})
    commit(repo, "0.9.0", "main", "regresja przy porcie: gubi sie transkrypcja i checklisty; dodaje upload gate",
           {"transcription_bridge.py": None, "core/upload_gate.cpp": UPLOAD_GATE_CPP_0_9_0})
    commit(repo, "1.0.0", "main", "sync v2 (REST); przywraca checklisty; knowledge graph v2",
           {"sync_git.py": None, "core/sync_rest.cpp": SYNC_REST_CPP_1_0_0,
            "notes.py": NOTES_PY_0_5_0, "core/graph_memory.cpp": None,
            "core/knowledge_graph_v2.cpp": KNOWLEDGE_GRAPH_V2_CPP_1_0_0})
    # 1.0.1 / 1.0.2: bugfix-only, repo-only -- no conversation ever mentions these.
    commit(repo, "1.0.1", "main", "bugfix: naprawa wyciekajacego uchwytu pliku w storage.py",
           {"storage.py": STORAGE_PY_0_1_2 + "\n# 1.0.1: fixed a file-handle leak\n"})
    commit(repo, "1.0.2", "main", "bugfix: naprawa rzadkiego race w sync_rest",
           {"core/sync_rest.cpp": SYNC_REST_CPP_1_0_0 + "\n// 1.0.2: fixed a rare race\n"})
    # ── T = 2026-05-01 ──
    commit(repo, "1.1.0", "main", "transkrypcja restored + on-device fallback za tym samym interfejsem",
           {"transcription_bridge.py": TRANSCRIPTION_BRIDGE_0_4_0,
            "core/transcription_ondevice.cpp": ONDEVICE_FALLBACK_CPP_1_1_0})
    commit(repo, "1.2.0", "main", "szyfrowanie (ChaCha20-Poly1305); checklisty zgubione ponownie (refaktor UI)",
           {"core/crypto_chacha.cpp": CHACHA_PROVIDER_CPP_1_2_0,
            "notes.py": "# checklist rendering dropped again by a UI refactor (still lost) -- see "
                        "feat.nf.checklist_notes in ground_truth.json\n" + NOTES_PY_0_5_0})

    run(repo, "checkout", "-q", "main")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=str, default=None,
                     help="target directory (default: a fresh dir under the system temp dir)")
    ap.add_argument("--force", action="store_true", help="allow building into an existing empty --out dir")
    args = ap.parse_args()

    if args.out:
        repo = pathlib.Path(args.out).resolve()
        if repo.exists():
            if not args.force or any(repo.iterdir()):
                print(f"error: {repo} already exists (use --force with an empty dir)", file=sys.stderr)
                sys.exit(1)
            repo.rmdir()
    else:
        repo = pathlib.Path(tempfile.mkdtemp(prefix="noteflow-fake-repo-"))
        repo.rmdir()  # build() re-creates it; keeps the "must not exist" precondition uniform

    build(repo)
    print(str(repo))


if __name__ == "__main__":
    main()
