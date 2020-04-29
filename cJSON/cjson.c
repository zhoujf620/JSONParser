#include "cjson.h"
#include <assert.h>  /* assert() */
#include <stdlib.h>  /* NULL */

#define EXPECT(c, ch) \
    do { \
        assert(*c->json == (ch)); \
        c->json++; \
    } while(0)

typedef struct {
    const char* json;
}cjson_context;

static void cjson_parse_whitespace(cjson_context* c) {
    const char *p = c->json;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    c->json = p;
}

static int cjson_parse_true(cjson_context* c, cjson_value* v) {
    EXPECT(c, 't');
    if (c->json[0] != 'r' || c->json[1] != 'u' || c->json[2] != 'e')
        return CJSON_PARSE_INVALID_VALUE;
    c->json += 3;
    v->type = CJSON_TRUE;
    return CJSON_PARSE_OK;
}

static int cjson_parse_false(cjson_context* c, cjson_value* v) {
    EXPECT(c, 'f');
    if (c->json[0] != 'a' || c->json[1] != 'l' || c->json[2] != 's' || c->json[3] != 'e')
        return CJSON_PARSE_INVALID_VALUE;
    c->json += 4;
    v->type = CJSON_FALSE;
    return CJSON_PARSE_OK;
}

static int cjson_parse_null(cjson_context* c, cjson_value* v) {
    EXPECT(c, 'n');
    if (c->json[0] != 'u' || c->json[1] != 'l' || c->json[2] != 'l')
        return CJSON_PARSE_INVALID_VALUE;
    c->json += 3;
    v->type = CJSON_NULL;
    return CJSON_PARSE_OK;
}

static int cjson_parse_value(cjson_context* c, cjson_value* v) {
    switch (*c->json) {
        case 't': return cjson_parse_true(c, v);
        case 'f': return cjson_parse_false(c, v);
        case 'n':  return cjson_parse_null(c, v);
        case '\0': return CJSON_PARSE_EXPECT_VALUE;
        default:   return CJSON_PARSE_INVALID_VALUE;
    }
}

int cjson_parse(cjson_value* v, const char* json) {
    cjson_context c;
    int ret;
    assert(v != NULL);
    c.json = json;
    v->type = CJSON_NULL;
    cjson_parse_whitespace(&c);
    if ((ret = cjson_parse_value(&c, v)) == CJSON_PARSE_OK) {
        cjson_parse_whitespace(&c);
        if (*c.json != '\0')
            ret = CJSON_PARSE_ROOT_NOT_SINGULAR;
    }
    return ret;
}

cjson_type cjson_get_type(const cjson_value* v) {
    assert(v != NULL);
    return v->type;
}
