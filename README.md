# TicoDL+

A Nintendo Switch homebrew that browses and downloads game files from
[archive.org](https://archive.org) and drops them straight into the folder
layout used by **TICO**, decompressing archives along the way. Built for the
devkitPro / libnx toolchain.

> Created by **digdat0**. TicoDL+ ships **no ROMs and no credentials** — you
> point it at public archive.org collections (and optionally supply your own
> archive.org keys for restricted items). You are responsible for complying with
> the laws that apply to you.

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

## Install

1. Download `TicoDLplus.nro` from the
   [latest release](https://github.com/digdat0/ticodlplus/releases/latest).
2. Copy it to your SD card at:
   ```
   sdmc:/switch/TicoDLplus/TicoDLplus.nro
   ```
3. Launch it from the homebrew menu.

On first run it seeds a default `dl_sources.json` with collections for many
systems. From then on you can update in-app (see **Updating** below).

---

## Quick start

1. Open a console from the main list (**A**).
2. Pick one of its repos to browse its file list.
3. Highlight a game and press **A** to add it to the download queue.
4. Keep browsing, or press **ZL** to watch the queue. Files extract/move into
   `sdmc:/tico/roms/<console>/` automatically when done.

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

Seeded on first run; editable by hand. Schema:

```json
{
  "console_list_groups": [
    {
      "console": "snes",
      "target": "snes",
      "repos": [
        {
          "label": "USA Complete",
          "a_id": "snes-usa-romset-complete-collection.-7z",
          "URL": "https://archive.org/download/snes-usa-romset-complete-collection.-7z",
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

Requires the devkitPro toolchain with libnx and the portlibs used by the app
(libcurl, libarchive and its codecs, zlib):

```sh
dkp-pacman -S switch-dev switch-curl switch-libarchive switch-zlib
make
```

Output is `TicoDLplus.nro`. `make` auto-increments the patch version on each
build; `make clean` removes build artifacts.

---

## Credits

- **digdat0** — creator.
- Built with [devkitPro / libnx](https://devkitpro.org/),
  [libcurl](https://curl.se/libcurl/), and
  [libarchive](https://www.libarchive.org/). JSON parsing via the vendored
  [jsmn](https://github.com/zserge/jsmn) tokenizer (MIT).
