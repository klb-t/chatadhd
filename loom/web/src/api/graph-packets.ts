/** The existing native GraphPacket acceptance boundary, without new storage. */
export type GraphPacketSelection = { entities: string[]; claims: string[]; sources: string[] };
export type GraphPacketExpectedRows = { entities: Record<string, string | null>; claims: Record<string, string | null>; sources: Record<string, string | null> };
export interface GraphPacketStoreAcceptRequest {
  operation: "accept";
  target: string;
  packet: Record<string, unknown>;
  selection: GraphPacketSelection;
  expected_rows: GraphPacketExpectedRows;
  explicitly_accepted: true;
}
export type GraphPacketStoreRequest = GraphPacketStoreAcceptRequest | { operation: "read" | "replay"; receipt_id: string };
export interface GraphPacketStoreReceipt extends Record<string, unknown> {
  id: string;
  run_id: string;
  target: string;
  packet: Record<string, unknown>;
  selection: GraphPacketSelection;
  stored_row_sha256: GraphPacketExpectedRows;
  acceptance_establishes_content_truth: false;
}
export interface GraphPacketStoreResult {
  receipt: GraphPacketStoreReceipt;
  row_drift: { matches: boolean; rows: unknown[]; current_row_snapshots: Record<string, unknown> };
  replayed: boolean;
}
