# TicoDL+ — Changelog

Notes for each release. `release.sh` pulls the section matching the version in
`VERSION` and attaches it to the GitHub release. Add a `## <version>` section
here before running a release.

## 2.0.3
- Loading a repo's file list no longer freezes the screen: the metadata now
  downloads in the background with an animated **"Loading metadata..."**
  indicator.
- **Installed** tab is now a table: files show a color-coded size column, and
  folders show the number of files/apps inside in a right-hand column.
- **Browse** tab now shows each console's repo count in its own right column.
- **Download queue**: reorder items with **ZL/ZR** (an item can't move above the
  one that's actively downloading).
- Queue **retry** now resumes a cancelled/failed item in place instead of
  sending it to the bottom of the list.
- Queue polish: the selected-row highlight is now teal (distinct from the tab
  bar), and the active download's text is white.
- **Download log**: press **X** to clear it (asks for confirmation first).
- **Settings**: editing the archive.org secret now pre-fills the current value
  so it's easy to tweak.
- Footer button hints are now spread evenly across the row.

## 2.0.2
- The in-app updater now shows live **download progress** instead of looking
  frozen while it fetches the new build.
- New **dark theme**: near-black background with light text.
- **Tabbed navigation**: a top bar with Browse / Installed / Queue / Settings,
  cycled with the L/R shoulder buttons. (Switching repos inside a file list
  moved to D-pad Left/Right.) Button hints now live in a bottom footer.
- File and queue lists are now real **tables**: the file name and a right-
  aligned size/info column, with sizes color-coded by KB / MB / GB and queue
  rows colored by status.

## 2.0.1
- Fixed a crash when exiting with **+** ("software was closed because an error
  occurred"). The app now shuts the background download worker thread and its
  services down cleanly on exit; a download interrupted by exiting keeps its
  partial file and resumes on the next launch.
- **Download all**: press **−** in a file list to queue every file matching the
  current filter at once (with a confirmation).
- **L/R switch repo** while browsing a file list — jump to the previous/next
  repo of the same console without backing out (restores 1.x behavior).
- Build: the version string now comes from a single source (the `VERSION` file);
  the Makefile bakes it into the app and regenerates `version.h` automatically.

## 2.0.0
- New **graphical UI** (Plutonium / SDL2) replacing the text console: full-screen
  menus with on-screen highlight, dialogs, toasts, and the Switch's shared fonts.
- Full parity with 1.x: console groups, add/edit/delete repos & consoles, browse +
  download, background queue (cancel/retry/clear with live progress), installed
  browser, file filter + installed markers, settings, manual-URL download, download
  log, in-app self-update, SD/battery status, hold-repeat navigation + paging.
- Same proven backend (downloads, resume, MD5 verification, extraction) as 1.x.
- Controls: D-pad (hold to repeat) / ZL-ZR page; L queue, R installed, R-stick
  settings, + exit, B back.

## 1.0.0
- First stable release.
- Ships with NO collections — only the supported console folder names are
  bundled, no archive.org item ids or links. You provide your own collections
  in-app (press Y and enter an archive.org item id) or by editing
  dl_sources.json on your SD card.
- Console list sorted A-Z; brighter main-menu and installed-folder text.
- Background download queue with resume, persistence, MD5 verification, and
  auto-extraction; console groups with multiple repos; in-app updater; full
  text-UI polish (highlight bar, consistent controls, toasts, breadcrumbs).

## 0.1.65
- File lists now show a breadcrumb in the header (e.g. "snes > USA Complete") so
  you can see which console/repo you're browsing.
- List headers show your position and total, e.g. "(12/340)".
- Friendlier repo editor labels: Name / Archive.org ID / Download URL / Enabled.
- Help screen now includes a color legend (size colors, the * installed marker,
  and queue status colors).

## 0.1.64
- Selected list rows now show a full-width highlight bar (much easier to see
  where you are) across every screen.
- Consistent buttons everywhere: A = open/confirm, B = back, X = edit, Y = add,
  − = delete. (Manual URL download moved into Settings.)
- Action confirmations: adding/deleting/saving and queue actions now show a brief
  on-screen toast so you know it worked.

## 0.1.61
- Security: downloaded filenames are now sanitized before use as paths, so a
  malicious metadata entry can't write outside the intended folder.
- Security: archive.org S3 credentials are only sent to archive.org hosts, never
  to a repo URL pointing elsewhere.
- Archives that can't be extracted now show as "saved" (logged "saved-raw")
  instead of "done", so it's clear the raw file was kept un-unpacked.
- Fixed a thread-safety issue in download-history logging (localtime_r).

## 0.1.60
- Adding a repo now shows a picker of TICO-supported consoles instead of a
  free-text field, so repos are always grouped under a valid console folder.
- The supported list comes from "tico_consoles" in dl_sources.json; you can no
  longer create unsupported consoles in-app (edit the JSON to change the set).
- Manual URL downloads also pick the target folder from this list.

## 0.1.59
- Console groups: each console can now hold multiple download repos (sources),
  so you can add several archive.org collections per system. All of a console's
  repos install into the same tico/roms/<console> folder.
- New Settings toggle "Group consoles": ON shows consoles (open one to see its
  repos); OFF shows a flat list with one row per repo ("console - repo").
- Full in-app editor: add/delete consoles, and add/edit/delete repos (label,
  archive id, URL, active) from the menus.
- dl_sources.json updated to the grouped schema and prepopulated with many more
  repos across NES/SNES/Genesis/Saturn/Dreamcast/PSX/PSP and more. Old config
  files are auto-migrated.

## 0.1.56
- Footer no longer runs off the edge: removed the trailing "[DL Queue: N active]"
  text (the info now lives in the download monitor line).
- Download monitor now reads "DL 1/2:" — the current item number and the total
  number of items in the queue.

## 0.1.55
- Fixed archive extraction silently dropping files (notably some `.rar` sets
  where only the raw archive ended up in /roms). The extractor now tolerates
  libarchive warnings during header/data reads instead of discarding the file.
- Extraction problems are now written to `debug.log` with the exact reason.
- Releases now publish with release notes pulled from this changelog.

## 0.1.54
- Header shows SD space and battery % (no labels).
- Active-download line now matches the footer style, with a spacer row above it.
- Installed browser: L goes back a level, ZL opens the download queue.

## 0.1.53
- Added a scrollbar / position indicator to long lists.
- Added a Controls / Help overlay (ZR on the main menu).

## 0.1.52
- Battery percentage shown in the header.

## 0.1.50
- More reliable update check: uses the releases list with retries, so it no
  longer trips on GitHub's intermittent `/releases/latest` errors, and it picks
  the highest version itself.

## 0.1.47
- "Installed" markers (`*`) on files you already have.
- Live download status line; retry failed downloads; safer delete confirmation;
  persistent SD free-space readout.

## 0.1.43
- Resume interrupted downloads, persistent download queue across launches, MD5
  checksum verification, and an on-screen file-name filter.
