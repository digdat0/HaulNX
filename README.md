# <img width="34" height="34" alt="icon" src="https://github.com/user-attachments/assets/0c0cbe7e-3e2e-4068-9518-44179f6406d3" /> HaulNX

> Please report issues on the [GitHub Issues](https://github.com/digdat0/HaulNX/issues) page.

A Nintendo Switch homebrew that browses and downloads game files from
[archive.org](https://archive.org) into its own centralized ROM library at
`sdmc:/roms/<console>/`, decompressing archives along the way. Point your
emulators at that folder and your games show up. Built for the devkitPro /
libnx toolchain using Claude Code. **Yes, this is 100% AI created, but it
works.**

> HaulNX ships **no ROMs, no collections, and no
> credentials** — it's an empty downloader. **You provide your own** archive.org
> item ids (and optionally your own archive.org keys for restricted items). No
> links to any content are bundled.
>
> **Disclaimer:** This project does not condone piracy or copyright infringement in any form.
> The screenshots shown use placeholder file names for illustration only. Please do not discuss
> piracy, ROM sources, or copyrighted content in issues, pull requests, or discussions —
> such will be removed. You are responsible for complying with the laws that apply to you.

---

## What it does

HaulNX is a background download manager for archive.org game files that installs
them into one central ROM library — `sdmc:/roms/<console>/` — across **53
supported console folders**. You point your emulators at that folder once, and
everything you download from then on just appears.

---

## Features

**Browse & search**
- Archive.org collections grouped by console, with full console names; a console
  can hold several repos when one set is incomplete
- **Global search** across every cached repo — results tagged with their console,
  marked `*` if you already have the file, and queueable straight from the results
- Filter and sort any file list, pin favourites to the top, and show or hide
  consoles you don't collect for

**Bulk downloads**
- **Mark files with Y**, then queue the whole set with **A**. The selection is
  keyed to the file rather than the row, so you can filter, mark, change the
  filter and mark again to build one set across several passes
- Before anything starts you get the totals: **how many files, how many bytes,
  free space, what the queue already owes, and how many were skipped as already
  installed.** If the set doesn't fit you can take only what fits, or queue it
  anyway
- **Skip installed** (default on) drops files you already have from a marked
  selection — a single deliberate **A** on one file is never filtered

**Download queue**
- Holds **256 items** and runs **5 at once** by default; change the limit and it
  applies immediately — lowering it pauses the excess, which resume where they
  stopped as slots free up
- Resumable, and **persists across app restarts** — interrupted downloads pick up
  automatically on the next launch
- **Network-loss aware**: if the connection drops, active downloads pause keeping
  their partial files, and everything resumes in order when it comes back
- **Waits for free space** instead of failing item after item as the card fills,
  and retries transient server errors with backoff
- Pipelined extraction — the next download starts while the previous archive unpacks
- Progress, speed, ETA, cancel, reorder; a **Y** menu to retry every failed item
  or clear the finished ones at once
- Download history with one-press **re-download from the log**

**Extraction & verification**
- `.zip` / `.7z` / `.rar` / `.tar.*` unpacked into the console folder; plain files
  moved as-is
- Verified by size and MD5 where the source publishes them, so a corrupt or
  truncated download is rejected rather than installed

**Installed library**
- Browse what you have by console, sorted by name or size with pinned folders on top
- **Search installed games** across every ROM folder; open a result to jump to it
- Multi-select to delete, or rename in place

**In-app self-update**
- **Settings → Check for updates** pulls the newest release from GitHub in one tap
- Or push a build **over Wi-Fi** from the [App Utility](#collections-config--the-app-utility) — no USB
  cable, and the same version as installed is accepted so you can test new builds
- Either way the build is validated and staged, so an interrupted install can't
  corrupt the app

**Also** — an optional card view, full touch control, 25 languages, light and dark
themes, a live network/space/battery header, and a configurable ROM folder.
**Settings → Manage data** refreshes or clears cached metadata, cleans up the
temporary downloads folder, and takes a collection sent over Wi-Fi from the
[App Utility](#collections-config--the-app-utility).

---

## Using it with your emulators

HaulNX doesn't play anything — it fills a library that your emulators read from.
Every download lands in `sdmc:/roms/<console>/`:

```
sd:/roms/snes/    sd:/roms/psx/    sd:/roms/gba/    sd:/roms/nds/  ...
```

**Point each emulator at `sd:/roms` rather than moving downloads to suit one
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
| GameCube, Wii | — | [not realistically playable](https://github.com/digdat0/HaulNX/wiki/Emulator-Experimental) — the folders exist for organization |

The wiki also covers the cross-cutting things:
**[BIOS files](https://github.com/digdat0/HaulNX/wiki/Reference-BIOS-Files)** (which
systems need firmware and the exact filenames),
**[disc images](https://github.com/digdat0/HaulNX/wiki/Reference-Disc-Images)**
(`.cue`/`.bin`/`.chd` and multi-disc `.m3u`),
**[overclocking](https://github.com/digdat0/HaulNX/wiki/Reference-Overclocking)**,
and **[troubleshooting](https://github.com/digdat0/HaulNX/wiki/Reference-Troubleshooting)**.

Two things trip up almost everyone:

- **RetroArch doesn't notice new files by itself** — rescan `sd:/roms` after a
  HaulNX session or the playlists won't show what you just downloaded.
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

Each release also attaches **`appUtility-<version>.html`** — the companion
[App Utility](#collections-config--the-app-utility) you open in a browser on your
computer to build collections and push files to the console.

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
[App Utility](#collections-config--the-app-utility) instead.** Typing item ids on
the Switch's on-screen keyboard gets old fast. The utility is a single HTML file
that ships with every release — open it in a browser on your PC, build the whole
collection with a real keyboard, preview any item's file list before you commit to
it, and send the result straight to the console over your network. No SD card
swapping, and no re-typing. It can also pull the collection the console is
currently running back into the editor, so you can fetch, edit and send it on
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

Keys live only on your SD card (`sdmc:/switch/HaulNX/credentials.json`) and
are sent only to archive.org hosts, and only over HTTPS.

### 3. Download

1. On **Browse**, open a console (**A**) and pick a repo to browse its files.
   (Repo metadata loads in the background with a brief "Loading…" indicator.)
2. Highlight a file and press **A** to queue it. For more than one, mark files
   with **Y** and then press **A** — you'll get the totals (files, bytes, free
   space) before anything starts. **X** opens the filter, sort, and *select all
   shown*.
3. Switch to the **Queue** tab (**L/R**) to watch progress. Completed downloads
   extract/move into `sdmc:/roms/<console>/` automatically.

### 4. Point an emulator at the library

Downloads are now in `sdmc:/roms/<console>/`. Set your emulator's ROM folder to
`sd:/roms` (or the per-system subfolder it wants) and your games appear — see
**[using it with your emulators](#using-it-with-your-emulators)** for the per-app
steps.

---

## Collections, config & the App Utility

You add collections in-app (**Y** on the Browse tab), but the easy way is the
**App Utility** — a self-contained HTML file, attached to every release, that you
open in a browser on your PC to build collections with a real keyboard and send
them to the console over Wi-Fi.

The wiki has the details:

- **[App Utility](https://github.com/digdat0/HaulNX/wiki/Reference-App-Utility)** —
  building collections on a PC, sending them across, and updating the app.
- **[Configuration](https://github.com/digdat0/HaulNX/wiki/Reference-Configuration)** —
  the `dl_sources.json` schema, `credentials.json`, the 53 supported console
  folders, and every file HaulNX keeps on the card.

Restricted archive.org items need your own S3 keys
(<https://archive.org/account/s3.php>); public collections need none. Keys live
only on your SD card and are sent only to archive.org, only over HTTPS — **none
are bundled.**

---

## Updating

**Settings → Check for updates** pulls the newest release from GitHub in one tap,
or accepts a build pushed **over Wi-Fi** from the App Utility (no USB cable). The
build is validated and staged with a backup, so an interrupted install can't
corrupt the app; you get a **Restart now** option to relaunch straight into it.
Full walkthrough on the
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

Released under the [MIT License](LICENSE) — free to use, modify and
redistribute. The only condition is that the copyright notice and license stay
included, so **please keep the credit**.

HaulNX's own code and the [Plutonium](https://github.com/XorTroll/Plutonium) UI
library it builds on are both MIT-licensed, so the project is cleanly permissive.
Third-party license notices (Plutonium © XorTroll, jsmn, and the bundled Noto
Sans font subset under the SIL Open Font License) are collected in
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
- Inspired by [TicoBro](https://github.com/StonedModder/Ticobro) — I wanted a
  simple downloader whose only job was to download, with some enhancements.
