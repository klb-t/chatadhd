#!/bin/sh
# Installs tools/git-hooks/post-commit as the shared hook of this clone and of
# every worktree made from it (hooks live in the common git dir).
# Reverse: delete the file it prints, or set `git config chatadhd.checkpointPush false`.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
common=$(cd "$here" && cd "$(git rev-parse --git-common-dir)" && pwd)
dest="$common/hooks"
mkdir -p "$dest"
if [ -e "$dest/post-commit" ] && ! cmp -s "$here/git-hooks/post-commit" "$dest/post-commit"; then
  echo "refusing to overwrite a different $dest/post-commit; merge by hand" >&2
  exit 1
fi
cp "$here/git-hooks/post-commit" "$dest/post-commit"
chmod +x "$dest/post-commit"
echo "installed: $dest/post-commit"
