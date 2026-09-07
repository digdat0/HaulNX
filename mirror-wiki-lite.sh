#!/bin/sh
# Mirror the main HaulNX wiki (github.com/digdat0/HaulNX/wiki) into the public
# digdat0/HaulNX-lite wiki, almost verbatim. The wiki's per-emulator setup
# guides and App Utility/Building-from-Source reference pages aren't about the
# downloader feature Lite removes -- they're shared reference material both
# builds need, so there's no separate "Lite wiki" content to author for those.
#
# One page IS Lite-specific: Reference-Configuration.md is almost entirely
# about the acquisition feature (dl_sources.json/collections, credentials.json,
# download history) that doesn't exist in Lite at all, so the generic mirror
# gets overwritten with the hand-authored Reference-Configuration-lite.md
# (same pattern as README-lite.md overriding README.md in release-lite.sh) --
# keep that file in sync by hand if the main wiki's page changes structurally.
#
# Rerun this by hand whenever the main wiki changes and you want Lite's copy
# to catch up -- it isn't tied to a version release the way release-lite.sh
# is, so it doesn't run automatically as part of that script.
#
# Requires: gh CLI with a `digdat0` account logged in (same reasoning as
# release-lite.sh -- authenticates this one operation without touching your
# active `gh auth` account or global git config).
set -e
cd "$(dirname "$0")"
root="$(pwd)"

SRC_WIKI="https://github.com/digdat0/HaulNX.wiki.git"
LITE_REPO="digdat0/HaulNX-lite"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

LITE_TOKEN="$(gh auth token --hostname github.com --user digdat0)" \
  || { echo "No 'digdat0' account logged in to gh (run: gh auth login)."; exit 1; }
LITE_WIKI_URL="https://x-access-token:${LITE_TOKEN}@github.com/${LITE_REPO}.wiki.git"

echo "== 1/3: cloning source wiki =="
git clone --quiet "$SRC_WIKI" "$WORK/src"

echo "== 2/3: preparing Lite wiki clone =="
if git clone --quiet "$LITE_WIKI_URL" "$WORK/dst" 2>/dev/null; then
  cd "$WORK/dst"
  git checkout -B master
  # Wipe tracked+untracked (but keep .git) so a page removed on the source
  # side actually disappears here too, instead of lingering forever.
  git rm -rf --ignore-unmatch . >/dev/null
  git clean -fdx -e .git >/dev/null
else
  # First-ever page: GitHub only creates the wiki's own git repo once
  # something is pushed to it, so a plain clone of an empty-but-enabled wiki
  # 404s. Bootstrap a fresh local repo instead.
  mkdir -p "$WORK/dst"
  cd "$WORK/dst"
  git init --quiet -b master
fi
cd "$WORK"

echo "== 3/3: copying pages, rewriting self-links, committing, pushing =="
cp "$WORK"/src/*.md "$WORK/dst/"
[ -f "$root/Reference-Configuration-lite.md" ] && \
  cp "$root/Reference-Configuration-lite.md" "$WORK/dst/Reference-Configuration.md"
cd "$WORK/dst"
# Pages that link back to the main repo (issues, releases, license/source
# links, "the GitHub repo") would otherwise point a Lite-repo visitor at the
# full HaulNX repo instead of this one. Wiki-page cross-links use bare
# [Page Name](PageName) refs, not full URLs, so they resolve locally in
# either wiki untouched -- only repo-qualified links need rewriting. Blanket
# literal substitution is safe here: the source wiki (freshly cloned above,
# every run) never contains "HaulNX-lite" itself, so this can't double up.
#
# Same reasoning for the two rewrites below: the shipped filename/folder
# differ for Lite (HaulNX-Lite.nro, not HaulNX.nro; sdmc:/switch/haulnx-lite/,
# not sdmc:/switch/HaulNX/ -- see config.h's HAULNX_LITE-gated
# DEFAULT_SELF_PATH/CONFIG_DIR), and the source wiki never already contains
# either Lite-specific form, so these can't double up either.
find . -maxdepth 1 -name '*.md' -print0 | xargs -0 sed -i \
  -e 's#digdat0/HaulNX#digdat0/HaulNX-lite#g' \
  -e 's#HaulNX\.nro#HaulNX-Lite.nro#g' \
  -e 's#sdmc:/switch/HaulNX/#sdmc:/switch/haulnx-lite/#g'
git -c user.name=digdat0 -c user.email=digdat0@users.noreply.github.com add -A
if git diff --cached --quiet; then
  echo "  (no wiki changes to publish)"
else
  git -c user.name=digdat0 -c user.email=digdat0@users.noreply.github.com \
      commit --quiet -m "Sync wiki from HaulNX"
  git push --quiet "$LITE_WIKI_URL" HEAD:master
fi

echo ""
echo "Done: https://github.com/$LITE_REPO/wiki"
