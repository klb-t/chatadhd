// W6 fix (CI 6ab55aae): a component that both reports a window parameter and follows it (the navigator's map: zoom, pan) must not take
// the echo of its own report for a value from elsewhere. The report comes back through React state; on a slow device the echo of an older
// report can be rendered together with a newer own change (React batches a continuous update with a pending default one), and following it
// threw the newer change away (the camera went back, then swapped between the two values). So the component keeps the values it reported
// and not yet heard back: a followed value equal to one of them is that echo (consumed with every older one, never applied); any other
// value comes from elsewhere (a binding, a shared channel, a restore after a reload) and is applied, and the queue is dropped.

/** At most this many own reports wait for their echo (preset; a report that never comes back - refused while a restored value waits for
 *  its first read, or equal to the value already there - must not grow the queue without bound). */
export const ECHO_QUEUE_LIMIT_PRESET = 32;

/** The same parameter value: numbers within 1e-9 (a value that went through a binding's arithmetic), objects and arrays field by field. */
export function sameParamValue(a: unknown, b: unknown): boolean {
  if (typeof a === "number" && typeof b === "number") return a === b || Math.abs(a - b) <= 1e-9;
  if (a && b && typeof a === "object" && typeof b === "object") {
    if (Array.isArray(a) !== Array.isArray(b)) return false;
    const ka = Object.keys(a as object), kb = Object.keys(b as object);
    return ka.length === kb.length && ka.every(k => Object.prototype.hasOwnProperty.call(b, k) && sameParamValue((a as Record<string, unknown>)[k], (b as Record<string, unknown>)[k]));
  }
  return Object.is(a, b);
}

/** An own report of `value` (call before the write). `current` = the value the component reads now: a report equal to it changes
 *  nothing, so no echo will come and nothing is queued. */
export function wrote(queue: readonly unknown[], value: unknown, current: unknown, limit = ECHO_QUEUE_LIMIT_PRESET): unknown[] {
  if (sameParamValue(value, current) && queue.length === 0) return [];
  const next = [...queue, value];
  return next.length > Math.max(1, limit) ? next.slice(next.length - Math.max(1, limit)) : next;
}

/** A followed value: `apply` = false for the echo of an own report (consumed together with the older reports still waiting); `apply` =
 *  true for a value from elsewhere (the queue is dropped: the reports before it are overtaken). */
export function heard(queue: readonly unknown[], value: unknown): { apply: boolean; queue: unknown[] } {
  for (let i = queue.length - 1; i >= 0; i--) if (sameParamValue(queue[i], value)) return { apply: false, queue: queue.slice(i + 1) };
  return { apply: true, queue: [] };
}
