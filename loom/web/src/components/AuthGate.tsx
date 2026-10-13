import { createContext, useCallback, useContext, useEffect, useMemo, useReducer, useState, type ReactNode } from "react";
import { authApi, AUTH_REQUIRED_EVENT } from "../api/auth";
import {
  ACCOUNTS_FEATURE, accountText, authReduce, registrationOffer, type AccountsTextId, type AuthState,
} from "../context/auth-state";
import { usePresentationFeature } from "./ConversationSourceView";

/** Present only when the server runs with accounts and someone is signed in. */
export interface AccountSession { login: string; logoutLabel: string; signedInAs: string; logout: () => void }
const AccountContext = createContext<AccountSession | null>(null);
export function useAccountSession(): AccountSession | null { return useContext(AccountContext); }

export default function AuthGate({ children }: { children: ReactNode }) {
  const [state, dispatch] = useReducer(authReduce, { phase: "loading" } as AuthState);
  const { feature } = usePresentationFeature(undefined, ACCOUNTS_FEATURE);
  const text = useCallback((id: AccountsTextId, parameters = {}) => accountText(feature, id, parameters), [feature]);

  const refresh = useCallback(async () => {
    const result = await authApi.me();
    dispatch(result.ok ? { type: "me", body: result.body } : { type: "me_failed", reason: result.reason });
  }, []);
  useEffect(() => { void refresh(); }, [refresh]);
  useEffect(() => {
    const onRequired = () => dispatch({ type: "unauthorized" });
    window.addEventListener(AUTH_REQUIRED_EVENT, onRequired);
    return () => window.removeEventListener(AUTH_REQUIRED_EVENT, onRequired);
  }, []);

  const session = useMemo<AccountSession | null>(() => state.phase !== "signed_in" ? null : {
    login: state.user.login,
    logoutLabel: text("logout"),
    signedInAs: text("signed_in_as", { login: state.user.login }),
    logout: () => { void authApi.logout().then(() => dispatch({ type: "signed_out" })); },
  }, [state, text]);

  if (state.phase === "loading") return <div className="auth-screen" data-testid="auth-loading"><p>{text("loading")}</p></div>;
  if (state.phase === "error")
    return <div className="auth-screen" data-testid="auth-error"><p>{text("error.unreachable", { reason: state.reason })}</p></div>;
  if (state.phase === "signed_out") return <SignIn registration={state.registration} text={text} onDone={refresh} />;
  return <AccountContext.Provider value={session}>{children}</AccountContext.Provider>;
}

function SignIn({ registration, text, onDone }: {
  registration: "open" | "invite" | "closed";
  text: (id: AccountsTextId, parameters?: Record<string, string>) => string;
  onDone: () => Promise<void>;
}) {
  const offer = registrationOffer(registration);
  const [creating, setCreating] = useState(false);
  const [login, setLogin] = useState("");
  const [password, setPassword] = useState("");
  const [invite, setInvite] = useState("");
  const [busy, setBusy] = useState(false);
  const [problem, setProblem] = useState<string | null>(null);

  const submit = async () => {
    setBusy(true);
    setProblem(null);
    const result = creating
      ? await authApi.register(login, password, offer === "invite" ? invite : undefined)
      : await authApi.login(login, password);
    setBusy(false);
    if (result.ok) { setPassword(""); await onDone(); return; }
    setProblem(result.status === 401 && !creating ? text("error.auth")
      : result.status === 0 ? text("error.unreachable", { reason: result.reason })
      : text("error.failed", { reason: result.reason }));
  };

  return (
    <form className="auth-screen" data-testid="auth-form" onSubmit={(event) => { event.preventDefault(); void submit(); }}>
      <h1>{text(creating ? "title_register" : "title")}</h1>
      <label>{text("login_label")}
        <input data-testid="auth-login" autoComplete="username" value={login} onChange={(e) => setLogin(e.target.value)} />
      </label>
      <label>{text("password_label")}
        <input data-testid="auth-password" type="password" autoComplete={creating ? "new-password" : "current-password"}
          value={password} onChange={(e) => setPassword(e.target.value)} />
      </label>
      {creating && offer === "invite" && (
        <label>{text("invite_label")}
          <input data-testid="auth-invite" value={invite} onChange={(e) => setInvite(e.target.value)} />
        </label>
      )}
      {problem && <p role="alert" data-testid="auth-problem">{problem}</p>}
      <button type="submit" data-testid="auth-submit" disabled={busy || !login || !password}>
        {busy ? text("working") : text(creating ? "register" : "sign_in")}
      </button>
      {offer === "none"
        ? <p>{text("registration_closed")}</p>
        : <>
            {offer === "invite" && !creating && <p>{text("registration_invite")}</p>}
            <button type="button" data-testid="auth-switch" onClick={() => { setCreating(!creating); setProblem(null); }}>
              {text(creating ? "to_login" : "to_register")}
            </button>
          </>}
    </form>
  );
}
