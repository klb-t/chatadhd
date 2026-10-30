// The words of the guest model line follow what the source REALLY is (tier from the server's data, free_providers.pack),
// never a constant claim: a cost-free tier may say "free models", any other tier says it is a paid source, an unknown
// tier makes no claim about cost.
import type { AccountsTextId } from "./auth-state";

export function modelLineId(tier: unknown): Extract<AccountsTextId, "guest.model_line" | "guest.model_line_free" | "guest.model_line_paid"> {
  if (typeof tier !== "string" || tier === "") return "guest.model_line";
  return tier === "free" ? "guest.model_line_free" : "guest.model_line_paid";
}

/** Key of the layer's remembered state (reversible: the toggle writes it every time). */
export const LAYER_STATE_KEY = "loom.guest.layer";
