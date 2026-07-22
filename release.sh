#!/bin/sh
# Publish the current build as a GitHub release whose tag == the version baked
# into HaulNX.nro. Run AFTER `make` (do not run `make` in between, or the
# VERSION will have advanced past the built .nro).
#
# This uploads the .nro as a release asset (via the GitHub API) and tags the
# release AT THE COMMIT THAT IS ON origin/main, so the tag always matches source
# that is actually pushed. Before tagging it syncs with the remote:
#   * remote-only changes that touch nothing but README.md (e.g. screenshot
#     edits made in the GitHub web UI) are pulled down automatically;
#   * any local commits are pushed;
#   * anything else that has diverged (remote changes to non-README files)
#     stops the release so you can reconcile by hand.
# Release notes for the version are pulled from CHANGELOG.md.
#
# Requires the GitHub CLI (`gh auth login` once) and that the public repo in
# include/config.h (UPDATE_REPO) exists on GitHub (e.g. created with
#   gh repo create <repo> --public --add-readme).
set -e
cd "$(dirname "$0")"

nro="HaulNX.nro"
[ -f "$nro" ] || { echo "No $nro found - run 'make' first."; exit 1; }

# The app utility ships with every release: a single self-contained HTML file
# users open in a browser to build/edit dl_sources.json and push files to the
# console. Newest version wins if several are lying around.
editor="$(ls -1 tools/app-utility/appUtility-*.html 2>/dev/null | sort -V | tail -n 1)"
[ -n "$editor" ] || { echo "No tools/app-utility/appUtility-*.html found."; exit 1; }

v="$(cat VERSION)"
repo="$(grep -oE '#define[[:space:]]+UPDATE_REPO[[:space:]]+"[^"]+"' include/config.h \
        | sed -E 's/.*"([^"]+)".*/\1/')"

if [ -z "$repo" ] || [ "$repo" = "YOURUSER/HaulNX" ]; then
  echo "Set UPDATE_REPO in include/config.h to your real GitHub repo first."
  exit 1
fi

# --- Sync guard: the tag must point at a commit that is on origin/main --------
# Only release from main; the tag is created there.
branch="$(git rev-parse --abbrev-ref HEAD)"
if [ "$branch" != "main" ]; then
  echo "Refusing to release from branch '$branch' (expected 'main')."
  exit 1
fi

echo "Syncing with origin/main ..."
git fetch --quiet origin main || { echo "git fetch failed."; exit 1; }

local_head="$(git rev-parse HEAD)"
remote_head="$(git rev-parse origin/main)"

if [ "$local_head" != "$remote_head" ]; then
  behind="$(git rev-list --count HEAD..origin/main)"

  # Remote has commits we don't. Auto-pull them ONLY if they touch nothing but
  # README.md (screenshot edits made in the web UI); otherwise stop.
  if [ "$behind" -gt 0 ]; then
    remote_changed="$(git diff --name-only HEAD...origin/main)"
    non_readme="$(printf '%s\n' "$remote_changed" | grep -v '^$' | grep -v '^README\.md$' || true)"
    if [ -n "$non_readme" ]; then
      echo "origin/main has $behind commit(s) you don't have that change more than README.md:"
      printf '%s\n' "$non_readme" | sed 's/^/  /'
      echo "Reconcile manually (e.g. 'git pull --rebase origin main'), then re-run."
      exit 1
    fi
    echo "  pulling README-only updates from origin/main ..."
    if ! git rebase origin/main; then
      git rebase --abort 2>/dev/null || true
      echo "Automatic rebase failed (README conflict). Reconcile by hand, then re-run."
      exit 1
    fi
    local_head="$(git rev-parse HEAD)"
    remote_head="$(git rev-parse origin/main)"
  fi

  # Push any local commits so the tag lands on a pushed commit.
  if [ "$local_head" != "$remote_head" ]; then
    echo "  pushing local commit(s) to origin/main ..."
    git push origin main || { echo "git push failed."; exit 1; }
    local_head="$(git rev-parse HEAD)"
    remote_head="$(git rev-parse origin/main)"
  fi
fi

if [ "$local_head" != "$remote_head" ]; then
  echo "HEAD ($local_head) is still not on origin/main ($remote_head). Aborting."
  exit 1
fi

# Don't silently clobber an existing release for this version.
if gh release view "$v" -R "$repo" >/dev/null 2>&1; then
  echo "A release '$v' already exists on $repo."
  echo "Bump VERSION, or delete it first: gh release delete $v -R $repo --cleanup-tag --yes"
  exit 1
fi
echo "Verified: HEAD ($local_head) is on origin/main."

# Pull the notes for this version out of CHANGELOG.md (everything under the
# FIRST "## <version>" heading up to the next "## "). Stopping at the first match
# guards against a duplicate heading later in the file (e.g. the pre-rebrand
# history block) getting stitched onto the notes.
notes=""
if [ -f CHANGELOG.md ]; then
  notes="$(awk -v ver="$v" '
    $0 ~ ("^## " ver "([ \t]|$)") { if (seen) exit; seen=1; grab=1; next }
    /^## / { if (grab) exit }
    grab { print }
  ' CHANGELOG.md)"
fi
# Trim leading/trailing blank lines.
notes="$(printf '%s\n' "$notes" | sed -e '/./,$!d' | sed -e ':a' -e '/^\n*$/{$d;N;ba}')"

notesfile="$(mktemp)"
trap 'rm -f "$notesfile"' EXIT
if [ -n "$notes" ]; then
  printf 'HaulNX %s\n\n%s\n' "$v" "$notes" > "$notesfile"
else
  echo "WARN: no CHANGELOG.md section for $v - add a '## $v' entry for real notes."
  printf 'HaulNX %s\n' "$v" > "$notesfile"
fi

echo "Releasing v$v to $repo with notes:"
echo "----------------------------------------"
cat "$notesfile"
echo "----------------------------------------"
echo "Assets: $nro, $editor"
gh release create "$v" "$nro" "$editor" -R "$repo" -t "HaulNX $v" -F "$notesfile" \
  --target "$local_head" --latest
echo "Done. Users can now update in-app via Settings (L) -> R."
