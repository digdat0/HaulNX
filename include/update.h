#ifndef UPDATE_H
#define UPDATE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Query https://api.github.com/repos/<repo>/releases/latest and return the tag
 * name and the download URL of the first ".nro" asset. Returns true on success.
 * Retries transient failures (3 attempts); if `attempt` is non-NULL it is set
 * to the current attempt number (1-based) so a UI can display progress.
 */
bool update_fetch_latest(const char *repo, char *tag, size_t tag_sz, char *url,
                         size_t url_sz, volatile int *attempt);

/*
 * Same as update_fetch_latest, but when a release ships several .nro assets,
 * prefer the one whose (lowercased) name contains `asset_hint`. A NULL/empty
 * hint falls back to the first .nro (identical to update_fetch_latest). The
 * chosen asset's file name is returned in `asset` when non-NULL, so a caller can
 * name the download / install destination.
 *
 * When `last_code` is non-NULL it receives the final HTTP status observed: 0 if
 * the request never reached GitHub (no network / transport failure), 403 or 429
 * on a rate limit, 200 on success. Lets a caller tell "offline" from "throttled".
 */
bool update_fetch_latest_asset(const char *repo, const char *asset_hint,
                               char *tag, size_t tag_sz, char *url,
                               size_t url_sz, char *asset, size_t asset_sz,
                               volatile int *attempt, long *last_code);

/* Compare dotted versions ("1.2.3", optionally "v"-prefixed).
 * Returns <0 if a<b, 0 if equal, >0 if a>b. */
int version_cmp(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* UPDATE_H */
