"""Separate synthetic application fixture speaking the tested MCP stdio subset.

No model, secret, real thermostat, Watchdog deployment or external endpoint.
"""
import argparse
import json
import sys


def descriptor():
    return {'name': 'temperature', 'description': 'Synthetic typed temperature reading',
        'inputSchema': {'type': 'object', 'additionalProperties': False,
            'required': ['sensor'], 'properties': {'sensor': {'const': 'fixture'}}},
        'outputSchema': {'type': 'object', 'additionalProperties': False,
            'required': ['value', 'unit', 'source'], 'properties': {
                'value': {'type': 'number'}, 'unit': {'const': 'degC'},
                'source': {'const': 'synthetic_fixture'}}},
        'annotations': {'readOnlyHint': True}, 'x-domain': {'quantity': 'temperature'}}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', default='normal', choices=['normal', 'bad-output',
        'tool-error', 'bad-version', 'no-tools', 'change-catalog', 'protocol-error', 'silent'])
    options = parser.parse_args()
    initialized, lists = False, 0
    for line in sys.stdin.buffer:
        request = json.loads(line)
        method = request['method']
        if 'id' not in request:
            if method == 'notifications/initialized':
                initialized = True
            continue
        if options.mode == 'silent':
            import time
            time.sleep(30)
            continue
        if method == 'initialize':
            result = {'protocolVersion': 'wrong' if options.mode == 'bad-version' else '2025-11-25',
                'capabilities': {} if options.mode == 'no-tools' else {'tools': {}},
                'serverInfo': {'name': 'synthetic-temperature-app', 'version': '1'}}
        elif not initialized:
            raise RuntimeError('client_did_not_send_initialized')
        elif method == 'tools/list':
            lists += 1
            tool = descriptor()
            if options.mode == 'change-catalog' and lists > 1:
                tool['outputSchema']['properties']['unit']['const'] = 'K'
            result = {'tools': [tool]}
        elif method == 'tools/call':
            if options.mode == 'protocol-error':
                response = {'jsonrpc': '2.0', 'id': request['id'],
                    'error': {'code': -32602, 'message': 'fixture protocol error'}}
                print(json.dumps(response), flush=True)
                continue
            value = {'value': 21.5, 'unit': 'K' if options.mode == 'bad-output' else 'degC',
                     'source': 'synthetic_fixture'}
            result = {'content': [{'type': 'text', 'text': json.dumps(value)}],
                'structuredContent': value, 'isError': options.mode == 'tool-error'}
        else:
            raise RuntimeError('fixture_unsupported_method')
        print(json.dumps({'jsonrpc': '2.0', 'id': request['id'], 'result': result}), flush=True)


if __name__ == '__main__':
    main()
