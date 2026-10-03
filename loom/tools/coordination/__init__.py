"""Opt-in, local cross-process coordination for injected task executors."""

from .leases import CoordinationError, LeaseStore, execute_once

__all__ = ["CoordinationError", "LeaseStore", "execute_once"]
