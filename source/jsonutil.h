#ifndef JSONUTIL_H
#define JSONUTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "jsmn.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parse `js` and return a malloc'd token array (caller frees). *ntok gets the
 * token count. Returns NULL on parse failure. */
jsmntok_t *json_parse_alloc(const char *js, size_t len, int *ntok);

/* True if token is a string equal to s. */
bool json_tok_eq(const char *js, const jsmntok_t *t, const char *s);

/* Index just past the subtree rooted at i (for skipping values). */
int json_tok_skip(const jsmntok_t *t, int i);

/* Value-token index for key in the object token at `obj`, or -1. */
int json_obj_get(const char *js, const jsmntok_t *t, int obj, const char *key);

/* Copy a token's raw text into out (NUL-terminated, truncated to fit). */
void json_copy(const char *js, const jsmntok_t *t, int idx, char *out,
               size_t out_sz);

uint64_t json_u64(const char *js, const jsmntok_t *t, int idx);
bool json_bool(const char *js, const jsmntok_t *t, int idx);

/* Read a whole file into a malloc'd, NUL-terminated buffer. *out_len optional. */
char *json_read_file(const char *path, size_t *out_len);

/* Write a JSON string value (with surrounding quotes + escaping) to fp. */
void json_write_escaped(FILE *fp, const char *s);

#ifdef __cplusplus
}
#endif

#endif /* JSONUTIL_H */
