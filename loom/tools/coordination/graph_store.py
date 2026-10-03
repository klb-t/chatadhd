"""Explicit GraphPacket acceptance into Loom's existing native KnowledgeStore.

No models or credentials are loaded. The caller chooses a target and a closed
selection, explicitly accepts it, and supplies exact per-row compare-and-swap
hashes (None for a previously absent row). Receipts retain the full original
packet and reversible history. A replay verifies rows; it never repairs drift.
"""
from copy import deepcopy
import ctypes
import json

from ..structure.agentic_graph_v1 import packet as codec


class GraphStoreError(ValueError):
    def __init__(self, error):
        self.code = error['code']
        super().__init__(error['message'])


def acceptance_request(packet, *, target, selection, expected_rows, explicitly_accepted):
    """Freeze and fully validate packet/history before crossing the C ABI."""
    packet = deepcopy(packet)
    codec.validate_packet(packet)
    if not isinstance(target, str) or not target:
        raise ValueError('graph_store_target_required')
    if explicitly_accepted is not True:
        raise ValueError('graph_store_explicit_acceptance_required')
    return {'operation': 'accept', 'target': target, 'packet': packet,
            'selection': deepcopy(selection), 'expected_rows': deepcopy(expected_rows),
            'explicitly_accepted': True}


class NativeGraphStore:
    """Small standalone FFI caller; use it with ``with`` to close its context."""
    def __init__(self, library_path, data_directory):
        self.library = ctypes.CDLL(str(library_path))
        self.library.loom_init_ex.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
        self.library.loom_init_ex.restype = ctypes.c_void_p
        self.library.loom_shutdown.argtypes = [ctypes.c_void_p]
        self.library.loom_free_string.argtypes = [ctypes.c_void_p]
        self.library.loom_graph_packet_store.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        self.library.loom_graph_packet_store.restype = ctypes.c_void_p
        error = ctypes.c_void_p()
        options = {'data_dir': str(data_directory), 'start_workers': False}
        self.context = self.library.loom_init_ex(json.dumps(options).encode(), ctypes.byref(error))
        if not self.context:
            if error.value:
                raise GraphStoreError(self._take(error.value)['error'])
            raise RuntimeError('native graph store initialization returned no context or error')

    def _take(self, pointer):
        if not pointer:
            raise MemoryError('native graph store returned a null string')
        try:
            return json.loads(ctypes.string_at(pointer).decode('utf-8'))
        finally:
            self.library.loom_free_string(pointer)

    def execute(self, request):
        if not self.context:
            raise RuntimeError('native graph store is closed')
        result = self._take(self.library.loom_graph_packet_store(
            self.context, json.dumps(request, ensure_ascii=False, allow_nan=False).encode('utf-8')))
        if 'error' in result:
            raise GraphStoreError(result['error'])
        return result

    def accept(self, packet, *, target, selection, expected_rows, explicitly_accepted):
        return self.execute(acceptance_request(packet, target=target, selection=selection,
            expected_rows=expected_rows, explicitly_accepted=explicitly_accepted))

    def read(self, receipt_id):
        result = self.execute({'operation': 'read', 'receipt_id': receipt_id})
        codec.validate_packet(result['receipt']['packet'])
        return result

    def replay(self, receipt_id):
        result = self.execute({'operation': 'replay', 'receipt_id': receipt_id})
        codec.validate_packet(result['receipt']['packet'])
        return result

    def close(self):
        if self.context:
            self.library.loom_shutdown(self.context)
            self.context = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
