# <img width="34" height="34" alt="icon" src="https://github.com/user-attachments/assets/ee14e0ce-f7f5-4e19-8edf-7a994468f915" /> HaulNX Lite

> Please report issues on the [GitHub Issues](https://github.com/digdat0/HaulNX-lite/issues) page.

A **ROM library manager for Nintendo Switch homebrew**. HaulNX Lite builds and
maintains one central library at `sdmc:/roms/<console>/` — you fill it by **sending
files from your PC** over USB or Wi-Fi — and it keeps that library **organized,
verified, and tidy**: checked against No-Intro/Redump DATs, deduplicated, misfiled
files sorted, folders per console. Point your emulators at `sd:/roms` once and
everything you add shows up. It also keeps your **installed emulators and apps up
to date** from GitHub, and ships with a **companion you run on your PC**. Built for
the devkitPro / libnx toolchain using Claude Code. **Yes, this is 100% AI created,
but it works.**

> HaulNX Lite has no built-in downloader of any kind — no archive.org browsing, no
> collections, nothing that fetches game files from the internet. The only way
> files reach the library is a **transfer you initiate** from your own PC.
>
> HaulNX Lite ships **no ROMs and no credentials** — it's an empty library. **You
> provide your own files.**
>
> **Disclaimer:** This project does not condone piracy or copyright infringement in any form.
> The screenshots shown use placeholder file names for illustration only. Please do not discuss
> piracy, ROM sources, or copyrighted content in issues, pull requests, or discussions —
> such will be removed. You are responsible for complying with the laws that apply to you.

---

## What it does

HaulNX Lite manages one central ROM library — `sdmc:/roms/<console>/`, across **60
supported console folders**. Send files in from your PC, and HaulNX Lite handles the
rest: it extracts archives, verifies dumps against the official DATs, files everything
under the right console, and cleans up duplicates and strays. Point your emulators at
`sd:/roms` once, and the library takes care of itself. A **desktop companion** and the
on-device tools cover the whole loop — transfer, organize, verify, and maintain.

---

## Features

**Installed library**
- Browse what you have by console, sorted by name or size with pinned folders on top,
  with an **on-device badge** so you can see at a glance what's already installed
- **Search installed games** across every ROM folder; open a result to jump to it
- Multi-select to delete, or rename in place

**Emulators tab** — keep emulators & apps updated
- Opens straight to the list: scans everything under `sdmc:/switch`, checks each
  against its **GitHub release**, and installs or updates it in place — with
  per-app backups so you can revert
- **Add an emulator manually** — pick any `.nro` already on your SD card and give it
  a GitHub repo, and it joins the same check/update flow (for anything the bundled
  catalogue doesn't already know)
- Update sources live in one shared manifest you can edit on the console **or** from
  the desktop companion, and sync either direction over USB or Wi-Fi
- App updates specifically live under **Settings → App Updates**

**Folders tab** — where your library lives
- The library lives at `sdmc:/roms/<console>/` by default, but you can **move it
  anywhere**, or switch to **per-console folders** and expand the list right there on
  the same page to set one per console. Handy when one emulator insists on its own
  directory, or you keep a system on a separate card

**Get ROMs into the library**
- **Send from your PC** — push files straight to the console over a **USB cable** or
  over **Wi-Fi** (no cable), from the [desktop companion](#the-desktop-companion).
  Incoming files land in an inbox and are auto-sorted to the right console
- Everything routes through one **Xfer Queue tab** — PC transfers and app updates
  all show the same progress rows

**Xfer Queue**
- Resumable and **persists across app restarts**; **network-loss aware** (pauses and
  resumes in order when the connection drops), retries transient errors with backoff
- Progress, speed, ETA, cancel, reorder; a **Y** menu to retry every failed item or
  clear finished ones; history with one-press re-transfer from the log

**Organize & verify**
- **Verify against DATs** — checks each ROM's size and hash against No-Intro / Redump
  DATs (auto-downloaded, or sent from your PC). Byte-order and header quirks are
  normalized, and single-file archives are hashed *inside the zip*, so a good dump
  matches whether loose or zipped
- **Have-vs-missing view** — see what a set is missing
- **Tidy library** — finds misfiled files (wrong console folder) and exact duplicates;
  **report-only, confirm each** — nothing is moved or deleted without your say-so
- **Reduce to 1G1R** — flag clone/region duplicates down to one game per title
- **Per-file options** (**X** in the library) — rename in place, sort, delete, or
  **move a game to another console folder**

**Also** — an optional card view with fetched **box art** (SteamGridDB, optional API
key), full touch control, 25 languages, light and dark themes with a **customizable
accent color** (Settings → Appearance), and a live network/space/battery header.

---

## The desktop companion

Shuffling files with the Switch's on-screen keyboard gets old fast, so HaulNX Lite
pairs with a **native desktop app (Windows)**, attached to every release as
`HaulNX-AppUtility-Lite.exe`, built with Rust + Tauri. It **auto-discovers your
Switch on the network** (no typing an IP), talks to it over a **USB cable**,
**sends games to the console** over USB or Wi-Fi, and **manages and updates your
installed emulators and apps** from GitHub. It self-updates, too. It has no
archive.org integration at all — no Collections tab, no archive.org Credentials,
and it refuses an archive.org download outright.

The **[App Utility](https://github.com/digdat0/HaulNX-lite/wiki/Reference-App-Utility)**
and **[Configuration](https://github.com/digdat0/HaulNX-lite/wiki/Reference-Configuration)**
wiki pages cover the details: sending files across, updating the app, the 60
supported console folders, and every file HaulNX Lite keeps on the card.

---

## Using it with your emulators

HaulNX Lite doesn't play anything — it fills a library that your emulators read from.
Everything lands in `sdmc:/roms/<console>/`:

```
sd:/roms/snes/    sd:/roms/psx/    sd:/roms/gba/    sd:/roms/nds/  ...
```

**Point each emulator at `sd:/roms` rather than moving files to suit one
emulator** — that way a single library is shared by all of them.

The **[HaulNX Lite wiki](https://github.com/digdat0/HaulNX-lite/wiki)** has a setup
page per emulator — install steps, where it expects ROMs, and how to line that up
with these folder names. It also covers the cross-cutting things: BIOS files, disc
images, overclocking, and troubleshooting.

Two things trip up almost everyone:

- **RetroArch doesn't notice new files by itself** — rescan `sd:/roms` after a
  HaulNX session or the playlists won't show what you just added.
- **NetherSX2, DraStic and Cemu won't launch from the homebrew menu normally.**
  They need the full memory of a game override: hold **R** while opening an
  installed game, then start the emulator from the menu that appears.

---

## Prerequisites

A Nintendo Switch running custom firmware (Atmosphère) with the homebrew menu,
and an emulator or two — see
**[using it with your emulators](#using-it-with-your-emulators)** above.

---

## Install

1. Download `HaulNX-Lite.nro` from the
   [latest release](https://github.com/digdat0/HaulNX-lite/releases/latest).
2. Copy it to your SD card at:
   ```
   sdmc:/switch/haulnx-lite/HaulNX-Lite.nro
   ```
3. Launch it from the homebrew menu.

Each release also attaches **`HaulNX-AppUtility-Lite.exe`** — the
[desktop companion](#the-desktop-companion) you run on your PC to send files to
the console.

---

## Quick start

HaulNX Lite starts **empty** — nothing shows up until you send files to it.

### 1. Send files from your PC

1. Open the [desktop companion](#the-desktop-companion) and connect to the console
   — it auto-discovers the Switch on your network, or connect a USB cable.
2. Pick the files you want and send them across. They arrive in an inbox and are
   auto-sorted to the right console folder.
3. On the console, switch to the **Xfer Queue** tab (**L/R**) to watch progress.
   Completed items extract/move into `sdmc:/roms/<console>/` automatically.

### 2. Point an emulator at the library

Everything is now in `sdmc:/roms/<console>/`. Set your emulator's ROM folder to
`sd:/roms` (or the per-system subfolder it wants) and your games appear — see
**[using it with your emulators](#using-it-with-your-emulators)** for the per-app
steps.

---

## Updating

**Settings → Updates → Check for updates** pulls the newest release from GitHub in
one tap, or accepts a build pushed **over Wi-Fi** from the companion (no USB cable).
The build is validated and staged with a backup, so an interrupted install can't
corrupt the app; you get a **Restart now** option to relaunch straight into it.

HaulNX Lite can also keep your **installed emulators and apps** updated the same
way — see the **Emulators tab** above.

---

## Building from source

Most people just want `HaulNX-Lite.nro` from the
[latest release](https://github.com/digdat0/HaulNX-lite/releases/latest). To compile
it yourself you need the **devkitPro** toolchain (devkitA64 + libnx) and a handful of
portlibs; Plutonium is a submodule and builds automatically:

```sh
git clone --recursive https://github.com/digdat0/HaulNX-lite
cd HaulNX-lite
make lite       # builds the Plutonium submodule, then HaulNX-Lite.nro in build-lite/
```

The desktop companion is a separate Rust + Tauri project under
[`desktop/`](desktop/):

```powershell
cd desktop
./build-lite.ps1   # -> desktop/HaulNX-AppUtility-Lite.exe
```

The prerequisites, the Windows/MSYS2 shell invocation, and a note on why networking
only works on real hardware are on the **[Building from
Source](https://github.com/digdat0/HaulNX-lite/wiki/Building-from-Source)** wiki
page. Contributions and translations are welcome — see
**[Contributing](https://github.com/digdat0/HaulNX-lite/wiki/Contributing)**.

---

## License

Released under the [GNU General Public License, version 3 or later](LICENSE)
— free to use, study, modify and redistribute, provided derivatives stay
under the GPL and ship their source. The full license text is bundled at
[`licenses/GPL-3.0.txt`](licenses/GPL-3.0.txt).

Two features pull in code that requires this: the native RAR3 filter-decoder
fallback ports a slice of the LGPLv3-licensed [unarr](https://github.com/zeniko/unarr)
project, and the embedded USB MTP responder's protocol layer follows the
pattern of [cmtp-responder](https://github.com/cmtp-responder/cmtp-responder)
(Apache-2.0) — both require GPLv3 for the combined work. Details and full
attribution are in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

The other bundled and linked components stay under their own, GPL-compatible
licenses: [Plutonium](https://github.com/XorTroll/Plutonium) © XorTroll and the
vendored jsmn tokenizer (both MIT), and the bundled Noto Sans font subset under
the SIL Open Font License 1.1. Their notices are collected in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md); the full OFL text ships at
[`licenses/OFL-1.1.txt`](licenses/OFL-1.1.txt). These accompany any distribution.

---

## Credits

- Built with [devkitPro / libnx](https://devkitpro.org/),
  [libcurl](https://curl.se/libcurl/), and
  [libarchive](https://www.libarchive.org/). JSON parsing via the vendored
  [jsmn](https://github.com/zserge/jsmn) tokenizer (MIT).
- Graphical UI powered by [Plutonium](https://github.com/XorTroll/Plutonium) by
  [XorTroll](https://github.com/XorTroll).
- Inspired in part by [TicoBro](https://github.com/StonedModder/Ticobro).
