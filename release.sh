#!/bin/sh
# Publish the current build as a GitHub release whose tag == the version baked
# into TicoDLplus.nro. Run AFTER `make` (do not run `make` in between, or the
# VERSION will have advanced past the built .nro).
#
# This uploads ONLY the .nro as a release asset (via the GitHub API) and tags
# the release. It does NOT push your source code anywhere. Release notes for the
# version are pulled from CHANGELOG.md and attached to the GitHub release.
#
# Requires the GitHub CLI (`gh auth login` once) and that the public repo in
# include/config.h (UPDATE_REPO) exists on GitHub (e.g. created with
#   gh repo create <repo> --public --add-readme).
set -e
cd "$(dirname "$0")"

nro="TicoDLplus.nro"
[ -f "$nro" ] || { echo "No $nro found - run 'make' first."; exit 1; }

v="$(cat VERSION)"
repo="$(grep -oE '#define[[:space:]]+UPDATE_REPO[[:space:]]+"[^"]+"' include/config.h \
        | sed -E 's/.*"([^"]+)".*/\1/')"

if [ -z "$repo" ] || [ "$repo" = "YOURUSER/TicoDLplus" ]; then
  echo "Set UPDATE_REPO in include/config.h to your real GitHub repo first."
  exit 1
fi

# Pull the notes for this version out of CHANGELOG.md (everything under the
# "## <version>" heading up to the next "## ").
notes=""
if [ -f CHANGELOG.md ]; then
  notes="$(awk -v ver="$v" '
    $0 ~ ("^## " ver "([ \t]|$)") { grab=1; next }
    /^## / { grab=0 }
    grab { print }
  ' CHANGELOG.md)"
fi
# Trim leading/trailing blank lines.
notes="$(printf '%s\n' "$notes" | sed -e '/./,$!d' | sed -e ':a' -e '/^\n*$/{$d;N;ba}')"

notesfile="$(mktemp)"
trap 'rm -f "$notesfile"' EXIT
if [ -n "$notes" ]; then
  printf 'TicoDL+ %s\n\n%s\n' "$v" "$notes" > "$notesfile"
else
  echo "WARN: no CHANGELOG.md section for $v - add a '## $v' entry for real notes."
  printf 'TicoDL+ %s\n' "$v" > "$notesfile"
fi

echo "Releasing v$v to $repo with notes:"
echo "----------------------------------------"
cat "$notesfile"
echo "----------------------------------------"
gh release create "$v" "$nro" -R "$repo" -t "TicoDL+ $v" -F "$notesfile" --latest
echo "Done. Users can now update in-app via Settings (L) -> R."
