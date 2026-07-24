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

/* Copy a token's text into out (NUL-terminated, truncated to fit). String
 * tokens have their JSON escapes (\n, \", \\, \uXXXX, ...) decoded to UTF-8. */
void json_copy(const char *js, const jsmntok_t *t, int idx, char *out,
               size_t out_sz);

/* Decode `len` bytes of raw JSON string content (escapes included) into out
 * as UTF-8. Returns the decoded length. Output never exceeds the input. */
size_t json_unescape(const char *src, size_t len, char *out, size_t out_sz);

uint64_t json_u64(const char *js, const jsmntok_t *t, int idx);
bool json_bool(const char *js, const jsmntok_t *t, int idx);

/* Largest byte count worth believing from a JSON field. Declared sizes reach us
 * from archive.org metadata and from dl_sources.json — which the Wi-Fi receiver
 * accepts off the LAN — so neither is ours to trust, and a value near UINT64_MAX
 * turns every "free space vs. size + margin" test into a wraparound that reads
 * as "plenty of room". No file on a FAT32 card can pass 4 GiB; 1 TiB leaves
 * exFAT room to spare while staying far below where summing a full queue wraps. */
#define JSON_SIZE_MAX (1024ull * 1024 * 1024 * 1024)

/* json_u64 clamped to JSON_SIZE_MAX. Use this, not json_u64, for any byte count
 * that came from outside the app. Clamping rather than rejecting keeps an absurd
 * value from starting a download while still letting the rest of the entry
 * parse: nothing that big is real, so the download fails on space either way. */
uint64_t json_u64_size(const char *js, const jsmntok_t *t, int idx);

/* Ceiling on a JSON file this app will read into memory. Every producer is
 * already bounded well below it — a received dl_sources.json by the receiver's
 * 16 MB body cap, a cached metadata blob by HTTP_GET_MAX — so this only exists
 * so the shared reader can't be handed an arbitrary allocation by a file that
 * arrived some other way (sideloaded onto the card, or a truncated write that
 * left a nonsense length). Heap here is a few tens of MB, so failing is far
 * better than a malloc that takes the app down with it. */
#define JSON_FILE_MAX (32ull * 1024 * 1024)

/* Read a whole file into a malloc'd, NUL-terminated buffer. *out_len optional.
 * Returns NULL if the file is missing, unreadable, or larger than
 * JSON_FILE_MAX. */
char *json_read_file(const char *path, size_t *out_len);

/* Write a JSON string value (with surrounding quotes + escaping) to fp. */
void json_write_escaped(FILE *fp, const char *s);

#ifdef __cplusplus
}
#endif

#endif /* JSONUTIL_H */
