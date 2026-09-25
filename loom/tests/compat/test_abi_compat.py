"""C ABI contract: loom.h declares every function of the original spec with the
exact signature, libloom.so exports exactly the declared functions, and the
library is usable through a plain FFI (ctypes)."""
import ctypes
import json
import os
import re
import subprocess
import unittest

from compat_common import REPO, CompatTestCase

HEADER = REPO / "loom" / "include" / "loom" / "loom.h"

ORIGINAL_SPEC = """
LoomContext* loom_init(const char* data_dir);
void loom_shutdown(LoomContext* ctx);
const char* loom_list_conversations(LoomContext* ctx, int limit);
const char* loom_create_conversation(LoomContext* ctx, const char* title);
int loom_delete_conversation(LoomContext* ctx, const char* conv_id);
const char* loom_get_messages(LoomContext* ctx, const char* conv_id);
void loom_chat(LoomContext* ctx, const char* conv_id, const char* user_message, const char* model_id, int context_depth, LoomStreamCallback callback, void* user_data);
const char* loom_get_nodes(LoomContext* ctx, const char* filter_json);
const char* loom_get_edges(LoomContext* ctx, const char* filter_json);
const char* loom_expand_graph(LoomContext* ctx, const char* seed_ids_json, int depth);
const char* loom_import_file(LoomContext* ctx, const char* path, const char* title, LoomProgressCallback cb, void* ud);
const char* loom_export_conversation(LoomContext* ctx, const char* conv_id, const char* fmt);
const char* loom_select_context(LoomContext* ctx, const char* text, int depth, int max_tokens);
const char* loom_semantic_status(LoomContext* ctx);
void loom_semantic_pause(LoomContext* ctx);
void loom_semantic_resume(LoomContext* ctx);
void loom_semantic_wake(LoomContext* ctx);
const char* loom_list_memory(LoomContext* ctx);
const char* loom_create_memory(LoomContext* ctx, const char* json);
const char* loom_update_memory(LoomContext* ctx, const char* id, const char* json);
int loom_delete_memory(LoomContext* ctx, const char* id);
const char* loom_get_config(LoomContext* ctx);
void loom_set_config(LoomContext* ctx, const char* key, const char* value);
const char* loom_get_models(LoomContext* ctx);
void loom_free_string(const char* str);
"""

ORIGINAL_TYPEDEFS = [
    "typedef void (*LoomStreamCallback)(const char* chunk, int done, void* user_data);",
    "typedef void (*LoomProgressCallback)(int current, int total, const char* status, void* ud);",
]


def _norm(s):
    s = re.sub(r"\s+", " ", s).strip()
    return s.replace("( ", "(").replace(" )", ")")


def declarations():
    text = HEADER.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    decls = {}
    for m in re.finditer(r"LOOM_API\s+([^;]+?\b(loom_\w+)\s*\([^;]*\))\s*;", text, re.S):
        decls.setdefault(m.group(2), []).append(_norm(m.group(1)) + ";")
    return text, decls


class AbiTest(CompatTestCase):

    def test_original_spec_signatures_are_exact(self):
        text, decls = declarations()
        for line in ORIGINAL_SPEC.strip().splitlines():
            name = re.search(r"\b(loom_\w+)\s*\(", line).group(1)
            self.assertIn(name, decls, name)
            self.assertEqual(len(decls[name]), 1, f"{name} declared more than once")
            self.assertEqual(decls[name][0], _norm(line))
        flat = _norm(text)
        for td in ORIGINAL_TYPEDEFS:
            self.assertIn(_norm(td), flat)

    def test_shared_library_exports_exactly_the_declared_api(self):
        lib = os.environ.get("LOOM_LIBRARY")
        if not lib:
            self.skipTest("LOOM_LIBRARY not set (build with LOOM_SHARED=ON)")
        _, decls = declarations()
        out = subprocess.run(["nm", "-D", "--defined-only", lib], capture_output=True, text=True, check=True).stdout
        exported = {line.split()[-1] for line in out.splitlines() if " T " in line}
        self.assertEqual(exported, set(decls))

    def test_ctypes_smoke(self):
        lib_path = os.environ.get("LOOM_LIBRARY")
        if not lib_path:
            self.skipTest("LOOM_LIBRARY not set (build with LOOM_SHARED=ON)")
        lib = ctypes.CDLL(lib_path)
        lib.loom_version.restype = ctypes.c_void_p
        lib.loom_free_string.argtypes = [ctypes.c_void_p]
        lib.loom_init_ex.restype = ctypes.c_void_p
        lib.loom_init_ex.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
        lib.loom_shutdown.argtypes = [ctypes.c_void_p]
        lib.loom_create_conversation.restype = ctypes.c_void_p
        lib.loom_create_conversation.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.loom_list_conversations.restype = ctypes.c_void_p
        lib.loom_list_conversations.argtypes = [ctypes.c_void_p, ctypes.c_int]
        lib.loom_get_conversation.restype = ctypes.c_void_p
        lib.loom_get_conversation.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.loom_set_log_stderr.argtypes = [ctypes.c_int]

        def take(ptr):
            s = ctypes.string_at(ptr).decode("utf-8")
            lib.loom_free_string(ptr)
            return json.loads(s)

        lib.loom_set_log_stderr(0)
        self.assertEqual(take(lib.loom_version())["abi"], 1)
        err = ctypes.c_void_p()
        opts = json.dumps({"data_dir": str(self.tmp.path / "data"), "start_workers": False}).encode()
        ctx = lib.loom_init_ex(opts, ctypes.byref(err))
        self.assertTrue(ctx, take(err.value) if err.value else "init failed")
        try:
            conv = take(lib.loom_create_conversation(ctx, "FFI ✓".encode()))
            self.assertEqual(conv["title"], "FFI ✓")
            self.assertEqual([c["id"] for c in take(lib.loom_list_conversations(ctx, 10))], [conv["id"]])
            self.assertEqual(take(lib.loom_get_conversation(ctx, b"c_missing"))["error"]["code"], "not_found")
        finally:
            lib.loom_shutdown(ctx)
        # The file is a normal ChatADHD database for the Python engine.
        from engine.db import Database
        db = Database(self.tmp.path / "data" / "chatadhd.db")
        self.assertEqual(db.list_convs()[0]["title"], "FFI ✓")
        db.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
