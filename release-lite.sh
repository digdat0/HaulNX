#!/bin/sh
# Build the Lite NRO + Lite desktop exe from the CURRENT source and publish a
# mirror of this repo to the separate public digdat0/HaulNX-lite repo: same
# tracked source (the HAULNX_LITE #ifdef guards ship intact — see CLAUDE.md's
# "HAULNX_LITE build flag" section for why that's safe: config.c guarantees no
# repo can ever exist regardless), a Lite-specific README, and both built
# binaries attached to a GitHub Release tagged with the current VERSION.
#
# Never carries over: anything under source/ext or include/ext (3DS decrypt,
# NRO side) or desktop/src-tauri/src/ext_ops.rs / desktop/src/local-ext.js
# (3DS decrypt, desktop side) — all four are untracked in THIS repo already
# (.git/info/exclude), so `git ls-files` never sees them in the first place.
# Also excluded explicitly: CHANGELOG.md — it's the full product's history
# (predates the HaulNX rebrand, full of archive.org/Collections entries) and
# would undercut the Lite repo standing on its own. The wiki is mirrored
# separately by mirror-wiki-lite.sh (rerun that by hand when the main wiki
# changes; it isn't tied to a version release the way this script is).
#
# Requires: gh CLI with a `digdat0` account logged in (this script authenticates
# every git/gh operation against the Lite repo with that account's token,
# without touching your active `gh auth` account or global git config), and
# PowerShell on PATH for the desktop build.
#
# Usage: run from the repo root, after `git commit`ing whatever this Lite
# release should include (the export mirrors your CURRENT working tree, not
# just what's committed — commit first if you want a rebuildable snapshot).
#   ./release-lite.sh
set -e
cd "$(dirname "$0")"
root="$(pwd)"

LITE_REPO="digdat0/HaulNX-lite"
EXPORT_DIR="$root/../HaulNX-lite-export"

echo "== 1/6: building Lite NRO (make lite) =="
PATH="/c/devkitPro/devkitA64/bin:/c/devkitPro/tools/bin:/c/devkitPro/portlibs/switch/bin:$PATH" \
PKG_CONFIG_PATH="/c/devkitPro/portlibs/switch/lib/pkgconfig" \
make DEVKITPRO=/c/devkitPro DEVKITA64=/c/devkitPro/devkitA64 PORTLIBS_PATH=/c/devkitPro/portlibs \
     TMP='C:\Users\Steve\AppData\Local\Temp' TEMP='C:\Users\Steve\AppData\Local\Temp' lite

echo "== 2/6: building Lite desktop exe (build-lite.ps1) =="
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$root/desktop/build-lite.ps1"

v="$(cat VERSION)"
nro="$root/HaulNX-Lite.nro"
exe="$root/desktop/HaulNX-AppUtility-Lite.exe"
[ -f "$nro" ] || { echo "Missing $nro"; exit 1; }
[ -f "$exe" ] || { echo "Missing $exe"; exit 1; }
[ -f "$root/README-lite.md" ] || { echo "Missing README-lite.md — write it once, see the HaulNX-lite repo setup notes."; exit 1; }

# One token, used explicitly for every git/gh call below — never touches your
# active `gh auth` account or global git credential store.
LITE_TOKEN="$(gh auth token --hostname github.com --user digdat0)" \
  || { echo "No 'digdat0' account logged in to gh (run: gh auth login)."; exit 1; }
AUTH_URL="https://x-access-token:${LITE_TOKEN}@github.com/${LITE_REPO}.git"

echo "== 3/6: preparing export tree at $EXPORT_DIR =="
if [ ! -d "$EXPORT_DIR/.git" ]; then
  rm -rf "$EXPORT_DIR"
  git clone "$AUTH_URL" "$EXPORT_DIR"
fi
cd "$EXPORT_DIR"
git checkout -B main
# Wipe everything tracked + untracked (but keep .git) so a file removed on the
# source side actually disappears here too, instead of lingering forever.
git rm -rf --ignore-unmatch . >/dev/null
git clean -fdx -e .git >/dev/null
# Commit the wipe before touching Plutonium below: `git rm` only *stages* the
# deletions against HEAD, and `git submodule add` refuses to (re-)add a path
# ("Plutonium") that still has uncommitted staged changes -- it fails with the
# misleading "please make sure that the .gitmodules file is in the working
# tree", not anything mentioning the real cause. Only bites on the second and
# later runs against a reused export clone (the first run's HEAD has nothing
# to stage a deletion against), which is exactly the "rerun this every
# release" case this script exists for.
git -c user.name=digdat0 -c user.email=digdat0@users.noreply.github.com \
    commit --quiet --allow-empty -m "wipe for re-export" >/dev/null
cd "$root"

echo "== 4/6: copying NRO source (git-tracked files, minus main-repo-only extras) =="
git ls-files -z \
  | grep -zv -E '^(HaulNX\.nro|README\.md|CHANGELOG\.md|release\.sh|release-lite\.sh|README-lite\.md|\.gitmodules|Plutonium|desktop/HaulNX-AppUtility\.exe|desktop/build-public\.ps1)$' \
  | while IFS= read -r -d '' f; do
      mkdir -p "$EXPORT_DIR/$(dirname "$f")"
      cp "$f" "$EXPORT_DIR/$f"
    done
cp README-lite.md "$EXPORT_DIR/README.md"

echo "== 5/6: copying desktop source (minus the untracked 3DS-decrypt seam) =="
mkdir -p "$EXPORT_DIR/desktop"
cp -r desktop/src "$EXPORT_DIR/desktop/"
cp -r desktop/src-tauri "$EXPORT_DIR/desktop/"
cp desktop/build-lite.ps1 desktop/.gitignore "$EXPORT_DIR/desktop/"
rm -f "$EXPORT_DIR/desktop/src/local-ext.js" \
      "$EXPORT_DIR/desktop/src-tauri/src/ext_ops.rs"
rm -rf "$EXPORT_DIR/desktop/src-tauri/target" \
       "$EXPORT_DIR/desktop/src-tauri/target-lite" \
       "$EXPORT_DIR/desktop/src-tauri/target-public" \
       "$EXPORT_DIR/desktop/src-tauri/gen"
rm -f "$EXPORT_DIR"/desktop/src-tauri/build_release*.log

# Plutonium as its own submodule in the new repo, pinned to the same commit
# this repo currently has (so `make lite` there builds the identical UI lib).
# The export dir is reused across runs (see EXPORT_DIR above), and the wipe
# above only clears the working tree -- `git clean -e .git` deliberately never
# touches .git/, so a submodule registered by an earlier successful run is
# still sitting in .git/config and .git/modules/Plutonium even though
# .gitmodules itself just got deleted. `git submodule add` then fails outright
# ("please make sure that the .gitmodules file is in the working tree")
# instead of just re-adding. Fully unregister any such leftover first so this
# is idempotent no matter how many times it's been run before.
plutonium_sha="$(git ls-tree HEAD Plutonium | awk '{print $3}')"
cd "$EXPORT_DIR"
git submodule deinit -f Plutonium >/dev/null 2>&1 || true
git config --remove-section submodule.Plutonium >/dev/null 2>&1 || true
rm -rf .git/modules/Plutonium
git submodule add https://github.com/XorTroll/Plutonium Plutonium >/dev/null
(cd Plutonium && git checkout --quiet "$plutonium_sha")
cd "$root"

cat > "$EXPORT_DIR/.gitignore" <<'EOF'
# build output
/build/
/build-lite/
*.nro
*.elf
*.nacp
*.lst
*.map

# desktop build output (the shipped exe is attached to Releases, not tracked)
/desktop/HaulNX-AppUtility-Lite.exe
/desktop/src-tauri/target/
/desktop/src-tauri/target-lite/
/desktop/src-tauri/gen/schemas/
EOF

echo "== 6/6: commit, tag, push, release =="
cd "$EXPORT_DIR"
git -c user.name=digdat0 -c user.email=digdat0@users.noreply.github.com add -A
if git diff --cached --quiet; then
  echo "  (no source changes since the last Lite export)"
else
  git -c user.name=digdat0 -c user.email=digdat0@users.noreply.github.com \
      commit --quiet -m "HaulNX Lite $v"
fi
git push --quiet "$AUTH_URL" main
git tag -f "$v" >/dev/null
git push --quiet -f "$AUTH_URL" "refs/tags/$v"

if GH_TOKEN="$LITE_TOKEN" gh release view "$v" -R "$LITE_REPO" >/dev/null 2>&1; then
  GH_TOKEN="$LITE_TOKEN" gh release upload "$v" "$nro" "$exe" -R "$LITE_REPO" --clobber
else
  GH_TOKEN="$LITE_TOKEN" gh release create "$v" "$nro" "$exe" \
    -R "$LITE_REPO" -t "HaulNX Lite $v" --latest \
    --notes "HaulNX Lite $v — built from HaulNX $v."
fi

echo ""
echo "Done: https://github.com/$LITE_REPO  (tag $v)"
