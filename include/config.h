#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CONSOLES 64
#define MAX_REPOS    16 /* download repos per console */

/* GitHub repo (owner/name) the in-app updater pulls releases from.
 * >>> EDIT THIS to your repo before building a release you intend to ship. <<< */
#define UPDATE_REPO   "digdat0/ticodlplus"

/* Where the app lives if it can't determine its own path from argv[0]. */
#define DEFAULT_SELF_PATH "sdmc:/switch/TicoDLplus/TicoDLplus.nro"

#define CONFIG_DIR    "sdmc:/switch/ticodlplus"
#define SOURCES_PATH  "sdmc:/switch/ticodlplus/dl_sources.json"
#define CREDS_PATH    "sdmc:/switch/ticodlplus/credentials.json"
#define PREFS_PATH    "sdmc:/switch/ticodlplus/prefs.json"
#define LANG_DIR      "sdmc:/switch/ticodlplus/lang"
#define CACHE_DIR     "sdmc:/switch/ticodlplus/cache"
#define LOG_PATH      "sdmc:/switch/ticodlplus/debug.log"
#define DLLOG_PATH    "sdmc:/switch/ticodlplus/downloads.log"
#define DLLOG_JSON    "sdmc:/switch/ticodlplus/downloads.jsonl"
#define DL_TMP_DIR    "sdmc:/switch/ticodlplus/downloads"
#define QUEUE_STATE_PATH "sdmc:/switch/ticodlplus/queue.json"
#define TICO_DEFAULT_ROMS  "sdmc:/tico/roms"
#define TICO_CONFIG_PATH   "sdmc:/tico/config/general.jsonc"

/* One download source (an archive.org item) within a console group. */
typedef struct {
    char label[64];          /* repo display name, e.g. "No-Intro" */
    char id[256];            /* archive.org item id */
    char download_base[512]; /* base URL; defaults to .../download/<id> */
    bool enabled;            /* "active" flag */
    bool pinned;             /* pinned/favorite — sorted to top of browse */
} Repo;

/* A console (e.g. "snes") that groups one or more download repos. All of a
 * console's repos install into the same tico/roms/<target> folder. */
typedef struct {
    char console[64]; /* display name */
    char target[64];  /* folder under tico/roms */
    Repo repos[MAX_REPOS];
    int repo_count;
    bool shown; /* show on the primary Browse page (default true) */
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

#define MAX_PINNED_DIRS 32

typedef struct {
    bool use_cache;      /* true: load cached metadata if present; false: always refetch */
    bool prevent_sleep;  /* true: keep console awake while downloads are active */
    bool group_consoles; /* true: main list shows consoles (open to see repos);
                            false: flat list, one row per repo */
    int max_downloads;   /* 1–5; how many downloads run simultaneously (default 1) */
    bool net_check;      /* true: warn on startup if no network (default true) */
    char lang[16];       /* language code, e.g. "en", "es", "ja"; empty = English */
    char theme[16];      /* "dark" (default) or "light" */
    bool card_view;      /* true: console lists render as a card grid */
    /* Advanced override for the ROM install root. Empty = auto (detect from
     * TICO). When set, this exact path is used instead of tico/roms. */
    char roms_override[512];
    /* Top-level ROM folders pinned to the top of the Installed tab. */
    char pinned_dirs[MAX_PINNED_DIRS][64];
    int pinned_dir_count;
} Prefs;

/* Load dl_sources.json; seeds from romfs:/dl_sources.json on first run if the
 * sdmc file is missing. Understands the grouped schema and falls back to the
 * older flat sources/console_list schema. Always returns a usable config. */
void config_load(SourcesConfig *cfg);
bool config_save(const SourcesConfig *cfg);

/* Find a console group by its console name or target; NULL if none. */
ConsoleGroup *config_find_console(SourcesConfig *cfg, const char *name);

/* Add a console group (console == target == name) if absent. Returns it (or the
 * existing one), or NULL if full. */
ConsoleGroup *config_add_console(SourcesConfig *cfg, const char *name);

/* Remove the console group at index idx (and its repos). Returns true if removed. */
bool config_remove_console(SourcesConfig *cfg, int idx);

/* Sort console groups alphabetically (case-insensitive) by console name. */
void config_sort(SourcesConfig *cfg);

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

/* Build an archive.org S3 auth header into out, or empty string if no key.
 * Form: "authorization: LOW <access>:<secret>". */
void creds_auth_header(const Credentials *c, char *out, size_t out_sz);

/* Tico emulator detection + ROM path resolution.
 * tico_detect() checks for the emulator in known locations.
 * tico_load_roms_path() reads general.jsonc for a custom roms_path.
 * roms_root() returns the resolved path (valid after tico_init). */
typedef struct {
    bool installed;         /* true if tico.nro or sdmc:/tico/ was found */
    char roms_path[512];    /* resolved ROM folder path */
} TicoState;

/* Run once at startup: detects Tico + reads its config. */
void tico_init(TicoState *ts);

/* Returns the current roms root (pointer into ts->roms_path). */
const char *roms_root(const TicoState *ts);

/* Force the roms root to a user-supplied path (overriding TICO detection).
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
