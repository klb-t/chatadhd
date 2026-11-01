"""Renderer-independent two-lane parameter controller.

Fast lane accepts an explicit library/surrogate parameter vector, never computes
embeddings or invokes the slow evaluator. Slow work runs on one bounded thread:
one active request + one replaceable pending request. No conversation text logged.
"""
from dataclasses import dataclass
from threading import Condition, Lock, Thread
import hashlib
import json
import math
import time


def vector(values):
    result = tuple(float(v) for v in values)
    if len(result) != 9 or not all(math.isfinite(v) for v in result):
        raise ValueError('Expected nine finite expression coordinates')
    # Renderer defaults use six signed and three nonnegative coordinates.
    for i, v in enumerate(result):
        low = 0 if i in (4, 5, 8) else -1
        if not low <= v <= 1:
            raise ValueError('Expression coordinate outside renderer domain')
    return result


@dataclass(frozen=True)
class Work:
    version: int
    context_id: str
    initial: tuple
    goal: tuple
    cache_key: str


@dataclass(frozen=True)
class Result:
    params: tuple
    loss: float
    evidence: str

    def __post_init__(self):
        object.__setattr__(self, 'params', vector(self.params))
        if not math.isfinite(self.loss) or self.loss < 0:
            raise ValueError('Invalid objective loss')


class ResultCache:
    """In-memory immutable records; the lock never covers rendering or I/O.

    Cache content has no current context/version. Applying a cache hit always
    passes the controller version gate. Bounded memory; insert order eviction.
    """
    def __init__(self, capacity=64):
        if capacity < 1:
            raise ValueError('Cache capacity must be positive')
        self.capacity = capacity
        self._lock = Lock()
        self._items = {}

    def get(self, key):
        with self._lock:
            return self._items.get(key)

    def put(self, key, result):
        if not isinstance(result, Result):
            raise TypeError('Cache only accepts validated immutable Result')
        with self._lock:
            if key in self._items:
                # Do not silently replace an identical-key conflicting result.
                if self._items[key] != result:
                    raise ValueError('Conflicting result for pinned cache key')
                return
            if len(self._items) >= self.capacity:
                del self._items[next(iter(self._items))]
            self._items[key] = result

    def snapshot(self):
        with self._lock:
            return dict(self._items)


class TwoLaneController:
    def __init__(self, neutral, evaluator, evaluator_pin, *, transition_seconds=.12, cache=None):
        self._condition = Condition()
        self._version = 0
        self._context_id = ''
        self._target = vector(neutral)
        self._displayed = self._target
        self._transition_from = self._target
        self._transition_start = time.perf_counter()
        if transition_seconds <= 0:
            raise ValueError('Transition duration must be positive')
        self._duration = transition_seconds
        self._source = 'neutral'
        self._pending = None
        self._active = None
        self._closed = False
        self._evaluator = evaluator
        self._pin = str(evaluator_pin)
        self.cache = cache if cache is not None else ResultCache()
        self._stats = dict(submitted=0, coalesced=0, started=0, accepted=0,
                           stale_discarded=0, cancelled=0, cache_hits=0, errors=0)
        self._events = []
        self._thread = Thread(target=self._worker, name='face-slow-lane', daemon=False)
        self._thread.start()

    def _event(self, kind, version):
        # Small bounded operational ring, no private context identifiers/text.
        self._events.append(dict(kind=kind, version=version, at=time.perf_counter()))
        if len(self._events) > 256:
            del self._events[:128]

    def _animate(self, now):
        t = max(0., min(1., (now-self._transition_start)/self._duration))
        t = t*t*(3-2*t)
        return tuple(a+(b-a)*t for a,b in zip(self._transition_from, self._target))

    def _retarget(self, params, source):
        now = time.perf_counter()
        self._transition_from = self._animate(now)
        self._target = params
        self._transition_start = now
        self._source = source

    def submit(self, context_id, fast_target, slow_goal):
        # Validation/hash happen before locking. No cache, evaluator, or joins.
        fast_target, slow_goal = vector(fast_target), vector(slow_goal)
        if not isinstance(context_id, str):
            raise TypeError('Context id must be opaque string')
        pin = json.dumps(dict(evaluator=self._pin, initial=fast_target, goal=slow_goal),
                         sort_keys=True, separators=(',', ':')).encode()
        key = hashlib.sha256(pin).hexdigest()
        with self._condition:
            if self._closed:
                raise RuntimeError('Controller closed')
            self._version += 1
            self._context_id = context_id
            if self._pending is not None:
                self._stats['coalesced'] += 1
                self._event('coalesced', self._pending.version)
            work = Work(self._version, context_id, fast_target, slow_goal, key)
            self._pending = work
            self._retarget(fast_target, 'fast_library_explicit_parameters')
            self._stats['submitted'] += 1
            self._event('submitted', work.version)
            self._condition.notify_all()
            return work.version

    def invalidate(self):
        """Cancel pending and active work by epoch, retaining current display.

        Noncooperative native calls can finish, but can no longer commit.
        """
        with self._condition:
            self._version += 1
            self._context_id = ''
            self._pending = None
            self._retarget(self._animate(time.perf_counter()), 'invalidated_hold')
            self._event('invalidated', self._version)
            self._condition.notify_all()
            return self._version

    def tick(self):
        with self._condition:
            self._displayed = self._animate(time.perf_counter())
            return dict(version=self._version, params=self._displayed,
                        target=self._target, source=self._source)

    def _is_cancelled(self, work):
        with self._condition:
            return self._closed or work.version != self._version

    def _worker(self):
        while True:
            with self._condition:
                self._condition.wait_for(lambda: self._closed or self._pending is not None)
                if self._closed:
                    return
                work = self._pending
                self._pending = None
                self._active = work.version
                self._stats['started'] += 1
                self._event('started', work.version)
            # Lock is never held during cache access/evaluation/native rendering.
            result = None
            error = False
            try:
                result = self.cache.get(work.cache_key)
                if result is not None:
                    with self._condition:
                        self._stats['cache_hits'] += 1
                else:
                    result = self._evaluator(work, lambda: self._is_cancelled(work))
                    if result is not None:
                        if not isinstance(result, Result):
                            raise TypeError('Evaluator returned invalid type')
                        self.cache.put(work.cache_key, result)
            except Exception:
                # Do not leak evaluator exception payload or private context.
                error = True
            with self._condition:
                self._active = None
                if error:
                    self._stats['errors'] += 1
                    self._event('error_sanitized', work.version)
                elif work.version != self._version or self._closed:
                    self._stats['stale_discarded'] += 1
                    self._event('stale_discarded', work.version)
                elif result is None:
                    self._stats['cancelled'] += 1
                    self._event('cancelled', work.version)
                else:
                    self._retarget(result.params, 'slow_synthetic_objective')
                    self._stats['accepted'] += 1
                    self._event('accepted', work.version)
                self._condition.notify_all()

    def wait_idle(self, timeout=10.):
        # An explicit test/shutdown helper. Never call from conversation/UI tick.
        with self._condition:
            return self._condition.wait_for(lambda:self._active is None and self._pending is None, timeout)

    def status(self):
        with self._condition:
            return dict(version=self._version, active=self._active,
                        pending=None if self._pending is None else self._pending.version,
                        stats=self._stats.copy(), events=list(self._events))

    def close(self, timeout=10.):
        with self._condition:
            self._closed = True
            self._pending = None
            self._condition.notify_all()
        self._thread.join(timeout)
        return not self._thread.is_alive()
