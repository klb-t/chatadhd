#!/usr/bin/env node
/** Sparse, on-demand address spaces; no N-object array, DB or parser is created. */
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { performance } from 'node:perf_hooks';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
import { createModuleLoader, reportRoot } from './test-loader.mjs';

export function sparseAdapter(size, topology = 'chain') {
  const stats = { resolved: 0, neighborCalls: 0, relationObjects: 0 };
  const source = `benchmark:${topology}:${size}`;
  const ref = index => ({ source, selector: `/objects/${index}`, canonicalId: `${source}:${index}` });
  const indexOf = object => Number(object.selector.split('/').at(-1));
  return {
    stats, ref,
    descriptor: {
      id: source, label: `Synthetic lazy ${topology} (${size} objects)`,
      structures: [{ id: topology, label: topology, relations: ['linked'], directions: ['both'] }],
    },
    async resolve(object) {
      stats.resolved++;
      const index = indexOf(object);
      assert.ok(Number.isSafeInteger(index) && index >= 0 && index < size);
      return { ref: object, label: `Object ${index}`, kind: 'benchmark_object', status: 'available', evidence: 'source', properties: { index } };
    },
    async neighbors(object, request) {
      stats.neighborCalls++;
      const index = indexOf(object), start = Number(request.cursor ?? 0);
      let total, edgeAt;
      if (topology === 'star') {
        total = index === 0 ? size - 1 : 1;
        edgeAt = offset => index === 0 ? [0, offset + 1] : [0, index];
      } else {
        const entries = [];
        if (index > 0) entries.push([index - 1, index]);
        if (index < size - 1) entries.push([index, index + 1]);
        total = entries.length; edgeAt = offset => entries[offset];
      }
      const end = Math.min(total, start + request.limit);
      const relations = [];
      for (let cursor = start; cursor < end; cursor++) {
        const [from, to] = edgeAt(cursor);
        const relation = { id: `${source}:edge:${from}:${to}`, from: ref(from), to: ref(to), kind: 'linked', evidence: 'source' };
        const allowed = request.direction === 'both' || (request.direction === 'outgoing' ? from === index : to === index);
        if (allowed && (request.relations.length === 0 || request.relations.includes('linked'))) relations.push(relation);
      }
      stats.relationObjects += relations.length;
      return { relations, total, ...(end < total ? { nextCursor: String(end) } : {}) };
    },
  };
}

function planFor(adapter, index, topology, budgets) {
  const focus = adapter.ref(index);
  return {
    schema: 'loom.graph_perspective_plan/1', perspectiveId: `benchmark:${topology}`,
    focus, structure: topology, relations: ['linked'], direction: 'both',
    hops: topology === 'star' ? 2 : 8, resolution: 'detail', localResolution: [],
    compareSnapshots: [], evidence: [], ...budgets, visual: [], values: {}, explanation: [], unsupported: [], errors: [],
    sourcePerspective: { schema: 'loom.graph_perspective/1', id: `benchmark:${topology}`, focus, components: {}, analysis: { selected: [] } },
  };
}

export async function runBenchmark(selectPerspective, {
  sizes = (process.env.GP_BENCHMARK_SIZES ?? '10000,100000').split(',').map(Number),
  queryBudget = Number(process.env.GP_QUERY_BUDGET ?? 256),
  renderBudget = Number(process.env.GP_RENDER_BUDGET ?? 80),
  pageSize = Number(process.env.GP_PAGE_SIZE ?? 32),
  onSelection,
} = {}) {
  for (const value of [...sizes, queryBudget, renderBudget, pageSize]) assert.ok(Number.isSafeInteger(value) && value > 0);
  const permission = { id: 'benchmark:read-local-synthetic', canRead: () => true };
  const budgets = { queryBudget, renderBudget, pageSize };
  const warm = sparseAdapter(100);
  await selectPerspective(planFor(warm, 50, 'chain', budgets), [warm], permission);
  const rows = [];
  for (const size of sizes) {
    for (const topology of ['chain', 'star']) {
      const adapter = sparseAdapter(size, topology);
      for (const focusIndex of [0, Math.floor(size / 2), size - 1]) {
        globalThis.gc?.();
        const memoryBefore = process.memoryUsage();
        const before = { ...adapter.stats };
        const start = performance.now();
        const result = await selectPerspective(planFor(adapter, focusIndex, topology, budgets), [adapter], permission);
        const wallMs = performance.now() - start;
        const memoryAfter = process.memoryUsage();
        const materialized = adapter.stats.resolved - before.resolved;
        assert.ok(result.objects.some(object => object.ref.selector === `/objects/${focusIndex}`), 'focused object remains visible');
        assert.ok(materialized <= queryBudget, 'query budget must bound materialization');
        assert.ok(result.objects.length <= renderBudget, 'render budget must bound projection');
        assert.equal(result.metrics.resolvedObjects, materialized, 'measured materialization agrees with engine metric');
        if (topology === 'star') {
          assert.equal(result.complete, false, 'large connected source must explicitly report incomplete selection');
          assert.ok(result.omissions.some(row => row.reason === 'query_budget' || row.reason === 'render_budget'), 'no silent budget truncation');
          assert.ok(result.continuation.length > 0, 'unvisited source must remain addressable by continuation');
        }
        const row = {
          size, topology, focusIndex, theoreticalEdges: size - 1, wallMs, ...result.metrics,
          adapterResolvedObjects: materialized, adapterRelationObjects: adapter.stats.relationObjects - before.relationObjects,
          adapterNeighborCalls: adapter.stats.neighborCalls - before.neighborCalls,
          graphNodes: result.graph.nodes.length, graphEdges: result.graph.edges.length,
          heapDeltaBytes: memoryAfter.heapUsed - memoryBefore.heapUsed,
          rssDeltaBytes: memoryAfter.rss - memoryBefore.rss,
          omissions: result.omissions.reduce((counts, item) => ({ ...counts, [item.reason]: (counts[item.reason] ?? 0) + (item.count ?? 1) }), {}),
          continuations: result.continuation.length, complete: result.complete,
        };
        if (onSelection) row.presentation = await onSelection(result, row);
        rows.push(row);
      }
    }
  }
  return {
    schema: 'loom.graph_perspectives_benchmark/1', generatedAt: new Date().toISOString(),
    provenance: 'Synthetic sparse address spaces, generated lazily by an adapter; measured production selectPerspective implementation.',
    runtime: { node: process.version, platform: process.platform, arch: process.arch, exposedGc: !!globalThis.gc },
    budgets, rows,
    measurement: {
      selection: 'SelectionResult.metrics.selectionMs; wallMs adds the async caller boundary. Includes projection, excludes renderer and layout.',
      materialization: 'resolve calls counted independently by the adapter; no up-front node list or index is built.',
      memory: 'Process heap/RSS deltas per query; GC/JIT/allocator effects make signed values non-comparable as exact retained bytes.',
      layout: onSelection ? 'Measured separately by browser presentation callback.' : 'Not executed in headless benchmark; browser evidence records real renderer timing separately.',
      render: onSelection ? 'Measured separately by browser presentation callback.' : 'Not executed in headless benchmark; browser evidence records real renderer timing separately.',
    },
  };
}

async function main() {
  const loader = await createModuleLoader();
  try {
    const { selectPerspective } = await loader.load('src/graph-perspectives/select.ts');
    const result = await runBenchmark(selectPerspective);
    mkdirSync(reportRoot, { recursive: true });
    writeFileSync(path.join(reportRoot, 'benchmark.json'), JSON.stringify(result, null, 2) + '\n');
    console.log(`[graph-perspectives-benchmark] ${result.rows.length} sparse focus changes passed; ${result.rows.map(row => `${row.size}/${row.topology}/${row.focusIndex}=${row.selectionMs.toFixed(2)}ms`).join(', ')}`);
  } finally { await loader.close(); }
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) await main();
