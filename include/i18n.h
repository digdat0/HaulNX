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
    S_CONTROLS_HELP,
    S_CREDITS,
    S_ROM_FOLDER,

    /* ---- advanced ---- */
    S_TITLE_ADVANCED,
    S_SUB_ADVANCED,
    S_STAY_AWAKE,
    S_GROUP_CONSOLES,
    S_ARCHIVE_CREDS,
    S_META_CACHE,
    S_MAX_DOWNLOADS,
    S_ON,
    S_OFF,
    S_SET,
    S_UNSET,

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
    S_DIR_PREFIX,
    S_DELETE_SELECTED,

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
