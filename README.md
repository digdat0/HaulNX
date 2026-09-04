# <img width="34" height="34" alt="icon" src="https://github.com/user-attachments/assets/0c0cbe7e-3e2e-4068-9518-44179f6406d3" /> HaulNX

> Please report issues on the [GitHub Issues](https://github.com/digdat0/HaulNX/issues) page.

A **ROM library manager for Nintendo Switch homebrew**. HaulNX builds and maintains
one central library at `sdmc:/roms/<console>/` — you fill it from several sources
(download from [archive.org](https://archive.org), or send files from your PC over
USB or Wi-Fi), and it keeps that library **organized, verified, and tidy**: checked
against No-Intro/Redump DATs, deduplicated, misfiled files sorted, folders per
console. Point your emulators at `sd:/roms` once and
everything you add shows up. It also keeps your **installed emulators and apps up to
date** from GitHub, and ships with a **companion you run on your PC**. Built for the
devkitPro / libnx toolchain using Claude Code. **Yes, this is 100% AI created, but it
works.**

> HaulNX ships **no ROMs, no collections, and no
> credentials** — it's an empty library. **You provide your own** archive.org
> item ids (and optionally your own archive.org keys for restricted items). No
> links to any content are bundled.
>
> **Disclaimer:** This project does not condone piracy or copyright infringement in any form.
> The screenshots shown use placeholder file names for illustration only. Please do not discuss
> piracy, ROM sources, or copyrighted content in issues, pull requests, or discussions —
> such will be removed. You are responsible for complying with the laws that apply to you.

---

## What it does

HaulNX manages one central ROM library — `sdmc:/roms/<console>/`, across **60
supported console folders**. Get files in from wherever (archive.org, or your PC),
and HaulNX handles the rest: it extracts archives, verifies dumps against the
official DATs, files everything under the right console, and cleans up duplicates and
strays. Point your emulators at `sd:/roms` once, and the library takes care of
itself. A **desktop companion** and the on-device tools cover the whole loop —
acquire, organize, verify, and maintain.

---

## Features

**Installed library**
- Browse what you have by console, sorted by name or size with pinned folders on top,
  with an **on-device badge** so you can see at a glance what's already installed
- **Search installed games** across every ROM folder; open a result to jump to it
- Multi-select to delete, or rename in place

**Keep emulators & apps updated**
- **Settings → Emulator & app updates** scans everything under `sdmc:/switch`, checks
  each against its **GitHub release**, and installs or updates it in place — with
  per-app backups so you can revert
- Update sources live in one shared manifest you can edit on the console **or** from
  the desktop companion, and sync either direction over USB or Wi-Fi

**Custom install folders**
- The library lives at `sdmc:/roms/<console>/` by default, but you can **move it
  anywhere** (Settings → Storage → ROM Download Folder) or send **individual consoles
  to their own folders** (Settings → Storage → Install folders → *Custom per
  console*). Handy when one emulator insists on its own directory, or you keep a system
  on a separate card. Full walkthrough on the
  **[Custom Folders](https://github.com/digdat0/HaulNX/wiki/Reference-Custom-Folders)**
  wiki page.

**Get ROMs into the library**
- **Have a personal archive of your own backed-up collection on archive.org?** —
  collections grouped by console, with **global search** across every cached repo
  (results tagged with their console and marked `*` if you already have the file),
  bulk-mark with **Y** and queue with **A**
- **Send from your PC** — push files straight to the console over a **USB cable** or
  over **Wi-Fi** (no cable), from the [desktop companion](#the-desktop-companion).
  Incoming files land in an inbox and are auto-sorted to the right console
- Everything routes through one **Queue tab** — downloads, PC transfers, and app
  updates all show the same progress rows

**Download queue**
- Holds **256 items** and runs **3 at once** by default (adjustable up to 10);
  lowering the limit pauses the excess, which resume as slots free up
- Resumable and **persists across app restarts**; **network-loss aware** (pauses and
  resumes in order when the connection drops), **waits for free space** rather than
  failing item after item, and retries transient server errors with backoff
- Pipelined extraction — the next download starts while the previous archive unpacks
- Progress, speed, ETA, cancel, reorder; a **Y** menu to retry every failed item or
  clear finished ones; download history with one-press **re-download from the log**
- Before a bulk queue starts you get the totals — **files, bytes, free space, what the
  queue already owes, and how many were skipped as already installed** — and can take
  only what fits

**Organize & verify**
- **Verify against DATs** — checks each ROM's size and hash against No-Intro / Redump
  DATs (auto-downloaded, or sent from your PC). Byte-order and header quirks are
  normalized, and single-file archives are hashed *inside the zip*, so a good dump
  matches whether loose or zipped
- **Have-vs-missing view** — see what a set is missing and jump straight to an
  archive.org search to acquire it
- **Tidy library** — finds misfiled files (wrong console folder) and exact duplicates;
  **report-only, confirm each** — nothing is moved or deleted without your say-so
- **Reduce to 1G1R** — flag clone/region duplicates down to one game per title
- **Per-file options** (**X** in the library) — rename in place, sort, delete, or
  **move a game to another console folder**

**Also** — an optional card view with fetched **box art** (SteamGridDB, optional API
key), full touch control, 25 languages, light and dark themes with a **customizable
accent color** (Settings → Appearance), and a live network/space/battery header.
**Settings → Manage data** refreshes or clears cached metadata and cleans up the
temporary downloads folder.

---

## The desktop companion

Building collections and shuffling files with the Switch's on-screen keyboard gets old
fast, so HaulNX pairs with a **native desktop app (Windows)**, attached to every
release as `HaulNX-AppUtility.exe`, built with Rust + Tauri. It **auto-discovers your
Switch on the network** (no typing an IP), talks to it over a **USB cable**, downloads
from archive.org **free of browser CORS limits** (including restricted items with your
S3 keys), **extracts zip/7z/rar**, **sends games to the console** over USB or Wi-Fi,
and **manages and updates your installed emulators and apps** from GitHub. It
self-updates, too.

The wiki has the details:

- **[App Utility](https://github.com/digdat0/HaulNX/wiki/Reference-App-Utility)** —
  building collections on a PC, sending them across, and updating the app.
- **[Configuration](https://github.com/digdat0/HaulNX/wiki/Reference-Configuration)** —
  the `dl_sources.json` schema, `credentials.json`, the 60 supported console folders,
  and every file HaulNX keeps on the card.

Restricted archive.org items need your own S3 keys
(<https://archive.org/account/s3.php>); public collections need none. Keys live
only on your SD card and are sent only to archive.org, only over HTTPS — **none
are bundled.**

---

## Using it with your emulators

HaulNX doesn't play anything — it fills a library that your emulators read from.
Everything lands in `sdmc:/roms/<console>/`:

```
sd:/roms/snes/    sd:/roms/psx/    sd:/roms/gba/    sd:/roms/nds/  ...
```

**Point each emulator at `sd:/roms` rather than moving files to suit one
emulator** — that way a single library is shared by all of them.

The **[HaulNX wiki](https://github.com/digdat0/HaulNX/wiki)** has a setup page per
emulator: install steps, where it expects ROMs, and how to line that up with
HaulNX's folder names.

| System(s) | Emulator | Setup |
|---|---|---|
| Nearly everything — NES through PS1, arcade, handhelds | **RetroArch** | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-RetroArch) — scan `sd:/roms` once and it builds a playlist per system |
| Multi-system frontends | TICO, Lakka, SCCM-Retro | [TICO](https://github.com/digdat0/HaulNX/wiki/Emulator-TICO) · [Lakka](https://github.com/digdat0/HaulNX/wiki/Emulator-Lakka) · [SCCM-Retro](https://github.com/digdat0/HaulNX/wiki/Emulator-SCCM-Retro) |
| PlayStation | DuckStation | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-DuckStation) |
| PSP | PPSSPP | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-PPSSPP) |
| GBA / GB / GBC | mGBA | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-mGBA) |
| Nintendo DS | melonDS or DraStic | [melonDS](https://github.com/digdat0/HaulNX/wiki/Emulator-melonDS) · [DraStic](https://github.com/digdat0/HaulNX/wiki/Emulator-DraStic) |
| Nintendo 3DS | Raikopon or Dekopon | [Raikopon](https://github.com/digdat0/HaulNX/wiki/Emulator-Raikopon) · [Dekopon](https://github.com/digdat0/HaulNX/wiki/Emulator-Dekopon) |
| Dreamcast / NAOMI / Atomiswave | Flycast | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-Flycast) |
| Saturn | Yaba Sanshiro | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-YabaSanshiro) |
| PlayStation 2 | NetherSX2 | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-NetherSX2) — lighter titles only |
| Wii U | Cemu | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-Cemu) — brand new, expect rough edges |
| Arcade / Neo Geo | MAME, pFBN | [MAME](https://github.com/digdat0/HaulNX/wiki/Emulator-MAME) · [pFBN](https://github.com/digdat0/HaulNX/wiki/Emulator-pFBN) |
| SNES / NES standalones | pSNES / pNES | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-pSNES-pNES) |
| Commodore Amiga | UAE4ALL2 | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-UAE4ALL2) — unofficial port, hidden by default |
| ZX Spectrum | Vapor Spec | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-VaporSpec) — unofficial port, hidden by default |
| CHIP-8 | RetroArch (CHIP-8/Emux core) | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-CHIP-8) — hidden by default |
| PICO-8 | PICO-8 (Switch export) | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-PICO-8) — needs a PICO-8 license; hidden by default |
| Tamagotchi | TamaLIB-based port | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-Tamagotchi) — unofficial port, hidden by default |
| Adobe Flash Games | Ruffle-based port | [guide](https://github.com/digdat0/HaulNX/wiki/Emulator-Flash) — unofficial/experimental, hidden by default |
| GameCube, Wii, PS Vita | — | [not realistically playable](https://github.com/digdat0/HaulNX/wiki/Emulator-Experimental) — the folders exist for organization |

The wiki also covers the cross-cutting things:
**[BIOS files](https://github.com/digdat0/HaulNX/wiki/Reference-BIOS-Files)** (which
systems need firmware and the exact filenames),
**[disc images](https://github.com/digdat0/HaulNX/wiki/Reference-Disc-Images)**
(`.cue`/`.bin`/`.chd` and multi-disc `.m3u`),
**[overclocking](https://github.com/digdat0/HaulNX/wiki/Reference-Overclocking)**,
and **[troubleshooting](https://github.com/digdat0/HaulNX/wiki/Reference-Troubleshooting)**.

Two things trip up almost everyone:

- **RetroArch doesn't notice new files by itself** — rescan `sd:/roms` after a
  HaulNX session or the playlists won't show what you just added.
- **NetherSX2, DraStic and Cemu won't launch from the homebrew menu normally.**
  They need the full memory of a game override: hold **R** while opening an
  installed game, then start the emulator from the menu that appears.

---

## Screenshots

Coming soon.

---

## Prerequisites

A Nintendo Switch running custom firmware (Atmosphère) with the homebrew menu,
and an emulator or two — see
**[using it with your emulators](#using-it-with-your-emulators)** above.

---

## Install

1. Download `HaulNX.nro` from the
   [latest release](https://github.com/digdat0/HaulNX/releases/latest).
2. Copy it to your SD card at:
   ```
   sdmc:/switch/HaulNX/HaulNX.nro
   ```
3. Launch it from the homebrew menu.

Each release also attaches **`HaulNX-AppUtility.exe`** — the
[desktop companion](#the-desktop-companion) you run on your PC to build
collections and push files to the console.

On first run it seeds an **empty** `dl_sources.json` containing only the list of
supported console folders — **no collections or links are included**. Add your
own collections in-app (press **Y** and enter an archive.org item id) or by
editing `dl_sources.json` on your SD card.

---

## Quick start

HaulNX starts **empty** — you add your own collections before anything shows up.

### 1. Add a collection

A *collection* is an archive.org **item** that holds the game files for a system.
Each item has an **item id** — the last part of its URL, e.g. for
`https://archive.org/details/MyExampleItem` the id is `MyExampleItem`.

1. On the **Browse** tab, press **Y** (add).
2. Choose the **console** the files belong to — this is the
   `sdmc:/roms/<console>` folder your emulators read from. (The selectable list
   comes from `consoles`; you can't pick an unsupported folder.)
3. Enter a **name** for the repo — any label, e.g. `My SNES set`.
4. Enter the archive.org **item id** (the `<id>` from `archive.org/details/<id>`).

The console now appears on the Browse tab. Open it with **A**, pick the repo, and
you'll see its file list. Repeat **Y** to add more collections — a console can
hold several.

**Building more than a couple of these? Use the
[desktop companion](#the-desktop-companion) instead.** Typing item ids on the
Switch's on-screen keyboard gets old fast. Build the whole collection with a real
keyboard, preview any item's file list before you commit to it, and send the result
straight to the console over your network. It can also pull the collection the console
is currently running back into the editor, so you can fetch, edit and send it on
again.

(You can also edit `dl_sources.json` on the SD card by hand — see
[Configuration](https://github.com/digdat0/HaulNX/wiki/Reference-Configuration) on
the wiki.)

### 2. Add your archive.org keys (optional)

Public collections download anonymously and need **no keys**. You only need keys
for **restricted** items that require an archive.org account.

1. On a computer, sign in at [archive.org](https://archive.org) and open your S3
   keys page: <https://archive.org/account/s3.php>. You'll get an **access key**
   and a **secret key**.
2. In HaulNX, switch to the **Settings** tab (**L/R**).
3. Open **Advanced → Archive.org credentials**.
4. Edit the **Access key** and **Secret** — the edit field is pre-filled with the
   current value so it's easy to change.

Keys live only on your SD card (`sdmc:/switch/HaulNX/config/credentials.json`) and
are sent only to archive.org hosts, and only over HTTPS.

### 3. Fill the library

1. **From archive.org:** on **Browse**, open a console (**A**) and pick a repo to
   browse its files. Highlight a file and press **A** to queue it; for more than one,
   mark files with **Y** and then press **A**. **X** opens the filter, sort, and
   *select all shown*.
2. **From your PC:** open the [companion](#the-desktop-companion), connect to the
   console, and send files over USB or Wi-Fi — they arrive in the inbox and sort to the
   right console.
3. Switch to the **Queue** tab (**L/R**) to watch progress. Completed items
   extract/move into `sdmc:/roms/<console>/` automatically.

### 4. Point an emulator at the library

Everything is now in `sdmc:/roms/<console>/`. Set your emulator's ROM folder to
`sd:/roms` (or the per-system subfolder it wants) and your games appear — see
**[using it with your emulators](#using-it-with-your-emulators)** for the per-app
steps.

---

## Updating

**Settings → Check for updates** pulls the newest release from GitHub in one tap,
or accepts a build pushed **over Wi-Fi** from the companion (no USB cable). The
build is validated and staged with a backup, so an interrupted install can't
corrupt the app; you get a **Restart now** option to relaunch straight into it.

HaulNX can also keep your **installed emulators and apps** updated the same way —
see **Keep emulators & apps updated** above. Full walkthrough on the
[App Utility wiki page](https://github.com/digdat0/HaulNX/wiki/Reference-App-Utility#updating-the-app).

---

## Building from source

Most people just want `HaulNX.nro` from the
[latest release](https://github.com/digdat0/HaulNX/releases/latest). To compile it
yourself you need the **devkitPro** toolchain (devkitA64 + libnx) and a handful of
portlibs; Plutonium is a submodule and builds automatically:

```sh
git clone --recursive https://github.com/digdat0/HaulNX
cd HaulNX
make            # builds the Plutonium submodule, then HaulNX.nro
```

The desktop companion is a separate Rust + Tauri project under
[`desktop/`](desktop/) with its own build.

The prerequisites, the Windows/MSYS2 shell invocation, a note on why networking
only works on real hardware, and a map of the source tree are on the
**[Building from Source](https://github.com/digdat0/HaulNX/wiki/Building-from-Source)**
wiki page.

---

## Contributing

Translations are the most useful contribution — HaulNX ships 25 languages, all
plain JSON in [`romfs/lang/`](romfs/lang/). See
**[Contributing](https://github.com/digdat0/HaulNX/wiki/Contributing)** for how to
fix a string or add a language (and how to test one on the console without
rebuilding). Bug reports and PRs go to
[Issues](https://github.com/digdat0/HaulNX/issues).

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
(Apache-2.0) — both require GPLv3 for the combined work, which is why HaulNX
moved there (from GPLv2, itself an earlier relicense from MIT for a since-
replaced MTP implementation). Details and full attribution are in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

The other bundled and linked components stay under their own, GPL-compatible
licenses: [Plutonium](https://github.com/XorTroll/Plutonium) © XorTroll and the
vendored jsmn tokenizer (both MIT), and the bundled Noto Sans font subset under
the SIL Open Font License 1.1. Their notices are collected in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md); the full OFL text ships at
[`licenses/OFL-1.1.txt`](licenses/OFL-1.1.txt). These accompany any
distribution.

---

## Credits

- Built with [devkitPro / libnx](https://devkitpro.org/),
  [libcurl](https://curl.se/libcurl/), and
  [libarchive](https://www.libarchive.org/). JSON parsing via the vendored
  [jsmn](https://github.com/zserge/jsmn) tokenizer (MIT).
- Graphical UI powered by [Plutonium](https://github.com/XorTroll/Plutonium) by
  [XorTroll](https://github.com/XorTroll).
- Started as a simple archive.org downloader inspired by
  [TicoBro](https://github.com/StonedModder/Ticobro), and grew into a full ROM
  library manager from there.
</content>
</invoke>
