#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CONSOLES 128 /* ~50 pre-seeded consoles + room for custom ones */
#define MAX_REPOS    32 /* download repos per console */

/* GitHub repo (owner/name) the in-app updater pulls releases from.
 * >>> EDIT THIS to your repo before building a release you intend to ship. <<< */
#define UPDATE_REPO   "digdat0/HaulNX"

/* Where the app lives if it can't determine its own path from argv[0]. */
#define DEFAULT_SELF_PATH "sdmc:/switch/HaulNX/HaulNX.nro"

#define CONFIG_DIR    "sdmc:/switch/HaulNX"
/* The app's files live in two subfolders under CONFIG_DIR so its root stays
 * tidy: config/ holds the JSON state (collections, credentials, prefs, queue,
 * size cache) and logs/ holds every append-only log. Builds up to 1.0.2 wrote
 * all of these directly under CONFIG_DIR; app_migrate_layout() relocates any
 * such leftovers into the new folders on startup so an update keeps them. */
#define DATA_DIR      CONFIG_DIR "/config"
#define LOGS_DIR      CONFIG_DIR "/logs"

#define SOURCES_PATH  DATA_DIR "/dl_sources.json"
/* The two previous dl_sources.json files, rotated on every import so that two
 * bad imports in a row can still be walked back. Slot 0 is the most recent. */
#define SOURCES_BAK_PATH  DATA_DIR "/dl_sources.bak.json"
#define SOURCES_BAK2_PATH DATA_DIR "/dl_sources.bak2.json"
#define SOURCES_BAK_SLOTS 2
/* Staging file for config_save; never read back. */
#define SOURCES_TMP_PATH DATA_DIR "/dl_sources.tmp.json"
#define CREDS_PATH    DATA_DIR "/credentials.json"
#define PREFS_PATH    DATA_DIR "/prefs.json"
/* Staging files, same reasoning as SOURCES_TMP_PATH; never read back. */
#define CREDS_TMP_PATH DATA_DIR "/credentials.tmp.json"
#define PREFS_TMP_PATH DATA_DIR "/prefs.tmp.json"
/* Download queue state and the Installed-tab folder-size cache: derived data,
 * so they live with the other state in config/, not with the logs. */
#define QUEUE_STATE_PATH DATA_DIR "/queue.json"
#define INST_SIZES_PATH  DATA_DIR "/inst_sizes.json"
/* Live queue snapshot (active downloads + history) served to the desktop
 * companion's Downloads tab, regenerated fresh on every GET like
 * INVENTORY_PATH's diag_bundle sibling. Distinct from QUEUE_STATE_PATH above,
 * which only persists resumable in-flight work across a relaunch. */
#define QUEUE_STATUS_PATH DATA_DIR "/queue_status.json"
/* Device inventory served to the desktop companion over the LAN. Regenerated
 * app-side while the inventory server pref is on; the server just serves this
 * file (see httpsrv.c HTTPSRV_MODE_INVENTORY). A staging file keeps a reader
 * from ever seeing a half-written document. */
#define INVENTORY_PATH     DATA_DIR "/inventory.json"
#define INVENTORY_TMP_PATH DATA_DIR "/inventory.tmp.json"
/* Shared emulator/app update manifest: the id/kind/detect list plus each entry's
 * GitHub release repo. Single source of truth for the on-device update manager
 * (Tools) and the desktop companion, which reads/writes it over the LAN. Seeded
 * from romfs:/update_sources.json on first run (see updman.c). */
#define UPDSRC_PATH   DATA_DIR "/update_sources.json"
/* When each update entry was last checked against its GitHub release, keyed by
 * id ("id\tepoch" per line). The update manager no longer auto-checks on open;
 * this remembers the last manual check so the list can show "checked 5m ago"
 * across launches. Best-effort, app-side only (the desktop never touches it). */
#define UPDCHK_PATH   DATA_DIR "/update_checks.txt"
/* Previous builds of updated/installed apps, kept so the manager can roll back:
 * BACKUPS_DIR/<id>/<version>.nro, the two most recent per app. */
#define BACKUPS_DIR   CONFIG_DIR "/backups"
#define LANG_DIR      CONFIG_DIR "/lang"
#define CACHE_DIR     CONFIG_DIR "/cache"
/* Persistent file-hash cache for DAT verification: (path,size,mtime) -> CRC/SHA1,
 * so re-verifying an unchanged library skips re-reading every byte off the SD.
 * v2: entries hold the digest of the ROM *inside* a single-file archive, not the
 * archive container. v3: digests are the No-Intro canonical form (header-stripped
 * NES/FDS/Lynx/7800, N64 reordered to .z64), so the name is bumped again to
 * discard any v2 entries that predate normalisation and would now mismatch. */
#define HASH_CACHE_PATH CACHE_DIR "/hashes3.tsv"
/* Persistent per-file verify status (verified/bad/unknown), so the Installed
 * browser can show a lasting badge after a verify pass instead of that result
 * evaporating with the transient VerifyJob. See vfystatus.h. */
#define VFYSTATUS_PATH  CACHE_DIR "/vfystatus.tsv"
/* Downloaded SteamGridDB cover art (one PNG per resolved title) plus the
 * title -> art (or confirmed-miss) index that keeps a re-scan from re-
 * querying a title already resolved either way. See boxart.h. */
#define BOXART_DIR        CACHE_DIR "/boxart"
#define BOXART_INDEX_PATH CACHE_DIR "/boxart_index.tsv"
/* Scratch home for the art picker's preview thumbnails (boxart_fetch_thumb) --
 * small, disposable, overwritten per slot each time the picker opens, never
 * indexed. Kept apart from BOXART_DIR so a picker session can't be mistaken
 * for a resolved title's real cover. */
#define BOXART_TMP_DIR    CACHE_DIR "/boxart_tmp"
/* No-Intro/Redump DAT files for library verification, one per console folder:
 * DATS_DIR/<target>.dat. User-supplied — the app ships none. */
#define DATS_DIR      CONFIG_DIR "/dats"
/* Exported verify reports (one per console, overwritten each export):
 * REPORTS_DIR/<target>-verify.txt. A user-facing deliverable to read on a PC,
 * so it lives in its own folder rather than under logs/. */
#define REPORTS_DIR   CONFIG_DIR "/reports"
/* Staging area for games arriving from a PC (USB or the LAN receiver) that have
 * not been sorted into a console folder yet. The inbox sorter identifies each
 * file (see idgame.h) and files it into <roms_root>/<target> — or asks. */
#define INBOX_DIR     CONFIG_DIR "/inbox"
#define DL_TMP_DIR    CONFIG_DIR "/downloads"
#define LOG_PATH      LOGS_DIR "/debug.log"
/* Per-archive extraction throughput, appended one line per archive when the
 * benchmark toggle is on. Kept separate from debug.log so an A/B run is easy to
 * read back and diff without the churn of routine logging scrolling it away. */
#define EXBENCH_PATH  LOGS_DIR "/exbench.log"
/* Collection imports/exports. Kept apart from debug.log: an import rewrites
 * dl_sources.json wholesale, and that record must not scroll away under the
 * churn of routine HTTP logging. */
#define XFERLOG_PATH  LOGS_DIR "/transfers.log"
/* Diagnostics speed test: one line per run (date, per-direction bytes + rate,
 * outcome). Kept apart from debug.log so a history of throughput readings is
 * easy to scan and diff without routine HTTP logging scrolling it away. */
#define SPEEDLOG_PATH LOGS_DIR "/speedtest.log"
#define DLLOG_PATH    LOGS_DIR "/downloads.log"
#define DLLOG_JSON    LOGS_DIR "/downloads.jsonl"
/* Every log folded into one file for a bug report: debug.log, transfers.log,
 * speedtest.log, downloads.log, exbench.log, queue.json. Written by
 * diag_bundle_write() (config.c) -- Settings > Diagnostics > Export debug
 * bundle calls it for a manual copy, and the inventory server's
 * /debug_bundle.txt route (httpsrv.c) calls it fresh on every desktop-companion
 * pull so a sync always gets current logs without the user copying anything
 * off the SD card by hand. */
#define DIAG_BUNDLE_PATH LOGS_DIR "/diag_bundle.txt"
/* The app's own ROM library. Games install to <root>/<console>/. Users point
 * their emulators here (see the wiki); we no longer read any emulator's config. */
#define DEFAULT_ROMS_ROOT  "sdmc:/roms"

/* One download source (an archive.org item) within a console group. */
typedef struct {
    char label[64];          /* repo display name, e.g. "No-Intro" */
    char id[256];            /* archive.org item id */
    char download_base[512]; /* base URL; defaults to .../download/<id> */
    bool enabled;            /* "active" flag */
    bool pinned;             /* pinned/favorite — sorted to top of browse */
} Repo;

/* A console (e.g. "snes") that groups one or more download repos. All of a
 * console's repos install into the same <roms_root>/<target> folder. */
typedef struct {
    char console[64]; /* display name */
    char target[64];  /* folder under the ROM root */
    Repo repos[MAX_REPOS];
    int repo_count;
    bool shown;  /* show on the primary Browse page (default true) */
    bool shown_installed; /* show on the Installed page (default true). Independent
                    of `shown`: a console can appear on Browse, Installed, both,
                    or neither. */
    bool pinned; /* console pinned to the top of the grouped Browse list.
                    Independent of the per-repo `pinned` flags — pinning a
                    console does NOT pin the repos inside it, and vice versa. */
    /* Custom install folder for this console. Empty = the default
     * <roms_root>/<target>. When set, downloads for this console install into
     * this exact path instead — for emulators whose ROM folder can't be pointed
     * at our library. Stored normalised to "sdmc:/..." form (see
     * roms_normalize_path). The console name is NOT appended: this is the final
     * directory. */
    char folder[512];
    /* false (default): Browse/Installed show this console's built-in square
     * icon. true: they show its SteamGridDB cover art instead, same as a
     * game's box art — resolved and cached under the "console:<target>" key
     * (see boxart.h) so it shares the game-cover index/cache rather than
     * needing a parallel one. Set from the console's Options > Console Art
     * menu; only ever true when a cover has actually been picked, and only
     * takes effect while a SteamGridDB key is configured. */
    bool use_boxart;
} ConsoleGroup;

typedef struct {
    ConsoleGroup consoles[MAX_CONSOLES];
    int console_count;
    /* Master list of TICO-supported console folders. Repos can only be grouped
     * under one of these (the UI offers them as a picker). */
    char supported[MAX_CONSOLES][64];
    int supported_count;
} SourcesConfig;

typedef struct {
    char access_key[128];
    char secret[128];
    char github_token[128]; /* optional GitHub PAT; when set, sent as a Bearer
                               token on api.github.com update checks so the
                               unauthenticated 60/hr rate limit doesn't stall them */
    char steamgriddb_key[128]; /* optional SteamGridDB API key; when set, the
                                  Tools "Scan for Box Art" action can fuzzy-
                                  search + download cover art for the local
                                  library. Nothing box-art-related runs without
                                  a user-supplied key — see boxart.h. */
} Credentials;

#define MAX_PINNED_DIRS 32
#define MAX_FILTER_EXTS 40

/* One entry in the Browse file-view extension filter. */
typedef struct {
    char ext[16];  /* extension without leading dot, lowercased, e.g. "txt" */
    bool enabled;  /* participates in filtering while the master switch is on */
} FilterExt;

typedef struct {
    bool use_cache;      /* true: load cached metadata if present; false: always refetch */
    bool prevent_sleep;  /* true: keep console awake while downloads are active */
    bool group_consoles; /* true: main list shows consoles (open to see repos);
                            false: flat list, one row per repo */
    int max_downloads;   /* 1–10; how many downloads run simultaneously (default 3) */
    int rate_all_kbps;   /* global download cap across ALL active downloads, in
                            KiB/s (0 = unlimited, the default) */
    int rate_item_kbps;  /* per-download cap, in KiB/s (0 = unlimited, default) */
    bool net_check;      /* true: warn on startup if no network (default true) */
    bool chk_updates;    /* true: silently check GitHub for an app update on
                            startup (only when network is up); advise, never
                            auto-install (default true) */
    char lang[16];       /* language code, e.g. "en", "es", "ja"; empty = English */
    char theme[16];      /* "dark" (default) or "light" */
    char accent[16];     /* accent color preset key: "signature" (default),
                            "violet", "ember", "aqua", "rose", "slate" --
                            drives accent_green()/accent_blue() in
                            MainApplication.cpp, so it recolors every ring,
                            glow, progress bar and pulse dot app-wide */
    bool card_view;      /* true: console lists render as a card grid */
    /* true (default): the Installed browser collapses a multi-file game — a
     * .cue with its .bin tracks, a multi-disc set — into one row standing for
     * every piece. Turning it off lists the raw files again, which is the way
     * to reach a single track when a set is grouped wrongly. */
    bool group_sets;
    /* Advanced override for the ROM install root. Empty = use the default
     * ROM root (DEFAULT_ROMS_ROOT). When set, this exact path is used instead. */
    char roms_override[512];
    /* Master switch for per-console custom install folders (ConsoleGroup.folder).
     * false (default) = every console installs under the single ROM folder;
     * true = each console may redirect its downloads to its own folder. The
     * per-console paths persist regardless, so turning this off just ignores
     * them and back on restores them. */
    bool custom_folders;
    /* Top-level ROM folders pinned to the top of the Installed tab. */
    char pinned_dirs[MAX_PINNED_DIRS][64];
    int pinned_dir_count;
    /* Browse file-view extension filter. filter_exts is the master switch; the
     * per-entry enabled flags persist regardless of it (turning it off just
     * stops the filtering from being applied). Seeded with defaults on first run. */
    bool filter_exts;
    FilterExt exclude_exts[MAX_FILTER_EXTS];
    int exclude_ext_count;
    /* Bulk add only: drop files already present in the console's folder from a
     * marked selection instead of re-downloading them (default true). A single
     * deliberate A press is never filtered by this — asking for one file and
     * being silently refused is worse than a duplicate. */
    bool skip_installed;
    /* Extraction benchmarking knobs (dev/perf tuning; see extract.c). The
     * defaults reproduce the shipped behavior: preallocation on, 1 MB write
     * chunks, throughput logging off. */
    bool ex_bench;      /* append per-archive throughput to EXBENCH_PATH */
    bool ex_prealloc;   /* grow each output file to its final size up front */
    int  ex_chunk_mb;   /* write-coalescing chunk size in MB: 1, 2, or 4 */
    /* Desktop-companion inventory server (read-only, port HTTPSRV_INV_PORT).
     * When on, the app keeps a listener up while running so the app utility can
     * pull a device inventory; inv_code is the persistent 4-digit code shown in
     * Settings and required in the URL. inv_code is generated on first enable. */
    bool inv_server;
    char inv_code[5];   /* 4 digits + NUL; empty until first enabled */
    /* Keep downloaded archives compressed: when true, a downloaded .zip/.7z is
     * saved as-is in the console folder instead of being extracted (default
     * false — extract as usual). See queue_set_keep_archives. */
    bool keep_archives;
    /* Run the optional post-import converter on freshly downloaded files (see
     * queue_set_post_import_enabled). Only has an effect when a converter
     * add-on module is present; default true so it applies automatically when
     * available. */
    bool convert_import;
    /* 1G1R keep-preference order for the four named regions, most preferred
     * first: a permutation of the letters W(orld) U(SA) E(urope) J(apan).
     * A file tagged with a region not in this set (or none at all) always
     * ranks below all four, same as before this was configurable. Default
     * "WUEJ" reproduces the original fixed ranking exactly. See onegr_score. */
    char region_order[5];
    /* true (default): the MTP responder may come up over USB — either the
     * user opening "Connect to PC over USB", or automatically in the
     * background while inv_server is on and a cable is plugged in. false:
     * the console never presents itself as a USB file-transfer device, so a
     * cable plugged in "just to charge" can't be used to browse the library. */
    bool mtp_enabled;
    /* true (default): the Installed list shows cached SteamGridDB covers and
     * lets a scan resolve new ones. false: the list never looks up or decodes
     * box art, for anyone who'd rather not have the extra disk/network use —
     * cached covers already on disk are left alone and still browsable from
     * Storage > Manage Box Art, just not shown in the row list. See
     * boxart_row_icon, which is the single point this gates. */
    bool box_art_enabled;
    /* true (default): a newly landed game (finished download, or an extracted
     * archive's contents) is queued for a quiet, single-title box art fetch in
     * the background -- no scan screen, no scan progress, just the cover
     * showing up next time that folder's list is built. Independent of
     * box_art_enabled above: this only decides whether new arrivals trigger a
     * fetch, not whether already-cached covers are shown. Still gated on a
     * SteamGridDB key being set (see MainApplication's queue_on_landed
     * handler) -- turning this off just means new games wait for the next
     * manual Scan. */
    bool box_art_auto_fetch;
    /* false (default): the SD card is only ever reached through the app's own
     * managed folders (console install dirs + inbox), same as every other
     * transfer in the app. true: the inventory server's fs_* routes and the
     * embedded MTP responder both additionally expose the WHOLE SD card
     * (sdmc:/) as a plain browsable/writable/deletable tree — full parity
     * with mounting the card in Windows Explorer over MTP (add/copy/rename/
     * delete anywhere), for the desktop companion's SD Card tab. Off by
     * default because it is real, unscoped filesystem access rather than the
     * app's normal confined-to-its-own-folders reach; the toggle lives in
     * Settings > PC Sync so turning it on is a deliberate choice. */
    bool sd_full_access;
    /* Set the moment the first-run guided tour is shown, so it never
     * auto-triggers again on a later launch -- see the startup check in
     * MainApplication.cpp and GuidedTour() itself. Still reachable any time
     * from Settings > Help, which doesn't consult this flag. */
    bool tour_done;
} Prefs;

/* Relocate app files left in the old flat layout (everything directly under
 * CONFIG_DIR) into the config/ and logs/ subfolders. Idempotent and safe to
 * call on every launch; run it once at startup before anything reads or writes
 * a config or log file. */
void app_migrate_layout(void);

/* Fold every log the app keeps into one file at DIAG_BUNDLE_PATH -- see its
 * comment above. Pure file I/O (no UI); false only on a write failure (e.g.
 * card full), in which case any partial bundle is left in place rather than
 * torn down, same as a failed export today. Cheap enough (a handful of small
 * text files) to call on every pull, not just a manual export. */
bool diag_bundle_write(void);

/* Load dl_sources.json; seeds from romfs:/dl_sources.json on first run if the
 * sdmc file is missing. Understands the grouped schema and falls back to the
 * older flat sources/console_list schema. Always returns a usable config. */
void config_load(SourcesConfig *cfg);
bool config_save(const SourcesConfig *cfg);

/* Find a console group by its console name or target; NULL if none. */
ConsoleGroup *config_find_console(SourcesConfig *cfg, const char *name);

/* Custom install folder set for the console matching `target` (by console name
 * or target), or "" if none is set (or the console is unknown). The returned
 * pointer is into the config and stays valid until the config is reloaded. */
const char *config_console_folder(SourcesConfig *cfg, const char *target);

/* Add a console group (console == target == name) if absent. Returns it (or the
 * existing one), or NULL if full. */
ConsoleGroup *config_add_console(SourcesConfig *cfg, const char *name);

/* Remove the console group at index idx (and its repos). Returns true if removed. */
bool config_remove_console(SourcesConfig *cfg, int idx);

/* Sort console groups alphabetically (case-insensitive) by console name. */
void config_sort(SourcesConfig *cfg);

/* Create <roms_root>/<target> for every supported console that doesn't already
 * have a folder, so consoles show in the Installed tab before their first
 * download. Existing folders are left untouched. */
void config_seed_rom_folders(const SourcesConfig *cfg, const char *roms_root);

/* Check whether js/len is a usable dl_sources.json without touching disk or
 * the live config, reporting what it holds. False if it has no consoles. */
bool config_probe_json(const char *js, size_t len, int *out_consoles,
                       int *out_repos);

/* Replace the collections in cfg (and on disk) with the dl_sources.json
 * document in js/len, after moving the current file to SOURCES_BAK_PATH.
 * Returns false and changes nothing if the document holds no consoles.
 * On success *out_consoles / *out_repos report what was imported (may be NULL).
 * The master consoles list is preserved even if the import omits it. */
bool config_import_json(SourcesConfig *cfg, const char *js, size_t len,
                        int *out_consoles, int *out_repos);

/* Report what backup `slot` (0 = most recent) holds, without changing anything.
 * False when that slot is empty or has no consoles — nothing worth restoring. */
bool config_backup_info(int slot, int *out_consoles, int *out_repos);

/* Make backup `slot` the live collection, swapping the current one into that
 * slot. Unlike an import this does not rotate, so the other backup is left
 * alone and restoring the same slot twice returns to where you started.
 * False (changing nothing) if the slot is empty. */
bool config_restore_backup(SourcesConfig *cfg, int slot, int *out_consoles,
                           int *out_repos);

/* Append a repo to a console. Returns the new repo, or NULL if full. */
Repo *config_add_repo(ConsoleGroup *g, const char *label, const char *id);

/* Remove the repo at index idx within a console. Returns true if removed. */
bool config_remove_repo(ConsoleGroup *g, int idx);

/* Fill repo->download_base from its id if it is empty. */
void repo_set_url_default(Repo *r);

/* Credentials (archive.org S3 access/secret). */
void creds_load(Credentials *c);
bool creds_save(const Credentials *c);

/* Preferences. Defaults to use_cache=true if no prefs file exists. */
void prefs_load(Prefs *p);
bool prefs_save(const Prefs *p);

/* Pinned Installed-tab folders (by top-level folder name). */
bool prefs_dir_pinned(const Prefs *p, const char *name);
void prefs_dir_pin_toggle(Prefs *p, const char *name);

/* Browse file-view extension filter. */
bool prefs_ext_add(Prefs *p, const char *ext);   /* normalize + append; false if invalid/full */
bool prefs_ext_remove(Prefs *p, int idx);
/* True when the master switch is on and filename's extension matches an enabled
 * entry — i.e. this file should be hidden from the Browse file view. */
bool prefs_ext_hidden(const Prefs *p, const char *filename);

/* Build an archive.org S3 auth header into out, or empty string if no key.
 * Form: "authorization: LOW <access>:<secret>". */
void creds_auth_header(const Credentials *c, char *out, size_t out_sz);

/* ROM path state. Holds the resolved ROM root the app installs into and
 * browses. roms_root() returns the resolved path (valid after tico_init).
 * `installed` is a legacy TICO-present flag, retained but no longer used to
 * drive any behavior. */
typedef struct {
    bool installed;         /* legacy: true if sdmc:/tico/ was found (unused) */
    char roms_path[512];    /* resolved ROM folder path */
} TicoState;

/* Run once at startup: seeds roms_path with the default ROM root. */
void tico_init(TicoState *ts);

/* Returns the current roms root (pointer into ts->roms_path). */
const char *roms_root(const TicoState *ts);

/* Force the roms root to a user-supplied path (overriding the default).
 * No-op if path is NULL/empty. The path is normalised (see
 * roms_normalize_path) and its trailing slash trimmed. Call after tico_init. */
void tico_set_roms_override(TicoState *ts, const char *path);

/* Normalise a user-entered SD-card path to libnx "sdmc:/..." form:
 * leading whitespace and slashes are stripped and an "sdmc:/" prefix added
 * unless one is already present; trailing slashes are trimmed. A blank input
 * yields a blank output (meaning "auto"). */
void roms_normalize_path(const char *in, char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
