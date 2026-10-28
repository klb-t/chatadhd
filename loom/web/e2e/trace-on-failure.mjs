// Test infrastructure only: a Playwright trace (DOM snapshots, screenshots, actions, network) kept only when a suite fails, so the state
// of the page at the moment of a failed step can be opened later (`npx playwright show-trace <zip>`). Any e2e script can opt in:
//   const trace = await traceOnFailure(context, evidenceDir, 'suite-name');   ...   await trace.finish(failure);
// Env (presets): LOOM_E2E_TRACE=0 turns it off; LOOM_E2E_TRACE=always keeps the trace of a green run too.
import path from 'node:path';

export async function traceOnFailure(context, dir, name) {
  const mode = process.env.LOOM_E2E_TRACE ?? 'on-failure';
  if (mode === '0' || mode === 'off') return { async finish() { return null; } };
  try { await context.tracing.start({ name, screenshots: true, snapshots: true, sources: false }); }
  catch (e) { console.warn(`[e2e] trace not started for ${name}: ${e.message}`); return { async finish() { return null; } }; }
  return {
    async finish(failure) {
      const keep = Boolean(failure) || mode === 'always';
      const file = path.join(dir, `${name}-trace.zip`);
      try {
        if (keep) { await context.tracing.stop({ path: file }); console.log(`[e2e] trace kept: ${file}`); return file; }
        await context.tracing.stop(); return null;
      } catch (e) { console.warn(`[e2e] trace not saved for ${name}: ${e.message}`); return null; }
    },
  };
}
