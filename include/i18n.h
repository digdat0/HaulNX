#ifndef I18N_H
#define I18N_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* ---- general / shared ---- */
    S_OK,
    S_YES,
    S_CANCEL,
    S_DELETE,
    S_DELETED,
    S_SAVED,
    S_SAVE_FAILED,
    S_ADDED,
    S_CLEARED,
    S_RENAMED,
    S_RENAME_FAILED,
    S_EMPTY,
    S_BACK,
    S_EXIT,
    S_FILE,
    S_LOADING,

    /* ---- tabs ---- */
    S_TAB_BROWSE,
    S_TAB_INSTALLED,
    S_TAB_QUEUE,
    S_TAB_SETTINGS,

    /* ---- browse / home ---- */
    S_TITLE_CONSOLES,
    S_TITLE_REPOS,
    S_SUB_HOME_GROUPED,
    S_SUB_HOME_FLAT,
    S_SUB_REPOS,
    S_NO_COLLECTIONS,
    S_NO_REPOS,

    /* ---- files ---- */
    S_SUB_FILES,
    S_NO_FILES_MATCH,
    S_META_FAILED,
    S_LOADING_META,
    S_QUEUED,
    S_QUEUE_FULL,
    S_DOWNLOAD_ALL,
    S_FREE_SPACE_WARN,
    S_FAT32_WARN,
    S_FAT32_WARN_MSG,
    S_QUEUED_N,
    S_ALREADY_HAVE,

    /* ---- queue ---- */
    S_TITLE_QUEUE,
    S_SUB_QUEUE,
    S_QUEUE_EMPTY,
    S_CANCELLED,
    S_RETRYING,
    S_TOAST_DONE,
    S_TOAST_SAVED,
    S_TOAST_FAILED,
    S_CLEARED_FINISHED,

    /* ---- settings ---- */
    S_TITLE_SETTINGS,
    S_SUB_SETTINGS,
    S_CHECK_UPDATES,
    S_VIEW_LOG,
    S_MANAGE_CONSOLES,
    S_MANAGE_DOWNLOADS,
    S_ADVANCED,
    S_UI_SETTINGS,
    S_TITLE_UI_SETTINGS,
    S_SUB_UI_SETTINGS,
    S_CONTROLS_HELP,
    S_CREDITS,
    S_ROM_FOLDER,
    S_FILTER_EXTS,
    S_TITLE_EXT_FILTER,
    S_SUB_EXT_FILTER,
    S_ADD_EXTENSION,
    S_ADD_EXT_PROMPT,
    S_EXT_ADD_FAILED,
    S_EXT_FILTER_INFO,

    /* ---- advanced ---- */
    S_TITLE_ADVANCED,
    S_SUB_ADVANCED,
    S_STAY_AWAKE,
    S_GROUP_CONSOLES,
    S_ARCHIVE_CREDS,
    S_META_CACHE,
    S_MAX_DOWNLOADS,
    S_MAX_TOTAL_RATE,
    S_MAX_ITEM_RATE,
    S_RATE_UNLIMITED,
    S_ON,
    S_OFF,
    S_SET,
    S_UNSET,
    S_ROMS_OVERRIDE,
    S_ROMS_AUTO,
    S_ROMS_OVERRIDE_TITLE,
    S_ROMS_OVERRIDE_WARN,
    S_ROMS_OVERRIDE_SET,
    S_ROMS_OVERRIDE_CLEARED,
    S_TITLE_ROM_PICKER,
    S_SUB_ROM_PICKER,
    S_ROMS_CURRENT,
    S_NO_SUBFOLDERS,
    S_ROMS_USE_ROOT_WARN,
    /* ---- per-console custom install folder ---- */
    S_INSTALL_FOLDER,         /* dialog title when choosing a console folder */
    S_INSTALL_FOLDER_DEFAULT, /* value when no custom folder is set */
    S_INSTALL_FOLDER_WARN,    /* confirm body when choosing a folder */
    S_INSTALL_FOLDER_SET,     /* toast after setting one */
    S_INSTALL_FOLDER_CLEARED, /* toast after resetting to default */
    S_INSTALL_MODE,           /* Storage row: install-folder mode */
    S_INSTALL_MODE_DEFAULT,   /* value: single ROM folder for every console */
    S_INSTALL_MODE_CUSTOM,    /* value: a folder per console */
    S_CONSOLE_FOLDERS,        /* Storage row: open the per-console folder list */
    S_CONSOLE_FOLDERS_LOCKED, /* toast when the row is used in default mode */
    S_TITLE_CONSOLE_FOLDERS,  /* per-console folder list title */
    S_SUB_CONSOLE_FOLDERS,    /* per-console folder list subtitle */
    S_OPEN,                   /* right-cell value: actionable */
    S_LOCKED,                 /* right-cell value: disabled until unlocked */
    /* ---- ROM-folder dialog on the Installed tab (Y on a console) ---- */
    S_ROM_FOLDER_INSTALLS_TO, /* dialog body: "Installs to:\n%s" */
    S_ROM_FOLDER_DEFAULT_TAG, /* body line when using the default location */
    S_ROM_FOLDER_CUSTOM_TAG,  /* body line when a custom folder is set */
    S_ROM_FOLDER_LOCKED_NOTE, /* body note when per-console folders are off */
    S_SET_FOLDER,             /* button: choose a custom folder */
    S_CHANGE_FOLDER,          /* button: choose a different custom folder */
    S_RESET_DEFAULT,          /* button: clear the custom folder */
    S_RECEIVE_FROM_PC,        /* button: open the LAN receiver for this console */
    S_PIN,                    /* menu entry: pin this console to the top */
    S_UNPIN,                  /* menu entry: unpin this console */
    S_SORT_MENU,              /* menu entry: open the sort picker */
    S_ROM_RECV_TITLE,         /* receive-screen title, e.g. "Receive game" */
    S_ROM_RECV_STEPS,         /* on-screen steps for the ROM receiver (%s = console) */
    S_ROM_RECV_INTO,          /* live line: which folder files land in (%s = path) */
    S_ROM_RECV_CONFIRM,       /* overwrite confirm body (%s = filename) */
    S_ROM_RECV_DONE,          /* toast after a game is saved (%s = filename) */
    S_ROM_RECV_FAIL,          /* toast when the received file couldn't be saved */

    /* ---- downloads folder ---- */
    S_TITLE_DOWNLOADS,
    S_SUB_DOWNLOADS,
    S_DELETE_ALL,
    S_DELETE_ALL_CONFIRM,
    S_DL_ACTIVE_WARN,
    S_DL_QUEUE_WARN,
    S_DL_CLEARED,

    /* ---- installed ---- */
    S_TITLE_INSTALLED,
    S_SUB_INSTALLED,
    S_SUB_INSTALLED_FOLDER,
    S_DIR_PREFIX,
    S_DELETE_SELECTED,
    S_DELETE_ONE,
    S_SIZE_LABEL,
    S_MOVE_UP,
    S_MOVE_UP_MULTI,
    S_MOVING,
    S_MOVING_N,
    S_MOVED_N,
    S_MOVE_PARTIAL,
    S_EMPTY_FOLDER_DELETE,
    S_FOLDER_DELETED,

    /* ---- repo edit ---- */
    S_TITLE_EDIT_REPO,
    S_SUB_EDIT_REPO,
    S_DELETE_REPO,
    S_DELETE_REPO_CONFIRM,

    /* ---- console picker ---- */
    S_TITLE_SELECT_CONSOLE,
    S_SUB_SELECT_CONSOLE,
    S_NO_CONSOLES,

    /* ---- log ---- */
    S_TITLE_LOG,
    S_SUB_LOG,
    S_NO_LOG,
    S_CLEAR_LOG,
    S_CLEAR_LOG_CONFIRM,
    S_LOG_CLEARED,

    /* ---- manage consoles ---- */
    S_TITLE_MANAGE,
    S_SUB_MANAGE,
    S_VIS_BOTH,      /* console shows on both Browse and Installed */
    S_VIS_BROWSE,    /* Browse only */
    S_VIS_INSTALLED, /* Installed only */
    S_VIS_HIDDEN,    /* neither tab */

    /* ---- credentials ---- */
    S_TITLE_CREDS,
    S_SUB_CREDS,
    S_CLEAR_CREDS,
    S_CLEAR_CREDS_CONFIRM,
    S_ACCESS_KEY,
    S_SECRET_KEY,

    /* ---- RomM (second download source, alongside archive.org) ---- */
    S_ROMM,
    S_ROMM_STATUS_NOT_CONFIGURED,
    S_ROMM_STATUS_CONNECTED,
    S_ROMM_STATUS_FAILED,
    S_TITLE_ROMM_CREDS,
    S_SUB_ROMM_CREDS,
    S_ROMM_SERVER_URL,
    S_ROMM_USERNAME,
    S_ROMM_PASSWORD,
    S_ROMM_API_TOKEN,
    S_ROMM_IGNORE_CERT,
    S_ROMM_TEST_CONNECTION,
    S_ROMM_TEST_RUNNING,
    S_ROMM_TEST_OK_TOKEN,
    S_ROMM_TEST_FAIL,
    S_ROMM_CLEAR_CREDS,
    S_ROMM_CLEAR_CREDS_CONFIRM,

    /* ---- RomM: Browse (Y) source picker + platform picker ---- */
    S_ADD_SOURCE_TITLE,
    S_ADD_SOURCE_BODY,
    S_SOURCE_ARCHIVE_ORG,
    S_SOURCE_ROMM,
    S_TITLE_ROMM_PLATFORMS,
    S_SUB_ROMM_PLATFORMS,
    S_N_ROMS,
    S_ROMM_PLATFORM_UNMAPPED,
    S_LABEL_ROMM_PLATFORM_ID,
    S_LABEL_ROMM_SOURCE,
    S_ROMM_REPO_FIELD_LOCKED,
    S_SWITCH_TO_GRID,
    S_SWITCH_TO_LIST,

    /* ---- update ---- */
    S_TITLE_UPDATE,
    S_UPDATE_FETCH_FAIL,
    S_UPDATE_UP_TO_DATE,
    S_UPDATE_CONFIRM,
    S_UPDATING,
    S_UPDATE_DOWNLOADING,
    S_UPDATE_DL_CANCEL,
    S_UPDATE_START_FAIL,
    S_UPDATE_FAIL,
    S_UPDATE_OK,

    S_CONTINUE,

    /* ---- controls dialog ---- */
    S_CONTROLS_BODY,

    /* ---- exit ---- */
    S_EXIT_CONFIRM,

    /* ---- filter ---- */
    S_FILTER_PROMPT,
    S_RENAME_PROMPT,

    /* ---- language ---- */
    S_LANGUAGE,
    S_TITLE_LANGUAGE,
    S_SUB_LANGUAGE,
    S_LANG_RESTART,

    /* ---- theme ---- */
    S_THEME,
    S_THEME_DARK,
    S_THEME_LIGHT,

    /* ---- network check ---- */
    S_NET_CHECK_STARTUP,
    S_NO_NETWORK,
    S_NO_NETWORK_MSG,
    S_RETRY,
    S_REDOWNLOAD,       /* queue action: drop the file and pull it again */
    S_CANCEL_DOWNLOAD,  /* queue action: abort the item */
    S_QUEUE_BUSY_PROMPT, /* menu shown for an item stuck verifying/unzipping */

    /* ---- repo edit labels ---- */
    S_LABEL_NAME,
    S_LABEL_ARCHIVE_ID,
    S_LABEL_DOWNLOAD_URL,
    S_LABEL_ENABLED,
    S_AUTO,
    S_CONSOLE_PREFIX,
    S_N_REPOS,
    S_N_APPS,

    /* ---- search ---- */
    S_TITLE_SEARCH,
    S_SUB_SEARCH,
    S_SEARCH_PROMPT,
    S_SEARCH_CONSOLE,
    S_SEARCH_REPO,
    S_SEARCH_INSTALLED,
    S_SEARCHING,
    S_SUB_SEARCHING,     /* footer hint while a scan runs: "B cancel" */
    S_SEARCH_NO_RESULTS,
    S_SEARCH_N_RESULTS,

    /* ---- cache management ---- */
    S_MANAGE_CACHE,
    S_TITLE_CACHE,
    S_SUB_CACHE,
    S_CACHE_EMPTY,
    S_CLEAR_CACHE,
    S_CLEAR_CACHE_CONFIRM,
    S_CACHE_CLEARED,
    S_N_CACHED,

    /* ---- hardcoded-string fixes ---- */
    S_QUEUE_ALL_CONFIRM,
    S_QUEUED_N_FULL,
    S_DL_N_TOTAL,
    S_DELETED_N,
    S_FILTER_GUIDE,

    /* ---- sort ---- */
    S_SORT_DEFAULT,
    S_SORT_NAME_AZ,
    S_SORT_NAME_ZA,
    S_SORT_SIZE_DESC,
    S_SORT_SIZE_ASC,

    /* ---- manage data submenu ---- */
    S_MANAGE_DATA,
    S_TITLE_MANAGE_DATA,
    S_SUB_MANAGE_DATA,

    /* ---- queue offline banner ---- */
    S_WAITING_NETWORK,

    /* ---- search result cap notice ---- */
    S_SEARCH_CAPPED,

    /* ---- view logs submenu + debug log viewer ---- */
    S_VIEW_LOGS,
    S_TITLE_VIEW_LOGS,
    S_SUB_VIEW_LOGS,
    S_DEBUG_LOG,
    S_TITLE_DEBUG_LOG,
    S_SUB_DEBUG_LOG,
    S_CLEAR_DEBUG_CONFIRM,

    /* ---- metadata refresh ---- */
    S_REFRESH_ALL,
    S_REFRESH_META,
    S_REFRESH_DONE,

    /* ---- queue actions + summary ---- */
    S_RETRY_ALL,
    S_CLEAR_FINISHED,
    S_RETRIED_N,
    S_TOAST_ALL_DONE,

    /* ---- installed search ---- */
    S_TITLE_INST_SEARCH,
    S_SUB_INST_SEARCH,

    /* ---- card view ---- */
    S_CARD_VIEW,
    S_SUB_HOME_CARDS,
    S_SUB_HOME_FLAT_CARDS,
    S_SUB_INSTALLED_CARDS,
    S_SUB_QUEUE_CARDS,

    /* ---- destructive-action warning ---- */
    S_CANT_UNDO,

    /* ---- empty-state hints ---- */
    S_QUEUE_EMPTY_HINT,
    S_INSTALLED_EMPTY_HINT,

    /* ---- queue header summary ---- */
    S_QUEUE_N_ACTIVE,
    S_QUEUE_N_WAITING,
    S_QUEUE_N_FAILED,

    /* ---- persisted queue-data viewer (View logs) ---- */
    S_QUEUE_STATE,
    S_TITLE_QUEUE_STATE,
    S_SUB_QUEUE_STATE,
    S_CLEAR_QUEUE_STATE,
    S_CLEAR_QUEUE_CONFIRM,

    /* ---- import collection over the LAN ---- */
    S_IMPORT_COLLECTION,
    S_TITLE_IMPORT,
    S_SUB_IMPORT,
    S_IMPORT_STEPS,
    S_IMPORT_REPO_NOTE,  /* accent chip: push straight from the app utility */
    S_IMPORT_NO_NET,
    S_IMPORT_SRV_FAIL,
    S_IMPORT_BAD_FILE,
    S_RECV_PROGRESS, /* live subtitle while an upload is arriving */
    S_IMPORT_CONFIRM,
    S_IMPORT_DONE,
    S_IMPORT_SAVE_FAIL,

    /* ---- export collection over the LAN (mirror of import) ---- */
    S_EXPORT_COLLECTION,
    S_TITLE_EXPORT,
    S_SUB_EXPORT,
    S_EXPORT_STEPS,

    /* ---- update source prompt + update-over-Wi-Fi receive screen ---- */
    S_UPDATE_HOW,
    S_UPDATE_SRC_GITHUB,
    S_UPDATE_SRC_WIFI,
    S_UPDATE_WIFI_TITLE,
    S_UPDATE_WIFI_STEPS,

    /* ---- an .nro build pushed over the same receiver ---- */
    S_NRO_CONFIRM,
    S_NRO_STAGE_FAIL,
    S_NRO_STAGED,
    S_NRO_RESTART_NOW,
    S_NRO_LATER,

    S_XFER_LOG,
    S_TITLE_XFER_LOG,
    S_CLEAR_XFER_CONFIRM,

    S_SPEEDTEST_LOG,
    S_TITLE_SPEEDTEST_LOG,
    S_CLEAR_SPEEDTEST_CONFIRM,

    /* ---- restore the backup an import left behind ---- */
    S_RESTORE_COLLECTION,
    S_RESTORE_NONE,
    S_RESTORE_PICK,
    S_RESTORE_RECENT,
    S_RESTORE_OLDER,
    S_RESTORE_CONFIRM,
    S_RESTORE_DONE,

    /* ---- first-run welcome (shown while there are no collections) ---- */
    S_WELCOME_TITLE,
    S_WELCOME_BODY,
    S_WELCOME_IMPORT,
    S_WELCOME_MANUAL,
    S_WELCOME_LATER,

    /* ---- update detection + release-notes viewer ---- */
    S_UPDATES_AVAIL,
    S_RELEASE_NOTES,
    S_UPDATE_AVAIL,          /* count-less "Update available" chip */
    S_RESTART_TO_UPDATE,     /* chip after an update is staged, awaiting restart */
    S_CHK_UPDATES_STARTUP,   /* advanced toggle: check for updates on startup */

    /* ---- keyboard guide/hint text for repo fields (no %s; the label
     *      variants above carry %s for the RepoEdit row display) ---- */
    S_HINT_NAME,
    S_HINT_ARCHIVE_ID,
    S_HINT_DOWNLOAD_URL,

    /* ---- Browse multi-select + bulk enqueue ---- */
    S_VIEW,                  /* X menu title in the file list */
    S_FILTER,                /* menu entry: set the name filter */
    S_SORT,                  /* menu entry: open the sort picker */
    S_SELECT_ALL_SHOWN,      /* "Select all shown (%d)" */
    S_CLEAR_SELECTION,
    S_N_SELECTED,            /* "%d selected" — info line under the list */
    S_QUEUE_SELECTED,        /* confirm-dialog title */
    S_N_FILES,               /* "%d files" — first line of the summary */
    S_FREE_SPACE,
    S_QUEUE_PENDING,         /* bytes the queue still owes before this batch */
    S_N_SKIPPED_INSTALLED,   /* "%d skipped — already installed" */
    S_ONLY_N_SLOTS,          /* "Queue has room for %d more" */
    S_ARCHIVES_EXPAND,       /* caveat: extracted archives use more than this */
    S_QUEUE_N,               /* "Queue %d" */
    S_QUEUE_N_THAT_FIT,      /* "Queue the %d that fit" */
    S_QUEUE_N_ANYWAY,        /* "Queue all %d anyway" */
    S_ALL_ALREADY_INSTALLED,
    S_SKIP_INSTALLED,        /* advanced toggle */
    S_SPACE_HOLD,            /* queue status: waiting for space, nothing failed */

    /* ---- extraction benchmark knobs (Advanced; dev/perf tuning) ---- */
    S_EX_BENCH,              /* toggle: log per-archive extraction throughput */
    S_EX_PREALLOC,           /* toggle: preallocate output files */
    S_EX_CHUNK,              /* value: write chunk size (1/2/4 MB) */

    /* ==== reorganized settings hierarchy (v2) ============================= */
    /* Top-level section rows on the Settings screen. */
    S_SEC_APPEARANCE,
    S_SEC_DOWNLOADS,
    S_SEC_SOURCES,
    S_SEC_STORAGE,
    S_SEC_TRANSFERS,
    S_SEC_INSTALL_PC,
    S_SEC_ACCOUNT,
    S_SEC_UPDATES,
    S_SEC_LOGS,
    S_SEC_DIAGNOSTICS,
    S_SEC_ABOUT,

    /* Titles + subtitles for the reorganized / new sub-screens. */
    S_TITLE_APPEARANCE,   S_SUB_APPEARANCE,
    S_TITLE_DLPREFS,      S_SUB_DLPREFS,
    S_TITLE_SOURCES,      S_SUB_SOURCES,
    S_TITLE_STORAGE,      S_SUB_STORAGE,
    S_TITLE_TRANSFERS,    S_SUB_TRANSFERS,
    S_TITLE_RECV_CONSOLE, S_SUB_RECV_CONSOLE,
    S_TITLE_ACCOUNT,      S_SUB_ACCOUNT,
    S_TITLE_UPDATES,      S_SUB_UPDATES,
    S_TITLE_DIAGNOSTICS,  S_SUB_DIAGNOSTICS,
    S_TITLE_ABOUT,        S_SUB_ABOUT,
    S_TITLE_EXT_TUNING,   S_SUB_EXT_TUNING,

    /* New row labels. */
    S_KEEP_AWAKE,         /* Downloads: keep awake while downloading */
    S_UPDATE_OVER_WIFI,   /* Transfers: receive a pushed .nro build */
    S_CHECK_NOW,          /* Updates: run a check right now */
    S_SD_CARD,            /* Storage: SD-card status row label */
    S_SD_FREE_OF,         /* "%s free of %s" — SD status value */
    S_MANAGE_META,        /* Storage: open the metadata-cache manager */
    S_EXT_TUNING,         /* Diagnostics: open the extraction-tuning sub-screen */

    /* Diagnostics build features + their results. */
    S_EXPORT_BUNDLE,
    S_BUNDLE_DONE,        /* "Saved debug bundle to %s" */
    S_BUNDLE_FAIL,
    S_NET_SELFTEST,
    S_SELFTEST_RUNNING,
    S_SELFTEST_RESULT,    /* dialog body: "%s\n%s" (LAN line, internet line) */
    S_SELFTEST_LAN_OK,    /* "LAN address: %s" */
    S_SELFTEST_LAN_FAIL,
    S_SELFTEST_NET_OK,
    S_SELFTEST_NET_FAIL,
    S_SPEEDTEST,          /* Diagnostics: run a download + upload speed test */
    S_SPEEDTEST_RUNNING,
    S_SPEEDTEST_RESULT,   /* dialog body: "Download: %.1f Mbps\nUpload: %.1f Mbps" */
    S_SPEEDTEST_FAIL,
    S_SPEEDTEST_SUB,      /* live-view subtitle */
    S_SPEEDTEST_DOWNLOAD, /* live-view row label */
    S_SPEEDTEST_UPLOAD,   /* live-view row label */
    S_SPEEDTEST_CANCEL_HINT, /* footer hint while the test runs */
    S_RESET_DEFAULTS,
    S_RESET_DEFAULTS_CONFIRM,
    S_RESET_DONE,

    /* Storage detail dialog (A on the SD-card row). */
    S_STORAGE_TITLE,
    S_STORAGE_DETAIL,     /* "%s free of %s\nDownloads: %s\nCache: %s" */

    /* About: re-runnable onboarding entry. */
    S_GETTING_STARTED,

    S__COUNT
};

/* Get the localized string for a given ID. Never returns NULL. */
const char *tr(int id);

/* Load language strings from a JSON file on SD card.
 * Pass NULL to reset to built-in English. */
void i18n_load(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* I18N_H */
