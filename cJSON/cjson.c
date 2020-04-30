#include "cjson.h"
#include <assert.h>  /* assert() */
#include <errno.h>   /* errno, ERANGE */
#include <math.h>    /* HUGE_VAL */
#include <stdlib.h>  /* NULL, malloc(), realloc(), free(), strtod() */
#include <string.h>  /* memcpy() */

const static int CJSON_PARSE_STACK_INIT_SIZE = 256;

typedef struct {
    const char* json;
    char* stack;
    size_t size, top;
}cjson_context;

static void cjson_context_push(cjson_context* c, char ch) {
    size_t size = sizeof(ch);
    assert(size > 0);
    if (c->top + size >= c->size) {
        if (c->size == 0)
            c->size = CJSON_PARSE_STACK_INIT_SIZE;
        while (c->top + size >= c->size)
            c->size += c->size >> 1;  /* c->size * 1.5 */
        c->stack = (char*)realloc(c->stack, c->size);
    }
    c->stack[c->top] = ch;
    c->top += size;
}

static void* cjson_context_pop(cjson_context* c, size_t size) {
    assert(c->top >= size);
    return c->stack + (c->top -= size);
}

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
    v->u.num = strtod(c->json, NULL);
    if (errno == ERANGE && (v->u.num == HUGE_VAL || v->u.num == -HUGE_VAL))
        return CJSON_PARSE_NUMBER_TOO_BIG;
    c->json = p;
    v->type = CJSON_NUMBER;
    return CJSON_PARSE_OK;
}

static int cjson_parse_string(cjson_context* c, cjson_value* v) {
    size_t head = c->top, len;

    const char* p = c->json;
    if (p[0] != ('\"')) return CJSON_PARSE_INVALID_VALUE;
    
    for (p++;;) {
        char ch = *p++;
        switch (ch) {
            case '\"':
                len = c->top - head;
                cjson_set_string(v, (const char*)cjson_context_pop(c, len), len);
                c->json = p;
                return CJSON_PARSE_OK;
            case '\\':
                switch (*p++) {
                case '\"': cjson_context_push(c, '\"'); break;
                case '\\': cjson_context_push(c, '\\'); break;
                case '/': cjson_context_push(c, '/'); break;
                case 'b': cjson_context_push(c, '\b'); break;
                case 'f': cjson_context_push(c, '\f'); break;
                case 'n': cjson_context_push(c, '\n'); break;
                case 'r': cjson_context_push(c, '\r'); break;
                case 't': cjson_context_push(c, '\t'); break;
                default:
                    c->top = head;
                    return CJSON_PARSE_INVALID_STRING_ESCAPE;
                }
                break;
            case '\0':
                c->top = head;
                return CJSON_PARSE_MISS_QUOTATION_MARK;
            default:
                if ((unsigned char)ch < 0x20) {
                    c->top = head;
                    return CJSON_PARSE_INVALID_STRING_CHAR;
                }
                cjson_context_push(c, ch);
        }
    }
}

int cjson_parse_value(cjson_context* c, cjson_value* v) {
    switch (*c->json) {
        case 't':  return cjson_parse_literal(c, v, "true", CJSON_TRUE);
        case 'f':  return cjson_parse_literal(c, v, "false", CJSON_FALSE);
        case 'n':  return cjson_parse_literal(c, v, "null", CJSON_NULL);
        default:   return cjson_parse_number(c, v);
        case '"':  return cjson_parse_string(c, v);
        case '\0': return CJSON_PARSE_EXPECT_VALUE;
    }
}

int cjson_parse(cjson_value* v, const char* json) {
    int ret;

    assert(v != NULL);
    v->type = CJSON_NULL;
    
    cjson_context c;
    c.json = json;
    c.stack = NULL;
    c.size = c.top = 0;

    cjson_parse_whitespace(&c);
    if ((ret = cjson_parse_value(&c, v)) == CJSON_PARSE_OK) {
        cjson_parse_whitespace(&c);
        if (*c.json != '\0') {
            v->type = CJSON_NULL;
            ret = CJSON_PARSE_ROOT_NOT_SINGULAR;
        }
    }
    assert(c.top == 0);
    free(c.stack);
    return ret;
}

void cjson_free(cjson_value* v) {
    assert(v != NULL);
    if (v->type == CJSON_STRING)
        free(v->u.str.s);
    v->type = CJSON_NULL;
}

cjson_type cjson_get_type(const cjson_value* v) {
    assert(v != NULL);
    return v->type;
}

void cjson_set_null(cjson_value* v) {
    cjson_free(v);
}

int cjson_get_boolean(const cjson_value* v) {
    assert(v != NULL && (v->type == CJSON_TRUE || v->type == CJSON_FALSE));
    return v->type == CJSON_TRUE;
}

void cjson_set_boolean(cjson_value* v, int b) {
    cjson_free(v);
    v->type = b ? CJSON_TRUE : CJSON_FALSE;
}

double cjson_get_number(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_NUMBER);
    return v->u.num;
}

void cjson_set_number(cjson_value* v, double n) {
    cjson_free(v);
    v->u.num = n;
    v->type = CJSON_NUMBER;
}

const char* cjson_get_string(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_STRING);
    return v->u.str.s;
}

size_t cjson_get_string_length(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_STRING);
    return v->u.str.len;
}

void cjson_set_string(cjson_value* v, const char* s, size_t len) {
    assert(v != NULL && (s != NULL || len == 0));
    cjson_free(v);
    v->u.str.s = (char*)malloc(len + 1);
    memcpy(v->u.str.s, s, len);
    v->u.str.s[len] = '\0';
    v->u.str.len = len;
    v->type = CJSON_STRING;
}
