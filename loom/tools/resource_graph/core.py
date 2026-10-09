"""Composable resource handles; native persistence, rendering and eager import are optional.

Only explicitly requested fragments become packet nodes. The built-in syntax adapter
parses a bounded byte source when demanded; registered seekable adapters can read less.
This object is a transient access session, not a second graph or configuration store.
"""
from __future__ import annotations
import base64
from contextlib import contextmanager
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import time
from urllib.parse import urlsplit
from .projection import entity, ident, make_packet, observation, profile, relation


class ResourceError(ValueError):
    def __init__(self, status, code):
        self.status, self.code = status, code
        super().__init__(code)


def _escape(value):
    return str(value).replace('~', '~0').replace('/', '~1')


def select_value(value, pointer):
    if pointer == '':
        return value
    if not pointer.startswith('/'):
        raise ResourceError('unsupported', 'invalid_json_pointer')
    for raw in pointer[1:].split('/'):
        key = raw.replace('~1', '/').replace('~0', '~')
        try:
            if isinstance(value, list):
                if not key.isdigit() or (key != '0' and key.startswith('0')):
                    raise KeyError(key)
                value = value[int(key)]
            elif isinstance(value, dict):
                value = value[key]
            else:
                raise KeyError(key)
        except (KeyError, IndexError):
            raise ResourceError('unavailable', 'fragment_not_found') from None
    return value


def _children(value):
    if isinstance(value, dict):
        return list(value.items())
    if isinstance(value, list):
        return [(str(i), item) for i, item in enumerate(value)]
    return []


class _SyntaxHandle:
    def __init__(self, value, metadata):
        self.value, self._metadata = value, metadata

    def select(self, pointer):
        return select_value(self.value, pointer)

    def children(self, pointer, offset, limit):
        return _children(self.select(pointer))[offset:offset + limit]

    def node(self, pointer):
        value = self.select(pointer)
        compound = isinstance(value, (dict, list))
        return {'is_container': compound, 'size': len(value) if compound else None,
                'value': None if compound else value, 'value_type': type(value).__name__}

    def metadata(self):
        return deepcopy(self._metadata)


class SyntaxAdapter:
    """Syntax parser composed with independently registered transport/container access."""
    def open(self, resource, access):
        from .parsers import parse, parser_descriptor, UnsupportedFormat, ParseError
        if resource.get('_embedded') is not None and (resource.get('_inline') or resource['policy']['mode'] == 'snapshot'):
            data = resource['_embedded']
            result = {'data': data, 'status': 'available', 'source_version': resource.get('source_version'),
                      'content_sha256': hashlib.sha256(data).hexdigest()}
        else:
            result = access.read(resource['locator'], tuple(resource['members']))
        if result['status'] not in {'available', 'ready', 'loaded', 'empty'}:
            raise ResourceError(result['status'], result.get('reason', result.get('code', 'resource_read_failed')))
        format_id = resource['format']
        if not format_id:
            raise ResourceError('unsupported', 'parser_not_recognized')
        try:
            value = parse(result['data'], format_id, options=resource.get('parser_options'))
            descriptor = parser_descriptor(format_id)
        except UnsupportedFormat as exc:
            raise ResourceError('unsupported', 'parser_unavailable') from exc
        except ParseError as exc:
            status = 'partial' if 'budget' in str(exc) else 'corrupt'
            raise ResourceError(status, 'syntax_parse_failed') from exc
        except (ValueError, TypeError, RecursionError) as exc:
            raise ResourceError('corrupt', 'syntax_parse_failed') from exc
        if resource['policy']['embedding']:
            resource['_embedded'] = result['data']
        return _SyntaxHandle(value, {'source_version': result.get('source_version'),
            'content_sha256': result.get('content_sha256'), 'parser_id': format_id,
            'parser_version': descriptor.get('version', 'unknown'), 'parser_descriptor': descriptor, 'status': 'empty' if value in ({}, [], '') else 'available',
            'recognition': 'syntax_only', 'read_scope': 'selected_container_member'})


class ResourceGraph:
    def __init__(self, *, access=None, policy=None, clock=time.monotonic):
        from .access import Access
        self.access = access if access is not None else Access()
        self.profile = profile()
        self.policy = deepcopy(self.profile['defaults'])
        if policy:
            self.policy.update(policy)
        self._validate_policy(self.policy)
        self.resources, self._cache, self._index = {}, {}, {}
        self.adapters = {'syntax': SyntaxAdapter()}
        self.clock = clock
        self.metrics = {'opens': 0, 'projected_nodes': 0, 'cache_hits': 0}

    def _validate_policy(self, policy):
        if policy['mode'] not in {'live', 'snapshot'}:
            raise ValueError('invalid_resource_mode')
        for name in ('embedding', 'cache', 'index'):
            if type(policy[name]) is not bool:
                raise ValueError('invalid_resource_policy_boolean')
        if policy['retention_seconds'] < 0:
            raise ValueError('invalid_resource_retention')
        for key in ('max_projection_nodes', 'max_projection_depth'):
            if type(policy[key]) is not int or policy[key] < 1 or policy[key] > self.profile['environment_limits'][key]:
                raise ValueError('projection_environment_boundary')

    def register_adapter(self, adapter_id, adapter):
        if not adapter_id or not callable(getattr(adapter, 'open', None)):
            raise ValueError('invalid_adapter')
        self.adapters[adapter_id] = adapter

    def attach(self, locator, *, logical_id, members=(), format=None, adapter='syntax', permissions=None, policy=None, parser_options=None):
        """Attach even unavailable/unknown sources. This performs ZERO transport reads."""
        if not isinstance(logical_id, str) or not logical_id or logical_id in self.resources:
            raise ValueError('resource_logical_identity_required_or_duplicate')
        parsed = urlsplit(str(locator))
        if parsed.username or parsed.password or parsed.query or parsed.fragment:
            raise ValueError('credential_bearing_or_ambiguous_locator')
        effective = dict(self.policy, **(policy or {}))
        self._validate_policy(effective)
        if format is None:
            suffix = Path(members[-1] if members else parsed.path).suffix.lower()
            format = next((row['parser'] for row in self.profile['formats'] if suffix in row['suffixes']), None)
        resource = {'logical_id': logical_id, 'locator': str(locator), 'members': list(members),
                    'format': format, 'adapter': adapter, 'policy': effective, 'parser_options': deepcopy(parser_options or {}),
                    'permissions': dict({'read': True, 'export_values': True, 'write_back': False}, **(permissions or {})),
                    'status': 'unloaded', 'source_version': None, 'content_sha256': None,
                    'selector': '', 'parser_version': None, 'mapping_version': None,
                    'history': [], 'recognition': 'unknown', '_embedded': None}
        self.resources[logical_id] = resource
        return self.describe(logical_id)

    def attach_inline(self, data, *, logical_id, format, locator=None, **kwargs):
        """Explicit byte embedding remains independent of cache, index and snapshot."""
        self.attach(locator or 'inline:' + logical_id, logical_id=logical_id, format=format, **kwargs)
        resource = self.resources[logical_id]
        resource['_embedded'] = bytes(data)
        resource['_inline'] = True
        resource['content_sha256'] = hashlib.sha256(data).hexdigest()
        resource['source_version'] = {'kind': 'embedded', 'revision': resource['content_sha256']}
        resource['policy']['embedding'] = True
        return self.describe(logical_id)

    def describe(self, logical_id):
        resource = self.resources[logical_id]
        descriptor = {key: deepcopy(value) for key, value in resource.items() if not key.startswith('_')}
        descriptor['embedded_bytes'] = resource['_embedded'] is not None
        descriptor['capabilities'] = {'read': resource['permissions']['read'],
            'select': resource['adapter'] in self.adapters, 'project': resource['adapter'] in self.adapters,
            'write_back': False, 'overlay': True, 'native_persistence_required': False, 'renderer_required': False}
        return descriptor

    def reference_packet(self, logical_id):
        descriptor = self.describe(logical_id)
        root = entity(ident(logical_id), self.profile['kinds']['resource'], logical_id, descriptor)
        return make_packet([root], task={'operation': 'resource_reference', 'materialized_fields': 0})

    def _drop_cache(self, logical_id):
        cached = self._cache.pop(logical_id, None)
        if cached and hasattr(cached[1], 'close'):
            cached[1].close()

    def evict(self, logical_id):
        self._drop_cache(logical_id)
        self._index.pop(logical_id, None)

    def _update_metadata(self, logical_id, metadata):
        resource = self.resources[logical_id]
        previous = {key: deepcopy(resource.get(key)) for key in ('source_version', 'content_sha256')}
        if resource['policy']['mode'] == 'snapshot' and any(value is not None for value in previous.values()):
            if any(previous[key] is not None and previous[key] != metadata.get(key) for key in previous):
                resource['status'] = 'unavailable'
                raise ResourceError('unavailable', 'snapshot_source_changed')
        resource.update({key: deepcopy(metadata[key]) for key in ('source_version', 'content_sha256', 'parser_version', 'mapping_version', 'recognition', 'status') if key in metadata})
        observed = {key: deepcopy(resource.get(key)) for key in ('source_version', 'content_sha256')}
        if not resource['history'] or observed != {key: resource['history'][-1].get(key) for key in observed}:
            resource['history'].append(dict(observed, origin='own_observation', observed_at=time.time()))
        if previous != observed:
            self._index.pop(logical_id, None)
        if resource['policy']['index'] and logical_id not in self._index:
            self._index[logical_id] = {'source_version': resource['source_version'], 'entries': {}}

    def _handle(self, logical_id):
        resource = self.resources[logical_id]
        if not resource['permissions']['read']:
            resource['status'] = 'unavailable'
            raise ResourceError('unavailable', 'read_permission_denied')
        cached = self._cache.get(logical_id)
        policy = resource['policy']
        if cached and policy['cache'] and self.clock() - cached[0] < policy['retention_seconds']:
            self.metrics['cache_hits'] += 1
            try:
                if hasattr(cached[1], 'refresh'):
                    cached[1].refresh()
                self._update_metadata(logical_id, cached[1].metadata())
                return cached[1]
            except Exception:
                self._drop_cache(logical_id)
                raise
        self._drop_cache(logical_id)
        if resource['adapter'] not in self.adapters:
            resource['status'] = 'unsupported'
            raise ResourceError('unsupported', 'adapter_unavailable')
        handle = None
        try:
            handle = self.adapters[resource['adapter']].open(resource, self.access)
            self._update_metadata(logical_id, handle.metadata())
            self.metrics['opens'] += 1
            if policy['cache']:
                self._cache[logical_id] = (self.clock(), handle)
            return handle
        except ResourceError as exc:
            if handle is not None and hasattr(handle, 'close'):
                handle.close()
            resource['status'] = exc.status
            raise
        except (OSError, ValueError) as exc:
            if handle is not None and hasattr(handle, 'close'):
                handle.close()
            resource['status'] = 'unavailable'
            raise ResourceError('unavailable', 'adapter_open_failed') from exc

    @contextmanager
    def _using_handle(self, logical_id):
        handle = None
        try:
            handle = self._handle(logical_id)
            yield handle
        except ResourceError as exc:
            self.resources[logical_id]['status'] = exc.status
            raise
        finally:
            if handle is not None and (logical_id not in self._cache or self._cache[logical_id][1] is not handle):
                if hasattr(handle, 'close'):
                    handle.close()

    def select(self, logical_id, pointer=''):
        with self._using_handle(logical_id) as handle:
            value = deepcopy(handle.select(pointer))
            self._update_metadata(logical_id, handle.metadata())
            self._record_index(logical_id, pointer, value)
            return value

    def children(self, logical_id, pointer='', *, offset=0, limit=None):
        limit = self.resources[logical_id]['policy']['page_size'] if limit is None else limit
        if type(offset) is not int or offset < 0 or type(limit) is not int or limit < 1:
            raise ValueError('invalid_page')
        if limit > self.resources[logical_id]['policy']['max_projection_nodes']:
            raise ResourceError('partial', 'projection_node_budget')
        with self._using_handle(logical_id) as handle:
            rows = [{'selector': pointer + '/' + _escape(key), 'key': key, 'value': deepcopy(value)}
                    for key, value in handle.children(pointer, offset, limit)]
            self._update_metadata(logical_id, handle.metadata())
            return rows

    def _record_index(self, logical_id, pointer, value):
        if logical_id in self._index:
            self._index[logical_id]['entries'][pointer] = json.dumps(value, ensure_ascii=False, sort_keys=True)

    def search_index(self, logical_id, text):
        """Only already indexed fragments; coverage and revision are explicit."""
        index = self._index.get(logical_id)
        return {'status': 'unloaded' if index is None else 'partial', 'coverage': 'requested_fragments_only',
                'source_version': None if index is None else deepcopy(index['source_version']),
                'selectors': [] if index is None else [key for key, value in index['entries'].items() if text in value]}

    def capture(self, logical_id):
        """Explicitly retain bytes, independent of snapshot/cache/index. Never writes source."""
        resource = self.resources[logical_id]
        if not resource['permissions']['read']:
            raise ResourceError('unavailable', 'read_permission_denied')
        result = self.access.read(resource['locator'], tuple(resource['members']))
        if result['status'] not in {'available', 'ready', 'loaded', 'empty'}:
            raise ResourceError(result['status'], result.get('code', 'capture_failed'))
        resource['_embedded'] = result['data']
        resource['source_version'] = result.get('source_version')
        resource['content_sha256'] = result['content_sha256']
        resource['policy']['embedding'] = True
        self.evict(logical_id)
        return self.describe(logical_id)

    def export_reference(self, logical_id, *, include_bytes=False):
        descriptor = self.describe(logical_id)
        if include_bytes:
            if not self.resources[logical_id]['permissions']['export_values']:
                raise ResourceError('unavailable', 'export_permission_denied')
            raw = self.resources[logical_id]['_embedded']
            if raw is None:
                raise ResourceError('unloaded', 'bytes_not_embedded')
            descriptor['content_base64'] = base64.b64encode(raw).decode('ascii')
        return descriptor

    def restore_reference(self, descriptor):
        """Reopen a saved descriptor without accessing the source or native store."""
        if not isinstance(descriptor, dict):
            raise ValueError('resource_reference_object_required')
        raw = None
        if 'content_base64' in descriptor:
            try:
                raw = base64.b64decode(descriptor['content_base64'], validate=True)
            except (ValueError, TypeError):
                raise ValueError('resource_embedded_encoding_invalid') from None
            if hashlib.sha256(raw).hexdigest() != descriptor.get('content_sha256'):
                raise ValueError('resource_embedded_hash_mismatch')
            self.access._check('max_source_bytes', len(raw))
        logical_id = descriptor['logical_id']
        self.attach(descriptor['locator'], logical_id=logical_id, members=descriptor['members'],
                    format=descriptor['format'], adapter=descriptor['adapter'],
                    permissions=descriptor['permissions'], policy=descriptor['policy'],
                    parser_options=descriptor.get('parser_options'))
        resource = self.resources[logical_id]
        for key in ('source_version', 'content_sha256', 'parser_version', 'mapping_version', 'history', 'recognition'):
            if key in descriptor:
                resource[key] = deepcopy(descriptor[key])
        resource['_embedded'] = raw
        resource['_inline'] = descriptor['locator'].startswith('inline:')
        return self.describe(logical_id)

    def project(self, logical_id, pointer='', *, depth=None, offset=0, limit=None):
        resource = self.resources[logical_id]
        if not resource['permissions']['export_values']:
            raise ResourceError('unavailable', 'export_permission_denied')
        policy = resource['policy']
        depth = policy['projection_depth'] if depth is None else depth
        limit = policy['max_projection_nodes'] if limit is None else limit
        if type(depth) is not int or depth < 0 or depth > policy['max_projection_depth'] or type(limit) is not int or limit < 1 or limit > policy['max_projection_nodes'] or offset < 0:
            raise ValueError('projection_policy_boundary')
        with self._using_handle(logical_id) as handle:
            metadata = handle.metadata()
            revision = ident(metadata.get('source_version'), metadata.get('content_sha256'), metadata.get('parser_version'), metadata.get('mapping_id'), metadata.get('mapping_version'), self.profile)
            root_id = ident(logical_id)
            entities = [entity(root_id, self.profile['kinds']['resource'], logical_id, self.describe(logical_id))]
            method_id = ident(resource['adapter'], metadata.get('parser_id'), metadata.get('parser_version'), metadata.get('mapping_id'), metadata.get('mapping_version'), self.profile)
            entities.append(entity(method_id, self.profile['kinds']['method'], resource['adapter'], {'parser': metadata, 'projection_version': self.profile['version']}))
            claims, sources = [], []
            pending = [(pointer, 0, root_id)]
            count, partial = 0, False
            while pending and count < limit:
                selector, level, parent = pending.pop(0)
                if hasattr(handle, 'node'):
                    node = handle.node(selector)
                    value, compound = node['value'], node['is_container']
                else:
                    value = handle.select(selector)
                    compound = isinstance(value, (dict, list))
                    node = {'size': len(value) if compound else None, 'value_type': type(value).__name__}
                node_id = ident(logical_id, revision, selector)
                attrs = {'logical_resource': logical_id, 'selector': selector, 'source_version': metadata.get('source_version'),
                         'content_sha256': metadata.get('content_sha256'), 'parser_version': metadata.get('parser_version'),
                         'mapping_version': metadata.get('mapping_version'), 'mapping_id': metadata.get('mapping_id'), 'value_type': node['value_type']}
                if not compound:
                    attrs['value'] = deepcopy(value)
                    if metadata.get('mapping_id') is None:
                        sources.append(observation(resource, selector, value, revision=revision, ordinal=count))
                    else:
                        attrs['derived_selector'] = True
                else:
                    attrs['size'] = node['size']
                entities.append(entity(node_id, self.profile['kinds']['field'], selector, attrs))
                claims.extend([relation(parent, self.profile['predicates']['contains'], node_id, version=revision),
                               relation(node_id, self.profile['predicates']['produced_by'], method_id, version=revision)])
                count += 1
                if compound and level < depth:
                    rows = handle.children(selector, offset if level == 0 else 0, limit + 1)
                    if len(rows) > limit:
                        partial = True
                    pending.extend((selector + '/' + _escape(key), level + 1, node_id) for key, _ in rows[:limit])
                elif compound and node['size'] != 0:
                    partial = True
                self._record_index(logical_id, selector, value if not compound else {'size': node['size']})
            final_metadata = handle.metadata()
            if any(final_metadata.get(key) != metadata.get(key) for key in ('source_version', 'content_sha256')):
                resource['status'] = 'partial'
                raise ResourceError('partial', 'source_changed_during_projection')
            self._update_metadata(logical_id, final_metadata)
            partial = partial or bool(pending) or offset > 0
            self.metrics['projected_nodes'] += count
            return make_packet(entities, claims, sources, task={'operation': 'resource_projection',
                'status': 'partial' if partial else resource['status'], 'selector': pointer,
                'source_revision': revision, 'materialized_fields': count,
                'coverage': {'depth': depth, 'offset': offset, 'limit': limit}, 'native_store': False})

    def recognize(self, logical_id, *, candidates=None):
        from .parsers import recognize
        resource = self.resources[logical_id]
        if not resource['permissions']['read']:
            raise ResourceError('unavailable', 'read_permission_denied')
        if resource['_embedded'] is not None:
            data = resource['_embedded']
        else:
            result = self.access.read(resource['locator'], tuple(resource['members']))
            if result['status'] not in {'available', 'empty'}:
                raise ResourceError(result['status'], result.get('reason', 'recognition_read_failed'))
            data = result['data']
        return recognize(data, candidates=candidates)

    def select_format(self, logical_id, format_id):
        from .parsers import parser_descriptor
        parser_descriptor(format_id)
        self.evict(logical_id)
        self.resources[logical_id]['format'] = format_id
        self.resources[logical_id]['status'] = 'unloaded'

    def discover(self, logical_id, *, discovery=None, declared_schema=None):
        from .discovery import Discovery
        value = self.select(logical_id)
        return (discovery or Discovery()).discover(value, declared_schema=declared_schema,
                                                   source=self.describe(logical_id))

    def overlay(self, logical_id, pointer, value):
        """A proposal only. No implicit config activation or write-back capability."""
        return {'logical_id': logical_id, 'selector': pointer, 'value': deepcopy(value),
                'basis': self.describe(logical_id), 'status': 'proposed', 'source_modified': False}
