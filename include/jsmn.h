/* jsmn — minimal JSON tokenizer (MIT, by Serge Zaitsev). Vendored. */
#ifndef JSMN_H
#define JSMN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JSMN_UNDEFINED = 0,
    JSMN_OBJECT    = 1 << 0,
    JSMN_ARRAY     = 1 << 1,
    JSMN_STRING    = 1 << 2,
    JSMN_PRIMITIVE = 1 << 3
} jsmntype_t;

enum jsmnerr {
    JSMN_ERROR_NOMEM = -1, /* not enough tokens */
    JSMN_ERROR_INVAL = -2, /* invalid character */
    JSMN_ERROR_PART  = -3  /* incomplete JSON */
};

typedef struct jsmntok {
    jsmntype_t type;
    int start;
    int end;
    int size;
} jsmntok_t;

typedef struct jsmn_parser {
    unsigned int pos;     /* offset in the JSON string */
    unsigned int toknext; /* next token to allocate */
    int toksuper;         /* superior token node */
} jsmn_parser;

void jsmn_init(jsmn_parser *parser);
int jsmn_parse(jsmn_parser *parser, const char *js, const size_t len,
               jsmntok_t *tokens, const unsigned int num_tokens);

#ifdef __cplusplus
}
#endif

#endif /* JSMN_H */
