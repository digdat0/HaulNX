#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CONSOLES 128 /* ~50 pre-seeded consoles + room for custom ones */
#define MAX_REPOS    16 /* download repos per console */

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
/* RomM instance credentials: a second, independent source alongside
 * archive.org. Kept in its own file (not merged into credentials.json) so the
 * two sources can be configured, tested and removed independently. */
#define ROMM_CREDS_PATH     DATA_DIR "/romm_credentials.json"
#define ROMM_CREDS_TMP_PATH DATA_DIR "/romm_credentials.tmp.json"
/* Download queue state and the Installed-tab folder-size cache: derived data,
 * so they live with the other state in config/, not with the logs. */
#define QUEUE_STATE_PATH DATA_DIR "/queue.json"
#define INST_SIZES_PATH  DATA_DIR "/inst_sizes.json"
#define LANG_DIR      CONFIG_DIR "/lang"
#define CACHE_DIR     CONFIG_DIR "/cache"
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
/* The app's own ROM library. Games install to <root>/<console>/. Users point
 * their emulators here (see the wiki); we no longer read any emulator's config. */
#define DEFAULT_ROMS_ROOT  "sdmc:/roms"

/* One download source within a console group: either an archive.org item, or
 * (is_romm) a RomM platform linked from that instance's live library. */
typedef struct {
    char label[64];          /* repo display name, e.g. "No-Intro" */
    /* archive.org item id, or (is_romm) the RomM platform id as a decimal
     * string, e.g. "42" — reused rather than adding a separate field, since a
     * repo is always exactly one or the other. */
    char id[256];
    char download_base[512]; /* archive.org only: base URL, defaults to
                                .../download/<id>. Always empty for a RomM
                                repo (see repo_set_url_default) -- its
                                download URL is per-file, built fresh by
                                romm_content_url each time the repo opens. */
    bool enabled;            /* "active" flag */
    bool pinned;             /* pinned/favorite — sorted to top of browse */
    /* true = a RomM-linked repo (id holds a platform id, fetched live from
     * the configured RomM instance every time it's opened -- there is no
     * metadata cache for it, unlike archive.org's). false (the default, and
     * what every repo in a dl_sources.json written before this field existed
     * decodes to) = an ordinary archive.org repo, entirely unaffected by any
     * of this. */
    bool is_romm;
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
} Credentials;

/* RomM instance credentials (a self-hosted RomM server, independent of
 * archive.org). server_url has no trailing slash, e.g.
 * "https://romm.example.com" or "http://192.168.1.10:8080". Either
 * username+password (HTTP Basic) or api_token (Bearer, "rmm_...") authenticate
 * requests; if both are set, api_token takes priority (see
 * romm_creds_auth_header). ignore_cert_verify skips TLS certificate
 * verification, for instances with a self-signed/LAN certificate. */
typedef struct {
    char server_url[256];
    char username[128];
    char password[128];
    char api_token[128];
    bool ignore_cert_verify;
} RommCredentials;

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
    bool card_view;      /* true: console lists render as a card grid */
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
} Prefs;

/* Relocate app files left in the old flat layout (everything directly under
 * CONFIG_DIR) into the config/ and logs/ subfolders. Idempotent and safe to
 * call on every launch; run it once at startup before anything reads or writes
 * a config or log file. */
void app_migrate_layout(void);

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

/* Append a repo backed by a RomM platform to a console, marking it is_romm
 * and storing platform_id (as a decimal string) as the repo's id -- see
 * Repo.is_romm. Returns the new repo, or NULL if full. */
Repo *config_add_romm_repo(ConsoleGroup *g, const char *label,
                          int platform_id);

/* Remove the repo at index idx within a console. Returns true if removed. */
bool config_remove_repo(ConsoleGroup *g, int idx);

/* Fill repo->download_base from its id if it is empty. No-op for a RomM repo
 * (Repo.is_romm) -- it has no archive.org-shaped download_base to default. */
void repo_set_url_default(Repo *r);

/* Credentials (archive.org S3 access/secret). */
void creds_load(Credentials *c);
bool creds_save(const Credentials *c);

/* RomM instance credentials. romm_creds_load always fills *c (zeroed if no
 * file exists yet). romm_creds_remove deletes the file, leaving RomM
 * unconfigured; true if removed or already absent. */
void romm_creds_load(RommCredentials *c);
bool romm_creds_save(const RommCredentials *c);
bool romm_creds_remove(void);

/* True if enough is set to attempt a connection (server_url plus either
 * api_token or username+password). Does not mean the credentials are valid —
 * only that romm_client_test_connection() is worth calling. */
bool romm_creds_configured(const RommCredentials *c);

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
