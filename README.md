# TicoDL+ (beta)

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
> links to any content are bundled. You are responsible for complying with the
> laws that apply to you.

---

## Features

- **Browse & organize**
  - Multiple repos per console — add extra archive.org collections when one set is incomplete
  - Show/hide consoles from **Settings → Manage consoles**
  - On-screen name filter, `*` markers for already-installed files, ZL/ZR paging

- **Download queue**
  - Queue files and keep browsing — downloads run in the background
  - Up to **5 simultaneous downloads** (configurable), each with its own speed tracking
  - Pipelined extraction — the next download starts while the previous archive unpacks
  - Progress bar, speed, ETA, cancel, retry (resumes in place), reorder (ZL/ZR)
  - Queue the entire file list at once with **−** (free-space check included)
  - Queue persists across app restarts; interrupted downloads resume automatically

- **Automatic extraction**
  - `.zip` / `.7z` / `.rar` / `.tar.*` unpacked into the console folder; plain files moved as-is
  - Integrity verified by size and MD5 — corrupt files are rejected

- **Installed browser**
  - Sorted alphabetically by full console name (e.g. "Nintendo Entertainment System (NES)")
  - Multi-select with **Y**, bulk-delete with **−**, rename with **X**

- **TICO integration**
  - Auto-detects TICO and reads its ROM folder path
  - Falls back to the default path with a warning if TICO isn't found

- **In-app self-update**
  - One-tap update from GitHub releases — press **B** to cancel

---

## Screenshots

> _Screenshots coming soon._

<!-- Add screenshots of the Browse, Installed, Queue, and Settings tabs here. -->

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
are sent only to archive.org hosts.

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
| `sdmc:/tico/roms/<console>/` | default ROM destination (or custom path from TICO's config) |

---

## Updating (in-app)

Open **Settings → Check for updates**. TicoDL+ checks the GitHub releases for a
newer version and, if found, downloads the new `.nro` (with a live progress
indicator) and replaces itself (keeping a `.previous` backup). Press **B** to
cancel the download at any time — the partial file is discarded and you're
returned to Settings. Close and relaunch to run the new build.

---

## Archive extraction notes

Most archives extract automatically. A known limitation: some **RAR3-compressed
`.rar` files that use RAR's programmable filters cannot be decompressed** by the
bundled library — for those, the raw `.rar` is saved into the console folder and
you'll need to extract it on a PC. If a specific archive won't unpack, check
`debug.log` for the exact reason.

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
  history (`downloads.log`, viewable in Settings → View download log), and the
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
| `Plutonium/` | UI library (git submodule) |

The backend (`net`/`archive`/`queue`/`extract`/`config`/`fsutil`/`md5`/`json`) is
plain C — shared unchanged from the original text-console version; only the UI
layer is Plutonium C++.

---

## License

Released under the [MIT License](LICENSE) — free to use, modify and
redistribute. The only condition is that the copyright notice and license stay
included, so **please keep the credit**.

TicoDL+'s own code and the [Plutonium](https://github.com/XorTroll/Plutonium) UI
library it builds on are both MIT-licensed, so the project is cleanly permissive.
Third-party license notices (Plutonium © XorTroll, jsmn, etc.) are collected in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and ship with any distribution.

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
