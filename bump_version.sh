#!/bin/sh
# Increment the patch component of the VERSION file and print the new value.
# Also writes source/version.h so the version is compiled into the app and every
# build recompiles main.c (which keeps the NRO, icon, and version in sync).
# Invoked once per build from the Makefile.
d="$(dirname "$0")"
f="$d/VERSION"
v="$(cat "$f" 2>/dev/null || echo 0.1.0)"
major="${v%%.*}"
rest="${v#*.}"
minor="${rest%%.*}"
patch="${rest##*.}"
patch=$((patch + 1))
nv="$major.$minor.$patch"
echo "$nv" > "$f"
printf '#ifndef VERSION_H\n#define VERSION_H\n#define APP_VERSION_STR "%s"\n#endif\n' \
    "$nv" > "$d/source/version.h"
echo "$nv"
