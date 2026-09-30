"""Exact-byte memoization and independently verified immediate-parent reuse.

Entries are admitted only by the bound strict validator or one complete new
event proof over an exact privately authorized parent. No canonical store,
model call, model quality claim or caller-mutable packet is kept in this cache.
"""
from __future__ import annotations

from collections import OrderedDict
from copy import deepcopy
import hashlib
from pathlib import Path
import sys
import threading

try:
    from ..agentic_graph_v1 import packet as codec
except ImportError:
    from agentic_graph_v1 import packet as codec

safe = codec.safe
STRICT_PACKET_SHA256 = '1b949dad8319f21713b984367cba162d1330f188d26557b21b264ef10910bbc4'


class CacheIntegrityError(RuntimeError):
    """Foreign/corrupted cache authorization is fatal, not a cache hit."""


def validate_policy(policy):
    safe._keys(policy, {'schema', 'mode', 'max_entries', 'max_retained_bytes', 'max_entry_bytes', 'on_capacity'})
    if (policy['schema'] != 'loom.graph_packet_cache_policy/1' or
            policy['mode'] not in {'whole_packet', 'verified_history'} or policy['on_capacity'] not in {'evict_lru', 'bypass'}):
        raise ValueError('graph_packet_cache_policy_invalid')
    for key in ('max_entries', 'max_retained_bytes', 'max_entry_bytes'):
        if policy[key] is not None and (type(policy[key]) is not int or policy[key] < 0):
            raise ValueError('graph_packet_cache_budget_invalid')
    return policy


def _parent_one_event(current):
    """Exactly one backwards/forward event proof; prefix is not assumed valid.

    Prefix authorization is a separate exact-byte cache lookup. This deliberately
    uses the frozen codec's validators and diff/replay operations. The retained
    order/value/provenance restoration mirrors its full-history baseline.
    """
    codec._validate_packet_content(current)
    event = current['history'][-1]
    safe._keys(event, {'application_id', 'base_packet_id', 'diff', 'changes', 'previous_task', 'previous_order', 'result_origin'})
    if event['application_id'] != codec.digest({k: v for k, v in event.items() if k != 'application_id'}):
        raise ValueError('graph_packet_history_hash_drift')
    codec.validate_origin(event['result_origin'])
    safe._keys(event['previous_order'], set(codec.COLLECTIONS))
    if not isinstance(event['changes'], list) or not isinstance(event['previous_task'], dict):
        raise ValueError('graph_packet_history_changes_invalid')
    indexes = deepcopy(codec._indexes(current)); provenance = deepcopy(current['provenance']); touched = set()
    for change in reversed(event['changes']):
        safe._keys(change, {'collection', 'action', 'record_id', 'before', 'after', 'before_provenance', 'after_provenance'})
        name, ident = change['collection'], change['record_id']
        if name not in codec.COLLECTIONS or change['action'] not in {'add', 'update', 'remove'} or (name, ident) in touched:
            raise ValueError('graph_packet_history_change_identity_invalid')
        touched.add((name, ident))
        if indexes[name].get(ident) != change['after'] or provenance[name].get(ident) != change['after_provenance']:
            raise ValueError('graph_packet_history_after_state_mismatch')
        before = change['before']
        if before is None:
            if change['action'] != 'add' or change['before_provenance'] is not None:
                raise ValueError('graph_packet_history_missing_before_record')
            del indexes[name][ident]; del provenance[name][ident]
        else:
            codec.VALIDATORS[name](before)
            if codec.record_id(name, before) != ident or change['action'] == 'add':
                raise ValueError('graph_packet_history_before_identity_invalid')
            metadata = change['before_provenance']
            safe._keys(metadata, {'known_at', 'origin', 'record_sha256'})
            codec.validate_origin(metadata['origin']); codec._timestamp(metadata['known_at'])
            if metadata['record_sha256'] != codec.digest(before):
                raise ValueError('graph_packet_history_before_provenance_hash_drift')
            indexes[name][ident] = deepcopy(before); provenance[name][ident] = deepcopy(metadata)
    parent = deepcopy(current)
    parent['history'].pop(); parent['task'] = deepcopy(event['previous_task']); parent['provenance'] = provenance
    for name in codec.COLLECTIONS:
        order = event['previous_order'][name]
        if not isinstance(order, list) or len(order) != len(set(order)) or set(order) != set(indexes[name]):
            raise ValueError('graph_packet_history_previous_order_invalid')
        parent[name] = [indexes[name][ident] for ident in order]
    parent['packet_id'] = codec.digest(codec._packet_payload(parent))
    if parent['packet_id'] != event['base_packet_id']:
        raise ValueError('graph_packet_history_parent_hash_mismatch')
    codec._validate_packet_content(parent)
    codec._validate_diff_structure(event['diff'], parent)
    replayed, replayed_event = codec._candidate(parent, event['diff'], validate_result=False)
    if replayed != current or replayed_event != event:
        raise ValueError('graph_packet_history_forward_replay_mismatch')
    return parent


class _Entry:
    __slots__ = ('raw', 'owner', 'binding', 'weight')

    def __init__(self, raw, owner, binding, key):
        self.raw, self.owner, self.binding = bytes(raw), owner, binding
        # Conservative per-entry accounting; OrderedDict allocator/container
        # overhead is measured separately, not advertised as a process limit.
        self.weight = sys.getsizeof(self) + sys.getsizeof(self.raw) + sys.getsizeof(key) + sum(sys.getsizeof(x) for x in key)


class VerifiedPacketCache:
    def __init__(self, policy):
        validate_policy(policy)
        source = Path(codec.__file__).read_bytes()
        if hashlib.sha256(source).hexdigest() != STRICT_PACKET_SHA256:
            raise CacheIntegrityError('strict_packet_source_version_mismatch')
        self._strict_source_sha256 = STRICT_PACKET_SHA256
        self._safe_source_sha256 = hashlib.sha256(Path(safe.__file__).read_bytes()).hexdigest()
        self._bindings = self._function_bindings()
        self._binding = codec.digest({'strict_packet_sha256': self._strict_source_sha256,
                                      'json_codec_sha256': self._safe_source_sha256})
        self._owner = object()
        self._lock = threading.RLock()
        self._entries = OrderedDict()
        self._charged_bytes = 0
        self._counts = {'whole_hits': 0, 'parent_hits': 0, 'strict_fallbacks': 0, 'stores': 0,
                        'evictions': 0, 'capacity_bypasses': 0, 'invalidations': 0}
        self._configure(policy)

    @staticmethod
    def _function_bindings():
        return tuple(getattr(codec, name) for name in ('validate_packet', 'validate_json_resources',
            '_validate_packet_content', '_validate_diff_structure', '_candidate', 'validate_origin', '_timestamp')) + \
            tuple(codec.VALIDATORS[name] for name in codec.COLLECTIONS) + (safe.canonical, safe.parse_json)

    def _guard_binding(self):
        if (self._function_bindings() != self._bindings or
                hashlib.sha256(Path(codec.__file__).read_bytes()).hexdigest() != self._strict_source_sha256 or
                hashlib.sha256(Path(safe.__file__).read_bytes()).hexdigest() != self._safe_source_sha256):
            raise CacheIntegrityError('strict_validator_binding_changed')

    def _configure(self, policy):
        self._policy_bytes = safe.canonical(policy)
        self._policy = safe.parse_json(self._policy_bytes)
        self._policy_sha256 = hashlib.sha256(self._policy_bytes).hexdigest()

    @property
    def policy(self):
        return deepcopy(self._policy)

    def reconfigure(self, policy):
        validate_policy(policy)
        with self._lock:
            self._entries.clear(); self._charged_bytes = 0
            self._counts['invalidations'] += 1; self._configure(policy)

    def clear(self):
        with self._lock:
            self._entries.clear(); self._charged_bytes = 0; self._counts['invalidations'] += 1

    def stats(self):
        with self._lock:
            return {**deepcopy(self._counts), 'entries': len(self._entries), 'charged_bytes': self._charged_bytes,
                    'payload_bytes': sum(len(entry.raw) for entry in self._entries.values()),
                    'policy_sha256': self._policy_sha256, 'validator_binding_sha256': self._binding}

    def _key(self, raw, limits_hash):
        return (self._binding, self._policy_sha256, limits_hash, hashlib.sha256(raw).hexdigest())

    def _lookup(self, raw, limits_hash):
        key = self._key(raw, limits_hash)
        with self._lock:
            entry = self._entries.get(key)
            if entry is None:
                return False
            if (not isinstance(entry, _Entry) or entry.owner is not self._owner or entry.binding != self._binding or
                    entry.raw != raw):
                raise CacheIntegrityError('foreign_or_corrupt_cache_authorization')
            self._entries.move_to_end(key)
            return True

    def _store_authorized(self, raw, limits_hash):
        key = self._key(raw, limits_hash)
        with self._lock:
            if key in self._entries:
                # An authorization token from another cache instance is never
                # adopted merely because its public digest matches.
                self._lookup(raw, limits_hash); return
            entry = _Entry(raw, self._owner, self._binding, key)
            policy = self._policy
            if (policy['max_entries'] == 0 or policy['max_retained_bytes'] == 0 or
                    (policy['max_entry_bytes'] is not None and len(raw) > policy['max_entry_bytes']) or
                    (policy['max_retained_bytes'] is not None and entry.weight > policy['max_retained_bytes'])):
                self._counts['capacity_bypasses'] += 1; return
            def exceeds():
                return ((policy['max_entries'] is not None and len(self._entries) >= policy['max_entries']) or
                        (policy['max_retained_bytes'] is not None and self._charged_bytes + entry.weight > policy['max_retained_bytes']))
            if exceeds() and policy['on_capacity'] == 'bypass':
                self._counts['capacity_bypasses'] += 1; return
            while exceeds() and self._entries:
                _, old = self._entries.popitem(last=False)
                self._charged_bytes -= old.weight; self._counts['evictions'] += 1
            self._entries[key] = entry; self._charged_bytes += entry.weight; self._counts['stores'] += 1

    def validate_with_receipt(self, packet, *, resource_limits=None):
        self._guard_binding()
        # Detach exact bytes first. No caller-owned mutable object enters a
        # validated entry, even if the caller mutates it during later work.
        codec.validate_json_resources(packet, resource_limits)
        raw = safe.canonical(packet)
        snapshot = safe.parse_json(raw)
        limits_hash = codec.digest(resource_limits)
        parent = None
        if self._lookup(raw, limits_hash):
            with self._lock:
                self._counts['whole_hits'] += 1
            path = 'whole_packet_hit'
        else:
            authorized_parent = False
            if self._policy['mode'] == 'verified_history' and isinstance(snapshot, dict) and snapshot.get('history'):
                try:
                    parent = _parent_one_event(snapshot)
                except (KeyError, TypeError, ValueError, IndexError):
                    # Unsupported/invalid prospective fast path never wins over
                    # the strict baseline. Its exact rejection remains primary.
                    parent = None
                if parent is not None:
                    authorized_parent = self._lookup(safe.canonical(parent), limits_hash)
            if authorized_parent:
                with self._lock:
                    self._counts['parent_hits'] += 1
                path = 'verified_immediate_parent_hit'
            else:
                codec.validate_packet(snapshot, resource_limits=resource_limits)
                with self._lock:
                    self._counts['strict_fallbacks'] += 1
                path = 'strict_fallback'
                # A successful full-history proof establishes its exact parent
                # too. Store that parent only for the explicit history method.
                if self._policy['mode'] == 'verified_history' and snapshot['history'] and parent is None:
                    parent = _parent_one_event(snapshot)
            if parent is not None and self._policy['mode'] == 'verified_history':
                self._store_authorized(safe.canonical(parent), limits_hash)
            self._store_authorized(raw, limits_hash)
        receipt = {'schema': 'loom.graph_packet_cache_validation/1', 'path': path,
                   'packet_canonical_sha256': hashlib.sha256(raw).hexdigest(), 'packet_id': snapshot['packet_id'],
                   'validator_binding_sha256': self._binding, 'policy_sha256': self._policy_sha256,
                   'resource_limits_sha256': limits_hash, 'stats': self.stats(),
                   'returns_detached_snapshot': True, 'canonical_store_written': False,
                   'semantic_quality_measured': False, 'receipt_is_not_cache_admission_authority': True}
        return snapshot, receipt

    def validate_packet(self, packet, *, resource_limits=None):
        return self.validate_with_receipt(packet, resource_limits=resource_limits)[0]
