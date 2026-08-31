# HaulNX — Changelog

> Formerly released as **TicoDL+ / ticodl+**. Entries below dated before the
> rename use the old name as it shipped at the time.

Notes for each release. `release.sh` pulls the section matching the version in
`VERSION` and attaches it to the GitHub release. Add a `## <version>` section
here before running a release.

## 2.2.0

**Console cover art is ~9x smaller and much faster; several box-art bugs fixed; new PS Vita icon.**

- Custom console cover art pushed from the desktop companion is now sent as a
  200×300 JPEG instead of a 600×900 PNG. This fixes slow tab navigation and
  missing/broken cover art that could show up once several consoles had
  custom art enabled at once.
- New desktop setting (on by default): "Auto-push cover art to the Switch
  right after picking it from the PC." Picking an image in the Console Art
  dialog now crops and sends it immediately, instead of requiring an extra
  manual "Push to Switch" step.
- Fixed a bug where enabling custom cover art on many consoles at once could
  make some consoles show blank, or another console's art, on the
  Library/Collections screens.
- The desktop app's Library folder setup now creates a subfolder for every
  console automatically, including ones added since you last picked your
  ROMs folder.
- New PS Vita console icon. PS Vita is now shown by default in the console
  list instead of requiring manual opt-in via Settings > Manage Consoles.

## 2.1.23

**Fix: backing out of the first-run guided tour without adding a repo could strand you on an empty Storage/Transfers/etc. screen.**

- The guided tour opens each step on the actual screen it's describing (Queue,
  Install Folders, Storage, Transfers, ...). If you closed the tour early (or
  said "Not now" at the end) before setting up your first repo, you'd land on
  whatever step's screen was showing behind the dialog instead of somewhere
  useful. Both exit paths now check whether a repo has actually been added
  yet and, if not, send you back to the Library tab.

## 2.1.22

**Full translation of the Help hub and guided tour into all 24 non-English languages.**

- The entire Help/Getting Started/How-To/Troubleshooting hub and the first-run
  guided tour (130 strings: the Help UI chrome, all 8 tour steps, all 6 Getting
  Started articles, all 24 How-To articles, and all 18 Troubleshooting
  articles, including the 6 added in 2.1.21) had never been translated since
  the feature was introduced — every non-English language silently fell back
  to English for that whole section. All 24 `romfs/lang/*.json` files now
  carry full translations, verified with zero keys missing against `en.json`
  and zero `gen_i18n.py` warnings.

## 2.1.21

**First-run tour now shows once, ever; six new Help articles; tour1 wording fix.**

- The first-run guided tour was gated on "no repos added anywhere yet," so it
  reappeared on every single launch until the user actually added one — even
  after they'd already seen it and closed it. It's now gated on a persistent
  `tourDone` flag, set the moment the tour opens, so it auto-shows at most
  once. Still reachable any time from Settings > Help > Getting Started >
  Replay the Guided Tour, which doesn't consult the flag.
- Tour intro copy reworded: "It also happens to browse curated archive.org
  collections..." read too much like a hedge; now "You can also browse
  curated archive.org collections...".
- Six new Help articles: How-To gets "Set a custom install folder for a
  console," "Control download speed and concurrency," and "Hide file types
  you don't want to see"; Troubleshooting gets "Downloads are slower than
  expected," "A file type isn't showing in Library," and "The first-run tour
  or Welcome screen won't come back."

## 2.1.20

**Queue tour step gets a mocked-up card; dialog titles wrap; tour copy cleanup.**

- Queue tab tour step: the real queue is empty on a fresh install (and its
  live per-frame refresh doesn't run while the tour's dialog loop owns
  rendering, same root cause 2.1.16 fixed for the tab bar), so there was
  nothing behind that step to point at. Now shows one mocked-up card: an
  N64 download partway through, in the same visual state a real one would
  be in.
- Dialog titles now word-wrap instead of running off the panel's edge — hit
  by "Point your emulators at your library" in the tour, but this is a
  SideMenu-level fix so it covers every long title in the app, not just
  that one.
- Reworded tour1, tour3, tour5, tour7, and tour8's copy to drop em dashes.
- Collections tab step no longer points at a magnifying-glass search icon
  that doesn't exist there (search only works across repos you've already
  added, so it's not a way to find new ones anyway) — now just points at
  X > Add Repo on a console, which is accurate in the default (grouped)
  view the tour actually shows.
- Welcome's two options are reordered so "Add your first repo" (renamed
  from "Add a repo by hand") leads, ahead of "Send from a computer".

## 2.1.19

**Tour gets a dedicated Collections step; Welcome copy drops "not just a downloader."**

- The tour used to jump straight from Library into "building your library"
  (picking files inside a repo) without ever explaining what a repository
  or the Collections tab actually is. New step, right after Library:
  "The Collections tab" — background switches there, and the copy covers
  what it's for and how to add a repository (Y, or Search), before the
  existing "building your library" step covers picking/queueing files.
- Welcome step's copy no longer leads with "not just a downloader" — it's a
  ROM management app that happens to browse curated archive.org collections
  for legitimate homebrew, public-domain, and preservation content.

## 2.1.18

**Help search moves to a footer button; more Help content; tour shows console cards on a totally fresh install.**

- Search is no longer a row on the Help hub — press **X** from Help (footer
  now reads "A open  B back  X search"). Same keyword search as before,
  otherwise unchanged.
- New How-To articles: how unzipping/extraction actually works, setting up
  your Archive.org/GitHub/SteamGridDB credentials, and a one-page map of
  what's in each Settings section. New Troubleshooting articles: an archive
  that downloads but won't extract, hitting a GitHub rate limit, and
  archive.org asking for a login. 6 new articles, 42 total.
- Guided tour: the Library step's trigger condition (no repos configured
  anywhere) meant the real Library listing was often just the "no games yet"
  empty state. That step now shows a card per known console instead, the
  same set/icons the Add tab lists, so the tour has something to point at
  even before a single game is installed.

## 2.1.16

**Fix: guided tour's tab bar didn't follow the screen it navigated to.**

- 2.1.15 made each contextual tour step navigate to its real screen, but the
  tab-bar highlight (Library/Add/Queue/Settings) stayed frozen on whichever
  tab was active when the tour started — the tour's blocking dialog loop
  never returns to the per-frame code that normally keeps the tab strip in
  sync. Each step now re-syncs the tab bar right after navigating, so it
  visibly follows along.

## 2.1.15

**Guided tour now shows its subject on screen; Help gets keyword search.**

- The guided tour navigates to the real screen behind each contextual step —
  "here's the Queue tab" now actually opens the Queue tab, "point your
  emulators at your library" opens Install Folders, and so on for
  Library/Add/Queue/Install Folders/Storage/PC Sync. General steps (welcome,
  the closing note) leave the background alone.
- New: **Settings > Help > Search**. Type one or more keywords (Switch
  keyboard) and every Getting Started/How-To/Troubleshooting article whose
  title or body contains all of them is listed, tagged with its category; A
  opens one, Y starts a new search, B returns to the Help hub.

## 2.1.14

**Fix: guided tour never ran on a fresh install. Help content overhauled.**

- **Fixed the first-launch bug**: the tour's trigger checked
  `g_cfg.console_count == 0`, but a fresh install seeds every supported
  console (with 0 repos each) from the bundled `dl_sources.json` — so
  `console_count` was never actually 0 and the tour silently never fired. It
  now checks whether any console has a repo configured instead, which is what
  "nothing to browse yet" actually means.
- Help content reframed around **ROM management, not just downloading**:
  organizing, verifying, and maintaining a library are now front and center
  in the tour and Getting Started, alongside browsing/downloading.
- New tour step and Getting Started/How-To articles on **pointing your
  emulators at your ROM folder** (sdmc:/roms/<console>, or a custom Install
  Folder) — previously missing entirely.
- How-To grew from 12 to 18 guides, adding: downloading several games at
  once, changing view/theme/language, choosing which consoles show, clearing
  out the Downloads/Inbox/largest-files scratch space, and viewing/exporting
  logs.
- Troubleshooting grew from 10 to 12, adding: "my emulator can't find my
  games" and "box art isn't showing."

## 2.1.13

**New: in-app Help hub + first-time guided tour.**

- Settings' old "Getting started" stub is now **Help**, with three sections:
  **Getting Started** (essentials for a fresh install, plus a "Replay the
  guided tour" action), **How-To** (12 short guides — adding a repo,
  downloading, box art, DAT verification, 1G1R, updating emulators, USB/Wi-Fi
  transfer, and more), and **Troubleshooting** (10 common problems with fixes,
  from stalled downloads to a companion that won't connect).
- New **first-time guided tour**: a 6-step walkthrough shown automatically the
  first time HaulNX has no consoles configured, covering Library/Add, queuing
  downloads, the Queue tab, keeping your library healthy, and the PC
  companion. Move forward and back freely, or close at any point — it ends in
  the same "send from a computer / add a repo by hand" prompt as before.
- Help content is English-only for now; other languages fall back to English
  for the new strings until translated.

## 2.1.12

**Desktop companion: manual update check + startup toggle.**

- New **Settings → General → App Utility updates** panel: a **Check for
  updates now** button (with status text) and a **Check for updates on
  startup** toggle, on by default. Turning the toggle off stops the silent
  on-launch check; the ⬆ Update pill only appears after you check manually.
- Note for anyone testing self-update from 2.1.10: that build's update check
  can never find a release — the bug fixed in 2.1.11 is baked into the 2.1.10
  binary itself and can't be patched after the fact. 2.1.11 onward checks
  correctly.

## 2.1.11

**Desktop companion: real self-update.**

- `self_update_check` was matching release tags against an `"app-utility-v"`
  prefix that `release.sh` never actually uses (it tags the bare version, e.g.
  `2.1.10`) — the update check could never find a release. Fixed.
- `self_update_run` used to download the update and hand it to `explorer.exe`
  as if it were an installer; for our actual asset (a portable `.exe`, no
  installer) that just opened a second copy and replaced nothing. It now
  replaces the running exe in place — rename aside, copy the new build over,
  relaunch, exit — with rollback on any failure.
- Applying an update now warns first if you have unsaved Archive Collection
  edits (same Export / Discard / Cancel prompt as closing the app), since
  updating relaunches it the same way.

## 2.1.10

**Desktop companion hardening + standalone App Utility HTML retired.**

- Desktop: `open_url` and the self-update installer launch now use `explorer.exe`
  instead of `cmd /C start`, closing a shell-metacharacter injection path in any
  clicked link or URL.
- Desktop: `esc()` now does a full HTML-entity encode (`&`, `<`, `>`, `"`, `'`)
  instead of just `"`/`<`, in both `index.html` and `local-ext.js`.
- Desktop: fixed a version-comparison bug, SD Card tab mtime/exact-size display,
  `.repo-name` wrapping, and moved the emulator-card version onto the header.
- The old standalone `tools/app-utility/appUtility-<version>.html` browser
  companion is retired — no longer built, released, or committed. The native
  desktop app (`HaulNX-AppUtility.exe`) is the only PC companion going forward.

## 2.1.6

**SD Card tab (desktop companion) — browse, add, rename and delete anywhere on the card.**

- New **Settings → PC Sync → Full SD card access** toggle (off by default,
  confirmed on enable): once on, the desktop app's new SD Card tab can browse,
  upload, download, rename, move and delete any file or folder on the SD
  card — not just HaulNX's own managed folders — over both Wi-Fi and USB,
  the same reach as mounting the card in Windows Explorer over MTP.
- Backed by a new `fs_*` route family on the Wi-Fi inventory server and a
  matching top-level "SD Card" object on the embedded MTP responder; large
  folder deletes run in the background so they can't freeze the connection
  for anyone else.
- The Downloads tab on the desktop companion now also mirrors the console's
  own Queue (live progress, history) via a new queue-status endpoint.
- Desktop companion stability: fixed a race where navigating the SD Card tab
  right after an upload could show a stale folder instead of the one you'd
  clicked into; fixed failed transfers sometimes logging twice in Download
  History; fixed a stuck "downloading…" row with no error if a download task
  died outright; fixed a small memory leak from dismissing finished/failed
  transfers; and fixed the same stale-overwrite race in the background
  device-inventory poll (could otherwise flip "Connected" back to
  "Disconnected" on its own, or show an outdated file listing).

## 2.1.0

**Box art, a customizable accent color, and a lot more solid downloads.**

**Box art**
- Card view can now show real cover art instead of a plain tile — fetched and
  cached automatically, with an optional SteamGridDB API key (Settings) for
  better hit rates.

**Accent color**
- Settings → Appearance → Accent Color: pick from 6 presets and the whole app
  follows — tabs, dialogs, the side menu, progress bars, and every other fill
  and outline that used to be a fixed blue.

**Downloads — reliability**
- Fixed a real bug where two queued items with the same name could corrupt
  each other's download by writing to the same temporary file; the queue also
  no longer lets you double-enqueue the same item.
- A stalled connection (weak signal, an archive.org node hiccup) is now
  correctly retried with backoff and resume instead of sometimes failing
  outright with a confusing error.
- The Queue tab shows **"Reconnecting…"** during that kind of retry instead of
  a blank speed field that looked like a dead download.
- Failed downloads now log the server's actual error response, so a repeated
  failure is diagnosable instead of just "http=500".

**Library maintenance**
- The DAT/hash/verify caches now clean out entries for files you've since
  deleted, moved, or renamed, instead of holding onto them indefinitely.

## 1.1.2

**Install a game straight from your PC, plus a live verify progress bar.**

**Install from PC**
- You can now push a game file from a browser on your PC straight onto the
  console over Wi-Fi — no USB cable, no re-downloading. The console runs a small
  upload page that streams the file directly to the SD card (in bounded slices,
  so the UI keeps rendering and shows a progress bar even for multi-gigabyte
  transfers).
- Two ways in:
  - **On the Installed tab**, press **X** on a console for an **Options** menu —
    *Receive from PC*, *Pin/Unpin*, and *Sort*. The same menu is available in
    both list and card view, so the controls no longer differ between the two.
  - **Settings › Install from PC** lets you pick the target console first, then
    opens the receive screen with on-device instructions.
- The **Y** button on a console is now purely *Install folder* — receiving a
  game is its own separate action, so neither is buried inside the other.
- New Settings section icon for Install from PC.

**Verify progress**
- MD5 verification now shows a **moving progress bar** instead of sitting on
  "vrfy". A large uncompressed ISO (which has no unzip step after it) used to
  look hung while it hashed; the bar now tracks the hash from 0–100%.

**Other**
- Footer button hints on the Installed tab were refreshed to match the new
  layout, and re-translated across all 24 supported languages.
- The bundled App Utility is now **`appUtility-v1.1.2.html`**.

## 1.1.1

**Speed-test reliability on slower connections.**

- The Diagnostics **speed test** now transfers a **50 MB** download sample
  instead of 90 MB. The larger sample made no measurable difference to the
  reading but pushed the minimum link speed that could finish inside the test's
  time limit up to ~6 Mbps — so a genuinely slow-but-working connection could
  time out and report a spurious failure. 50 MB drops that floor back to
  ~3.3 Mbps while still running long enough to measure accurately.
- Documentation: the README now calls out **custom install folders** (global
  and per-console), with a link to the new Custom Folders wiki page.

## 1.1.0

**A redesigned Settings, per-console install folders, a live speed test, and a
much wider console list.**

**Settings, rebuilt**
- Settings is now a set of **single-concern sections** — Appearance, Downloads,
  Filters, Storage, Collection Management, Account & Network, Updates, Logs,
  Diagnostics and About — each with its own icon, instead of one long list.
- **Logs** is a top-level section, and **Diagnostics** gains a **speed test**, a
  **self-test**, and an exportable **debug bundle** for troubleshooting.

**Per-console install folders**
- **Storage → per-console folders** lets you send each console's downloads to a
  folder you choose, instead of everything landing under one ROM root. The
  Installed tab follows those custom folders, so browsing and managing what you
  have keeps working wherever the files live.
- Each console row now reads as **icon, full name, (short name)** for clarity.

**More consoles**
- Added a large batch of systems: Famicom Disk System, Virtual Boy, Pokémon
  Mini, Game & Watch, SG-1000, Sega 32X, the PC Engine family (incl. SuperGrafx
  and PC-FX), the Neo Geo family (incl. CD and Pocket), the Atari line
  (2600/5200/7800/Lynx/Jaguar), WonderSwan (+ Color), ColecoVision,
  Intellivision, Odyssey², Vectrex, Channel F, 3DO, CD-i, Supervision, and
  arcade (Arcade / FinalBurn Neo). Existing installs pick them up on update.
- The **original launch console set keeps its previous defaults**; the newly
  added systems ship **disabled by default** in both the Browse and Installed
  console filters, so they're opt-in and don't clutter your lists.

**Speed test**
- The Diagnostics speed test reports **live upload and download rates** with a
  progress bar and ETA, rather than a single number at the end.

**Queue**
- You can now **cancel or retry an item while it's verifying or unpacking**,
  not only while it's downloading.

**Tidier on-disk layout**
- App data and logs are now organised into **`config/`** and **`logs/`**
  subfolders under `sdmc:/switch/HaulNX`, instead of sitting loose in the app
  folder. On update, existing files (including rotated logs) are **moved into
  the new folders automatically** — nothing to do by hand.

**App Utility**
- Bumped to **`appUtility-v1.1.0.html`**.

**Fixes**
- **Speed test no longer fails against Cloudflare.** The download leg was
  requesting a payload at the size Cloudflare rejects with HTTP 403, which
  surfaced as a failed test ("failed, cloudflare"). It now uses a size that
  stays under that ceiling.
- Removed the ROM-folder line from the foot of the main Settings page — the ROM
  folder is managed under Storage now, so it's no longer duplicated there.

## 1.0.2

**Bulk downloads, Nintendo Wii U support, and a two-way collection transfer.**

**Bulk downloads**
- **Y now selects files** in the Browse file list instead of opening the filter.
  Selections are keyed to the file, not the row, so you can filter, select,
  change the filter and select again to build one set across several passes.
  **X** opens a **View** menu holding the filter, the sort picker, **Select all
  shown** and **Clear selection**.
- **A queues the whole selection** when anything is marked, and still queues
  just the highlighted file when nothing is. A single deliberate A press is
  never silently skipped, even for a file you already have.
- Before anything downloads you get the totals: **how many files, how many
  bytes, free space, what the queue already owes, and how many were skipped as
  already installed**. If the set doesn't fit you can take **only what fits** or
  queue it anyway. Sizes come from the repo metadata, so this is known up front
  rather than discovered halfway through.
- Archives are flagged in that summary: they expand when unpacked, so the total
  shown is a floor, not the final footprint.
- The queue holds **256 items** (was 64).
- **Settings → Advanced → Skip installed on bulk add** (default **ON**) drops
  files you already have from a marked selection. It applies to bulk adds only.
- A download no longer starts when the card can't hold it. The queue **waits for
  free space** — shown in the queue header — instead of failing item after item
  as the disk fills. It resumes on its own once you free some up.

**Wii U**
- Added **Nintendo Wii U** (`wiiu`) as a supported console, with its own icon.
  Existing installs pick it up automatically on update — nothing to re-import.
- It ships **hidden** in **Settings → User interface settings → Manage
  consoles**: playing Wii U titles on a Switch depends on an unofficial emulator
  port, so it stays opt-in rather than implying first-class support. Turn it on
  there to use it.

**App Utility**
- Bumped to **`appUtility-v1.0.2.html`**.
- **Send to Switch** is now **Switch transfer**, and works both ways: the new
  **Get from Switch** button pulls the collection the console is currently
  running straight into the editor, so the round trip is fetch → edit → send
  back. It uses the same address and one-time code as sending, and asks before
  replacing anything you have open.

**Reliability**
- **A failed check no longer throws away a good download.** Verifying a file
  means reading it back off the card, and a read that fails partway through used
  to look exactly like a checksum mismatch — so the download was deleted and
  reported as corrupt. Those two cases are now told apart: a card hiccup keeps
  the partial file and a retry re-verifies it in seconds instead of pulling the
  whole thing down again. Only a genuine mismatch deletes.
- **Truncated writes are caught.** A downloaded file, an extracted file, a saved
  setting or a cached listing that didn't fully make it to the card is now
  reported as a failure rather than quietly left short. Writes go through the
  card in large batches, so the last chunk of a file lands when it is closed —
  which is exactly where a full or ejected card shows up.
- **Settings, credentials and collections are written atomically** — staged to a
  temporary file, checked, then moved into place. Losing power or filling the
  card mid-save can no longer leave a half-written file that fails to load on
  the next launch.
- **Archives extract more defensively.** An entry claiming to write outside its
  own declared size is dropped instead of trusted, and a failed seek is treated
  as an error rather than silently putting the data in the wrong place.
- **Logs are capped** — 1 MB for `debug.log`, 256 KB for `transfers.log`, 4 MB
  for the download history — with one previous generation kept. They can't grow
  without bound any more.
- A truncated language file can no longer put uninitialized memory on screen as
  UI text; keys past the cut fall back to English, the same as a missing key.
- The Wi-Fi receiver's one-time code drops the characters people misread (`0`,
  `1`, `i`, `l`), and a peer that dribbles a request a byte at a time can no
  longer hold the console's single connection slot open indefinitely.

## 1.0.1

**Maintenance and hardening release.** No new features on the console — this
tightens security around the Wi-Fi receiver, fixes a translation bug, and makes
the App Utility easier to use.

**Wi-Fi receiver hardening**
- The receive screen's address now carries a **one-time code** (e.g.
  `http://192.168.4.96:8080/a1b2c3`). Uploads and the config export require it,
  so a stray request on your network can't push a file or read your sources. A
  new code is generated every time the screen opens, and a Host-header check
  closes DNS-rebinding attempts.
- Unauthorized uploads are refused before the file body is read, and a peer that
  stops reading a response can no longer stall the console's UI for as long.

**App Utility**
- Bumped to **`appUtility-v1.0.1.html`**.
- Send to Switch now splits the address into **IP / Port / Code** fields: the IP
  and port are remembered between runs, the port defaults to `8080`, and only the
  one-time code needs re-typing each session (pasting the whole `IP:PORT/CODE`
  string into the IP box still auto-fills all three).

**Fixes**
- Translations are now validated against the built-in English format before use,
  and 24 languages had a stray `%s` repaired that showed a literal "%s" in the
  ROM-folder setting.
- Credentialed archive.org downloads can no longer be redirected to plaintext
  (HTTPS-only), and oversized metadata responses are capped so a hostile source
  can't exhaust memory.
- A resumed archive download now correctly picks back up at extraction instead
  of re-downloading after a restart.

**Wiki**
- Expanded the emulator wiki: added **Flycast** (Dreamcast/NAOMI/Atomiswave),
  **DuckStation** (PS1), **Yaba Sanshiro** (Saturn), an **Experimental**
  GC/Wii/PS2 page, and four reference guides — **BIOS Files**, **Overclocking**,
  **Disc Images & Multi-Disc Games**, and **Troubleshooting** — with the wiki
  home page reworked into a full landing hub.

## 1.0.0

**HaulNX — first release under the new name.** Formerly TicoDL+; the version
resets to 1.0.0 for the fresh brand and repository.

**Its own ROM library**
- HaulNX now owns a centralized ROM folder — **`sdmc:/roms/<console>/`** by
  default — instead of chasing an emulator's config. Downloads install there and
  the Installed tab browses it. Point whichever emulators you use at that folder
  (per-emulator setup in the wiki).
- The ROM-folder setting is now **Manage data → ROM Download Folder**; set a
  custom path or leave it on the default. The old TICO auto-detection and the
  "TICO not detected" startup prompt are gone.

**Update from GitHub — or push a build over Wi-Fi**
- **Settings → Check for updates** now asks where the update comes from:
  **From GitHub** (the release check, as before) or **Over Wi-Fi** — the console
  shows an address on this network; drop a HaulNX `.nro` on the served page or
  push one from the App Utility. The same version as installed is accepted,
  so new builds can be tested without a USB cable.
- Both paths stage the build safely (validated, swapped in on the next launch,
  previous build kept as a backup) and end with a **Restart now** option — the
  app relaunches itself straight into the new build.
- The receive screen shows **live progress** (percent and sizes) while a file
  is arriving — collections and app builds alike — and the sender shows a
  matching percentage.

**App Utility (formerly the repo editor)**
- The bundled HTML tool is now the **App Utility** (`appUtility-v1.0.0.html`).
  Its **Send to Switch** dialog gains two tabs: **Collection** (as before) and
  **App update (.nro)**, which validates the file and pushes it to the console.

**More consoles**
- Console icons for 31 more systems (3DO, the Atari line, Neo Geo family,
  PC Engine, WonderSwan, Vectrex, and friends) in the card views.

**Rebrand**
- New name, icon, and in-app wordmark throughout. Data now lives under
  `sdmc:/switch/HaulNX/` and the app ships as `HaulNX.nro`.

> Upgrading from TicoDL+? Your data folder path changed, so HaulNX starts fresh.
> Move anything you want to keep from `sdmc:/switch/ticodlplus/` (sources,
> credentials, prefs) and your ROMs from their old location into `sdmc:/roms/`.

## 2.0.1

**Download speed limits**
- Advanced settings gains two new caps: a **total download rate** shared across every
  active download, and a **per-download rate**. Pick from a range of presets or leave
  either at Unlimited. The total budget is split fairly across whatever is downloading at
  the moment and adjusts live as transfers start and finish.

**Move installed files up a folder**
- In an installed console's file view you can now mark one or more files and move them up
  into the parent folder. Each move is an atomic same-volume rename, so interrupting it
  (app close, power-off) never leaves a file half-moved. When the folder you emptied is
  left with nothing in it, TicoDL+ offers to delete it.

**Settings tidy-up**
- The **Metadata cache** toggle and the **ROM folder** setting have moved out of Advanced
  and into **Manage data**, alongside the other data and collection tools. Manage data is
  reordered to group importing, the ROM folder, and the download/metadata folders together.
- **Import collection** is now labelled **Import collection from computer**, to make it
  clearer where the collection comes from.

**Fixes**
- Setting **Max downloads** above 5 now actually runs that many at once. The queue was
  capped at 5 worker threads regardless of the setting (which allows up to 10), so a value
  of 6–10 silently behaved as 5.

**Translations**
- Completed translations across all 24 languages for strings that were still English-only:
  the file-extension filter, the new download speed limits, and the move-to-folder actions.

## 2.0.0

**Out of beta**
- TicoDL+ leaves beta with its 2.0 release. Thanks to everyone who tested along the way.

**Hide files you'd never download**
- The Browse file view can now filter out file types you never want to see — `.torrent`,
  `.xml`, `.sqlite`, `.out`, `.txt`, `.jpg`, and `.jpeg` are hidden by default. A new
  **UI settings → Filter out file extensions** screen lets you toggle the filter as a
  whole, enable or disable individual extensions, and add your own. Turning the filter
  off leaves your per-extension choices intact — it just stops hiding anything.

**Faster "Refresh all"**
- Refreshing metadata for a whole collection now fetches several repos in parallel
  instead of one at a time, so a full refresh finishes noticeably quicker.

**Stability**
- Fixed a race in the parallel refresh where the finished/failed counters could
  undercount when several repos completed at the same moment.

## 1.9.1-beta

**Safer imports**
- Collections you import (including over Wi-Fi) can no longer point a console's folder
  outside TicoDL+'s own ROM directory. A crafted `dl_sources.json` could previously use
  a folder name like `../../switch` to steer a download to write elsewhere on the SD
  card; folder names are now traversal-checked the same way filenames already were, and a
  bad one is rejected instead of downloaded.

**Stability**
- Fixed a large stack allocation on the UI thread in the pre-flight space check that could
  destabilize low-memory conditions; it now uses the same shared buffer as the rest of the
  queue code.

## 1.9.0-beta

**Won't fit? You'll know before it downloads**
- Before a download is queued, TicoDL+ now checks whether it — together with everything
  already waiting in the queue — will actually fit on your SD card. If it won't, you get
  a heads-up showing space needed vs free, and can still choose to queue it anyway. It
  only advises; it never blocks a download on its own.

**A cleaner, lowercase wordmark**
- The app now styles its name in lowercase — **ticodl+** — everywhere it appears: the
  header, credits, the home-menu tile, in-app messages, and the LAN import pages, to
  match the repo editor. The name's colours now match the editor's exactly. Brand text
  was updated across all 24 languages.
- The repo editor now shows the app icon beside its title.

**Clearer update status**
- Once an update is installed, the Settings chip now reads **Restart to update**
  (instead of continuing to say "Update available") until you relaunch — so it's
  obvious the new build is staged and just waiting on a restart.

## 1.8.0-beta

**Know when an update is waiting — without lifting a finger**
- On launch (only when you're online, and only if you leave it enabled) TicoDL+ now
  quietly asks GitHub whether a newer version exists. If one does, a green dot appears
  on the **Settings** tab and the **Check for updates** entry shows an **Update
  available** chip. It only ever advises — nothing installs itself; you still choose
  when to update.
- New **Advanced → Check for updates on startup** toggle (on by default) if you'd
  rather it stayed quiet.

**See what's new before you update**
- The update prompt now has a **Release notes** button, so you can read what changed
  in the new version before committing to the download.

**Translations**
- The new strings are translated across all 24 supported languages.

## 1.7.0-beta

**Send your collection to the Switch over Wi-Fi**
- New **Settings → Manage data → Import collection**. The console shows an address;
  open it in a browser on any computer on the same network and you get a small page
  to send your `dl_sources.json` straight across — no SD card, no file copying. The
  Switch shows you what the file contains and asks before replacing anything.
- The same page has a **Download current dl_sources.json** button, so you can pull
  the collection *off* the console to edit it.
- The repo editor can now skip the page entirely: its new **Send to Switch** button
  posts your work directly to a console that has the Import screen open.

**Your previous collections are kept, and you can go back**
- Importing keeps the two collections that came before it. **Manage data → Restore
  previous collection** lists what each one holds and swaps it back. Restoring is
  itself reversible — your current list is kept, so you can swap back again.

**New: transfer log**
- **Settings → View logs → Transfer log** records every import, export, rejection
  and cancellation, with what each file held. Kept separate from the debug log, so
  routine network chatter can't bury it.

**Translations**
- All of the above is translated into the 24 supported languages. The two prompts
  that stand between you and overwriting a collection — the import and restore
  confirmations — are worth a careful read in your language; corrections welcome.

**Fixes**
- `dl_sources.json` is now written to a temporary file and moved into place, and
  write errors are actually detected. Previously a full SD card or a console that
  lost power mid-write could leave the file truncated — losing every collection —
  while the app reported success. This affects every save, not just imports.
- Downloads whose filename legitimately contains `..` (for example
  `Zelda..Oracle.zip`) were refused as a path-traversal attempt.

## 1.6.2-beta

**Fix — downloads from repos that live in a subfolder**
- Items whose download URL points into a subfolder (for example
  `archive.org/download/nds_apfix/apfix/`) failed to download. archive.org
  already includes the subfolder in each file's name, so the app was appending
  it a second time and requesting a path that doesn't exist. Files under such a
  repo now download correctly, and files that sit outside the subfolder resolve
  against the item root.

**Repo editor is now part of the release**
- `repoEditor-*.html` is attached below. It's a single self-contained file —
  download it, open it in any browser (no install, nothing uploaded), and use it
  to build or edit your `dl_sources.json`: manage console groups and repos,
  browse an item's files, and test a download before putting it on the SD card.
- This release's editor: console groups shown as an A–Z card grid you click into,
  repos two per row, archive.org credentials behind a settings dialog, and a file
  browser whose header stays put while the list scrolls.

## 1.6.1-beta

**Custom ROM folder (Advanced)**
- New **Advanced settings → ROM folder** option lets you override where ROMs are
  installed and browsed, instead of the auto-detected `tico/roms`. It opens an
  **SD-card folder browser**: **A** enters a folder, **B** goes up a level, **X**
  uses the folder you're in as your ROM root, and **Y** resets to automatic TICO
  detection. A warning before you confirm explains the folder must match where
  TICO reads ROMs, or your games won't appear in the emulator. Takes effect
  immediately and persists in `prefs.json`.

## 1.6.0-beta

**Performance — the UI stays smooth during downloads, verify and unzip**
- Background workers (downloads, MD5 verify, archive extraction) now run on
  their own CPU cores instead of sharing the one the interface draws on, so
  tabs, lists and metadata no longer lag while work is in progress.
- Verification and unzipping briefly use the console's boost clock profile (the
  same one games use on loading screens — not an overclock), so they finish
  faster.
- Download writes and MD5 reads are buffered into larger SD-card operations,
  cutting the stutter that heavy disk activity used to cause.
- The Queue screen takes a single state snapshot per frame instead of two.

**Installed tab loads faster**
- Folder sizes are cached to disk, so the first visit after launch no longer
  re-scans every console folder; only a folder that actually changed (e.g. a
  fresh download) is recomputed.
- The file list's "already installed" check does one directory scan for the
  whole list instead of one per file.

**Fixes**
- archive.org downloads no longer fail with 401/403 on public items when
  credentials are set: the app now downloads unauthenticated first (like a
  browser) and only sends your credentials when the server actually requires
  them, keeping them across archive.org's download redirect for restricted
  items.

## 1.5.0-beta

**New app icon & branding**
- Fresh TicoDL+ icon, with a small logo badge in the header next to the title
  and a large one in the Credits dialog.

**Look & feel overhaul (tables now match the cards)**
- Table rows are floating rounded cards — no more edge-to-edge zebra stripes;
  selection uses the same blue fill + outline as the card view.
- Right-hand values (sizes, statuses, counts, settings) sit in subtle rounded
  chips; capsule scrollbars everywhere.
- Settings and Advanced show values in a colour-coded right column (green ON /
  grey OFF / blue values) with › chevrons on rows that open a screen.
- The active tab gets a rounded pill highlight; a soft pulsing dot appears on
  the Queue tab while downloads run and you're on another tab.
- The active download is a "hero" row: tinted background and a thicker
  progress bar; text and pill sit cleanly above the bar; the status column has
  a fixed width so icons align on every row.
- Loading states show an animated spinner; empty screens show a big dimmed
  icon with the hint message.

**Usability**
- Installed tab: **− now always deletes** (mark with Y for several at once);
  search and sort moved to an actions menu on **D-pad Left**.
- Destructive confirmations are red-titled, keep Cancel as the safe default,
  and deleting installed games warns that it can't be undone (translated into
  all 25 languages).

**Fixes**
- Downloads that fully arrived but hit a connection-close timeout (seen under
  emulators) are salvaged instead of failing — MD5 still verifies them.
- Card view: readable subtitles on selected cards, drag-scroll can no longer
  strand the selection off-screen, and the selected card's icon no longer
  touches two-line titles.

**Tools**
- New `tools/repo-editor`: a self-contained HTML editor for `dl_sources.json`
  — import/export, add/edit/delete consoles and repos, browse an item's file
  list and test downloads in the browser. Light mode + filtering in v0.0.2.

## 1.4.0-beta

**Card view**
- New optional **card view** for the console lists (Browse tab and the
  Installed root): a 4-wide grid of cards with a big console icon, the full
  console name (wrapping onto two lines) and repo/app counts. Toggle it in
  **Advanced settings → Card view**; file lists stay as tables.
- Navigate with the D-pad/stick in all four directions; **A** opens, **X**
  pins, **−** searches; tap and drag also work.

**More console icons**
- The download queue shows each item's console icon (between the status and
  the name), the Browse/Installed headers show the current console's icon,
  and both search screens tag every result with its console icon.
- Manage consoles now lists full console names.

**Look & feel**
- The selection highlight is now **blue** (was teal) in both themes, and it
  **fades in** as you move instead of snapping; the selected card lifts with
  a light outline and a slightly larger icon.
- Queue progress bars are rounded and blue, matching the app's accent color.

## 1.3.0-beta

**Console icons**
- Every console now shows an icon next to its name — on the Browse tab
  (grouped and flat), the Installed root, the console picker, Manage consoles,
  and the download queue. Custom folders get a generic icon.

**Search & queue**
- **Search installed games** from the Installed tab with **−** (recursive scan
  of the ROM folders); open a result to jump to its folder.
- Global search results now show a `*` when a file is already installed.
- **Move a queue item to the top/bottom** with D-pad ◀/▶ (the active download
  stays put).

**Light theme fixes**
- Cancelled, done, verifying, saved and failed queue statuses, plus the
  "shown/hidden" and active-language markers, were too pale to read on the
  light theme — all darkened. Dark theme unchanged.

## 1.2.0-beta

**Metadata**
- **Refresh all metadata** in one pass (Manage data) with live `(n/total)`
  progress and a done/failed summary — handy before a global search, which
  only covers cached repos.
- **Refresh metadata now** (hard refresh) restored on a repo's edit screen.

**Downloads**
- Exiting during extraction no longer re-downloads the archive on next
  launch — the completed file is kept and the unzip resumes (works offline).
- **Retry all failed** and **Clear finished** as a Queue actions menu (Y).
- A summary toast tallies a finished batch (e.g. "12 done, 1 failed").

**Installed browser**
- Re-sort with D-pad Left (name A–Z / Z–A / size); folders stay grouped and
  pinned folders stay on top.

**Languages**
- **Vietnamese now renders correctly** — a bundled fallback font supplies the
  glyphs (ế, ệ, ợ…) the console's system font lacks, with vertical metrics
  matched so it sits on the same baseline as the rest of the text.

**Fixes**
- Self-update progress text no longer slides off the left edge once the file
  size grows.
- Clearing the debug log now asks "Clear the debug log?" instead of the
  download-history wording.
- Cancelled queue items are readable in the light theme (were washed out).

## 1.1.1-beta

**Touch support**
- The whole app now works by touch in handheld mode: tap a tab to switch to
  it, tap a list row to select it (tap again to activate), drag up/down to
  scroll.

**Queue & downloads**
- **Live extraction progress**: archives show a percent bar while unpacking —
  it moves even inside one huge file — plus a count of files extracted so far.
- Cancelling an extraction now takes effect mid-file instead of waiting for
  the current file to finish.
- When the connection is down, the Queue screen now says so: "Offline -
  downloads resume when the network returns".

**Settings reorganised**
- New **View logs** submenu: the download history moved here, joined by a new
  **debug log viewer** (newest first) — both can be cleared in-app.
- **Manage consoles** moved under **Advanced settings** (renamed from
  "Advanced").
- New order: Check for updates · Advanced settings · View logs · Manage data ·
  Language · Theme · Credits.

**Browse & search**
- The Files screen now shows a persistent indicator for an active sort and/or
  filter (with the match count), so a filtered list can't silently "lose"
  files.
- Global search says "%d+ results - refine your search" when it hits the
  result cap instead of showing a misleading flat count.

**Polish**
- Better spacing in the update-download progress text.

## 1.1.0-beta

**Languages & themes**
- Full UI translation in **25 languages** (Settings → Language) with correct
  text everywhere (multi-line strings, CJK, accents). Translation files ship in
  the repo — corrections welcome via issues/PRs (see README → Translations).
- **Light and dark themes** (Settings → Theme).

**Downloads**
- **Network-loss aware queue**: if the connection drops, active downloads
  pause (keeping their partial files) and everything resumes automatically, in
  order, when the network returns — no more cascading failures offline.
- **Max downloads applies instantly** (Advanced): raising it starts more
  queued items; lowering it pauses the excess, which auto-resume as slots free.
- Transient server errors (HTTP 5xx/429) retry automatically with backoff;
  stalled transfers time out and resume instead of hanging forever.
- Cancelling a queue item now asks for confirmation.
- Same-named files headed to different consoles no longer share a temp file;
  unresumable old-format `.part` leftovers are cleaned up at startup.

**Browse & organize**
- **Global search** (−) across all cached repos, results tagged by console.
- **Pin favorites** (★) with D-pad Right — consoles, repos (grouped and flat
  mode), and installed folders, all the same button.
- Search results and metadata-cache entries show their console short code.

**Settings & UI**
- New **Manage data** submenu (downloads folder + metadata cache); Advanced
  moved above Credits; Controls/Help removed.
- Header status: network signal bars (full bars on wired LAN, red offline),
  free SD space, battery with charge indicator.
- Consistent buttons everywhere: A opens/inspects (never deletes), X carries
  edit/sort/retry and confirmed bulk deletes, − deletes one/selected, Y marks.

**Self-update**
- Version check runs in the background with a retry counter; B dismisses it.
- Downloads are validated as a real NRO and installed via a staged
  copy-then-rename with a `.previous` backup — an interrupted update can no
  longer corrupt the app.

**Fixes**
- Launching with no network (airplane mode) no longer shows a black screen,
  and the no-network dialog's Exit button actually exits.
- Non-English languages no longer show literal `\n` in dialogs.
- Extraction failures from a full SD card are reported instead of being
  counted as "done" (which used to delete the archive).
- A corrupt metadata cache entry now refetches instead of breaking the repo
  until the cache was cleared manually.
- Wired-LAN (docked) connections show full signal bars instead of the weakest.
- Renaming a file can no longer silently overwrite another file or move it to
  a different folder.
- archive.org credentials are only ever sent over HTTPS.

---

# Pre-rebrand history

These entries are from TicoDL+'s predecessor and use a separate, older version
scheme (note the overlapping 2.0.x / 0.1.x numbers). Kept for reference only —
they are demoted to ### so release.sh never mistakes them for current sections.

### 2.0.16
- The queue's overwrite indicator is now a readable colour-coded tag instead of
  a symbol the Switch font couldn't render: orange **(repl)** when a download
  replaced an existing file (with a count, e.g. `(repl 12)`, for multi-file
  archives), green **(new)** for a brand-new file.

### 2.0.15
- **How overwrites work (unchanged behaviour, now made visible):** when a
  download lands on a file of the **same name** in `sdmc:/tico/roms/<console>/`,
  it **overwrites that file in place** — there is no prompt and no separate
  backup. Re-downloading replaces the existing file; for archives, each
  extracted file replaces any same-name file already there. The `*` installed
  marker is informational and does not block re-downloading.
- This is now **logged** for audit: the download history (Settings -> View
  download log) records when an install `(overwrote existing)` or
  `(overwrote N files)`.
- And **shown in the queue**: a finished item's result column marks it with a
  symbol — orange **↺** if it replaced an existing file (with a count, e.g.
  `↺12`, for multi-file archives), or green **+** for a brand-new file.
- README now documents the overwrite behaviour.

### 2.0.14
- Browse console list is now sorted A-Z by the displayed full name (it was
  ordered by the underlying folder key, which no longer matched the names).

### 2.0.13
- Browse page now shows each console's full name with the folder in parentheses
  (e.g. "Nintendo Entertainment System (NES)"). Custom folders are unchanged.
  Display-only — no config/SD-card changes.

### 2.0.12
- In-app self-update now overwrites the .nro you actually launched (via
  argv[0]), with a fallback that finds it whether it's at
  `sdmc:/switch/TicoDLplus/TicoDLplus.nro` or `sdmc:/switch/TicoDLplus.nro`.
  The update dialog shows the exact path it installed to.

### 2.0.11
- Extractor now writes each data block at its declared offset (correct handling
  of sparse archive entries; no change for normal archives).
- Repo housekeeping (ignore local scratch files).

### 2.0.10
- Fixed a memory leak when a repo's metadata fetched but contained no usable
  files.
- Hardened the download write path (return byte count, not item count) and the
  JSON parser (guard against pathologically nested input).
- Failed-download reason and status are now published together (no stale text on
  a freshly failed item), and the completion-toast check no longer scans the
  queue every frame while idle.

### 2.0.9
- New app icon. (Applies on the next launch after updating.)

### 2.0.8
- The **left analog stick** now navigates lists (up/down), in addition to the
  D-pad.

### 2.0.7
- Download queue now draws a **progress bar** on the active download, and shows
  size, speed and an **ETA** (e.g. `1.2 GB @ 3.4 MB/s  ~2m30s`).
- Failed downloads now show a **real reason** (`HTTP 404`, `no space`, `bad md5`,
  `network`, …) instead of a bare "FAIL".
- **Completion toasts**: you get a Done / Saved / Failed notification when a
  download finishes, even from another tab.
- **Archive.org credentials** moved into their own Settings sub-menu (access key,
  secret, and a Clear option).
- Lists now **wrap around** — pressing Up on the first row jumps to the last,
  and vice-versa.
- Lists **remember your position** when you back out and return (file list per
  repo, console/repo lists, including across tab switches).

### 2.0.6
- New **Settings → Manage consoles (show/hide)** screen: toggle which consoles
  appear on the Browse page. Hidden consoles (and their repos in flat mode) are
  kept out of the primary list but stay easy to re-enable here. The visibility
  is saved per console in `dl_sources.json` (`"shown"`), defaulting to shown.

### 2.0.5
- The add-console picker is now a sorted (A-Z) table showing each console's
  repo count in its own column.
- Credits screen now also credits the TICO emulator (https://ticoverse.com/).

### 2.0.4
- Credits screen now also credits the Plutonium UI library by XorTroll.
- Project is now MIT-licensed, with third-party license notices included
  (Plutonium and jsmn, both MIT).

### 2.0.3
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

### 2.0.2
- The in-app updater now shows live **download progress** instead of looking
  frozen while it fetches the new build.
- New **dark theme**: near-black background with light text.
- **Tabbed navigation**: a top bar with Browse / Installed / Queue / Settings,
  cycled with the L/R shoulder buttons. (Switching repos inside a file list
  moved to D-pad Left/Right.) Button hints now live in a bottom footer.
- File and queue lists are now real **tables**: the file name and a right-
  aligned size/info column, with sizes color-coded by KB / MB / GB and queue
  rows colored by status.

### 2.0.1
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

### 2.0.0
- New **graphical UI** (Plutonium / SDL2) replacing the text console: full-screen
  menus with on-screen highlight, dialogs, toasts, and the Switch's shared fonts.
- Full parity with 1.x: console groups, add/edit/delete repos & consoles, browse +
  download, background queue (cancel/retry/clear with live progress), installed
  browser, file filter + installed markers, settings, manual-URL download, download
  log, in-app self-update, SD/battery status, hold-repeat navigation + paging.
- Same proven backend (downloads, resume, MD5 verification, extraction) as 1.x.
- Controls: D-pad (hold to repeat) / ZL-ZR page; L queue, R installed, R-stick
  settings, + exit, B back.

### 1.0.0
- First stable release.
- Ships with NO collections — only the supported console folder names are
  bundled, no archive.org item ids or links. You provide your own collections
  in-app (press Y and enter an archive.org item id) or by editing
  dl_sources.json on your SD card.
- Console list sorted A-Z; brighter main-menu and installed-folder text.
- Background download queue with resume, persistence, MD5 verification, and
  auto-extraction; console groups with multiple repos; in-app updater; full
  text-UI polish (highlight bar, consistent controls, toasts, breadcrumbs).

### 0.1.65
- File lists now show a breadcrumb in the header (e.g. "snes > USA Complete") so
  you can see which console/repo you're browsing.
- List headers show your position and total, e.g. "(12/340)".
- Friendlier repo editor labels: Name / Archive.org ID / Download URL / Enabled.
- Help screen now includes a color legend (size colors, the * installed marker,
  and queue status colors).

### 0.1.64
- Selected list rows now show a full-width highlight bar (much easier to see
  where you are) across every screen.
- Consistent buttons everywhere: A = open/confirm, B = back, X = edit, Y = add,
  − = delete. (Manual URL download moved into Settings.)
- Action confirmations: adding/deleting/saving and queue actions now show a brief
  on-screen toast so you know it worked.

### 0.1.61
- Security: downloaded filenames are now sanitized before use as paths, so a
  malicious metadata entry can't write outside the intended folder.
- Security: archive.org S3 credentials are only sent to archive.org hosts, never
  to a repo URL pointing elsewhere.
- Archives that can't be extracted now show as "saved" (logged "saved-raw")
  instead of "done", so it's clear the raw file was kept un-unpacked.
- Fixed a thread-safety issue in download-history logging (localtime_r).

### 0.1.60
- Adding a repo now shows a picker of TICO-supported consoles instead of a
  free-text field, so repos are always grouped under a valid console folder.
- The supported list comes from "tico_consoles" in dl_sources.json; you can no
  longer create unsupported consoles in-app (edit the JSON to change the set).
- Manual URL downloads also pick the target folder from this list.

### 0.1.59
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

### 0.1.56
- Footer no longer runs off the edge: removed the trailing "[DL Queue: N active]"
  text (the info now lives in the download monitor line).
- Download monitor now reads "DL 1/2:" — the current item number and the total
  number of items in the queue.

### 0.1.55
- Fixed archive extraction silently dropping files (notably some `.rar` sets
  where only the raw archive ended up in /roms). The extractor now tolerates
  libarchive warnings during header/data reads instead of discarding the file.
- Extraction problems are now written to `debug.log` with the exact reason.
- Releases now publish with release notes pulled from this changelog.

### 0.1.54
- Header shows SD space and battery % (no labels).
- Active-download line now matches the footer style, with a spacer row above it.
- Installed browser: L goes back a level, ZL opens the download queue.

### 0.1.53
- Added a scrollbar / position indicator to long lists.
- Added a Controls / Help overlay (ZR on the main menu).

### 0.1.52
- Battery percentage shown in the header.

### 0.1.50
- More reliable update check: uses the releases list with retries, so it no
  longer trips on GitHub's intermittent `/releases/latest` errors, and it picks
  the highest version itself.

### 0.1.47
- "Installed" markers (`*`) on files you already have.
- Live download status line; retry failed downloads; safer delete confirmation;
  persistent SD free-space readout.

### 0.1.43
- Resume interrupted downloads, persistent download queue across launches, MD5
  checksum verification, and an on-screen file-name filter.
