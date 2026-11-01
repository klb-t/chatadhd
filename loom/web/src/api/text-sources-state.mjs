// Pure G1 view/selector contract logic, shared by the GUI and dependency-free tests.
const object = (v) => v !== null && typeof v === "object" && !Array.isArray(v) ? v : null;
const hash = (v) => typeof v === "string" && /^[a-f0-9]{64}$/.test(v);

/** Validate the descriptor's two representations without interpreting ZIP bytes as text. */
export function sourceDerivation(source) {
  const meta = object(source?.metadata), value = meta?.derivation;
  if (value === undefined) {
    if (meta?.coordinate === "derived_utf8_bytes" || (source?.format === "docx" && source.parser === "loom.source_parts"))
      throw Error("text_source_derivation_invalid");
    return null;
  }
  const d = object(value);
  if (source.format !== "docx" || source.parser !== "loom.source_parts" || meta.coordinate !== "derived_utf8_bytes" ||
      d?.schema !== "loom.docx_source_derivation/1" || !hash(d.original_sha256) ||
      d.original_source_id !== "src_docx_" + d.original_sha256 || d.text_sha256 !== source.blob_hash ||
      !hash(d.extraction_blob) || !hash(d.transform_sha256) || !object(d.transform) ||
      !Number.isSafeInteger(d.original_size) || d.original_size < 0) throw Error("text_source_derivation_invalid");
  return d;
}

export function sourceOriginalHash(source) {
  return sourceDerivation(source)?.original_sha256 ?? source.blob_hash;
}
const versionKey = source => JSON.stringify([source.metadata.source_key, source.format, sourceOriginalHash(source)]);
const sameDerivation = (a, b) => {
  // Compare JSON values independently of transport property ordering.
  if (Object.is(a, b)) return true;
  if (Array.isArray(a) && Array.isArray(b)) return a.length === b.length && a.every((v, i) => sameDerivation(v, b[i]));
  if (!object(a) || !object(b)) return false;
  const keys = Object.keys(a);
  return keys.length === Object.keys(b).length && keys.every(key => sameDerivation(a[key], b[key]));
};

/** Hide upload receipts only when their key + original hash has a materialized Source.
 * DOCX versions with equal derived text (or different extraction receipts) remain distinct. */
export function textSources(rows) {
  const valid = rows.filter(value => {
    const row = object(value), meta = object(row?.metadata);
    if (!row || !meta || !["loom.client_upload", "loom.source_parts"].includes(row.parser) ||
        typeof row.id !== "string" || !row.id || typeof row.title !== "string" || !hash(row.blob_hash) ||
        typeof row.format !== "string" || typeof meta.source_key !== "string" || !meta.source_key ||
        (meta.previous_source_id !== undefined && typeof meta.previous_source_id !== "string")) return false;
    try { sourceDerivation(row); return true; } catch { return false; }
  });
  const made = new Set(valid.filter(s => s.parser === "loom.source_parts").map(versionKey));
  const versions = new Map();
  for (const row of valid) {
    if (row.parser === "loom.client_upload" && made.has(versionKey(row))) continue;
    const key = sourceDerivation(row) ? row.id : versionKey(row);
    if (!versions.has(key) || row.parser === "loom.source_parts") versions.set(key, row);
  }
  return [...versions.values()];
}

export function readSourceParts(value, source) {
  const row = object(value), d = sourceDerivation(source);
  if (row?.schema !== "loom.source_text_parts/1" || row.source_id !== source.id || row.source_sha256 !== source.blob_hash ||
      row.coordinate !== (d ? "derived_utf8_bytes" : "source_utf8_bytes") || typeof row.source_node !== "string" || !row.source_node ||
      typeof row.projection_node !== "string" || !row.projection_node || typeof row.version_node !== "string" || !row.version_node ||
      !object(row.method) || typeof row.reason !== "string" || typeof row.status !== "string" ||
      !Array.isArray(row.occurrence_nodes) || row.occurrence_nodes.some(id => typeof id !== "string" || !id) ||
      new Set(row.occurrence_nodes).size !== row.occurrence_nodes.length ||
      (d ? (!sameDerivation(row.derivation, d) || !sameDerivation(row.method.derivation, d) ||
        !Array.isArray(row.warnings) || row.warnings.some(w => typeof w !== "string") ||
        row.original_source_node !== "n_" + d.original_source_id || !Array.isArray(row.observations) || row.observations.some((block, i) => {
          const b = object(block), attrs = object(b?.attrs), loc = object(b?.locator);
          return b?.ordinal !== i || typeof b?.text !== "string" || typeof attrs?.recognized_kind !== "string" ||
            typeof attrs.original_locator !== "string" || attrs.coordinate !== "derived_utf8_bytes" || attrs.evidence_class !== "derived" ||
            loc?.source !== source.id || !Number.isSafeInteger(loc.byte_start) || loc.byte_start < 0 ||
            loc.byte_len !== new TextEncoder().encode(b.text).length;
        })) : row.derivation !== undefined))
    throw Error("text_source_projection_invalid");
  return row;
}

export function readSourceOccurrence(value, source, nodeId) {
  const row = object(value), descriptor = object(row?.source), meta = object(row?.provenance), d = sourceDerivation(source);
  const range = meta?.byte_range;
  if (!row || row.node_id !== nodeId || typeof row.text !== "string" || descriptor?.id !== source.id || descriptor.blob_hash !== source.blob_hash ||
      descriptor.metadata?.source_key !== source.metadata.source_key ||
      !sameDerivation(sourceDerivation(descriptor), d) ||
      meta?.schema !== "loom.source_text_occurrence/1" || meta.source_id !== source.id || meta.source_sha256 !== source.blob_hash ||
      meta.coordinate !== (d ? "derived_utf8_bytes" : "source_utf8_bytes") ||
      (d ? !sameDerivation(meta.derivation, d) : meta.derivation !== undefined) ||
      !Number.isSafeInteger(meta.index) || meta.index < 0 || !Array.isArray(range) || range.length !== 2 ||
      range.some(n => !Number.isSafeInteger(n) || n < 0) || range[1] < range[0] ||
      new TextEncoder().encode(row.text).length !== range[1] - range[0]) throw Error("text_source_occurrence_invalid");
  return row;
}

/** Bind a materialization response to this upload/version, never a same-text neighbour. */
export function materializedSource(rows, result, input) {
  const made = textSources(rows).find(s => s.id === object(result)?.source_id && s.parser === "loom.source_parts");
  if (!made || made.metadata.source_key !== input.metadata.source_key || made.format !== input.format ||
      sourceOriginalHash(made) !== sourceOriginalHash(input) ||
      (made.metadata.previous_source_id ?? "") !== (input.metadata.previous_source_id ?? ""))
    throw Error("text_source_descriptor_missing");
  readSourceParts(result, made);
  return made;
}

/** Synchronous guard: repeated clicks share no write; invalidated completions cannot update a reopened panel. */
export function createSourceTaskGate() {
  let current = null;
  return {
    begin() {
      if (current) return null;
      const ticket = {};
      current = ticket;
      return { isCurrent: () => current === ticket, finish: () => { if (current === ticket) current = null; } };
    },
    invalidate() { current = null; }
  };
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
  return { hash: sourceOriginalHash(source), source_key: source.metadata.source_key, name: source.title, format: source.format,
    language, ...(source.metadata.previous_source_id ? { previous_source_id: source.metadata.previous_source_id } : {}) };
}

/** Read/merge/readback of the existing account-wide selector, only after an explicit UI action. */
export async function saveSourceSelection(host, nodeId, include, hops, isCurrent = () => true) {
  if (!host.mergeConfigKey) throw Error("text_source_merge_unavailable");
  const fresh = await host.getConfig();
  if (!isCurrent()) throw Error("text_source_selection_stale");
  await host.mergeConfigKey("context_execution", sourceSelectionPatch(fresh, nodeId, include, hops));
  const saved = await host.getConfig(), read = sourceSelector(saved);
  if (read.ids.includes(nodeId) !== include || (include && (!read.enabled || read.hops !== hops)))
    throw Error("text_source_selection_readback_mismatch");
  return saved;
}
