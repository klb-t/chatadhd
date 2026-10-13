#!/bin/sh
# Rejestracja z CHATADHD_REGISTRATION (closed|invite|open) trafia do nakladki profilu w /data.
set -eu
case "${CHATADHD_REGISTRATION:-}" in
  closed|invite|open)
    mkdir -p "$LOOM_ACCOUNTS_ROOT/profiles"
    printf '{"schema":"loom.runtime_profile_overlay/1","domain":"server_accounts","overrides":{"registration":"%s","throttle":{"trust_forwarded_for":true}}}\n' \
      "$CHATADHD_REGISTRATION" > "$LOOM_ACCOUNTS_ROOT/profiles/server_accounts.pack";;
  "") ;;
  *) echo "CHATADHD_REGISTRATION: closed|invite|open" >&2; exit 2;;
esac
exec "$@"
