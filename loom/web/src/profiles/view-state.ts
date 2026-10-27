/** Browser projection state; conversation content remains in the native store. */
import { bindingValue, type Binding } from "../shell/windows/binding";
export type ConversationCoupling = "coupled" | "independent";
export interface ApplicationViewState {
  id: string;
  profile: string;
  conversation: {
    coupling: ConversationCoupling;
    selectedId: string | null;
    sidebarOpen: boolean;
  };
}

/** The v1 saved layout gains optional fields, so earlier view/workflow IDs survive. */
export function readApplicationView(value: unknown): ApplicationViewState {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("Invalid saved application view.");
  const row = value as Record<string, unknown>;
  if (typeof row.id !== "string" || !row.id || typeof row.profile !== "string" || !row.profile) {
    throw new Error("Invalid saved application view identity.");
  }
  if (row.conversation === undefined) return newApplicationView(row.id, row.profile);
  if (!row.conversation || typeof row.conversation !== "object" || Array.isArray(row.conversation)) {
    throw new Error("Invalid saved application view conversation settings.");
  }
  const conversation = row.conversation as Record<string, unknown>;
  if ((conversation.coupling !== "coupled" && conversation.coupling !== "independent") ||
      (conversation.selectedId !== null && (typeof conversation.selectedId !== "string" || !conversation.selectedId)) ||
      typeof conversation.sidebarOpen !== "boolean") {
    throw new Error("Invalid saved application view conversation settings.");
  }
  return { id: row.id, profile: row.profile, conversation: {
    coupling: conversation.coupling, selectedId: conversation.selectedId, sidebarOpen: conversation.sidebarOpen,
  } };
}

export function newApplicationView(id: string, profile: string): ApplicationViewState {
  return { id, profile, conversation: { coupling: "coupled", selectedId: null, sidebarOpen: false } };
}

/** W6 (OKNA §5.3): the stored coupling read as a binding of the parameter `conversation` - `coupled` = shared in the channel
 *  `conversation`, `independent` = the view's own value. The stored shape is unchanged (old saved views read the same). */
export function conversationBinding(view: ApplicationViewState): Binding {
  return view.conversation.coupling === "coupled" ? { binding: "shared", channel: "conversation" } : { binding: "independent", value: view.conversation.selectedId };
}

export function viewConversationId(view: ApplicationViewState, sharedId: string | null): string | null {
  return bindingValue(conversationBinding(view), view.conversation.selectedId, { conversation: sharedId }, "conversation") as string | null;
}

/** Detaching takes the conversation currently visible in this view, without changing siblings. */
export function setConversationCoupling(view: ApplicationViewState, coupling: ConversationCoupling,
  sharedId: string | null): ApplicationViewState {
  if (coupling === view.conversation.coupling) return view;
  return { ...view, conversation: { ...view.conversation, coupling,
    ...(coupling === "independent" ? { selectedId: sharedId, sidebarOpen: true } : {}),
  } };
}
