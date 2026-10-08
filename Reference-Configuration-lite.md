Everything HaulNX Lite stores lives under **`sdmc:/switch/haulnx-lite/`**. You
never have to touch these by hand — the app writes them for you. This page is
for when you *do* want to edit them directly.

> **Folder layout:** settings and data live in a **`config/`** subfolder and
> logs in a **`logs/`** subfolder. If you're updating from an earlier build that
> kept them loose in the app root, HaulNX Lite **moves them into the new
> subfolders automatically** on first launch — nothing to do by hand.

## Supported consoles

A **console** is a folder under `sdmc:/roms/` (e.g. `snes`), and each console
holds the files you send to it. The consoles you can pick from come from a
fixed supported list of **64 folders**, so files always land somewhere your
emulators will look — you can't create an arbitrary folder in the app.

The [folder-name table on the Home page](Home#haulnx-folder-names) lists every
slug with the system it holds. A few ship **hidden** (Wii U, for instance,
since playing those depends on an unofficial emulator port); turn them on in
**Settings → User interface settings → Manage consoles**.

## Every file on the card

| Path | Purpose |
|------|---------|
| `sdmc:/switch/haulnx-lite/config/prefs.json` | settings (theme, language, pins, custom folders, …) |
| `sdmc:/switch/haulnx-lite/config/queue.json` | saved transfer queue (Xfer Queue tab) |
| `sdmc:/switch/haulnx-lite/config/inst_sizes.json` | cached installed-folder sizes (rebuilt on demand) |
| `sdmc:/switch/haulnx-lite/config/update_sources.json` | emulator/app update sources (Tools tab, desktop Emulators tab) |
| `sdmc:/switch/haulnx-lite/lang/<code>.json` | optional language overrides (built-in translations ship in the app) |
| `sdmc:/switch/haulnx-lite/logs/debug.log` | network/extraction diagnostics (viewable + clearable in Settings → Logs) |
| `sdmc:/switch/haulnx-lite/logs/transfers.log` · `speedtest.log` · `exbench.log` | Wi-Fi transfer, speed-test and extraction-benchmark logs |
| `sdmc:/roms/<console>/` | default library destination (or your [custom override](Reference-Custom-Folders)) |

Every log has a size ceiling. Once one is reached the file is moved aside as
`<name>.1` (replacing any previous `.1`) and a fresh one starts, so a
long-lived install keeps at most two generations of each instead of growing
forever.

## The library folder

Files land in `sdmc:/roms/<console>/` by default. To put the library
elsewhere, use **Settings → Storage → ROM Download Folder** — an on-screen
SD-card browser lets you navigate to a folder and pick it (**X** to use the
current folder, **Y** to reset to the default).

You can also route **individual consoles** to their own folders while the rest
stay on the default — see **[Custom Folders](Reference-Custom-Folders)**. Either way, see
[using it with your emulators](Home) for pointing emulators at whatever you
choose.
