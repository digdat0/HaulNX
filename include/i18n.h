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
