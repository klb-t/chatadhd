import { useCallback, useLayoutEffect, useRef, useState } from "react";
import { createFaceClientPort, EMPTY_FACE_SNAPSHOT, type FaceClientPort, type FaceConnector,
  type FaceParameterSet, type FaceSnapshot } from "./port";

/** contextRevision is an identity-only token memoized by the caller from actual client context.
 * No IDs, text, settings or receipts are read, serialized or passed to the port by this hook. */
export function useFaceClient(contextRevision: object, connector?: FaceConnector) {
  const port = useRef<FaceClientPort | null>(null);
  const currentContext = useRef(contextRevision);
  const [stored, setStored] = useState<{ context: object; snapshot: FaceSnapshot } | null>(null);
  useLayoutEffect(() => {
    const client = createFaceClientPort(connector);
    port.current = client;
    const unsubscribe = client.subscribe(() => setStored({ context: currentContext.current, snapshot: client.getSnapshot() }));
    return () => {
      unsubscribe();
      port.current = null;
      client.dispose();
    };
  }, [connector]);
  useLayoutEffect(() => {
    currentContext.current = contextRevision;
    port.current?.contextChanged();
  }, [contextRevision, connector]);
  const select = useCallback((value: FaceParameterSet) => port.current?.select(value), []);
  // Hide the previous selection already in render, before layout invalidates its epoch.
  const snapshot = stored?.context === contextRevision ? stored.snapshot : EMPTY_FACE_SNAPSHOT;
  return { snapshot, select };
}
