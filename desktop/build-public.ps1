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
$extOpsAway = "$extOps.public-build-aside"
$localExtAway = "$localExt.public-build-aside"

$movedExtOps = $false
$movedLocalExt = $false

try {
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
    $out = Join-Path $root 'HaulNX-AppUtility.exe'
    Copy-Item $exe $out -Force
    Write-Host "Public exe written to $out"
} finally {
    if ($movedExtOps) { Move-Item $extOpsAway $extOps -Force }
    if ($movedLocalExt) { Move-Item $localExtAway $localExt -Force }
}
