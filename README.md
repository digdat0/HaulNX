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
- Or push a build **over Wi-Fi** from the [App Utility](#app-utility) — no USB
  cable, and the same version as installed is accepted so you can test new builds
- Either way the build is validated and staged, so an interrupted install can't
  corrupt the app

**Also** — an optional card view, full touch control, 25 languages, light and dark
themes, a live network/space/battery header, and a configurable ROM folder.
**Settings → Manage data** refreshes or clears cached metadata, cleans up the
temporary downloads folder, and takes a collection sent over Wi-Fi from the
[App Utility](#app-utility).

### Controls

| Button | Browse / file lists | Queue | Installed |
|---|---|---|---|
| **A** | open · queue the marked set | — | open |
| **Y** | mark file · add repo | actions menu | mark for delete |
| **X** | view menu (filter, sort, select all) | — | rename |
| **−** | global search | — | search · delete marked |
| **D-pad ▶ / ◀** | pin · re-sort | jump to top/bottom | pin · re-sort |
| **ZL / ZR** | page up/down | reorder an item | page up/down |
| **L / R** | switch tab | switch tab | switch tab |

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
[App Utility](#app-utility) you open in a browser on your computer to build
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
hold several. (You can also edit `dl_sources.json` directly — see
[Configuration](#configuration).)

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

## Console groups & supported consoles

A **console** is a folder under `sdmc:/roms/` (e.g. `snes`). Each console
groups one or more **repos** — archive.org collections to download from.

The consoles you can use come from a fixed **supported list** (`consoles`) of 53
folders, so files always land somewhere your emulators will look. When you add a
repo you pick its console from that list; you can't create arbitrary folders in
the app. To change the supported set, edit `consoles` in `dl_sources.json`.

The wiki has the
**[full slug table](https://github.com/digdat0/HaulNX/wiki#haulnx-folder-names)** —
every folder name with the system it holds. A few ship **hidden** (Wii U, for
instance, since playing those depends on an unofficial emulator port); turn them
on in **Settings → User interface settings → Manage consoles**.

---

## Configuration

All config lives under `sdmc:/switch/HaulNX/`.

### `dl_sources.json`

Seeded empty on first run; you fill it in. Schema (the `a_id` values shown are
placeholders — substitute the archive.org item ids you choose to use):

```json
{
  "console_list_groups": [
    {
      "console": "snes",
      "target": "snes",
      "repos": [
        {
          "label": "My SNES set",
          "a_id": "<your-archive.org-item-id>",
          "URL": "",
          "active": true
        }
      ]
    }
  ],
  "consoles": ["nes", "snes", "n64", "genesis", "psx", "psp"]
}
```

- `console` / `target` — display name and the ROM subfolder (usually the same).
- `a_id` — the archive.org item id.
- `URL` — download base; defaults to `https://archive.org/download/<a_id>` if omitted.
- `active` — include this repo.
- `consoles` — the master list of selectable consoles (legacy files using
  `tico_consoles` are still read).

### `credentials.json`

```json
{ "accessKey": "YOURKEY", "secret": "YOURSECRET" }
```

Optional archive.org S3 keys, sent as `authorization: LOW <access>:<secret>` for
restricted items — only to archive.org hosts and only over HTTPS. Public
collections download anonymously and need no keys.
**Use your own keys — none are bundled.**

### Files on the SD card

| Path | Purpose |
|------|---------|
| `sdmc:/switch/HaulNX/dl_sources.json` | console groups + repos + supported list |
| `sdmc:/switch/HaulNX/credentials.json` | archive.org S3 keys (optional) |
| `sdmc:/switch/HaulNX/prefs.json` | settings (theme, language, pins, download limit, …) |
| `sdmc:/switch/HaulNX/queue.json` | saved download queue |
| `sdmc:/switch/HaulNX/cache/<id>.json` | cached metadata |
| `sdmc:/switch/HaulNX/downloads/` | temporary `.part` files |
| `sdmc:/switch/HaulNX/downloads.log` | download history (text) |
| `sdmc:/switch/HaulNX/downloads.jsonl` | download history (structured, powers re-download from the log) |
| `sdmc:/switch/HaulNX/lang/<code>.json` | optional language overrides (built-in translations ship in the app) |
| `sdmc:/switch/HaulNX/debug.log` | network/extraction diagnostics (viewable + clearable in Settings → View logs) |
| `sdmc:/roms/<console>/` | default ROM destination (or your custom override) |

Every log has a size ceiling. Once one is reached the file is moved aside as
`<name>.1` (replacing any previous `.1`) and a fresh one starts, so a long-lived
install keeps at most two generations of each instead of growing forever.

---

## Updating (in-app)

Open **Settings → Check for updates** and pick where the update comes from:

- **From GitHub** — HaulNX checks the GitHub releases on a background thread —
  the UI stays responsive and shows the attempt counter (`(1/3)`) while
  transient errors are retried; press **B** to dismiss the check and keep
  using the app. If a newer version is found, it downloads the new `.nro`
  (with a live progress indicator).
- **Over Wi-Fi (push a build)** — the console shows an address on your
  network. Open it in a browser and drop a HaulNX `.nro` on the page, or push
  one from the [App Utility](#app-utility) (**Send to Switch → App update**).
  The same version as installed is accepted — this path exists so new builds
  can be tested without plugging in a USB cable. A live progress line shows
  the transfer as it arrives.

Either way the received build is validated (real-NRO check), you confirm the
install — the dialog shows the incoming build's version next to the installed
one — and it's staged with a copy-then-rename (keeping a `.previous` backup),
so an interrupted install can't corrupt the app. Finish with **Restart now**
(the app relaunches itself straight into the new build) or **Later** (the
Settings chip flips to *Restart to update* and the swap happens on the next
launch).

---

## App Utility

Every release ships **`appUtility-<version>.html`** — a single self-contained
HTML file (no install, no server, works offline) you open in any browser on
your computer. It's the comfortable way to build and maintain collections, and
it talks to the console directly:

- **Edit collections** — build `dl_sources.json` visually: console groups,
  repos, the supported-console list. Import an existing file (including the one
  your console is running — fetch it with **Get from Switch**, or download it
  from the import screen in a browser) or start fresh, then export.
- **Quick test** — paste any archive.org item id or URL to preview its file
  list before committing it to a collection.
- **Switch transfer → Collection** — a round trip over the LAN while the
  console's **Import collection** screen is open. **Get from Switch** pulls the
  collection the console is running on into the editor; **Send** pushes it back,
  and you confirm the import on the Switch. No SD card swapping.
- **Switch transfer → App update (.nro)** — push a HaulNX build to the console
  while **Settings → Check for updates → Over Wi-Fi** is open. The utility
  validates the file is a real NRO before sending and shows send progress;
  you confirm the install (and restart) on the Switch.
- **Export credentials** — write your archive.org S3 keys to a
  `credentials.json` for the SD card.

Both devices must be on the same network; the console shows the address to
enter. Nothing leaves your LAN and no third-party service is involved.

---

## Translations

HaulNX ships 25 languages. All translation files live in the repo as plain
JSON — one file per language, keyed by English, in
[`romfs/lang/`](romfs/lang/). These are bundled into the app and loaded at
runtime; there is a single source of truth, so edit here.

Spotted a wrong or awkward translation? Please
[open an issue](https://github.com/digdat0/HaulNX/issues) naming the
language, the key (or the on-screen text), and your suggested wording — or send
a PR updating the file in `romfs/lang/`. For English (`en.json`) changes,
`tools/gen_i18n.py` must be re-run afterwards (it regenerates the strings baked
into the binary).

You can also override any language locally without rebuilding: copy the file to
`sdmc:/switch/HaulNX/lang/<code>.json` and edit it — the SD copy takes
priority over the bundled one.

---

## Archive extraction notes

Most archives extract automatically. A known limitation: some **RAR3-compressed
`.rar` files that use RAR's programmable filters cannot be decompressed** by the
bundled library — for those, the raw `.rar` is saved into the console folder and
you'll need to extract it on a PC. If a specific archive won't unpack, check
the debug log (**Settings → View logs → View debug log**) for the exact reason.

---

## Overwrite behaviour

When a downloaded file lands in `sdmc:/roms/<console>/` and a file of the
**same name already exists there, it is overwritten in place.** This is intended
— it's how you re-download or refresh a file. There is **no prompt and no
separate backup**: the previous file is replaced (a single file via a move, or,
for archives, each extracted file as it's written). The `*` "installed" marker
in a file list is informational only — it does **not** block re-downloading.

It is never hidden, though:

- **Logged for audit.** Every completed download is recorded in the download
  history (`downloads.log`, viewable in Settings → View logs), and the
  entry notes when it **`(overwrote existing)`** or `(overwrote N files)` for an
  archive.
- **Shown in the queue.** A finished item's result column shows a colour-coded
  tag: **`(repl)` in orange** = an existing file was replaced (with a count, e.g.
  `(repl 12)`, for multi-file archives); **`(new)` in green** = a brand-new file.
  No prompt; it never interrupts the queue.

If you want to keep an old copy, move or rename it before downloading the new
one.

---

## Networking

devkitPro's libcurl uses the libnx **`ssl` system-service** backend, verifying
against the console's own certificate store. This works on **real hardware**.
Emulators that stub the `ssl` service (e.g. Ryujinx) will fail HTTPS regardless
of the app — metadata browsing and downloads should be tested on hardware. Every
request's result is logged to `debug.log`.

---

## Building from source

HaulNX is a **graphical app** built on the
[Plutonium](https://github.com/XorTroll/Plutonium) UI library (SDL2), with the
**devkitPro** toolchain (devkitA64 + libnx). Plutonium is included as a git
submodule and built automatically.

### Prerequisites

1. Install **devkitPro** and the `switch-dev` group — see the
   [Getting Started guide](https://devkitpro.org/wiki/Getting_Started). Ensure
   `DEVKITPRO` is set (on Windows use the **MSYS2** shell that ships with devkitPro).
2. Install the portlibs the app links against (codec deps are pulled in
   automatically):
   ```sh
   dkp-pacman -S switch-curl switch-libarchive switch-zlib \
                 switch-sdl2 switch-sdl2_ttf switch-sdl2_image \
                 switch-sdl2_gfx switch-sdl2_mixer
   ```

### Build

```sh
git clone --recursive https://github.com/digdat0/HaulNX
cd HaulNX
make            # builds the Plutonium lib (submodule), then HaulNX.nro
make clean
```

If you cloned without `--recursive`, run `git submodule update --init` first.
On Windows, build inside the devkitPro MSYS2 shell:

```sh
/c/devkitPro/msys2/usr/bin/bash.exe -lc "cd /c/path/to/HaulNX && make"
```

Output is **`HaulNX.nro`**. The version lives in `VERSION` (the single source
of truth) and is baked into the build and `include/version.h` automatically. To
publish a release: `sh release.sh` — it tags a GitHub release with that version,
attaches the `.nro`, and uses the matching `CHANGELOG.md` section as the notes.

> Networking (metadata + downloads) only works on **real hardware** — the libnx
> `ssl` backend isn't stubbed there. Emulators like Ryujinx will fail HTTPS.

### Source layout

| Path | Responsibility |
|------|----------------|
| `source/Main.cpp`, `source/MainApplication.cpp` | Plutonium GUI (screens, tabs, navigation, input) |
| `include/MainApplication.hpp`, `include/TableList.hpp` | UI layout + custom table-list element |
| `net.*` | libnx sockets + libcurl (downloads, HTTP GET, logging) |
| `archive.c` / `iarchive.h` | archive.org metadata + download URLs |
| `queue.*` | pipelined download + extract workers (resume, verify, persist, reorder) |
| `extract.*` | libarchive zip/7z/rar/tar extraction |
| `config.*` | `dl_sources.json` / credentials / prefs load + save |
| `fsutil.*` | mkdir-p, move, recursive delete, free-space |
| `update.*` | GitHub release check + in-app self-update |
| `httpsrv.*` | tiny LAN HTTP receiver behind Import collection and update-over-Wi-Fi |
| `md5.*` | MD5 for download verification |
| `jsonutil.*`, `jsmn.*` | JSON parsing (vendored jsmn) |
| `i18n.*`, `romfs/lang/`, `tools/gen_i18n.py` | translations — English strings are generated into the binary from `romfs/lang/en.json`; the other 24 languages load from romfs |
| `tools/app-utility/` | the App Utility — self-contained HTML collection editor + LAN push tool, attached to every release |
| `Plutonium/` | UI library (git submodule) |

The backend (`net`/`archive`/`queue`/`extract`/`config`/`fsutil`/`md5`/`json`/
`i18n`) is plain C; only the UI layer is Plutonium C++.

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
