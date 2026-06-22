# TicoDL+

A Nintendo Switch homebrew that browses and downloads game files from
[archive.org](https://archive.org) and drops them straight into the folder
layout used by **TICO**, decompressing archives along the way. Built for the
devkitPro / libnx toolchain using Claude Code. **Yes, this is 100% AI created, 
but it works.**

> TicoDL+ ships **no ROMs, no collections, and no
> credentials** — it's an empty downloader. **You provide your own** archive.org
> item ids (and optionally your own archive.org keys for restricted items). No
> links to any content are bundled. You are responsible for complying with the
> laws that apply to you.

---

## Features

- **Console groups with multiple repos.** Each console (snes, genesis, psx, …)
  can hold several archive.org collections, so if one set is missing games you
  can add another. Everything for a console installs into the same
  `sdmc:/tico/roms/<console>/` folder.
- **Background download queue.** Queue many files and keep browsing while they
  download FIFO in the background. Progress, speed, cancel and retry are all in
  the queue view, and the current download is always shown at the bottom of the
  screen.
- **Resume + persistence.** Interrupted downloads resume from where they
  stopped, and the queue survives closing the app — pending downloads pick back
  up on next launch.
- **Automatic extraction.** `.zip` / `.7z` / `.rar` / `.tar.*` archives are
  unpacked into the console folder automatically; plain files are moved as-is.
- **Integrity checks.** Downloads are verified by size and, when archive.org
  provides one, MD5 — corrupt files are rejected instead of installed.
- **Find things fast.** On-screen name filter, a position scrollbar for huge
  lists, and green `*` markers next to files you already have installed.
- **At-a-glance status bar.** Free SD space and battery % in the header; the live
  download (`DL 2/5: … 47% @ 3.4 MB/s`) above the footer.
- **In-app self-update** from GitHub releases — no manual `.nro` copying.
- **Installed browser** to view and delete what's in `sdmc:/tico/roms`.
- **Built-in help** (press **ZR** on the main menu for the full control list).


---

## Screenshots

_Coming soon._

<!-- Drop images in docs/screenshots/ and reference them here, e.g.:
![Main menu](docs/screenshots/main-menu.jpg)
![Browsing a repo](docs/screenshots/browse.jpg)
![Download queue](docs/screenshots/queue.jpg)
![Settings](docs/screenshots/settings.jpg)
-->

---

## pre-req

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

1. On the main menu, press **Y** (add).
2. Choose the **console** the files belong to — this is the
   `sdmc:/tico/roms/<console>` folder TICO reads from. (The selectable list comes
   from `tico_consoles`; you can't pick an unsupported folder.)
3. Enter a **name** for the repo — any label, e.g. `My SNES set`.
4. Enter the archive.org **item id** (the `<id>` from `archive.org/details/<id>`).

The console now appears on the main menu. Open it with **A**, pick the repo, and
you'll see its file list. Repeat **Y** to add more collections — a console can
hold several. (You can also edit `dl_sources.json` directly — see
[Configuration](#configuration).)

### 2. Add your archive.org keys (optional)

Public collections download anonymously and need **no keys**. You only need keys
for **restricted** items that require an archive.org account.

1. On a computer, sign in at [archive.org](https://archive.org) and open your S3
   keys page: <https://archive.org/account/s3.php>. You'll get an **access key**
   and a **secret key**.
2. In TicoDL+, open **Settings** (**L**).
3. Highlight **Archive.org access key**, press **A**, and type your access key.
4. Highlight **Archive.org secret**, press **A**, and type your secret. It's
   stored but shown only as `<set>` (never displayed again).

Keys live only on your SD card (`sdmc:/switch/ticodlplus/credentials.json`) and
are sent only to archive.org hosts.

### 3. Download

1. Open a console (**A**) and pick a repo to browse its files.
2. Highlight a file and press **A** to add it to the download queue.
3. Press **ZL** to watch the queue. Completed downloads extract/move into
   `sdmc:/tico/roms/<console>/` automatically.

---

## Controls

The header shows **free SD space** and **battery %**. The line above the footer
shows the **active download**. Press **ZR** any time on the main menu for the
in-app help.

### Main menu

There are two layouts, toggled by **Settings → Group consoles**:

**Grouped (default)** — one row per console:

| Key | Action |
|-----|--------|
| Up/Down | move (hold to scroll) |
| A | open the console (see its repos) |
| Y | add a repo (pick a supported console, then enter label + archive id) |
| X | delete the console and its repos (confirm) |
| ZL | download queue |
| R | installed games |
| L | settings |
| ZR | help |
| − | manual archive.org URL / item id |
| + | exit |

**Flat** — one row per repo (`console - repo`):

| Key | Action |
|-----|--------|
| A | browse this repo |
| X | edit this repo |
| Y | add a repo |
| (ZL / R / L / ZR / − / + as above) | |

### Console screen (grouped)

| Key | Action |
|-----|--------|
| A | browse the selected repo |
| X | edit the repo (label / archive id / URL / active) |
| Y | add a repo to this console |
| − | delete the selected repo (confirm) |
| B | back |

### Browsing a repo's files

| Key | Action |
|-----|--------|
| Up/Down | move (hold to scroll) |
| ZL / ZR | page up / down |
| L / R | previous / next repo |
| A | add the file to the download queue |
| Y | filter the list by name (blank = clear) |
| X | refresh (re-fetch metadata, ignoring the cache) |
| B | back |

A green `*` before a file means it already appears to be installed.

### Download queue (ZL)

| Key | Action |
|-----|--------|
| Up/Down | move |
| A | cancel the selected item |
| X | retry a failed/cancelled item (resumes from any partial file) |
| Y | clear finished / failed / cancelled items |
| B | back |

Statuses: `wait` · `dl` · `vrfy` (verifying) · `unzip` · `done` · `FAIL` · `cxl`.

### Installed games (R)

| Key | Action |
|-----|--------|
| Up/Down | move |
| ZR | page down |
| A | open folder / show file details |
| X | delete the selected file or folder (confirm) |
| ZL | jump to the download queue |
| L / B | back |

### Settings (L)

D-pad to move, **A** to toggle/open, **B** to exit. Changes save immediately.

- **Metadata cache** — load cached metadata instantly vs. always re-fetch.
- **Stay awake while downloading** — keep the console from sleeping while the
  queue is active.
- **Group consoles** — grouped vs. flat main menu.
- **Archive.org access key / secret** — optional S3 credentials for restricted
  items (the secret is shown as `<set>`, never echoed).
- **Check for updates** — in-app self-update.
- **View download log** — history of completed/failed downloads.
- **Credits**.

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
restricted items. Public collections download anonymously and need no keys.
**Use your own keys — none are bundled.**

### Files on the SD card

| Path | Purpose |
|------|---------|
| `sdmc:/switch/ticodlplus/dl_sources.json` | console groups + repos + supported list |
| `sdmc:/switch/ticodlplus/credentials.json` | archive.org S3 keys (optional) |
| `sdmc:/switch/ticodlplus/prefs.json` | settings |
| `sdmc:/switch/ticodlplus/queue.json` | saved download queue |
| `sdmc:/switch/ticodlplus/cache/<id>.json` | cached metadata |
| `sdmc:/switch/ticodlplus/downloads/` | temporary `.part` files |
| `sdmc:/switch/ticodlplus/downloads.log` | download history |
| `sdmc:/switch/ticodlplus/debug.log` | network/extraction diagnostics |
| `sdmc:/tico/roms/<console>/` | final destination |

---

## Updating (in-app)

Open **Settings (L) → Check for updates**. TicoDL+ checks the GitHub releases for
a newer version and, if found, downloads the new `.nro` and replaces itself
(keeping a `.previous` backup). Close and relaunch to run the new build.

---

## Archive extraction notes

Most archives extract automatically. A known limitation: some **RAR3-compressed
`.rar` files that use RAR's programmable filters cannot be decompressed** by the
bundled library — for those, the raw `.rar` is saved into the console folder and
you'll need to extract it on a PC. If a specific archive won't unpack, check
`debug.log` for the exact reason.

---

## Networking

devkitPro's libcurl uses the libnx **`ssl` system-service** backend, verifying
against the console's own certificate store. This works on **real hardware**.
Emulators that stub the `ssl` service (e.g. Ryujinx) will fail HTTPS regardless
of the app — metadata browsing and downloads should be tested on hardware. Every
request's result is logged to `debug.log`.

---

## Building from source

TicoDL+ builds with the **devkitPro** toolchain (devkitA64 + libnx) plus a few
portlibs. Source layout and build steps below.

### Prerequisites

1. Install **devkitPro** and the `switch-dev` group — see the
   [Getting Started guide](https://devkitpro.org/wiki/Getting_Started). Make sure
   `DEVKITPRO` is set in your environment (the installer sets this; on Windows use
   the **MSYS2** shell that ships with devkitPro).
2. Install the portlibs the app links against (dependencies, e.g. the
   bzip2/xz/zstd/lz4/expat codecs, are pulled in automatically):
   ```sh
   dkp-pacman -S switch-curl switch-libarchive switch-zlib
   ```

### Build

```sh
make            # builds TicoDLplus.nro (auto-bumps the patch version)
make clean      # removes build output (does not bump the version)
```

On Windows, run these inside the devkitPro MSYS2 shell, e.g.:

```sh
/c/devkitPro/msys2/usr/bin/bash.exe -lc "cd /c/path/to/ticodlplus && make"
```

Output is **`TicoDLplus.nro`**. Each `make` increments the patch version via
`bump_version.sh` (which also generates `source/version.h`, kept out of git). To
publish a release, run `make` then `sh release.sh` — it tags a GitHub release with
the built version, attaches the `.nro`, and uses the matching `CHANGELOG.md`
section as the notes.

> Networking (metadata + downloads) only works on **real hardware** — the libnx
> `ssl` backend isn't stubbed there. Emulators like Ryujinx will fail HTTPS.

### Source layout

| Path | Responsibility |
|------|----------------|
| `source/main.c` | text UI: menus, browsing, queue/installed views, settings |
| `source/net.*` | libnx sockets + libcurl (downloads, HTTP GET, logging) |
| `source/archive.c` / `iarchive.h` | archive.org metadata + download URLs |
| `source/queue.*` | background download worker (resume, verify, extract, persist) |
| `source/extract.*` | libarchive zip/7z/rar/tar extraction |
| `source/config.*` | `dl_sources.json` / credentials / prefs load + save |
| `source/fsutil.*` | mkdir-p, move, recursive delete, free-space |
| `source/update.*` | GitHub release check + in-app self-update |
| `source/md5.*` | MD5 for download verification |
| `source/jsonutil.*`, `source/jsmn.*` | JSON parsing (vendored jsmn) |

---

## Credits

- Built with [devkitPro / libnx](https://devkitpro.org/),
  [libcurl](https://curl.se/libcurl/), and
  [libarchive](https://www.libarchive.org/). JSON parsing via the vendored
  [jsmn](https://github.com/zserge/jsmn) tokenizer (MIT).
- Kudos to the creator of TICO https://ticoverse.com/
- Inspired by TicoBro https://github.com/StonedModder/Ticobro concept
