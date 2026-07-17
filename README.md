# <img width="256" height="256" alt="icon" src="https://github.com/user-attachments/assets/0c0cbe7e-3e2e-4068-9518-44179f6406d3" /> TicoDL+ (beta)

> **This is a beta release.** Features may change and bugs are expected.
> Please report issues on the [GitHub Issues](https://github.com/digdat0/ticodlplus/issues) page.

A Nintendo Switch homebrew that browses and downloads game files from
[archive.org](https://archive.org) and drops them straight into the folder
layout used by **TICO**, decompressing archives along the way. Built for the
devkitPro / libnx toolchain using Claude Code. **Yes, this is 100% AI created, 
but it works.**

> TicoDL+ ships **no ROMs, no collections, and no
> credentials** — it's an empty downloader. **You provide your own** archive.org
> item ids (and optionally your own archive.org keys for restricted items). No
> links to any content are bundled.
>
> **Disclaimer:** This project does not condone piracy or copyright infringement in any form.
> The screenshots shown use placeholder file names for illustration only. Please do not discuss
> piracy, ROM sources, or copyrighted content in issues, pull requests, or discussions —
> such will be removed. You are responsible for complying with the laws that apply to you.

---

## Features

- **Browse & organize**
  - Consoles shown with their full names (e.g. "Super Nintendo Entertainment
    System (SNES)"), grouped by console — or a flat repo list (Advanced toggle)
  - Multiple repos per console — add extra archive.org collections when one set is incomplete
  - **Pin favorites** to the top (★) with **D-pad Right** — works the same on
    consoles, repos, and installed folders
  - **Global search** with **−** across every cached repo — results tagged with
    their console (and a `*` if already installed), downloadable straight from
    the results
  - On-screen name filter (**Y**), sort by name/size (**X**), `*` markers for
    already-installed files, ZL/ZR paging
  - Show/hide consoles from **Settings → Advanced settings → Manage consoles**


- **Download queue**
  - Queue files and keep browsing — downloads run in the background
  - Up to **5 simultaneous downloads** — change the limit in Advanced and it
    applies immediately: raising it starts more queued items, lowering it
    **pauses** the excess downloads, which auto-resume (from where they
    stopped) as slots free up
  - Transient server errors (HTTP 5xx / throttling) are retried automatically
    with backoff, resuming the partial file
  - Pipelined extraction — the next download starts while the previous archive unpacks
  - Progress bar, speed, ETA, cancel, retry (resumes in place),
  - Reorder one row (ZL/ZR) or jump to top/bottom (D-pad ◀/▶) — the active download stays put
  - Queue actions menu (**Y**): retry every failed item or clear finished ones
    at once; a summary toast reports the tally when a batch finishes
  - Queue the entire file list at once with **−** (free-space check included)
  - Queue persists across app restarts; interrupted downloads resume automatically
  - Network-loss aware: if the connection drops, active downloads pause
    (keeping their partial files) and the rest stay queued — everything
    resumes automatically, in order, when the network comes back
  - Stalled transfers time out instead of hanging, and resume from where they
    stopped
  - Download history with one-press **re-download from the log**

- **Automatic extraction**
  - `.zip` / `.7z` / `.rar` / `.tar.*` unpacked into the console folder; plain files moved as-is
  - Integrity verified by size and MD5 — corrupt files are rejected

- **Installed browser**
  - Sorted alphabetically by full console name, pinned folders first
  - Re-sort with **D-pad Left** (name A–Z / Z–A / size); folders stay grouped
    and pinned ones stay on top
  - **Search installed games** with **−** — a recursive scan of the ROM
    folders; open a result to jump straight to its folder
  - Multi-select with **Y**, then **−** to delete the marked items; rename
    with **X**

- **Card view** (optional)
  - Toggle **Advanced settings → Card view** to browse consoles as a 4-wide
    grid of cards (icon, name, repo/app counts) on the Browse tab and the
    Installed root — file lists stay as tables
  - D-pad/stick moves in all four directions; **A** opens, **X** pins,
    **−** searches; tap/drag works too

- **Touch support**
  - Tap a tab to switch to it; tap a list row to select it, tap it again to
    activate; drag up/down to scroll — the whole app works in handheld mode
    without buttons

- **25 languages & themes**
  - Full UI translation (English, Español, Français, Deutsch, 日本語, 中文, and
    20 more) from **Settings → Language** (if you speak a native language and can
    improve, log an issue or drop a PR. Lang files here: https://github.com/digdat0/ticodlplus/tree/main/lang
  - Light and dark themes (**Settings → Theme**)

- **Status header**
  - Live network indicator (Wi-Fi signal bars, full bars when docked on wired
    LAN, red when offline), free SD space, battery level (+ while charging)
  - Optional no-network warning at startup (Advanced toggle)

- **TICO integration**
  - Auto-detects TICO and reads its ROM folder path
  - Falls back to the default path with a warning if TICO isn't found
  - **Override the ROM folder** from **Settings → Advanced settings → ROM
    folder** if you don't use `tico/roms` (e.g. a plain `roms/` on your SD root):
    an on-screen SD-card browser lets you navigate to the folder and pick it
    (**X** to use the current folder, **Y** to reset to automatic detection)

- **Data management**
  - **Settings → Manage data**: clean up the temporary downloads folder and the
    metadata cache (entries tagged by console), singly or all at once
  - **Refresh all metadata** in one go (with live progress) — useful before a
    global search, which covers cached repos; per-repo hard refresh lives on
    the repo edit screen
  - Unresumable leftover `.part` files are cleaned up automatically at startup

- **In-app self-update**
  - One-tap update from GitHub releases — the version check runs in the
    background with a retry counter; **B** dismisses the check or cancels the
    download. Installs are validated and staged so a power loss can't corrupt
    the app
  

---

## Screenshots

Console list with repo's underneath, card view

<img width="1920" height="1080" alt="0-browse" src="https://github.com/user-attachments/assets/77375940-4123-425b-ad8d-2cd098f0f11b" />

#

Browse Repo List per Console,

<img width="1024" height="576" alt="1-browse repo" src="https://github.com/user-attachments/assets/615582df-d71c-41f8-95be-8d40ef493a4f" />

#

Installed app console view, filesize and number of files, card view


<img width="1920" height="1080" alt="2-installed cards" src="https://github.com/user-attachments/assets/58f7e0f8-8eeb-45a1-81da-070064357544" />

#
Installed apps list view

<img width="1024" height="576" alt="3-installed list" src="https://github.com/user-attachments/assets/c27a79a7-a599-4376-b00c-498f44fd895f" />

#
Download Queue - Progress, Speed, File size, time remaining, card view

<img width="1920" height="1080" alt="4-dl cards" src="https://github.com/user-attachments/assets/73d0391d-9dac-4738-908e-b147d40f0ed0" />

---

Download Queue - Progress, Speed, File size, time remaining, list view

<img width="1024" height="576" alt="11- dl list" src="https://github.com/user-attachments/assets/d86354e0-7017-441f-b3b8-73fcc6bf27de" />

---

Console list with repo's underneath, list view

<img width="1024" height="576" alt="5-browse list" src="https://github.com/user-attachments/assets/2bd5a11e-f82d-4529-a22e-26b950f26578" />

---

Installed games by console, list view

<img width="1920" height="1080" alt="7-installed list" src="https://github.com/user-attachments/assets/38faa657-3b3e-408d-89dd-100b4e9ddc9b" />

---
Installed games, list view


<img width="1024" height="576" alt="8-installed apps list" src="https://github.com/user-attachments/assets/58d5e9e0-2655-47e6-a41f-ba44929d2edf" />

---
Options card view

<img width="1920" height="1080" alt="9-options card" src="https://github.com/user-attachments/assets/e86feeda-e7ba-43ae-aec6-b36ccc6a197b" />

---
Options list view

<img width="1920" height="1080" alt="10-options card png" src="https://github.com/user-attachments/assets/2826f510-af90-429a-87c7-7dd07f4e9d09" />

---

## Prerequisites

1. Download TICO from https://ticoverse.com/

---

## Install

1. Download `TicoDLplus.nro` from the
   [latest release](https://github.com/digdat0/ticodlplus/releases/latest).
2. Copy it to your SD card at:
   ```
   sdmc:/switch/TicoDLplus/TicoDLplus.nro
   ```
3. Launch it from the homebrew menu.

On first run it seeds an **empty** `dl_sources.json` containing only the list of
supported console folders — **no collections or links are included**. Add your
own collections in-app (press **Y** and enter an archive.org item id) or by
editing `dl_sources.json` on your SD card.

---

## Quick start

TicoDL+ starts **empty** — you add your own collections before anything shows up.

### 1. Add a collection

A *collection* is an archive.org **item** that holds the game files for a system.
Each item has an **item id** — the last part of its URL, e.g. for
`https://archive.org/details/MyExampleItem` the id is `MyExampleItem`.

1. On the **Browse** tab, press **Y** (add).
2. Choose the **console** the files belong to — this is the
   `sdmc:/tico/roms/<console>` folder TICO reads from. (The selectable list comes
   from `tico_consoles`; you can't pick an unsupported folder.)
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
2. In TicoDL+, switch to the **Settings** tab (**L/R**).
3. Open **Advanced → Archive.org credentials**.
4. Edit the **Access key** and **Secret** — the edit field is pre-filled with the
   current value so it's easy to change.

Keys live only on your SD card (`sdmc:/switch/ticodlplus/credentials.json`) and
are sent only to archive.org hosts, and only over HTTPS.

### 3. Download

1. On **Browse**, open a console (**A**) and pick a repo to browse its files.
   (Repo metadata loads in the background with a brief "Loading…" indicator.)
2. Highlight a file and press **A** to add it to the download queue — or press
   **−** to queue the whole (filtered) list at once.
3. Switch to the **Queue** tab (**L/R**) to watch progress. Completed downloads
   extract/move into `sdmc:/tico/roms/<console>/` automatically.

---

## Console groups & supported consoles

A **console** is a folder under `sdmc:/tico/roms/` (e.g. `snes`). Each console
groups one or more **repos** — archive.org collections to download from.

The consoles you can use come from a fixed **supported list** (`tico_consoles`)
so files always land in a folder TICO understands. When you add a repo you pick
its console from that list; you can't create arbitrary/unsupported folders in
the app. To change the supported set, edit `tico_consoles` in `dl_sources.json`.

---

## Configuration

All config lives under `sdmc:/switch/ticodlplus/`.

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
  "tico_consoles": ["nes", "snes", "n64", "genesis", "psx", "psp"]
}
```

- `console` / `target` — display name and the `tico/roms` folder (usually the same).
- `a_id` — the archive.org item id.
- `URL` — download base; defaults to `https://archive.org/download/<a_id>` if omitted.
- `active` — include this repo.
- `tico_consoles` — the master list of selectable consoles.

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
| `sdmc:/switch/ticodlplus/dl_sources.json` | console groups + repos + supported list |
| `sdmc:/switch/ticodlplus/credentials.json` | archive.org S3 keys (optional) |
| `sdmc:/switch/ticodlplus/prefs.json` | settings (theme, language, pins, download limit, …) |
| `sdmc:/switch/ticodlplus/queue.json` | saved download queue |
| `sdmc:/switch/ticodlplus/cache/<id>.json` | cached metadata |
| `sdmc:/switch/ticodlplus/downloads/` | temporary `.part` files |
| `sdmc:/switch/ticodlplus/downloads.log` | download history (text) |
| `sdmc:/switch/ticodlplus/downloads.jsonl` | download history (structured, powers re-download from the log) |
| `sdmc:/switch/ticodlplus/lang/<code>.json` | optional language overrides (built-in translations ship in the app) |
| `sdmc:/switch/ticodlplus/debug.log` | network/extraction diagnostics (viewable + clearable in Settings → View logs) |
| `sdmc:/tico/roms/<console>/` | default ROM destination (or custom path from TICO's config) |

---

## Updating (in-app)

Open **Settings → Check for updates**. TicoDL+ checks the GitHub releases on a
background thread — the UI stays responsive and shows the attempt counter
(`(1/3)`) while transient errors are retried; press **B** to dismiss the check
and keep using the app. If a newer version is found, it downloads the new
`.nro` (with a live progress indicator), validates it's a real NRO, and
replaces itself using a staged copy-then-rename (keeping a `.previous`
backup), so an interrupted install can't corrupt the app. Press **B** to
cancel the download at any time — the partial file is discarded and you're
returned to Settings. Close and relaunch to run the new build.

---

## Translations

TicoDL+ ships 25 languages. All translation files live in the repo as plain
JSON — one file per language, keyed by English:

- [`lang/`](lang/) — the source of truth (edit here)
- [`romfs/lang/`](romfs/lang/) — the copy bundled into the app (keep in sync)

Spotted a wrong or awkward translation? Please
[open an issue](https://github.com/digdat0/ticodlplus/issues) naming the
language, the key (or the on-screen text), and your suggested wording — or send
a PR updating the file in **both** folders. For English (`en.json`) changes,
`tools/gen_i18n.py` must be re-run afterwards (it regenerates the strings baked
into the binary).

You can also override any language locally without rebuilding: copy the file to
`sdmc:/switch/ticodlplus/lang/<code>.json` and edit it — the SD copy takes
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

When a downloaded file lands in `sdmc:/tico/roms/<console>/` and a file of the
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

TicoDL+ 2.x is a **graphical app** built on the
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
git clone --recursive https://github.com/digdat0/ticodlplus
cd ticodlplus
make            # builds the Plutonium lib (submodule), then TicoDLplus.nro
make clean
```

If you cloned without `--recursive`, run `git submodule update --init` first.
On Windows, build inside the devkitPro MSYS2 shell:

```sh
/c/devkitPro/msys2/usr/bin/bash.exe -lc "cd /c/path/to/ticodlplus && make"
```

Output is **`TicoDLplus.nro`**. The version lives in `VERSION` (the single source
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
| `md5.*` | MD5 for download verification |
| `jsonutil.*`, `jsmn.*` | JSON parsing (vendored jsmn) |
| `i18n.*`, `lang/`, `tools/gen_i18n.py` | translations — English strings are generated into the binary from `lang/en.json`; the other 24 languages load from romfs |
| `Plutonium/` | UI library (git submodule) |

The backend (`net`/`archive`/`queue`/`extract`/`config`/`fsutil`/`md5`/`json`/
`i18n`) is plain C; only the UI layer is Plutonium C++.

---

## License

Released under the [MIT License](LICENSE) — free to use, modify and
redistribute. The only condition is that the copyright notice and license stay
included, so **please keep the credit**.

TicoDL+'s own code and the [Plutonium](https://github.com/XorTroll/Plutonium) UI
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
- Kudos to the creator of TICO https://ticoverse.com/
- Inspired by [TicoBro](https://github.com/StonedModder/Ticobro) — I wanted a
  simple downloader whose only job was to download, with some enhancements.
