#include "cjson.h"
#include <assert.h>  /* assert() */
#include <errno.h>   /* errno, ERANGE */
#include <math.h>    /* HUGE_VAL */
#include <stdlib.h>  /* NULL, strtod() */

typedef struct {
    const char* json;
}cjson_context;

static void cjson_parse_whitespace(cjson_context* c) {
    const char *p = c->json;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    c->json = p;
}

static int cjson_parse_literal(cjson_context* c, cjson_value* v, const char* literal, cjson_type type) {
    size_t i;
    for (i = 0; literal[i]; ++i)
        if (c->json[i] != literal[i])
            return CJSON_PARSE_INVALID_VALUE;
    c->json += i;
    v->type = type;
    return CJSON_PARSE_OK;
}

static int cjson_parse_number(cjson_context* c, cjson_value* v) {
    const char* p = c->json;
    if (*p == '-') p++;
    if (*p == '0') p++;
    else {
        if (!(*p >= '1' && *p <= '9')) return CJSON_PARSE_INVALID_VALUE;
        while (*p >= '0' && *p <= '9') p++;
    }
    if (*p == '.') {
        p++;
        if (!(*p >= '0' && *p <= '9')) return CJSON_PARSE_INVALID_VALUE;
        while (*p >= '0' && *p <= '9') p++;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!(*p >= '0' && *p <= '9')) return CJSON_PARSE_INVALID_VALUE;
        while (*p >= '0' && *p <= '9') p++;
    }

    errno = 0;
    v->n = strtod(c->json, NULL);
    if (errno == ERANGE && (v->n == HUGE_VAL || v->n == -HUGE_VAL))
        return CJSON_PARSE_NUMBER_TOO_BIG;
    c->json = p;
    v->type = CJSON_NUMBER;
    return CJSON_PARSE_OK;
}

static int cjson_parse_value(cjson_context* c, cjson_value* v) {
    switch (*c->json) {
        case 't':  return cjson_parse_literal(c, v, "true", CJSON_TRUE);
        case 'f':  return cjson_parse_literal(c, v, "false", CJSON_FALSE);
        case 'n':  return cjson_parse_literal(c, v, "null", CJSON_NULL);
        default:   return cjson_parse_number(c, v);
        case '\0': return CJSON_PARSE_EXPECT_VALUE;
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
        if (*c.json != '\0') {
            v->type = CJSON_NULL;
            ret = CJSON_PARSE_ROOT_NOT_SINGULAR;
        }
    }
    return ret;
}

cjson_type cjson_get_type(const cjson_value* v) {
    assert(v != NULL);
    return v->type;
}

double cjson_get_number(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_NUMBER);
    return v->n;
}
