// Pure G1 view/selector contract logic, shared by the GUI and dependency-free tests.
const object = (v) => v !== null && typeof v === "object" && !Array.isArray(v) ? v : null;
const hash = (v) => typeof v === "string" && /^[a-f0-9]{64}$/.test(v);

/** Upload receipts and materialized versions share a key+hash, never just a name/hash/fingerprint. */
export function textSources(rows) {
  const versions = new Map();
  for (const value of rows) {
    const row = object(value), meta = object(row?.metadata);
    if (!row || !meta || !["loom.client_upload", "loom.source_parts"].includes(String(row.parser)) ||
        typeof row.id !== "string" || typeof row.title !== "string" || !hash(row.blob_hash) ||
        typeof row.format !== "string" || typeof meta.source_key !== "string" || !meta.source_key) continue;
    const key = JSON.stringify([meta.source_key, row.blob_hash]);
    if (!versions.has(key) || row.parser === "loom.source_parts") versions.set(key, row);
  }
  return [...versions.values()];
}

export function readSourceParts(value, source) {
  const row = object(value);
  if (row?.schema !== "loom.source_text_parts/1" || row.source_id !== source.id || row.source_sha256 !== source.blob_hash ||
      row.coordinate !== "source_utf8_bytes" || typeof row.source_node !== "string" || typeof row.projection_node !== "string" ||
      typeof row.version_node !== "string" || !object(row.method) || typeof row.reason !== "string" || typeof row.status !== "string" ||
      !Array.isArray(row.occurrence_nodes) || row.occurrence_nodes.some(id => typeof id !== "string" || !id) ||
      new Set(row.occurrence_nodes).size !== row.occurrence_nodes.length) throw Error("text_source_projection_invalid");
  return row;
}

export function readSourceOccurrence(value, source, nodeId) {
  const row = object(value), original = object(row?.source), meta = object(row?.provenance);
  const range = meta?.byte_range;
  if (!row || row.node_id !== nodeId || typeof row.text !== "string" || original?.id !== source.id || original.blob_hash !== source.blob_hash ||
      meta?.schema !== "loom.source_text_occurrence/1" || meta.source_id !== source.id || meta.source_sha256 !== source.blob_hash ||
      meta.coordinate !== "source_utf8_bytes" || !Number.isSafeInteger(meta.index) || meta.index < 0 || !Array.isArray(range) || range.length !== 2 ||
      range.some(n => !Number.isSafeInteger(n) || n < 0) || range[1] < range[0] ||
      new TextEncoder().encode(row.text).length !== range[1] - range[0]) throw Error("text_source_occurrence_invalid");
  return row;
}

/** Read persisted native selection. A malformed value must not silently become an empty selection. */
export function sourceSelector(config) {
  const execution = config.context_execution === undefined ? {} : object(config.context_execution);
  const unified = execution?.unified === undefined ? {} : object(execution.unified);
  if (!execution || !unified || (unified.enabled !== undefined && typeof unified.enabled !== "boolean") ||
      (unified.legacy_seed_ids !== undefined && (!Array.isArray(unified.legacy_seed_ids) || unified.legacy_seed_ids.some(id => typeof id !== "string" || !id))))
    throw Error("text_source_selector_invalid");
  const hops = unified.legacy_relation_hops;
  if (hops !== undefined && (!Number.isSafeInteger(hops) || hops < 0)) throw Error("text_source_selector_invalid");
  return { enabled: unified.enabled === true, ids: (unified.legacy_seed_ids ?? []), hops: hops === undefined ? null : hops };
}

/** Merge only the native selector's fields. Other execution options remain the server's responsibility. */
export function sourceSelectionPatch(config, nodeId, include, hops) {
  if (!nodeId || (include && (!Number.isSafeInteger(hops) || hops < 0))) throw Error("text_source_selector_invalid");
  const { ids } = sourceSelector(config);
  return { unified: { ...(include ? { enabled: true, legacy_relation_hops: hops } : {}),
    legacy_seed_ids: include ? [...new Set([...ids, nodeId])] : ids.filter(id => id !== nodeId) } };
}

export function materializationRequest(source, language) {
  return { hash: source.blob_hash, source_key: source.metadata.source_key, name: source.title, format: source.format,
    language, ...(source.metadata.previous_source_id ? { previous_source_id: source.metadata.previous_source_id } : {}) };
}

/** Read/merge/readback of the existing account-wide selector, only after an explicit UI action. */
export async function saveSourceSelection(host, nodeId, include, hops) {
  if (!host.mergeConfigKey) throw Error("text_source_merge_unavailable");
  const fresh = await host.getConfig();
  await host.mergeConfigKey("context_execution", sourceSelectionPatch(fresh, nodeId, include, hops));
  const saved = await host.getConfig(), read = sourceSelector(saved);
  if (read.ids.includes(nodeId) !== include || (include && (!read.enabled || read.hops !== hops)))
    throw Error("text_source_selection_readback_mismatch");
  return saved;
}
