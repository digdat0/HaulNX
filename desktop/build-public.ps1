<#
Public/release build for the desktop companion.

Unlike the NRO side, the local-only extras can't be excluded with a compiler
flag alone: Tauri's `frontendDist` (../src) embeds the *entire* src/ folder as
static webview assets regardless of any Rust cfg, so local-ext.js would still
ship even with the Rust half compiled out. This script physically moves both
extras out of the tree, builds, then restores them — so the exe it produces
provably never contained either one, and normal local `cargo build` resumes
working immediately after with no manual cleanup.

Builds to a separate target-public/ dir (via CARGO_TARGET_DIR) rather than the
normal target/, so this never clobbers — or gets stale mixed in with — an
ordinary local `cargo build --release` and its full-featured exe.

Output: target-public/release/haulnx-app-utility.exe copied to
desktop/HaulNX-AppUtility.exe — the only desktop exe that should ever leave
this machine; upload it as-is, no rename needed. Never attach the plain
haulnx-app-utility.exe from an ordinary local `cargo build --release` (your
full-featured local copy) to a release.
#>

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$extOps = Join-Path $root 'src-tauri\src\ext_ops.rs'
$localExt = Join-Path $root 'src\local-ext.js'
# Park the extras OUTSIDE src/ and src-tauri/src/. They used to be renamed in
# place (src/local-ext.js -> src/local-ext.js.public-build-aside), but Tauri
# embeds every file under src/ whatever its name, so local-ext.js shipped
# inside the exe under the renamed name in every public/Lite build.
$asideDir = Join-Path $root '.build-aside-public'
$extOpsAway = Join-Path $asideDir 'ext_ops.rs'
$localExtAway = Join-Path $asideDir 'local-ext.js'

$movedExtOps = $false
$movedLocalExt = $false

try {
    New-Item -ItemType Directory -Force $asideDir | Out-Null
    if (Test-Path $extOps) {
        Move-Item $extOps $extOpsAway -Force
        $movedExtOps = $true
    }
    if (Test-Path $localExt) {
        Move-Item $localExt $localExtAway -Force
        $movedLocalExt = $true
    }

    Push-Location (Join-Path $root 'src-tauri')
    try {
        $env:CARGO_TARGET_DIR = Join-Path $root 'src-tauri\target-public'
        cargo build --release
        if ($LASTEXITCODE -ne 0) { throw "cargo build failed with exit code $LASTEXITCODE" }
    } finally {
        Remove-Item Env:\CARGO_TARGET_DIR -ErrorAction SilentlyContinue
        Pop-Location
    }

    $exe = Join-Path $root 'src-tauri\target-public\release\haulnx-app-utility.exe'
    if (-not (Test-Path $exe)) { throw "build succeeded but $exe is missing" }
    # Refuse to publish an exe that still carries either extra (or anything
    # parked by an older version of this script): Tauri keeps asset names as
    # plain text in the binary, and a compiled ext_ops.rs leaves its path in
    # panic locations.
    $bytes = [IO.File]::ReadAllBytes($exe)
    $text = [Text.Encoding]::ASCII.GetString($bytes)
    foreach ($needle in @('local-ext', 'build-aside', 'ext_ops.rs')) {
        if ($text.Contains($needle)) {
            throw "refusing to publish: $exe contains '$needle' (a local-only file leaked into the build)"
        }
    }
    $out = Join-Path $root 'HaulNX-AppUtility.exe'
    Copy-Item $exe $out -Force
    Write-Host "Public exe written to $out"
} finally {
    if ($movedExtOps) { Move-Item $extOpsAway $extOps -Force }
    if ($movedLocalExt) { Move-Item $localExtAway $localExt -Force }
}
