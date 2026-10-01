"""Domain callback, OS process, and explicitly limited MCP stdio adapters."""
import base64
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import tempfile
import time

from .runtime import AgentError, Tool, observed
from ..coordination.leases import digest
from ..structure.agentic_graph_v1 import packet as codec


def graph_tool():
    def invoke(environment, arguments):
        packet = arguments['packet']
        codec.validate_packet(packet)
        preview = codec.preview_diff(packet, arguments['diff'])
        selected, receipt = codec.apply_diff(packet, arguments['diff'], arguments['policy'],
                            explicitly_accepted=arguments['explicitly_accepted'])
        return observed({'selected_packet': selected, 'preview': preview,
            'application': receipt, 'canonical_store_written': False})
    schema = {'type': 'object', 'additionalProperties': False,
        'required': ['packet', 'diff', 'policy', 'explicitly_accepted'],
        'properties': {'packet': {'type': 'object'}, 'diff': {'type': 'object'},
                       'policy': {'type': 'object'}, 'explicitly_accepted': {'type': 'boolean'}}}
    return Tool('loom.graph.apply_diff', 'graph_packet/1', schema, {'type': 'object'},
        ('python_callback',), {'kind': 'derived_packet', 'canonical_write': False}, invoke,
        {'semantic_validation': 'existing_graph_packet_codec',
         'preserved': 'full_native_records_sources_provenance_history'})


def _captured(file, limit):
    file.flush()
    file.seek(0)
    h, size, prefix = hashlib.sha256(), 0, bytearray()
    while chunk := file.read(65536):
        h.update(chunk)
        size += len(chunk)
        prefix.extend(chunk[:max(0, limit - len(prefix))])
    return {'byte_count': size, 'sha256': h.hexdigest(),
        'prefix_base64': base64.b64encode(prefix).decode('ascii'),
        'capture_bytes': limit, 'truncated': size > limit,
        'lost': 'tail_bytes' if size > limit else None,
        'reversible': size <= limit, 'encoding': 'raw_bytes_base64_no_text_coercion'}


def _kill_group(process):
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def process_tool():
    def invoke(environment, arguments):
        config = environment.config
        timeout, capture = config['timeout_seconds'], config['capture_bytes']
        if type(timeout) not in (int, float) or not 0 < timeout < float('inf'):
            raise AgentError('positive_finite_process_timeout_required')
        if type(capture) is not int or capture < 0:
            raise AgentError('nonnegative_capture_limit_required')
        started = time.perf_counter_ns()
        with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
            process = subprocess.Popen(arguments['argv'], cwd=config['cwd'],
                env=deepcopy(config['env']), stdin=subprocess.DEVNULL,
                stdout=stdout, stderr=stderr, start_new_session=True)
            timed_out = False
            try:
                process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
            finally:
                # Includes children still in this process group after leader exit.
                # Detached descendants may escape; this is not OS isolation.
                _kill_group(process)
            result = {'returncode': process.returncode, 'timed_out': timed_out,
                'stdout': _captured(stdout, capture), 'stderr': _captured(stderr, capture),
                'wall_seconds': (time.perf_counter_ns() - started) / 1e9,
                'isolation': environment.isolation,
                'process_group_cleanup': 'killpg_no_detached_descendant_guarantee'}
        return observed(result, 'outcome_unknown' if timed_out else
                        'completed' if process.returncode == 0 else 'tool_error')
    schema = {'type': 'object', 'additionalProperties': False, 'required': ['argv'],
        'properties': {'argv': {'type': 'array', 'minItems': 1,
                              'items': {'type': 'string', 'pattern': '^[^\u0000]+$'}}}}
    return Tool('system.process', 'local-process/experimental-1', schema, {'type': 'object'},
        ('os_process',), {'kind': 'arbitrary_process', 'external_effects': 'caller_declared',
                          'retry': 'never_implicit'}, invoke,
        {'shell': False, 'working_directory_is_not_sandbox': True,
         'output_storage': 'temporary_disk_no_hard_disk_quota'})


class MCPStdio:
    """Synchronous stdio subset: no HTTP, notifications handling, tasks or auth.

    Experimental client, not a full MCP SDK. Unexpected frames fail visibly.
    Responses are retained as exact newline frames alongside parsed domain data.
    """
    version = '2025-11-25'

    def __init__(self, argv, *, cwd, env, timeout_seconds, max_frame_bytes):
        self.timeout, self.max_frame = timeout_seconds, max_frame_bytes
        if not (type(timeout_seconds) in (int, float) and 0 < timeout_seconds < float('inf')
                and type(max_frame_bytes) is int and max_frame_bytes > 0):
            raise AgentError('invalid_mcp_transport_limits')
        self.stderr_file = tempfile.TemporaryFile()
        self.closed = False
        self.process = subprocess.Popen(argv, cwd=cwd, env=env, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=self.stderr_file, start_new_session=True)
        os.set_blocking(self.process.stdin.fileno(), False)
        self.sequence = 0
        self.buffer = bytearray()
        self.transcript = []
        try:
            initialized = self.request('initialize', {'protocolVersion': self.version,
                'capabilities': {}, 'clientInfo': {'name': 'ecosystem-agent-experiment', 'version': '1'}})
            if initialized['protocolVersion'] != self.version or 'tools' not in initialized['capabilities']:
                raise AgentError('mcp_version_or_tools_capability_unavailable')
            self.server_info = deepcopy(initialized)
            self.notify('notifications/initialized', {})
            self.catalog = self.request('tools/list', {})
            if 'nextCursor' in self.catalog:
                raise AgentError('mcp_catalog_pagination_unsupported')
        except BaseException:
            self.close()
            raise

    def _send(self, body):
        digest(body)
        frame = (json.dumps(body, ensure_ascii=False, allow_nan=False) + '\n').encode('utf-8')
        if len(frame) > self.max_frame:
            raise AgentError('mcp_outbound_frame_limit')
        deadline = time.monotonic() + self.timeout
        position = 0
        with selectors.DefaultSelector() as selector:
            selector.register(self.process.stdin, selectors.EVENT_WRITE)
            while position < len(frame):
                if not selector.select(max(0, deadline - time.monotonic())):
                    raise AgentError('mcp_send_timeout_effect_unknown')
                try:
                    position += os.write(self.process.stdin.fileno(), frame[position:])
                except BlockingIOError:
                    continue
        self.transcript.append({'direction': 'sent', 'base64': base64.b64encode(frame).decode()})

    def _frame(self):
        deadline = time.monotonic() + self.timeout
        with selectors.DefaultSelector() as selector:
            selector.register(self.process.stdout, selectors.EVENT_READ)
            while b'\n' not in self.buffer:
                if not selector.select(max(0, deadline - time.monotonic())):
                    raise AgentError('mcp_timeout_effect_unknown')
                chunk = os.read(self.process.stdout.fileno(), 4096)
                if not chunk:
                    raise AgentError('mcp_eof_effect_unknown')
                self.buffer.extend(chunk)
                if len(self.buffer) > self.max_frame:
                    raise AgentError('mcp_frame_limit_exceeded')
            position = self.buffer.index(b'\n') + 1
            frame, self.buffer = bytes(self.buffer[:position]), self.buffer[position:]
            self.transcript.append({'direction': 'received', 'base64': base64.b64encode(frame).decode()})
            return frame

    def notify(self, method, params):
        self._send({'jsonrpc': '2.0', 'method': method, 'params': params})

    def request(self, method, params):
        self.sequence += 1
        self._send({'jsonrpc': '2.0', 'id': self.sequence, 'method': method, 'params': params})
        response = json.loads(self._frame().decode('utf-8'))
        if response.get('jsonrpc') != '2.0' or response.get('id') != self.sequence:
            raise AgentError('mcp_unexpected_frame')
        if 'error' in response:
            raise AgentError('mcp_protocol_error:' + json.dumps(response['error'], sort_keys=True))
        if 'result' not in response:
            raise AgentError('mcp_result_missing')
        digest(response['result'])
        return response['result']

    def tools(self, *, namespace):
        tools = []
        for descriptor in self.catalog['tools']:
            declared = deepcopy(descriptor)
            if 'outputSchema' not in declared:
                raise AgentError('mcp_output_schema_required_by_this_adapter')
            def invoke(environment, arguments, declared=declared):
                current = self.request('tools/list', {})
                matches = [t for t in current['tools'] if t['name'] == declared['name']]
                if len(matches) != 1 or digest(matches[0]) != digest(declared):
                    raise AgentError('mcp_tool_descriptor_changed_no_tool_call')
                result = self.request('tools/call', {'name': declared['name'], 'arguments': arguments})
                # Keep the complete protocol result; payload schema validation
                # applies only to successful structured domain output.
                return observed(result, 'tool_error' if result.get('isError', False) else 'completed')
            tools.append(Tool(namespace + '.' + declared['name'], digest(declared),
                declared['inputSchema'], {'type': 'object'}, ('mcp_stdio',),
                {'kind': 'remote_application', 'annotations_are_not_authorization': True}, invoke,
                {'mcp_descriptor': declared, 'server': self.server_info,
                 'structured_output_schema': declared['outputSchema']}))
        return tools

    def close(self):
        if self.closed:
            return
        self.closed = True
        _kill_group(self.process)
        self.stderr_capture = _captured(self.stderr_file, self.max_frame)
        for file in (self.process.stdin, self.process.stdout, self.stderr_file):
            if file:
                file.close()
