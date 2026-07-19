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
    S_SHOWN,
    S_HIDDEN,

    /* ---- credentials ---- */
    S_TITLE_CREDS,
    S_SUB_CREDS,
    S_CLEAR_CREDS,
    S_CLEAR_CREDS_CONFIRM,
    S_ACCESS_KEY,
    S_SECRET_KEY,

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

    /* ---- tico detection ---- */
    S_TICO_NOT_FOUND,
    S_TICO_NOT_FOUND_MSG,
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
    S_IMPORT_REPO_NOTE,  /* accent chip: push straight from the repo editor */
    S_IMPORT_NO_NET,
    S_IMPORT_SRV_FAIL,
    S_IMPORT_BAD_FILE,
    S_IMPORT_CONFIRM,
    S_IMPORT_DONE,
    S_IMPORT_SAVE_FAIL,
    S_XFER_LOG,
    S_TITLE_XFER_LOG,
    S_CLEAR_XFER_CONFIRM,

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
