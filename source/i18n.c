#include "i18n.h"
#include "jsonutil.h"
#include "jsmn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- built-in English strings ------------------------------------------ */

static const char *const g_en[S__COUNT] = {
    /* S_OK */                  "OK",
    /* S_YES */                 "Yes",
    /* S_CANCEL */              "Cancel",
    /* S_DELETE */               "Delete",
    /* S_DELETED */              "Deleted",
    /* S_SAVED */                "Saved",
    /* S_ADDED */                "Added",
    /* S_CLEARED */              "Cleared",
    /* S_RENAMED */              "Renamed",
    /* S_RENAME_FAILED */        "Rename failed",
    /* S_EMPTY */                "(empty)",
    /* S_BACK */                 "back",
    /* S_EXIT */                 "Exit",
    /* S_FILE */                 "File",
    /* S_LOADING */              "Loading...",

    /* S_TAB_BROWSE */           "Browse",
    /* S_TAB_INSTALLED */        "Installed",
    /* S_TAB_QUEUE */            "Queue",
    /* S_TAB_SETTINGS */         "Settings",

    /* S_TITLE_CONSOLES */       "Consoles",
    /* S_TITLE_REPOS */          "Repos",
    /* S_SUB_HOME_GROUPED */     "A open  Y add repo  L/R tabs  ZL/ZR page",
    /* S_SUB_HOME_FLAT */        "A open  Y add repo  X edit  - delete  L/R tabs  ZL/ZR page",
    /* S_SUB_REPOS */            "A browse  X edit  Y add repo  - delete  L/R tabs  B back",
    /* S_NO_COLLECTIONS */       "(no collections - press Y to add)",
    /* S_NO_REPOS */             "(no repos - press Y to add)",

    /* S_SUB_FILES */            "A get  - all  Y filter  X refresh  Dpad< >repo  L/R tabs  B back",
    /* S_NO_FILES_MATCH */       "(no files match)",
    /* S_META_FAILED */          "(metadata fetch failed - B back)",
    /* S_LOADING_META */         "Loading metadata ...",
    /* S_QUEUED */                "Queued",
    /* S_QUEUE_FULL */           "Queue is full",
    /* S_DOWNLOAD_ALL */         "Download all",
    /* S_FREE_SPACE_WARN */      "Warning: total size exceeds free space.",
    /* S_QUEUED_N */             "Queued %d file(s)",
    /* S_ALREADY_HAVE */         "Already have %d — queue %d new (%s)?",

    /* S_TITLE_QUEUE */          "Download Queue",
    /* S_SUB_QUEUE */            "A cancel  X retry  ZL/ZR move  Y clear  - log  B back",
    /* S_QUEUE_EMPTY */          "(queue empty)",
    /* S_CANCELLED */            "Cancelled",
    /* S_RETRYING */             "Retrying",
    /* S_TOAST_DONE */           "Done: ",
    /* S_TOAST_SAVED */          "Saved: ",
    /* S_TOAST_FAILED */         "Failed: ",
    /* S_CLEARED_FINISHED */     "Cleared",

    /* S_TITLE_SETTINGS */       "Settings",
    /* S_SUB_SETTINGS */         "A select  L/R tabs  ZL/ZR page",
    /* S_CHECK_UPDATES */        "Check for updates",
    /* S_VIEW_LOG */             "View download log",
    /* S_MANAGE_CONSOLES */      "Manage consoles (show/hide)",
    /* S_MANAGE_DOWNLOADS */     "Manage downloads folder",
    /* S_ADVANCED */             "Advanced",
    /* S_CONTROLS_HELP */        "Controls / Help",
    /* S_CREDITS */              "Credits",
    /* S_ROM_FOLDER */           "ROM folder: %s",

    /* S_TITLE_ADVANCED */       "Advanced",
    /* S_SUB_ADVANCED */         "A toggle/edit  B back",
    /* S_STAY_AWAKE */           "Stay awake while downloading: %s",
    /* S_GROUP_CONSOLES */       "Group consoles: %s",
    /* S_ARCHIVE_CREDS */        "Archive.org credentials: %s",
    /* S_META_CACHE */           "Metadata cache: %s",
    /* S_MAX_DOWNLOADS */        "Max simultaneous downloads: %d",
    /* S_ON */                   "ON",
    /* S_OFF */                  "OFF",
    /* S_SET */                  "set",
    /* S_UNSET */                "unset",

    /* S_TITLE_DOWNLOADS */      "Downloads folder",
    /* S_SUB_DOWNLOADS */        "Y select  - delete  A delete all  B back",
    /* S_DELETE_ALL */           "Delete all",
    /* S_DELETE_ALL_CONFIRM */   "Delete ALL %d file(s) in the downloads folder?",
    /* S_DL_ACTIVE_WARN */      "\n\n%d download(s) are active and will be cancelled.",
    /* S_DL_QUEUE_WARN */       "\n\nSome are in the download queue and will be cancelled.",
    /* S_DL_CLEARED */          "Downloads cleared",

    /* S_TITLE_INSTALLED */      "Installed",
    /* S_SUB_INSTALLED */        "A open  Y select  X rename  - delete  L/R tabs  B back",
    /* S_DIR_PREFIX */           "[DIR] ",
    /* S_DELETE_SELECTED */      "Delete %d selected item(s)?",

    /* S_TITLE_EDIT_REPO */      "Edit repo",
    /* S_SUB_EDIT_REPO */        "A edit/toggle  B back",
    /* S_DELETE_REPO */          "Delete this repo",
    /* S_DELETE_REPO_CONFIRM */  "Delete this repo?",

    /* S_TITLE_SELECT_CONSOLE */ "Select console",
    /* S_SUB_SELECT_CONSOLE */   "A select  B cancel",
    /* S_NO_CONSOLES */          "(no supported consoles)",

    /* S_TITLE_LOG */            "Download Log",
    /* S_SUB_LOG */              "X clear log  B back",
    /* S_NO_LOG */               "(no downloads logged yet)",
    /* S_CLEAR_LOG */            "Clear log",
    /* S_CLEAR_LOG_CONFIRM */    "Clear all download history?",
    /* S_LOG_CLEARED */          "Log cleared",

    /* S_TITLE_MANAGE */         "Manage consoles",
    /* S_SUB_MANAGE */           "A show/hide  L/R tabs  B back",
    /* S_SHOWN */                "shown",
    /* S_HIDDEN */               "hidden",

    /* S_TITLE_CREDS */          "Archive.org credentials",
    /* S_SUB_CREDS */            "A edit  B back",
    /* S_CLEAR_CREDS */          "Clear credentials",
    /* S_CLEAR_CREDS_CONFIRM */  "Remove the saved access key and secret?",
    /* S_ACCESS_KEY */           "Access Key",
    /* S_SECRET_KEY */           "Secret Key",

    /* S_TITLE_UPDATE */         "Update",
    /* S_UPDATE_FETCH_FAIL */    "Could not fetch release info.",
    /* S_UPDATE_UP_TO_DATE */    "You are up to date (v%s).",
    /* S_UPDATE_CONFIRM */       "Update to %s?  Replaces the app.",
    /* S_UPDATING */             "Updating",
    /* S_UPDATE_DOWNLOADING */   "Downloading %s: %d%%  (%s / %s)  -  please wait",
    /* S_UPDATE_DL_CANCEL */     "Downloading update - press B to cancel",
    /* S_UPDATE_START_FAIL */    "Could not start the downloader.",
    /* S_UPDATE_FAIL */          "Update download failed.",
    /* S_UPDATE_OK */            "Update installed — v%s.\n\nClose and relaunch TicoDL+.",

    /* S_TICO_NOT_FOUND */       "TICO not detected",
    /* S_TICO_NOT_FOUND_MSG */   "The TICO emulator was not found on this console.\n"
                                 "Downloads will go to the default folder:\n"
                                 "%s\n\nContinue without TICO?",
    /* S_CONTINUE */             "Continue",

    /* S_CONTROLS_BODY */        "Tabs: L / R  (Browse | Installed | Queue | Settings)\n"
                                 "Navigate: D-pad (hold to repeat)   ZL/ZR: page\n"
                                 "+: exit   B: back\n"
                                 "Browse: A open  Y add  X edit/del  - delete\n"
                                 "Files: A get  - all  Y filter  X refresh  Dpad L/R: repo\n"
                                 "Queue: A cancel  X retry  ZL/ZR move  Y clear  - log",

    /* S_EXIT_CONFIRM */         "%d download(s) active. Exit anyway?",

    /* S_FILTER_PROMPT */        "Filter",
    /* S_RENAME_PROMPT */        "Rename to",
};

/* ---- runtime override table -------------------------------------------- */

static char *g_override[S__COUNT];

const char *tr(int id) {
    if (id < 0 || id >= S__COUNT) return "";
    if (g_override[id]) return g_override[id];
    return g_en[id] ? g_en[id] : "";
}

/* ---- JSON language file loader ----------------------------------------- */

/* Key names in the JSON file match the enum names without the S_ prefix,
 * lowercased. For now this is a stub — the mapping table will be filled in
 * when the language selector is implemented. */

static const char *const g_key_names[S__COUNT] = {
    [S_OK] = "ok",
    [S_YES] = "yes",
    [S_CANCEL] = "cancel",
    [S_DELETE] = "delete",
    [S_DELETED] = "deleted",
    [S_SAVED] = "saved",
    [S_ADDED] = "added",
    [S_CLEARED] = "cleared",
    [S_RENAMED] = "renamed",
    [S_RENAME_FAILED] = "rename_failed",
    [S_EMPTY] = "empty",
    [S_BACK] = "back",
    [S_EXIT] = "exit",
    [S_FILE] = "file",
    [S_LOADING] = "loading",
    [S_TAB_BROWSE] = "tab_browse",
    [S_TAB_INSTALLED] = "tab_installed",
    [S_TAB_QUEUE] = "tab_queue",
    [S_TAB_SETTINGS] = "tab_settings",
    [S_TITLE_CONSOLES] = "title_consoles",
    [S_TITLE_REPOS] = "title_repos",
    [S_SUB_HOME_GROUPED] = "sub_home_grouped",
    [S_SUB_HOME_FLAT] = "sub_home_flat",
    [S_SUB_REPOS] = "sub_repos",
    [S_NO_COLLECTIONS] = "no_collections",
    [S_NO_REPOS] = "no_repos",
    [S_SUB_FILES] = "sub_files",
    [S_NO_FILES_MATCH] = "no_files_match",
    [S_META_FAILED] = "meta_failed",
    [S_LOADING_META] = "loading_meta",
    [S_QUEUED] = "queued",
    [S_QUEUE_FULL] = "queue_full",
    [S_DOWNLOAD_ALL] = "download_all",
    [S_FREE_SPACE_WARN] = "free_space_warn",
    [S_QUEUED_N] = "queued_n",
    [S_ALREADY_HAVE] = "already_have",
    [S_TITLE_QUEUE] = "title_queue",
    [S_SUB_QUEUE] = "sub_queue",
    [S_QUEUE_EMPTY] = "queue_empty",
    [S_CANCELLED] = "cancelled",
    [S_RETRYING] = "retrying",
    [S_TOAST_DONE] = "toast_done",
    [S_TOAST_SAVED] = "toast_saved",
    [S_TOAST_FAILED] = "toast_failed",
    [S_CLEARED_FINISHED] = "cleared_finished",
    [S_TITLE_SETTINGS] = "title_settings",
    [S_SUB_SETTINGS] = "sub_settings",
    [S_CHECK_UPDATES] = "check_updates",
    [S_VIEW_LOG] = "view_log",
    [S_MANAGE_CONSOLES] = "manage_consoles",
    [S_MANAGE_DOWNLOADS] = "manage_downloads",
    [S_ADVANCED] = "advanced",
    [S_CONTROLS_HELP] = "controls_help",
    [S_CREDITS] = "credits",
    [S_ROM_FOLDER] = "rom_folder",
    [S_TITLE_ADVANCED] = "title_advanced",
    [S_SUB_ADVANCED] = "sub_advanced",
    [S_STAY_AWAKE] = "stay_awake",
    [S_GROUP_CONSOLES] = "group_consoles",
    [S_ARCHIVE_CREDS] = "archive_creds",
    [S_META_CACHE] = "meta_cache",
    [S_MAX_DOWNLOADS] = "max_downloads",
    [S_ON] = "on",
    [S_OFF] = "off",
    [S_SET] = "set",
    [S_UNSET] = "unset",
    [S_TITLE_DOWNLOADS] = "title_downloads",
    [S_SUB_DOWNLOADS] = "sub_downloads",
    [S_DELETE_ALL] = "delete_all",
    [S_DELETE_ALL_CONFIRM] = "delete_all_confirm",
    [S_DL_ACTIVE_WARN] = "dl_active_warn",
    [S_DL_QUEUE_WARN] = "dl_queue_warn",
    [S_DL_CLEARED] = "dl_cleared",
    [S_TITLE_INSTALLED] = "title_installed",
    [S_SUB_INSTALLED] = "sub_installed",
    [S_DIR_PREFIX] = "dir_prefix",
    [S_DELETE_SELECTED] = "delete_selected",
    [S_TITLE_EDIT_REPO] = "title_edit_repo",
    [S_SUB_EDIT_REPO] = "sub_edit_repo",
    [S_DELETE_REPO] = "delete_repo",
    [S_DELETE_REPO_CONFIRM] = "delete_repo_confirm",
    [S_TITLE_SELECT_CONSOLE] = "title_select_console",
    [S_SUB_SELECT_CONSOLE] = "sub_select_console",
    [S_NO_CONSOLES] = "no_consoles",
    [S_TITLE_LOG] = "title_log",
    [S_SUB_LOG] = "sub_log",
    [S_NO_LOG] = "no_log",
    [S_CLEAR_LOG] = "clear_log",
    [S_CLEAR_LOG_CONFIRM] = "clear_log_confirm",
    [S_LOG_CLEARED] = "log_cleared",
    [S_TITLE_MANAGE] = "title_manage",
    [S_SUB_MANAGE] = "sub_manage",
    [S_SHOWN] = "shown",
    [S_HIDDEN] = "hidden",
    [S_TITLE_CREDS] = "title_creds",
    [S_SUB_CREDS] = "sub_creds",
    [S_CLEAR_CREDS] = "clear_creds",
    [S_CLEAR_CREDS_CONFIRM] = "clear_creds_confirm",
    [S_ACCESS_KEY] = "access_key",
    [S_SECRET_KEY] = "secret_key",
    [S_TITLE_UPDATE] = "title_update",
    [S_UPDATE_FETCH_FAIL] = "update_fetch_fail",
    [S_UPDATE_UP_TO_DATE] = "update_up_to_date",
    [S_UPDATE_CONFIRM] = "update_confirm",
    [S_UPDATING] = "updating",
    [S_UPDATE_DOWNLOADING] = "update_downloading",
    [S_UPDATE_DL_CANCEL] = "update_dl_cancel",
    [S_UPDATE_START_FAIL] = "update_start_fail",
    [S_UPDATE_FAIL] = "update_fail",
    [S_UPDATE_OK] = "update_ok",
    [S_TICO_NOT_FOUND] = "tico_not_found",
    [S_TICO_NOT_FOUND_MSG] = "tico_not_found_msg",
    [S_CONTINUE] = "continue",
    [S_CONTROLS_BODY] = "controls_body",
    [S_EXIT_CONFIRM] = "exit_confirm",
    [S_FILTER_PROMPT] = "filter_prompt",
    [S_RENAME_PROMPT] = "rename_prompt",
};

static void i18n_free_overrides(void) {
    for (int i = 0; i < S__COUNT; i++) {
        free(g_override[i]);
        g_override[i] = NULL;
    }
}

void i18n_load(const char *path) {
    i18n_free_overrides();
    if (!path) return;

    FILE *f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 256 * 1024) { fclose(f); return; }

    char *buf = (char *)malloc(len + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);

    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(buf, (int)len, &ntok);
    if (!tok || tok[0].type != JSMN_OBJECT) {
        free(tok);
        free(buf);
        return;
    }

    for (int i = 0; i < S__COUNT; i++) {
        if (!g_key_names[i]) continue;
        int idx = json_obj_get(buf, tok, 0, g_key_names[i]);
        if (idx < 0 || tok[idx].type != JSMN_STRING) continue;
        int slen = tok[idx].end - tok[idx].start;
        g_override[i] = (char *)malloc(slen + 1);
        if (g_override[i]) {
            memcpy(g_override[i], buf + tok[idx].start, slen);
            g_override[i][slen] = '\0';
        }
    }

    free(tok);
    free(buf);
}
