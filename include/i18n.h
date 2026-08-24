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
    S_TOOLS,                  /* title of the global Tools panel (left slide) */
    S_ADD_REPO,               /* menu entry: add a repo to this console */
    S_ROM_RECV_TITLE,         /* receive-screen title, e.g. "Receive game" */
    S_ROM_RECV_STEPS,         /* on-screen steps for the ROM receiver (%s = console) */
    S_ROM_RECV_INTO,          /* live line: which folder files land in (%s = path) */
    S_ROM_RECV_CONFIRM,       /* overwrite confirm body (%s = filename) */
    S_ROM_RECV_DONE,          /* toast after a game is saved (%s = filename) */
    S_ROM_RECV_FAIL,          /* toast when the received file couldn't be saved */
    S_ROM_RECV_MULTI_DONE,    /* toast: N files saved this session (%d) */
    S_RECV_LIST_RECEIVING,    /* receive-list subtitle while a file is arriving */
    S_RECV_LIST_IDLE,         /* receive-list subtitle between files */
    S_RECV_DONE_ROW,          /* receive-list row right cell: file saved */
    S_RECV_SKIPPED,           /* receive-list row right cell: overwrite declined */

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
    S_SUB_INSTALLED_FOLDER_CARDS, /* folder hint, poster view: Y still marks
                                    * (blue border), but ▶ is D-pad navigation
                                    * here, not delete - that's X > Options only */
    S_DIR_PREFIX,
    S_DELETE_SELECTED,
    S_DELETE_ONE,
    S_SIZE_LABEL,
    S_MOVE_UP,
    S_OPEN_SETTINGS,
    S_OPTIONS,           /* library file Options menu title */
    S_RENAME,            /* menu label: rename the file under the cursor */
    S_MOVE_TO_CONSOLE,   /* menu label: move file(s) to another console */
    S_MOVE_PICK,         /* console picker title: "Move %d to…" */
    S_NO_MOVE_TARGET,    /* toast: no other console to move to */
    S_MOVE_UP_MULTI,
    S_MOVING,
    S_MOVING_N,
    S_MOVED_N,
    S_MOVE_PARTIAL,
    S_EMPTY_FOLDER_DELETE,
    S_FOLDER_DELETED,
    /* multi-file games (a .cue and its .bin tracks, a multi-disc set) shown
     * as a single row standing for every piece */
    S_GROUP_SUBTITLE,    /* row right cell: "%d files · %s" (count, total) */
    S_DELETE_GROUP_ONE,  /* confirm header: name the game and its file count */
    S_GROUP_FILE_COUNT,  /* file dialog line: "Files: %d" */
    S_GROUP_SETS,        /* Appearance toggle label */

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
    S_GITHUB_TOKEN,
    S_STEAMGRIDDB_KEY,

    /* ---- update ---- */
    S_TITLE_UPDATE,
    S_UPDATE_FETCH_FAIL,
    S_UPDATE_UP_TO_DATE,
    S_UPDATE_CONFIRM,
    S_UPDATING,
    S_UPDATE_DOWNLOADING,
    S_UPDATE_DL_CANCEL,
    S_UPDATE_START_FAIL,
    S_UPDATE_TOO_MANY,
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
    S_SUB_SEARCH_EMPTY,  /* footer when the result list is empty (no "A download") */
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
    S_PAUSE_ALL,
    S_CANCEL_ALL,
    S_RETRY_ALL_PAUSED,
    S_RETRY_ALL_CANCELLED,
    S_PAUSE_ALL_CONFIRM,
    S_CANCEL_ALL_CONFIRM,
    S_PAUSED_ALL,
    S_CANCELLED_ALL,
    S_RESUMED_N,

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
    S_TRANSFER_CODE,     /* label before the big 4-digit LAN transfer code */
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
    S_KEEP_ARCHIVES,         /* toggle: keep downloaded archives compressed */
    S_CONVERT_IMPORT,        /* toggle: run the post-import converter on new files */

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
    S_SEC_DATA_FILES,     /* Settings row + screen title: "Data Files" (hosts DAT files + metadata cache) */
    S_SUB_DATA_FILES,     /* Data Files screen subtitle */
    S_MANAGE_DAT_FILES,   /* Data Files: row opening the DAT-files manager */
    S_SUB_META_SECTION,   /* "Manage metadata cache" sub-screen subtitle */
    S_VIEW_CACHED_FILES,  /* "Manage metadata cache" sub-screen: row opening the cache browser */
    S_MANAGE_META,        /* Storage: open the metadata-cache manager */
    S_MANAGE_INBOX,       /* Storage: open the Inbox-folder manager */
    S_TITLE_INBOX_FILES,  /* Inbox-folder manager screen title */
    S_SUB_INBOX_FILES,    /* Inbox-folder manager subtitle (hints) */
    S_INBOX_FILES_EMPTY,  /* Inbox-folder manager empty state */
    S_DELETE_ALL_INBOX_CONFIRM, /* "Delete ALL %d file(s) in the Inbox folder?" */
    S_INBOX_CLEARED,      /* toast after clearing the Inbox folder */
    S_INSTALL_WIFI,       /* PC Sync: row — install a game over Wi-Fi */
    S_INSTALL_USB_CONN,   /* PC Sync: row — install a game over a USB connection */
    S_MANAGE_BACKUPS,     /* Storage: open the emulator/app backups manager */
    S_LARGE_FILES,        /* Storage row label + scan-start title: "Largest files" */
    S_LARGE_FILES_SCANNING, /* spinner while walking the library */
    S_LARGE_FILES_TITLE_N, /* results title (%d = file count shown) */
    S_SUB_LARGE_FILES,    /* results subtitle (hints) */
    S_SUB_LARGE_FILES_EMPTY, /* results subtitle when the library is empty */
    S_LARGE_FILES_NONE,   /* empty-state: no files found */
    S_LARGE_FILES_DEL_BODY, /* confirm-delete body (%s name, %s size) */
    S_SCAN_BOX_ART,        /* Tools row label + scan-start title: "Scan for Box Art" */
    S_SCAN_BOX_ART_NEED_KEY, /* toast: pressed with no SteamGridDB key set */
    S_SCAN_BOX_ART_BODY,   /* pre-scan dialog body: explains the fill/rescan/reset choice below (games) */
    S_SCAN_BOX_ART_FILL,   /* pre-scan dialog button: only resolve games with no art yet */
    S_SCAN_BOX_ART_FORCE,  /* pre-scan dialog button: re-query every game, even resolved ones */
    S_SCAN_BOX_ART_RESET,  /* pre-scan dialog button: wipe every game's cached art */
    S_SCAN_BOX_ART_RESET_CONFIRM, /* danger-confirm body before the reset above actually runs */
    S_SCAN_BOX_ART_RESET_DONE, /* toast after the reset completes */
    S_BOXART_SCANNING,     /* spinner while resolving/downloading covers */
    S_BOXART_RESULTS_TITLE_N, /* results title (%d = distinct titles scanned) */
    S_BOXART_RESULTS_SUB_N, /* results subtitle (%d found of %d titles) */
    S_BOXART_FOUND,        /* per-row status: a cover was resolved + cached */
    S_BOXART_FOUND_LOW,    /* per-row status: resolved, but the name match was a weak/fuzzy guess */
    S_BOXART_NOT_FOUND,    /* per-row status: no SteamGridDB match */
    S_SCAN_BOX_ART_CONSOLE, /* per-console Options row + scan-start title, scoped scan */
    S_MANAGE_BOX_ART,      /* Storage row label + screen title: browse/delete cached covers */
    S_BOXART_MANAGE_SCANNING, /* spinner while finding what's cached on disk */
    S_BOXART_MANAGE_EMPTY, /* empty-state: no cached covers yet (Storage) */
    S_BOXART_MANAGE_LIST_SUB, /* per-console cover list subtitle (hints) */
    S_BOXART_DELETE_BODY,  /* confirm-delete body for one cached cover (%s title) */
    S_BOX_ART_TOGGLE,      /* Appearance toggle label: show cached covers in the list */
    S_BOX_ART_AUTO_TOGGLE, /* Appearance toggle label: auto-fetch art for newly landed games */
    S_BOXART_FILTER_ALL,   /* results filter option: show every scanned title */
    S_BOXART_FILTER_LOW,   /* results filter option: show only weak/fuzzy-match hits */
    S_BOXART_SEARCH_GUIDE, /* swkbd guide text for a custom box-art search */
    S_BOXART_SEARCH_CUSTOM, /* row-menu option: type a search term and re-resolve a cover */
    S_BOXART_DELETE_COVER, /* row-menu option: delete this title's cached cover */
    S_BOXART_CUSTOM_FOUND, /* toast: custom search resolved a cover */
    S_BOXART_CUSTOM_NOT_FOUND, /* toast: custom search found no match */
    S_BOXART_PICKER_TITLE,  /* art picker screen title */
    S_BOXART_PICKER_SEARCHING, /* spinner: listing candidate covers + thumbs */
    S_BOXART_PICKER_SUB_N,  /* art picker subtitle/hints (%d = covers found) */
    S_BOXART_PICKER_OPTION_N, /* card label under each thumbnail (%d = 1-based) */
    S_BOXART_PICKER_DOWNLOADING, /* spinner: saving the picked cover */
    S_CONSOLE_ART,          /* per-console Options row label + menu title */
    S_CONSOLE_ART_BODY,     /* menu body: what the option controls */
    S_CONSOLE_ART_USE_DEFAULT, /* menu button: switch back to the built-in icon */
    S_CONSOLE_ART_USE_BOXART, /* menu button: switch to the already-saved cover */
    S_CONSOLE_ART_FIND,     /* menu button: no cover saved yet, go find one */
    S_CONSOLE_ART_CHANGE,   /* menu button: a cover is saved, search for a different one */
    S_SCAN_CONSOLE_ART,     /* Tools row label + scan-start title: bulk console-icon scan */
    S_SCAN_CONSOLE_ART_BODY, /* pre-scan dialog body: explains the fill/rescan choice below */
    S_SCAN_CONSOLE_ART_FILL, /* pre-scan dialog button: only resolve consoles with no art yet */
    S_SCAN_CONSOLE_ART_FORCE, /* pre-scan dialog button: re-query every console, even resolved ones */
    S_SCAN_CONSOLE_ART_RESET, /* pre-scan dialog button: wipe every console's cached art, back to default icons */
    S_SCAN_CONSOLE_ART_RESET_CONFIRM, /* danger-confirm body before the reset above actually runs */
    S_SCAN_CONSOLE_ART_RESET_DONE, /* toast after the reset completes */
    S_SCAN_ART,             /* Tools row label: combined entry point for the two scans below */
    S_SCAN_ART_BODY,        /* pre-scan chooser dialog body: pick which scan to run */
    S_ART_CACHE,           /* Data Files row label + menu title: the on-disk box-art cache */
    S_ART_CACHE_N,         /* Data Files row value (%d covers, %s total size) */
    S_ART_CACHE_NONE,      /* Data Files row value / menu body when nothing is cached */
    S_ART_CACHE_BROWSE,    /* Art Cache menu option: open Manage Box Art */
    S_ART_CACHE_CLEAR,     /* Art Cache menu option: delete every cached cover */
    S_ART_CACHE_CLEAR_CONFIRM, /* confirm body for Clear Entire Cache (%d covers) */
    S_ART_CACHE_CLEARED,   /* toast: the whole art cache was cleared */
    S_TITLE_BACKUPS,      /* Backups screen title */
    S_SUB_BACKUPS,        /* Backups screen subtitle (empty) */
    S_SUB_BACKUPS_N,      /* Backups subtitle with total size + hints (%s) */
    S_BACKUPS_EMPTY,      /* Backups screen empty state */
    S_CLEAR_BACKUPS,      /* Backups: clear-all action / confirm title */
    S_CLEAR_BACKUPS_CONFIRM, /* Backups: clear-all confirm body */
    S_MANAGE_DATS,        /* Storage: open the DAT-files manager */
    S_TITLE_DATS,         /* DAT-files screen title */
    S_SUB_DATS,           /* DAT-files screen subtitle */
    S_DAT_DOWNLOAD,       /* DAT-files: the auto-download action row */
    S_DAT_SYNC,           /* DAT-files: spinner/title while downloading */
    S_DAT_SYNC_DONE,      /* DAT-files: "%d downloaded, %d missing" result */
    S_DAT_LISTING_FAIL,   /* DAT-files: couldn't fetch the repo listing */
    S_DAT_RATELIMIT,      /* DAT-files: listing GET hit GitHub's rate limit */
    S_DAT_NONE,           /* DAT-files: empty-list placeholder */
    S_DAT_NO_MATCH,       /* DAT-files: no configured console matches the DB */
    S_REGION_PRIORITY,    /* DAT-files: row opening the 1G1R region-order screen */
    S_TITLE_REGION_PRIORITY, /* Region-order screen title */
    S_SUB_REGION_PRIORITY,   /* Region-order screen subtitle ("A moves up") */
    S_REGION_WORLD,       /* region name: World */
    S_REGION_USA,         /* region name: USA */
    S_REGION_EUROPE,      /* region name: Europe */
    S_REGION_JAPAN,       /* region name: Japan */
    S_EXT_TUNING,         /* Diagnostics: open the extraction-tuning sub-screen */

    /* Diagnostics build features + their results. */
    S_EXPORT_BUNDLE,
    S_VIEW_BUNDLE,        /* Logs row: open the exported bundle in the viewer */
    S_BUNDLE_DONE,        /* "Saved debug bundle to %s" */
    S_BUNDLE_FAIL,
    S_CLEAR_ALL_LOGS,     /* Logs row: delete every log file */
    S_CLEAR_ALL_LOGS_CONFIRM,
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
    S_MTP_ENABLED,        /* Diagnostics: USB file-transfer (MTP) on/off toggle */
    S_MTP_DISABLED_TOAST, /* toast: "Connect to PC over USB" while the toggle is off */
    S_USB3_STATUS,        /* Diagnostics: read-only "USB 3.0" row label */
    S_ENABLED,
    S_DISABLED,
    S_UNKNOWN,
    S_INV_SERVER,         /* Transfers: companion inventory server toggle label */
    S_INV_ADDRESS,        /* Transfers: read-only address the companion connects to */
    S_SD_FULL_ACCESS,         /* Transfers: full SD card access toggle label */
    S_SD_FULL_ACCESS_CONFIRM, /* confirm dialog body shown when turning it on */

    /* Storage detail dialog (A on the SD-card row). */
    S_STORAGE_TITLE,
    S_STORAGE_DETAIL,     /* "%s free of %s\nDownloads: %s\nCache: %s" */

    /* ---- Help hub: Getting Started / How-To / Troubleshooting ---- */
    S_SEC_HELP,             /* Settings row + Help hub screen title: "Help" */
    S_TITLE_HELP,           S_SUB_HELP,
    S_SUB_HELP_TOPICS,      /* article-list subtitle: "A select  B back" */
    S_SUB_HELP_ARTICLE,     /* article-detail subtitle: "B back" */
    S_GETTING_STARTED,      /* category label + row: "Getting started" */
    S_HELP_HOWTO,           /* category label + row: "How-To" */
    S_HELP_TROUBLESHOOTING, /* category label + row: "Troubleshooting" */
    S_REPLAY_TOUR,          /* Getting Started category: row 0, replays the tour */
    S_HELP_SEARCH,          /* Help hub row 3: "Search" */
    S_TITLE_HELP_SEARCH,    /* Screen::HelpSearch title: "Search Help" */
    S_HELP_SEARCH_GUIDE,    /* swkbd guide text prompting for a keyword */
    S_SUB_HELP_SEARCH,      /* results-list subtitle: "A open  Y new search  B back" */
    S_HELP_SEARCH_NO_RESULTS, /* empty-state body when no article matched */

    /* Guided first-time tour (Welcome() is its final step). */
    S_TOUR_NEXT, S_TOUR_BACK, S_TOUR_CLOSE, S_TOUR_DONE,
    S_TOUR1_TITLE, S_TOUR1_BODY,
    S_TOUR2_TITLE, S_TOUR2_BODY,
    S_TOUR3_TITLE, S_TOUR3_BODY,
    S_TOUR4_TITLE, S_TOUR4_BODY,
    S_TOUR5_TITLE, S_TOUR5_BODY,
    S_TOUR6_TITLE, S_TOUR6_BODY,
    S_TOUR7_TITLE, S_TOUR7_BODY,
    S_TOUR8_TITLE, S_TOUR8_BODY, /* Collections tab: what it is + adding a repo (shown between tour2 and tour3) */
    S_TOUR_QUEUE_MOCK_NAME, /* mocked-up queue card filename behind the Queue tour step */

    /* Getting Started articles. */
    S_GS1_TITLE, S_GS1_BODY,
    S_GS2_TITLE, S_GS2_BODY,
    S_GS3_TITLE, S_GS3_BODY,
    S_GS4_TITLE, S_GS4_BODY,
    S_GS5_TITLE, S_GS5_BODY,
    S_GS6_TITLE, S_GS6_BODY,

    /* How-To articles. */
    S_HOWTO1_TITLE, S_HOWTO1_BODY,
    S_HOWTO2_TITLE, S_HOWTO2_BODY,
    S_HOWTO3_TITLE, S_HOWTO3_BODY,
    S_HOWTO4_TITLE, S_HOWTO4_BODY,
    S_HOWTO5_TITLE, S_HOWTO5_BODY,
    S_HOWTO6_TITLE, S_HOWTO6_BODY,
    S_HOWTO7_TITLE, S_HOWTO7_BODY,
    S_HOWTO8_TITLE, S_HOWTO8_BODY,
    S_HOWTO9_TITLE, S_HOWTO9_BODY,
    S_HOWTO10_TITLE, S_HOWTO10_BODY,
    S_HOWTO11_TITLE, S_HOWTO11_BODY,
    S_HOWTO12_TITLE, S_HOWTO12_BODY,
    S_HOWTO13_TITLE, S_HOWTO13_BODY,
    S_HOWTO14_TITLE, S_HOWTO14_BODY,
    S_HOWTO15_TITLE, S_HOWTO15_BODY,
    S_HOWTO16_TITLE, S_HOWTO16_BODY,
    S_HOWTO17_TITLE, S_HOWTO17_BODY,
    S_HOWTO18_TITLE, S_HOWTO18_BODY,
    S_HOWTO19_TITLE, S_HOWTO19_BODY, /* how unzipping/extraction works */
    S_HOWTO20_TITLE, S_HOWTO20_BODY, /* set up archive.org/GitHub/SteamGridDB credentials */
    S_HOWTO21_TITLE, S_HOWTO21_BODY, /* what's in each Settings section */
    S_HOWTO22_TITLE, S_HOWTO22_BODY, /* set a custom install folder for a console */
    S_HOWTO23_TITLE, S_HOWTO23_BODY, /* control download speed and concurrency */
    S_HOWTO24_TITLE, S_HOWTO24_BODY, /* hide file types you don't want to see */

    /* Troubleshooting articles. */
    S_TS1_TITLE, S_TS1_BODY,
    S_TS2_TITLE, S_TS2_BODY,
    S_TS3_TITLE, S_TS3_BODY,
    S_TS4_TITLE, S_TS4_BODY,
    S_TS5_TITLE, S_TS5_BODY,
    S_TS6_TITLE, S_TS6_BODY,
    S_TS7_TITLE, S_TS7_BODY,
    S_TS8_TITLE, S_TS8_BODY,
    S_TS9_TITLE, S_TS9_BODY,
    S_TS10_TITLE, S_TS10_BODY,
    S_TS11_TITLE, S_TS11_BODY,
    S_TS12_TITLE, S_TS12_BODY,
    S_TS13_TITLE, S_TS13_BODY, /* an archive downloaded but won't extract */
    S_TS14_TITLE, S_TS14_BODY, /* GitHub rate limit hit */
    S_TS15_TITLE, S_TS15_BODY, /* archive.org says the item needs a login */
    S_TS16_TITLE, S_TS16_BODY, /* downloads are slower than expected */
    S_TS17_TITLE, S_TS17_BODY, /* a file type isn't showing in Library */
    S_TS18_TITLE, S_TS18_BODY, /* the first-run tour / Welcome screen won't come back */

    /* ---- DAT verification (Library tab) ---- */
    S_VERIFY_DAT,      /* console options-menu entry ("Verify Files") */
    S_VERIFYING,       /* progress spinner caption */
    S_SUB_VERIFY,      /* results-screen footer hint */
    S_NO_DAT_TITLE,
    S_NO_DAT_BODY,     /* how to add a DAT when nothing can fetch one; %s = expected path */
    S_NO_DAT_FETCH_BODY, /* offers an on-the-spot download; %s = system name */
    S_DAT_FETCH_NOW,     /* confirm button: download it now */
    S_DAT_FETCH_QUEUED,  /* toast after confirming the on-the-spot download */
    S_DAT_FETCH_FAIL,    /* toast: the on-the-spot download didn't find/land a match */
    S_DAT_LOAD_FAIL,
    S_VERIFY_SUMMARY,  /* %d verified, %d bad, %d unknown */
    S_VERIFY_OK,
    S_VERIFY_BAD,
    S_VERIFY_UNKNOWN,
    S_VERIFY_ERR,
    S_VERIFY_CANCELLED,
    S_VERIFY_MISNAMED,   /* verified data, non-canonical file name */
    S_SUB_VERIFY_RENAME, /* footer hint when renamable files are present */
    S_RENAME_TO_DAT,     /* single-file rename dialog title + confirm button */
    S_RENAME_ONE_BODY,   /* %s = canonical name */
    S_RENAME_ALL,        /* bulk rename dialog title + confirm button */
    S_RENAME_ALL_BODY,   /* %d = count of files to rename */
    S_RENAME_ALL_DONE,   /* %d renamed, %d skipped */

    /* ---- receive a DAT from a PC over Wi-Fi (Library tab) ---- */
    S_RECV_DAT,          /* console options-menu entry: get a DAT from a PC */
    S_DAT_RECV_TITLE,    /* DAT receive-screen title */
    S_DAT_RECV_STEPS,    /* on-screen steps for the DAT receiver */
    S_DAT_RECV_CONFIRM,  /* apply confirm body (%d games listed, %s = console) */
    S_DAT_RECV_DONE,     /* toast after a DAT is saved (%d games listed) */
    S_DAT_RECV_BAD,      /* toast when the received file isn't a valid DAT */
    S_DAT_RECV_UNKNOWN,  /* toast when the DAT's system isn't recognised (%s) */
    S_DAT_RECV_UNKNOWN_SHORT, /* same, no console name (passive inv-server push) */
    S_DAT_PUSH_DONE,     /* toast after a companion-pushed DAT is filed (%d, %s) */
    S_DAT_RECV_HOW,      /* prompt: how to send the DAT (companion connected) */
    S_DAT_RECV_VIA_APP,  /* choice: receive from the app utility */
    S_DAT_RECV_VIA_URL,  /* choice: show the browser upload URL */
    S_DAT_RECV_APP_HINT, /* toast: push a DAT from the app utility's DAT tab */

    /* ---- verify results: report export + forced re-verify (Library tab) ---- */
    S_VERIFY_ACTIONS,    /* Y-menu title on the verify results screen */
    S_EXPORT_REPORT,     /* Y-menu entry: write the full report to the SD card */
    S_EXPORT_FIXDAT,     /* Y-menu entry: write a fixdat of the missing entries */
    S_REVERIFY_FRESH,    /* Y-menu entry: re-verify ignoring the hash cache */
    S_REVERIFY_TITLE,    /* forced re-verify confirm title */
    S_REVERIFY_BODY,     /* forced re-verify confirm body */
    S_REPORT_FAIL,       /* toast when the report couldn't be written */
    S_REPORT_DONE,       /* toast after the report is written (%s = path) */
    S_FIXDAT_DONE,       /* toast after the fixdat is written (%d missing, %s path) */

    /* ---- verify every console in one pass (Library tab) ---- */
    S_VERIFY_ALL,        /* console options-menu entry: verify all consoles */
    S_NO_DATS,           /* toast when no console has a DAT to verify */
    S_SUB_VERIFY_ALL,    /* subtitle on the aggregate results screen */
    S_VERIFY_ALL_ROW,    /* per-console row tallies (%d ok/bad/unknown/missing) */
    S_VERIFY_ALL_DONE,   /* aggregate results title (%d consoles checked) */
    S_AUDIT_SUMMARY,     /* verify-all subtitle: library-wide bad/unknown/missing roll-up */

    /* ---- re-acquire a bad dump via search (Library tab) ---- */
    S_SUB_VERIFY_BAD,    /* results subtitle when there are bad dumps (A hint) */
    S_REACQUIRE,         /* confirm button: search for a replacement copy */
    S_REACQUIRE_TITLE,   /* re-acquire confirm title */
    S_REACQUIRE_BODY,    /* re-acquire confirm body (%s = file name) */
    S_SHOW_MISSING,      /* Y-menu: browse missing titles (%d = count) */
    S_MISSING_TITLE,     /* missing-list screen title (%d = count) */
    S_MISSING_TITLE_FILTERED, /* missing-list title while a filter is active (%d shown, %d total) */
    S_SUB_MISSING,       /* missing-list subtitle (A find & download hint) */
    S_SUB_MISSING_FILTERED, /* missing-list subtitle while a filter is active */
    S_MISSING_NO_MATCH,  /* empty-state: filter matched nothing */
    S_FILTER_MISSING_PROMPT, /* keyboard guide for the missing-list filter */
    S_ADD_SOURCE_HOW,    /* title: choose how to add a source */
    S_SRC_SEARCH_IA,     /* option: search archive.org */
    S_SRC_ENTER_ID,      /* option: enter an archive.org item id by hand */
    S_IA_QUERY_GUIDE,    /* keyboard guide for the search query */
    S_TITLE_IA_SEARCH,   /* archive.org search results title */
    S_SUB_IA_SEARCH,     /* archive.org search results subtitle */
    S_IA_NO_RESULTS,     /* empty-state: nothing matched */
    S_IA_RESULTS,        /* results count line (%d) */
    S_IA_SEARCH_FAIL,    /* toast: the search request failed */
    S_IA_ADD_TITLE,      /* confirm-add dialog title */
    S_IA_ADD_BODY,       /* confirm-add dialog body (%s console, %s item) */
    S_TIDY_LIBRARY,      /* menu: scan the library for issues to fix */
    S_TIDY_CONSOLE,      /* menu: tidy just one console's folder */
    S_STORAGE_OVERVIEW,  /* Tools menu: library-wide storage summary */
    S_HAVE_MISSING,      /* console Options: verify then show missing titles ("Missing Games") */
    S_CONSOLE_INFO,      /* console Options: read-only per-console stats dialog */
    S_TIDY_SCANNING,     /* spinner while scanning */
    S_TIDY_TITLE,        /* tidy results title (%d = issue count) */
    S_SUB_TIDY,          /* tidy results subtitle */
    S_SUB_TIDY_EMPTY,    /* tidy results subtitle when there is nothing to fix */
    S_SUB_TIDY_FILTER_EMPTY, /* subtitle when a filter hides every issue (but some exist) */
    S_TIDY_FILTER_EMPTY, /* empty-state: no issues match the active filter */
    S_TIDY_CLEAN,        /* empty-state: nothing to fix */
    S_TIDY_DUP,          /* row status: duplicate file */
    S_TIDY_MISFILED,     /* row status: move to console (%s) */
    S_TIDY_MOVE_TITLE,   /* confirm-move dialog title */
    S_TIDY_MOVE_BODY,    /* confirm-move body (%s file, %s console) */
    S_TIDY_MOVE_FAIL,    /* toast: move refused/failed */
    S_TIDY_DUP_TITLE,    /* confirm-delete-duplicate dialog title */
    S_TIDY_DUP_BODY,     /* confirm-delete body (%s this path, %s kept path) */
    S_MOVED,             /* toast: file moved */
    S_TIDY_CLONE,        /* 1G1R row status: duplicate regional/revision copy, keeping %s */
    S_ONEGR_SCAN,        /* menu: reduce regional/revision duplicates to one per title (1G1R) */
    S_ONEGR_TITLE,       /* 1G1R results title (%d = duplicate count) */
    S_ONEGR_CLEAN,       /* empty-state: no duplicate copies found */
    S_ONEGR_DELETE_TITLE,/* confirm-delete-duplicate dialog title */
    S_ONEGR_BODY,        /* confirm-delete-duplicate body (%s this file, %s kept) */
    S_TIDY_ORPHAN_EMPTY, /* row status: zero-byte file */
    S_TIDY_ORPHAN_PART,  /* row status: leftover incomplete transfer */
    S_TIDY_ORPHAN_TITLE, /* confirm-delete-orphan dialog title */
    S_TIDY_ORPHAN_BODY,  /* confirm-delete-orphan body (%s file name, %s reason) */
    S_TIDY_PRUNED_CACHE, /* toast: stale hash/verify cache rows dropped (%d) */
    S_TIDY_FILTER_ALL,       /* filter option: show every issue kind */
    S_TIDY_FILTER_MISFILED,  /* filter option: misfiled files only */
    S_TIDY_FILTER_ORPHAN,    /* filter option: orphaned files only */
    S_TIDY_FILTER_CLONE,     /* filter option: 1G1R clones only */
    S_TIDY_FIX_ALL_TITLE,    /* Ⓧ confirm dialog title */
    S_TIDY_FIX_ALL_CONFIRM,  /* Ⓧ confirm dialog body (%d = issue count) */
    S_TIDY_FIX_ALL_DONE,     /* toast: bulk fix finished, all succeeded (%d) */
    S_TIDY_FIX_ALL_PARTIAL,  /* toast: bulk fix finished, some failed (%d fixed, %d failed) */

    /* ---- inbox sorter: file games from a PC into the right console ---- */
    S_RECV_SORT,         /* X-menu: receive a game and auto-sort it */
    S_SORT_INBOX,        /* X-menu: sort whatever is staged in the inbox now */
    S_INBOX_EMPTY,       /* toast: nothing staged to sort */
    S_INBOX_LABEL,       /* on-screen label for the inbox as a receive target */
    S_INV_GAME_INBOX,    /* toast: a game pushed over the live link (%s) hit the inbox */
    S_INV_GAME_FILED,    /* toast: a Library push (%s) landed straight in a console folder (%s) */
    S_LIVE_RECV_TITLE,   /* title of the receive page shown for a push over the live link */
    S_LIVE_RECV_MSG,     /* body of that page while a file is arriving */
    S_SORT_DONE,         /* results title (%d filed, %d need a console) */
    S_SUB_SORT,          /* results subtitle when everything was filed */
    S_SUB_SORT_PICK,     /* results subtitle when some rows need a hand-pick */
    S_SORT_UNKNOWN,      /* row value: couldn't identify the console */
    S_SORT_GUESS,        /* row value prefix before a best-guess console name */
    S_SORTED,            /* toast: a staged file was filed after a manual pick */
    S_SORT_FAIL,         /* toast: filing failed (name clash / move error) */
    S_SORT_DELETE_FAIL,  /* toast: deleting an inbox file failed */

    S_PC_SYNC,           /* Tools row label: combined entry point for USB / Wi-Fi transfer below */
    S_PC_SYNC_BODY,      /* pre-connect chooser dialog body: pick which link to use */

    S_USB_MENU,          /* X-menu: connect the console to a PC over USB */
    S_USB_TITLE,         /* USB screen title */
    S_USB_STEPS,         /* USB screen instructions (empty-state body) */
    S_USB_WAIT,          /* USB subtitle: brought up, waiting for the PC */
    S_USB_CONNECTED,     /* USB subtitle: host has connected */
    S_USB_FAIL,          /* toast: couldn't bring USB device mode up */
    S_USB_RECEIVING,     /* USB subtitle: a file is arriving from the PC */
    S_USB_XFER,          /* USB row right: "%d%%  %s / %s" (pct, done, total) */
    S_USB_FAILED,        /* USB row right: this file's transfer failed */
    S_USB_EXTRACTING,    /* USB row right: unpacking a received archive, "%d%%" */
    S_USB_BUSY_NAV,      /* toast: can't leave mid-transfer, cancel (B) first */
    S_INSTALL_PC_CHOOSE, /* Install-from-PC chooser dialog body */
    S_INSTALL_PC_WIRELESS, /* chooser option: LAN/Wi-Fi receiver */
    S_INSTALL_PC_USB,    /* chooser option: USB MTP drive */

    /* ---- an emulator .nro updated in place from the app utility ---- */
    S_EMU_UPD_CONFIRM,   /* dialog body: overwrite <app> with v%s? (app, ver) */
    S_EMU_UPD_DONE,      /* toast: <app> updated to v%s */
    S_EMU_UPD_MISSING,   /* toast: the target app wasn't found on the SD card */
    S_TITLE_INSTALL,     /* dialog title: Install app */
    S_EMU_INSTALL_CONFIRM, /* dialog body: install <app> v%s now? (app, ver) */
    S_EMU_INSTALL_DONE,  /* toast: <app> v%s installed */

    /* ---- Tools: on-device emulator / app update manager ---- */
    S_APPMAN_MENU,        /* Tools slide-out row: shortcut to Settings -> Updates */
    S_UPDSRC_PUSHED,      /* toast: desktop pushed the shared update manifest */
    S_APPMAN_EMUS,        /* Updates row + section title: Emulators */
    S_APPMAN_APPS,        /* Updates row + section title: Apps */
    S_APPMAN_EMPTY,       /* section empty-state (no entries of this kind) */
    S_APPMAN_LIST_HINT,   /* section list footer hint */
    S_APPMAN_INSTALLED,   /* list status: installed (version unknown) */
    S_APPMAN_NOT_INSTALLED, /* list status: not installed */
    S_APPMAN_NO_SOURCE,   /* list status suffix: no update source set */
    S_APPMAN_SRC_UNREACHABLE, /* list status: source set but releases unreachable */
    S_APPMAN_RATE_LIMITED,    /* list status: GitHub rate limit hit */
    S_APPMAN_OFFLINE,         /* list status: no network / can't reach GitHub */
    S_APPMAN_RATE_LIMITED_MSG,/* dialog: rate limited, set a token / wait */
    S_APPMAN_OFFLINE_MSG,     /* dialog: no connection to GitHub */
    S_APPMAN_CHECK_CANCEL,    /* checking subtitle hint: B to cancel */
    S_APPMAN_NEEDS_SOURCE,/* dialog body: %s has no source; set one? */
    S_APPMAN_SET_SOURCE,  /* button: set the GitHub update source */
    S_APPMAN_CHANGE_SOURCE, /* entry action: change the update source */
    S_APPMAN_REPO_GUIDE,  /* swkbd guide: GitHub repo owner/name */
    S_APPMAN_SOURCE_SAVED,/* toast: update source saved */
    S_APPMAN_CHECKING,    /* toast: checking for the latest release */
    S_APPMAN_CHECK_FAIL,  /* dialog: couldn't reach GitHub for this app */
    S_APPMAN_UPDATE_AVAIL,/* body: installed %s, update to %s available */
    S_APPMAN_UPDATE_TO,   /* button: update to %s */
    S_APPMAN_UPDATE_ROW,  /* list row pill when an update is available: installed version -> new version */
    S_APPMAN_UP_TO_DATE,  /* body: up to date (%s) */
    S_APPMAN_REINSTALL,   /* button: reinstall %s */
    S_APPMAN_NOT_INST_LATEST, /* body: not installed, latest is %s */
    S_APPMAN_INSTALL_V,   /* button: install %s */
    S_APPMAN_REVERT,      /* entry action: revert to a backup */
    S_APPMAN_REVERT_PICK, /* revert list body: choose a build to restore */
    S_APPMAN_NO_BACKUPS,  /* dialog: no backups stored yet */
    S_APPMAN_REVERT_NOTINST, /* dialog: not installed, nothing to replace */
    S_APPMAN_REVERTED,    /* toast: %s reverted */
    S_APPMAN_PUSH_PC,     /* Updates row: push the sources/list to the PC */
    S_PCSYNC_PUSH_LIST,   /* PC Sync row: push emulator+app list (with repo) to PC */
    S_APPMAN_PC_CONNECTED,/* right cell: a companion is connected */
    S_APPMAN_PC_OFFLINE,  /* right cell: no companion connected */
    S_APPMAN_PUSH_SENT,   /* toast: the companion will pick up the pushed list */
    S_APPMAN_PUSH_NOCONN, /* dialog: connect a companion (Wi-Fi or USB) first */
    S_APPMAN_UNCHECKED,   /* list status: installed+sourced, not yet checked */
    S_APPMAN_CHECK_UPDATES, /* entry menu action: check GitHub for a newer release */
    S_APPMAN_SOURCE_LINE, /* entry menu body: "Source: owner/repo" under the name */
    S_APPMAN_PATH_LINE,   /* entry menu body: "Path: sdmc:/switch/..." under the source line, only when installed */
    S_APPMAN_INSTALLED_LINE, /* entry menu body: "Installed: vX" under the path line, only when installed + version known */
    S_APPMAN_LOADING,     /* spinner text while the list builds (no network) */
    S_APPMAN_NEVER_CHECKED, /* last-checked suffix when never checked this build */
    S_APPMAN_JUST_NOW,    /* last-checked suffix: within the last minute */
    S_APPMAN_MIN_AGO,     /* last-checked suffix: "%dm ago" */
    S_APPMAN_HR_AGO,      /* last-checked suffix: "%dh ago" */
    S_APPMAN_DAY_AGO,     /* last-checked suffix: "%dd ago" */
    S_APPMAN_SCAN_ALL,    /* footer/menu: check every entry for updates (X) */
    S_APPMAN_SCAN_ONE,    /* footer: check the selected entry (Y) */

    /* ---- accent color (Appearance) ---- */
    S_ACCENT,             /* Appearance row label: "Accent Color" */
    S_TITLE_ACCENT,       /* Accent picker screen title */
    S_SUB_ACCENT,         /* Accent picker screen subtitle (footer hints) */
    S_ACCENT_SIGNATURE,   /* preset name: the default green/blue */
    S_ACCENT_VIOLET,      /* preset name: purple/pink */
    S_ACCENT_EMBER,       /* preset name: amber/red */
    S_ACCENT_AQUA,        /* preset name: cyan/teal */
    S_ACCENT_ROSE,        /* preset name: pink/red */
    S_ACCENT_SLATE,       /* preset name: neutral silver/blue-grey */

    /* ---- queue: recovering from a stalled connection ---- */
    S_RECONNECTING,       /* Queue card: shown in place of the speed/ETA line
                              while a download is stalled and about to retry */

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
