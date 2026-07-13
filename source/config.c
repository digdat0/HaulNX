#include "config.h"
#include "jsonutil.h"
#include "fsutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Bounded string copy. A plain byte loop (not snprintf) so the compiler doesn't
 * warn about member-to-member copies within the same struct. */
static void sset(char *dst, size_t dsz, const char *src) {
    if (dsz == 0) {
        return;
    }
    size_t i = 0;
    if (src) {
        for (; src[i] && i + 1 < dsz; i++) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

/* Add a name to the supported-console list if absent and there's room. */
static void add_supported(SourcesConfig *cfg, const char *name) {
    if (!name || !name[0] || cfg->supported_count >= MAX_CONSOLES) {
        return;
    }
    for (int i = 0; i < cfg->supported_count; i++) {
        if (strcmp(cfg->supported[i], name) == 0) {
            return;
        }
    }
    sset(cfg->supported[cfg->supported_count], 64, name);
    cfg->supported_count++;
}

/* ---- sources ---------------------------------------------------------- */

static void seed_from_romfs(void) {
    if (fs_exists(SOURCES_PATH)) {
        return;
    }
    size_t len = 0;
    char *def = json_read_file("romfs:/dl_sources.json", &len);
    if (!def) {
        return;
    }
    fs_mkdir_p(CONFIG_DIR);
    FILE *f = fopen(SOURCES_PATH, "wb");
    if (f) {
        fwrite(def, 1, len, f);
        fclose(f);
    }
    free(def);
}

void repo_set_url_default(Repo *r) {
    if (!r->download_base[0] && r->id[0]) {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "https://archive.org/download/%s", r->id);
        snprintf(r->download_base, sizeof(r->download_base), "%s", tmp);
    }
}

ConsoleGroup *config_find_console(SourcesConfig *cfg, const char *name) {
    for (int i = 0; i < cfg->console_count; i++) {
        if (strcmp(cfg->consoles[i].console, name) == 0 ||
            strcmp(cfg->consoles[i].target, name) == 0) {
            return &cfg->consoles[i];
        }
    }
    return NULL;
}

ConsoleGroup *config_add_console(SourcesConfig *cfg, const char *name) {
    if (!name || !name[0]) {
        return NULL;
    }
    ConsoleGroup *g = config_find_console(cfg, name);
    if (g) {
        return g;
    }
    if (cfg->console_count >= MAX_CONSOLES) {
        return NULL;
    }
    g = &cfg->consoles[cfg->console_count++];
    memset(g, 0, sizeof(*g));
    sset(g->console, sizeof(g->console), name);
    sset(g->target, sizeof(g->target), name);
    g->shown = true;
    return g;
}

bool config_remove_console(SourcesConfig *cfg, int idx) {
    if (idx < 0 || idx >= cfg->console_count) {
        return false;
    }
    for (int i = idx; i < cfg->console_count - 1; i++) {
        cfg->consoles[i] = cfg->consoles[i + 1];
    }
    cfg->console_count--;
    return true;
}

static int cmp_console(const void *a, const void *b) {
    const ConsoleGroup *x = (const ConsoleGroup *)a;
    const ConsoleGroup *y = (const ConsoleGroup *)b;
    return strcasecmp(x->console, y->console);
}

void config_sort(SourcesConfig *cfg) {
    qsort(cfg->consoles, cfg->console_count, sizeof(ConsoleGroup), cmp_console);
}

Repo *config_add_repo(ConsoleGroup *g, const char *label, const char *id) {
    if (!g || g->repo_count >= MAX_REPOS) {
        return NULL;
    }
    Repo *r = &g->repos[g->repo_count++];
    memset(r, 0, sizeof(*r));
    sset(r->label, sizeof(r->label), (label && label[0]) ? label : id);
    sset(r->id, sizeof(r->id), id);
    r->enabled = true;
    repo_set_url_default(r);
    return r;
}

bool config_remove_repo(ConsoleGroup *g, int idx) {
    if (!g || idx < 0 || idx >= g->repo_count) {
        return false;
    }
    for (int i = idx; i < g->repo_count - 1; i++) {
        g->repos[i] = g->repos[i + 1];
    }
    g->repo_count--;
    return true;
}

/* Parse one repo object (token `obj`) into *r. Returns true if it has an id. */
static bool parse_repo(const char *js, jsmntok_t *tok, int obj, Repo *r) {
    memset(r, 0, sizeof(*r));
    json_copy(js, tok, json_obj_get(js, tok, obj, "label"), r->label,
              sizeof(r->label));
    json_copy(js, tok, json_obj_get(js, tok, obj, "a_id"), r->id,
              sizeof(r->id));
    json_copy(js, tok, json_obj_get(js, tok, obj, "URL"), r->download_base,
              sizeof(r->download_base));
    r->enabled = json_bool(js, tok, json_obj_get(js, tok, obj, "active"));
    int pi = json_obj_get(js, tok, obj, "pinned");
    r->pinned = (pi >= 0) ? json_bool(js, tok, pi) : false;
    if (!r->label[0]) {
        sset(r->label, sizeof(r->label), r->id);
    }
    repo_set_url_default(r);
    return r->id[0] != '\0';
}

/* New grouped schema: { "console_list_groups": [ {console,target,repos:[...]} ] } */
static bool load_grouped(const char *js, jsmntok_t *tok, SourcesConfig *cfg) {
    int gi = json_obj_get(js, tok, 0, "console_list_groups");
    if (gi < 0 || tok[gi].type != JSMN_ARRAY) {
        return false;
    }
    int count = tok[gi].size;
    int child = gi + 1;
    for (int i = 0; i < count && cfg->console_count < MAX_CONSOLES; i++) {
        if (tok[child].type == JSMN_OBJECT) {
            ConsoleGroup *g = &cfg->consoles[cfg->console_count];
            memset(g, 0, sizeof(*g));
            json_copy(js, tok, json_obj_get(js, tok, child, "console"),
                      g->console, sizeof(g->console));
            json_copy(js, tok, json_obj_get(js, tok, child, "target"),
                      g->target, sizeof(g->target));
            if (!g->target[0]) {
                sset(g->target, sizeof(g->target), g->console);
            }
            if (!g->console[0]) {
                sset(g->console, sizeof(g->console), g->target);
            }
            /* Absent "shown" defaults to true so existing configs are unchanged. */
            int shtok = json_obj_get(js, tok, child, "shown");
            g->shown = (shtok < 0) ? true : json_bool(js, tok, shtok);
            int reps = json_obj_get(js, tok, child, "repos");
            if (reps >= 0 && tok[reps].type == JSMN_ARRAY) {
                int rc = tok[reps].size;
                int rchild = reps + 1;
                for (int r = 0; r < rc && g->repo_count < MAX_REPOS; r++) {
                    if (tok[rchild].type == JSMN_OBJECT &&
                        parse_repo(js, tok, rchild, &g->repos[g->repo_count])) {
                        g->repo_count++;
                    }
                    rchild = json_tok_skip(tok, rchild);
                }
            }
            if (g->console[0]) {
                cfg->console_count++;
            }
        }
        child = json_tok_skip(tok, child);
    }
    return cfg->console_count > 0;
}

/* Legacy schema: { "sources":[{console,a_id,dl_source,active,URL}],
 * "console_list":[...] } -> one console group per dl_source. */
static void load_legacy(const char *js, jsmntok_t *tok, SourcesConfig *cfg) {
    int targets = json_obj_get(js, tok, 0, "console_list");
    if (targets >= 0 && tok[targets].type == JSMN_ARRAY) {
        int n = tok[targets].size, c = targets + 1;
        for (int i = 0; i < n; i++) {
            if (tok[c].type == JSMN_STRING) {
                char name[64];
                json_copy(js, tok, c, name, sizeof(name));
                if (name[0]) {
                    config_add_console(cfg, name);
                }
            }
            c = json_tok_skip(tok, c);
        }
    }
    int sources = json_obj_get(js, tok, 0, "sources");
    if (sources >= 0 && tok[sources].type == JSMN_ARRAY) {
        int n = tok[sources].size, c = sources + 1;
        for (int i = 0; i < n; i++) {
            if (tok[c].type == JSMN_OBJECT) {
                char tgt[64], label[64];
                json_copy(js, tok, json_obj_get(js, tok, c, "dl_source"), tgt,
                          sizeof(tgt));
                json_copy(js, tok, json_obj_get(js, tok, c, "console"), label,
                          sizeof(label));
                if (tgt[0]) {
                    ConsoleGroup *g = config_add_console(cfg, tgt);
                    if (g && g->repo_count < MAX_REPOS) {
                        Repo *r = &g->repos[g->repo_count];
                        memset(r, 0, sizeof(*r));
                        sset(r->label, sizeof(r->label), label[0] ? label : tgt);
                        json_copy(js, tok, json_obj_get(js, tok, c, "a_id"),
                                  r->id, sizeof(r->id));
                        json_copy(js, tok, json_obj_get(js, tok, c, "URL"),
                                  r->download_base, sizeof(r->download_base));
                        r->enabled = json_bool(
                            js, tok, json_obj_get(js, tok, c, "active"));
                        repo_set_url_default(r);
                        if (r->id[0]) {
                            g->repo_count++;
                        }
                    }
                }
            }
            c = json_tok_skip(tok, c);
        }
    }
}

void config_load(SourcesConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    seed_from_romfs();

    size_t len = 0;
    char *js = json_read_file(SOURCES_PATH, &len);
    if (!js) {
        return;
    }
    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(js, len, &ntok);
    if (!tok || tok[0].type != JSMN_OBJECT) {
        free(tok);
        free(js);
        return;
    }

    /* Master supported-console list (TICO folders). */
    int ti = json_obj_get(js, tok, 0, "tico_consoles");
    if (ti >= 0 && tok[ti].type == JSMN_ARRAY) {
        int n = tok[ti].size, c = ti + 1;
        for (int i = 0; i < n; i++) {
            if (tok[c].type == JSMN_STRING) {
                char name[64];
                json_copy(js, tok, c, name, sizeof(name));
                add_supported(cfg, name);
            }
            c = json_tok_skip(tok, c);
        }
    }

    if (!load_grouped(js, tok, cfg)) {
        load_legacy(js, tok, cfg); /* older sources/console_list file */
    }

    /* Any console that already has a group is, by definition, supported. */
    for (int i = 0; i < cfg->console_count; i++) {
        add_supported(cfg, cfg->consoles[i].console);
    }

    free(tok);
    free(js);
}

bool config_save(const SourcesConfig *cfg) {
    fs_mkdir_p(CONFIG_DIR);
    FILE *f = fopen(SOURCES_PATH, "wb");
    if (!f) {
        return false;
    }
    fputs("{\n  \"console_list_groups\": [\n", f);
    for (int i = 0; i < cfg->console_count; i++) {
        const ConsoleGroup *g = &cfg->consoles[i];
        fputs("    {\n      \"console\": ", f);
        json_write_escaped(f, g->console);
        fputs(",\n      \"target\": ", f);
        json_write_escaped(f, g->target);
        fprintf(f, ",\n      \"shown\": %s", g->shown ? "true" : "false");
        fputs(",\n      \"repos\": [\n", f);
        for (int r = 0; r < g->repo_count; r++) {
            const Repo *rp = &g->repos[r];
            fputs("        { \"label\": ", f);
            json_write_escaped(f, rp->label);
            fputs(", \"a_id\": ", f);
            json_write_escaped(f, rp->id);
            fputs(", \"URL\": ", f);
            json_write_escaped(f, rp->download_base);
            fprintf(f, ", \"active\": %s, \"pinned\": %s }",
                    rp->enabled ? "true" : "false",
                    rp->pinned ? "true" : "false");
            fputs(r + 1 < g->repo_count ? ",\n" : "\n", f);
        }
        fputs("      ]\n    }", f);
        fputs(i + 1 < cfg->console_count ? ",\n" : "\n", f);
    }
    fputs("  ],\n  \"tico_consoles\": [", f);
    for (int i = 0; i < cfg->supported_count; i++) {
        json_write_escaped(f, cfg->supported[i]);
        if (i + 1 < cfg->supported_count) {
            fputs(", ", f);
        }
    }
    fputs("]\n}\n", f);
    fclose(f);
    return true;
}

/* ---- credentials ------------------------------------------------------ */

void creds_load(Credentials *c) {
    memset(c, 0, sizeof(*c));
    size_t len = 0;
    char *js = json_read_file(CREDS_PATH, &len);
    if (!js) {
        return;
    }
    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(js, len, &ntok);
    if (tok && tok[0].type == JSMN_OBJECT) {
        json_copy(js, tok, json_obj_get(js, tok, 0, "accessKey"), c->access_key,
                  sizeof(c->access_key));
        json_copy(js, tok, json_obj_get(js, tok, 0, "secret"), c->secret,
                  sizeof(c->secret));
    }
    free(tok);
    free(js);
}

bool creds_save(const Credentials *c) {
    fs_mkdir_p(CONFIG_DIR);
    FILE *f = fopen(CREDS_PATH, "wb");
    if (!f) {
        return false;
    }
    fputs("{\n  \"accessKey\": ", f);
    json_write_escaped(f, c->access_key);
    fputs(",\n  \"secret\": ", f);
    json_write_escaped(f, c->secret);
    fputs("\n}\n", f);
    fclose(f);
    return true;
}

/* ---- preferences ------------------------------------------------------ */

void prefs_load(Prefs *p) {
    p->use_cache = true;       /* defaults */
    p->prevent_sleep = true;
    p->group_consoles = true;
    p->max_downloads = 1;
    p->net_check = true;
    p->lang[0] = '\0';
    strcpy(p->theme, "dark");
    p->card_view = false;
    p->roms_override[0] = '\0';
    p->pinned_dir_count = 0;
    size_t len = 0;
    char *js = json_read_file(PREFS_PATH, &len);
    if (!js) {
        return;
    }
    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(js, len, &ntok);
    if (tok && tok[0].type == JSMN_OBJECT) {
        int idx = json_obj_get(js, tok, 0, "useCache");
        if (idx >= 0) {
            p->use_cache = json_bool(js, tok, idx);
        }
        idx = json_obj_get(js, tok, 0, "preventSleep");
        if (idx >= 0) {
            p->prevent_sleep = json_bool(js, tok, idx);
        }
        idx = json_obj_get(js, tok, 0, "groupConsoles");
        if (idx >= 0) {
            p->group_consoles = json_bool(js, tok, idx);
        }
        idx = json_obj_get(js, tok, 0, "maxDownloads");
        if (idx >= 0) {
            int v = (int)json_u64(js, tok, idx);
            if (v >= 1 && v <= 5) p->max_downloads = v;
        }
        idx = json_obj_get(js, tok, 0, "netCheck");
        if (idx >= 0) {
            p->net_check = json_bool(js, tok, idx);
        }
        idx = json_obj_get(js, tok, 0, "lang");
        if (idx >= 0 && tok[idx].type == JSMN_STRING) {
            json_copy(js, tok, idx, p->lang, sizeof(p->lang));
        }
        idx = json_obj_get(js, tok, 0, "theme");
        if (idx >= 0 && tok[idx].type == JSMN_STRING) {
            json_copy(js, tok, idx, p->theme, sizeof(p->theme));
        }
        idx = json_obj_get(js, tok, 0, "cardView");
        if (idx >= 0) {
            p->card_view = json_bool(js, tok, idx);
        }
        idx = json_obj_get(js, tok, 0, "romsOverride");
        if (idx >= 0 && tok[idx].type == JSMN_STRING) {
            json_copy(js, tok, idx, p->roms_override,
                      sizeof(p->roms_override));
        }
        idx = json_obj_get(js, tok, 0, "pinnedDirs");
        if (idx >= 0 && tok[idx].type == JSMN_ARRAY) {
            int n = tok[idx].size;
            int child = idx + 1;
            for (int i = 0; i < n && p->pinned_dir_count < MAX_PINNED_DIRS; i++) {
                if (tok[child].type == JSMN_STRING) {
                    json_copy(js, tok, child,
                              p->pinned_dirs[p->pinned_dir_count],
                              sizeof(p->pinned_dirs[0]));
                    if (p->pinned_dirs[p->pinned_dir_count][0]) {
                        p->pinned_dir_count++;
                    }
                }
                child = json_tok_skip(tok, child);
            }
        }
    }
    free(tok);
    free(js);
}

bool prefs_save(const Prefs *p) {
    fs_mkdir_p(CONFIG_DIR);
    FILE *f = fopen(PREFS_PATH, "wb");
    if (!f) {
        return false;
    }
    fprintf(f,
            "{\n  \"useCache\": %s,\n  \"preventSleep\": %s,\n"
            "  \"groupConsoles\": %s,\n  \"maxDownloads\": %d,\n"
            "  \"netCheck\": %s,\n"
            "  \"lang\": ",
            p->use_cache ? "true" : "false",
            p->prevent_sleep ? "true" : "false",
            p->group_consoles ? "true" : "false",
            p->max_downloads,
            p->net_check ? "true" : "false");
    json_write_escaped(f, p->lang);
    fputs(",\n  \"theme\": ", f);
    json_write_escaped(f, p->theme);
    fprintf(f, ",\n  \"cardView\": %s", p->card_view ? "true" : "false");
    fputs(",\n  \"romsOverride\": ", f);
    json_write_escaped(f, p->roms_override);
    fputs(",\n  \"pinnedDirs\": [", f);
    for (int i = 0; i < p->pinned_dir_count; i++) {
        if (i) {
            fputs(", ", f);
        }
        json_write_escaped(f, p->pinned_dirs[i]);
    }
    fputs("]\n}\n", f);
    fclose(f);
    return true;
}

bool prefs_dir_pinned(const Prefs *p, const char *name) {
    for (int i = 0; i < p->pinned_dir_count; i++) {
        if (strcasecmp(p->pinned_dirs[i], name) == 0) {
            return true;
        }
    }
    return false;
}

void prefs_dir_pin_toggle(Prefs *p, const char *name) {
    for (int i = 0; i < p->pinned_dir_count; i++) {
        if (strcasecmp(p->pinned_dirs[i], name) == 0) {
            /* unpin: shift the rest down */
            for (int j = i; j < p->pinned_dir_count - 1; j++) {
                memcpy(p->pinned_dirs[j], p->pinned_dirs[j + 1],
                       sizeof(p->pinned_dirs[0]));
            }
            p->pinned_dir_count--;
            return;
        }
    }
    if (p->pinned_dir_count < MAX_PINNED_DIRS) {
        snprintf(p->pinned_dirs[p->pinned_dir_count],
                 sizeof(p->pinned_dirs[0]), "%s", name);
        p->pinned_dir_count++;
    }
}

void creds_auth_header(const Credentials *c, char *out, size_t out_sz) {
    if (c->access_key[0] && c->secret[0]) {
        snprintf(out, out_sz, "authorization: LOW %s:%s", c->access_key,
                 c->secret);
    } else {
        out[0] = '\0';
    }
}

/* ---- tico detection --------------------------------------------------- */

/* Strip // line comments and block comments from JSONC so jsmn can parse it. */
static char *strip_jsonc_comments(const char *src, size_t len) {
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    size_t j = 0;
    bool in_string = false;
    for (size_t i = 0; i < len; i++) {
        if (in_string) {
            out[j++] = src[i];
            if (src[i] == '\\' && i + 1 < len) {
                out[j++] = src[++i];
            } else if (src[i] == '"') {
                in_string = false;
            }
            continue;
        }
        if (src[i] == '"') {
            in_string = true;
            out[j++] = src[i];
        } else if (src[i] == '/' && i + 1 < len && src[i + 1] == '/') {
            while (i < len && src[i] != '\n') i++;
        } else if (src[i] == '/' && i + 1 < len && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/')) i++;
            if (i + 1 < len) i++;
        } else {
            out[j++] = src[i];
        }
    }
    out[j] = '\0';
    return out;
}

static bool tico_is_installed(void) {
    if (fs_exists("sdmc:/switch/tico.nro")) return true;
    if (fs_exists("sdmc:/switch/tico/tico.nro")) return true;
    if (fs_exists("sdmc:/tico")) return true;
    return false;
}

/* Remove trailing slash(es) from a path. */
static void trim_trailing_slash(char *p) {
    size_t n = strlen(p);
    while (n > 0 && (p[n - 1] == '/' || p[n - 1] == '\\')) {
        p[--n] = '\0';
    }
}

void tico_init(TicoState *ts) {
    memset(ts, 0, sizeof(*ts));
    ts->installed = tico_is_installed();
    sset(ts->roms_path, sizeof(ts->roms_path), TICO_DEFAULT_ROMS);

    /* Try to read Tico's config for a custom roms_path. */
    size_t len = 0;
    char *raw = json_read_file(TICO_CONFIG_PATH, &len);
    if (!raw) return;

    char *js = strip_jsonc_comments(raw, len);
    free(raw);
    if (!js) return;

    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(js, strlen(js), &ntok);
    if (tok && tok[0].type == JSMN_OBJECT) {
        int ri = json_obj_get(js, tok, 0, "roms_path");
        if (ri >= 0 && tok[ri].type == JSMN_STRING &&
            tok[ri].end > tok[ri].start) {
            char tmp[512];
            json_copy(js, tok, ri, tmp, sizeof(tmp));
            if (tmp[0]) {
                sset(ts->roms_path, sizeof(ts->roms_path), tmp);
                trim_trailing_slash(ts->roms_path);
            }
        }
    }
    free(tok);
    free(js);
}

const char *roms_root(const TicoState *ts) {
    return ts->roms_path;
}

void roms_normalize_path(const char *in, char *out, size_t out_sz) {
    if (out_sz == 0) {
        return;
    }
    out[0] = '\0';
    if (!in) {
        return;
    }
    /* Skip leading whitespace. */
    while (*in == ' ' || *in == '\t') {
        in++;
    }
    if (!*in) {
        return; /* blank -> auto */
    }
    if (strncasecmp(in, "sdmc:", 5) == 0) {
        sset(out, out_sz, in);
    } else {
        /* Strip leading slashes so we don't produce "sdmc://...". */
        while (*in == '/' || *in == '\\') {
            in++;
        }
        snprintf(out, out_sz, "sdmc:/%s", in);
    }
    trim_trailing_slash(out);
}

void tico_set_roms_override(TicoState *ts, const char *path) {
    if (!path || !path[0]) {
        return;
    }
    char norm[512];
    roms_normalize_path(path, norm, sizeof(norm));
    if (norm[0]) {
        sset(ts->roms_path, sizeof(ts->roms_path), norm);
    }
}
