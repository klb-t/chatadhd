"""Explicitly invoked, configurable bounded frontier transport; offline by default.

Uses the frozen runner's write-ahead/no-retry/BYOK/key-cap mechanism with a private
function scope. No global monkeypatch, original-guard edit, automatic invocation,
archive read, or budget reset. Current synthetic/paid pilot remains deferred.
"""
from __future__ import annotations
import argparse
from contextlib import contextmanager
from copy import deepcopy
import types
from pathlib import Path

from .. import openrouter_runner as safe
from . import adapter


def runner_scope(manifest):
    adapter.plan_manifest(manifest)
    limits = manifest['metadata']['execution']['limits']
    scope = dict(vars(safe))
    scope.update(VERSION=adapter.VERSION, MAX_INPUT_BYTES=limits['max_input_bytes'],
                 MAX_BODY_BYTES=limits['max_body_bytes'], MAX_RESPONSE_BYTES=limits['max_response_bytes'],
                 TIMEOUT_SECONDS=limits['timeout_seconds'])
    for name, value in vars(safe).items():
        if isinstance(value, types.FunctionType) and value.__module__ == safe.__name__:
            cloned = types.FunctionType(value.__code__, scope, value.__name__, value.__defaults__, value.__closure__)
            cloned.__kwdefaults__ = deepcopy(value.__kwdefaults__)
            scope[name] = cloned
    # contextmanager wrappers close over the original function. Rebind their
    # underlying functions too, so configured deadline/size limits are effective.
    for name in ('_deadline', '_lock'):
        value = getattr(safe, name).__wrapped__
        underlying = types.FunctionType(value.__code__, scope, value.__name__, value.__defaults__, value.__closure__)
        scope[name] = contextmanager(underlying)
    scope['_money'] = adapter.money
    scope['plan_manifest'] = adapter.plan_manifest
    return scope


def inspect_key(manifest, *, transport_fn=None, key_loader=None):
    return runner_scope(manifest)['inspect_key'](manifest, transport_fn=transport_fn, key_loader=key_loader)


def run_manifest(manifest, run_dir, *, transport_fn=None, key_loader=None):
    return runner_scope(manifest)['run_manifest'](manifest, run_dir, transport_fn=transport_fn, key_loader=key_loader)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('plan', 'inspect-key', 'run'))
    parser.add_argument('manifest', type=Path); parser.add_argument('--run-dir', type=Path)
    args = parser.parse_args(argv); manifest = adapter.read(args.manifest)
    if args.command == 'plan':
        result = adapter.plan_manifest(manifest)
    elif args.command == 'inspect-key':
        result = inspect_key(manifest)
    else:
        if args.run_dir is None: parser.error('--run-dir is required')
        result = run_manifest(manifest, args.run_dir)
    print(safe.canonical(result).decode())
    return 2 if args.command == 'inspect-key' and not result['gate_valid'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
