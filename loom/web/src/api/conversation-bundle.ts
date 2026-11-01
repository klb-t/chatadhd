import type { ActionsApi, LoomApi } from "./loom-api";

/** The native bundle path, not loom.guest_bundle or the generic auto/provider import. */
type BundleCommand =
  | { operation: "bundle_export"; conversation_id: string; allow_incomplete: boolean }
  | { operation: "bundle_import"; hash: string; expected_archive_sha256: string; allow_incomplete: boolean };
type Host = Pick<LoomApi, "actions">;
interface Options { allowIncomplete: boolean; signal?: AbortSignal }
export interface BundleExportReceipt {
  schema: "loom.bundle_native/1";
  hash: string;
  manifest_sha256: string;
  external_pin: string;
  complete_assets: boolean;
  lossless_database: false;
}
export interface BundleRestoreReceipt {
  schema: "loom.bundle_native/1";
  archive_sha256: string;
  manifest_sha256: string;
  conversation_id: string;
  message_mapping: Record<string, string>;
  attachment_slots: number;
  verified_slots: number;
  status: "INCOMPLETE" | "assets_verified";
  complete_assets: boolean;
  lossless_database: false;
  limits: string[];
}

function requireValue(condition: unknown, code = "bundle_response_invalid"): asserts condition {
  if (!condition) throw new Error(code);
}
function object(value: unknown): Record<string, unknown> {
  requireValue(value && typeof value === "object" && !Array.isArray(value));
  const row = value as Record<string, unknown>;
  if (row.error && typeof row.error === "object" && "message" in row.error) throw new Error(String(row.error.message));
  return row;
}
export function isBundleHash(value: unknown): value is string {
  return typeof value === "string" && /^[a-f0-9]{64}$/.test(value);
}
function current(signal?: AbortSignal): void {
  if (signal?.aborted) throw new DOMException("bundle_wait_aborted", "AbortError");
}
function header(value: unknown): Record<string, unknown> {
  const row = object(value);
  requireValue(row.schema === "loom.bundle_native/1" && row.lossless_database === false &&
    isBundleHash(row.manifest_sha256) && typeof row.complete_assets === "boolean");
  return row;
}
export function readBundleExport(value: unknown): BundleExportReceipt {
  const row = header(value);
  requireValue(isBundleHash(row.hash) && row.external_pin === row.hash);
  return row as unknown as BundleExportReceipt;
}
export function readBundleRestore(value: unknown, archiveHash: string): BundleRestoreReceipt {
  const row = header(value), mapping = object(row.message_mapping);
  requireValue(isBundleHash(archiveHash) && row.archive_sha256 === archiveHash &&
    typeof row.conversation_id === "string" && row.conversation_id.trim() !== "");
  requireValue(Number.isSafeInteger(row.attachment_slots) && Number(row.attachment_slots) >= 0 &&
    Number.isSafeInteger(row.verified_slots) && Number(row.verified_slots) >= 0 &&
    Number(row.verified_slots) <= Number(row.attachment_slots));
  requireValue(row.complete_assets === (row.verified_slots === row.attachment_slots) &&
    row.status === (row.complete_assets ? "assets_verified" : "INCOMPLETE"));
  const ids = new Set<string>();
  for (const [pointer, id] of Object.entries(mapping)) {
    requireValue(/^\/messages\/(0|[1-9][0-9]*)$/.test(pointer) && typeof id === "string" && id.trim() !== "" && !ids.has(id));
    ids.add(id);
  }
  requireValue(Array.isArray(row.limits) && row.limits.every(limit => typeof limit === "string"));
  return row as unknown as BundleRestoreReceipt;
}

/** The existing HTTP and JNI clientBlobText functions forward JSON unchanged.
 * Their public TS signature covers G1 only; add the already implemented native
 * operation overload locally (source_parts.cpp -> bundle_transport.cpp).
 * Do not route these commands through actions.run or a guessed export URL. */
async function command(host: Host, request: BundleCommand, signal?: AbortSignal): Promise<unknown> {
  type Dispatch = NonNullable<ActionsApi["clientBlobText"]> & ((request: BundleCommand) => Promise<unknown>);
  const dispatch = host.actions?.clientBlobText as Dispatch | undefined;
  requireValue(dispatch, "bundle_transport_unavailable");
  current(signal);
  const result = await dispatch(request);
  // These host calls have no transport AbortSignal. A dispatched native write
  // can finish after closing the view; abort only prevents later client steps.
  current(signal);
  return result;
}
export function bundleCapabilities(host: Host): { export: boolean; restore: boolean } {
  return {
    export: typeof host.actions?.clientBlobText === "function" && typeof host.actions?.clientBlobFetch === "function",
    restore: typeof host.actions?.clientBlobText === "function" && typeof host.actions?.clientBlobUpload === "function",
  };
}
export async function bundleFileHash(blob: Blob, signal?: AbortSignal): Promise<string> {
  current(signal);
  requireValue(typeof crypto !== "undefined" && crypto.subtle, "bundle_hash_unavailable");
  const bytes = await blob.arrayBuffer();
  current(signal);
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  current(signal);
  return Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, "0")).join("");
}
export async function exportConversationBundle(host: Host, conversationId: string, options: Options): Promise<{
  receipt: BundleExportReceipt; blob: Blob;
}> {
  requireValue(conversationId.trim() !== "", "bundle_conversation_required");
  requireValue(bundleCapabilities(host).export, "bundle_transport_unavailable");
  const receipt = readBundleExport(await command(host, {
    operation: "bundle_export", conversation_id: conversationId, allow_incomplete: options.allowIncomplete,
  }, options.signal));
  requireValue(options.allowIncomplete || receipt.complete_assets, "bundle_restore_incomplete");
  current(options.signal);
  const blob = await host.actions!.clientBlobFetch!(receipt.hash);
  requireValue(await bundleFileHash(blob, options.signal) === receipt.hash, "bundle_download_hash_mismatch");
  current(options.signal);
  return { receipt, blob };
}
export async function restoreConversationBundle(host: Host, file: File, options: Options & {
  expectedSha256?: string; onImportDispatched?: () => void;
}): Promise<BundleRestoreReceipt> {
  requireValue(bundleCapabilities(host).restore, "bundle_transport_unavailable");
  const expected = options.expectedSha256?.trim() || undefined;
  requireValue(expected === undefined || isBundleHash(expected), "bundle_expected_hash_invalid");
  const hash = await bundleFileHash(file, options.signal);
  requireValue(expected === undefined || expected === hash, "bundle_file_hash_mismatch");
  current(options.signal);
  const uploaded = await host.actions!.clientBlobUpload!(file, { name: file.name });
  current(options.signal);
  requireValue(uploaded.hash === hash && uploaded.size === file.size, "bundle_upload_hash_mismatch");
  options.onImportDispatched?.();
  // Native admission verifies ZIP/index/originals first, then invokes the existing
  // importer with export_mode=off and only conversation.json interpreted.
  const receipt = readBundleRestore(await command(host, {
    operation: "bundle_import", hash, expected_archive_sha256: hash, allow_incomplete: options.allowIncomplete,
  }, options.signal), hash);
  requireValue(options.allowIncomplete || receipt.complete_assets, "bundle_restore_incomplete");
  return receipt;
}
