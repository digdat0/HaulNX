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

/* Compare dotted versions ("1.2.3", optionally "v"-prefixed).
 * Returns <0 if a<b, 0 if equal, >0 if a>b. */
int version_cmp(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* UPDATE_H */
