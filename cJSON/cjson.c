#ifdef _WINDOWS
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
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
} cjson_context;

static void* cjson_context_push(cjson_context* c, size_t size) {
    void* ret;
    assert(size > 0);
    if (c->top + size >= c->size) {
        if (c->size == 0)
            c->size = CJSON_PARSE_STACK_INIT_SIZE;
        while (c->top + size >= c->size)
            c->size += c->size >> 1;  /* c->size * 1.5 */
        c->stack = (char*)realloc(c->stack, c->size);
    }
    ret = c->stack + c->top;
    c->top += size;
    return ret;
}

static void cjson_context_push_char(cjson_context* c, char ch) {
    char* p = cjson_context_push(c, sizeof(char));
    *p = ch;
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
    v->data.num = strtod(c->json, NULL);
    if (errno == ERANGE && (v->data.num == HUGE_VAL || v->data.num == -HUGE_VAL))
        return CJSON_PARSE_NUMBER_TOO_BIG;
    c->json = p;
    v->type = CJSON_NUMBER;
    return CJSON_PARSE_OK;
}

static const char* cjson_parse_hex4(const char* p, unsigned* u) {
    *u = 0;
    for (size_t i = 0; i < 4; ++i) {
        char ch = *p++;
        *u <<= 4;
        if      (ch >= '0' && ch <= '9') *u |= ch - '0';
        else if (ch >= 'A' && ch <= 'F') *u |= ch - ('A' - 10);
        else if (ch >= 'a' && ch <= 'f') *u |= ch - ('a' - 10);
        else return NULL;
    }
    return p;
}

static void cjson_encode_utf8(cjson_context* c, unsigned u) {
    if (u <= 0x7F) {
        cjson_context_push_char(c, u & 0xFF);
    }
    else if (u <= 0x7FF) {
        cjson_context_push_char(c, 0xC0 | ((u >> 6) & 0xFF));
        cjson_context_push_char(c, 0x80 | ( u       & 0x3F));
    }
    else if (u <= 0xFFFF) {
        cjson_context_push_char(c, 0xE0 | ((u >> 12) & 0xFF));
        cjson_context_push_char(c, 0x80 | ((u >>  6) & 0x3F));
        cjson_context_push_char(c, 0x80 | ( u        & 0x3F));
    }
    else {
        assert(u <= 0x10FFFF);
        cjson_context_push_char(c, 0xF0 | ((u >> 18) & 0xFF));
        cjson_context_push_char(c, 0x80 | ((u >> 12) & 0x3F));
        cjson_context_push_char(c, 0x80 | ((u >>  6) & 0x3F));
        cjson_context_push_char(c, 0x80 | ( u        & 0x3F));
    }
}

static int cjson_parse_string_raw(cjson_context* c, char** str, size_t* len) {
    size_t head = c->top;
    unsigned u, u2;

    const char* p = c->json;
    if (*p == ('\"')) p++;
    else return CJSON_PARSE_INVALID_VALUE;
    
    for (;;) {
        char ch = *p++;
        switch (ch) {
            case '\"':
                *len = c->top - head;
                *str = cjson_context_pop(c, *len);
                c->json = p;
                return CJSON_PARSE_OK;
            case '\\':
                switch (*p++) {
                case '\"': cjson_context_push_char(c, '\"'); break;
                case '\\': cjson_context_push_char(c, '\\'); break;
                case '/': cjson_context_push_char(c, '/'); break;
                case 'b': cjson_context_push_char(c, '\b'); break;
                case 'f': cjson_context_push_char(c, '\f'); break;
                case 'n': cjson_context_push_char(c, '\n'); break;
                case 'r': cjson_context_push_char(c, '\r'); break;
                case 't': cjson_context_push_char(c, '\t'); break;
                case 'u': 
                    if (!(p = cjson_parse_hex4(p, &u))) {
                        c->top = head;
                        return CJSON_PARSE_INVALID_UNICODE_HEX;
                    }
                    if (u >= 0xD800 && u <= 0xDBFF) { /* surrogate pair */
                        if (*p++ != '\\') {
                            c->top = head;
                            return CJSON_PARSE_INVALID_UNICODE_SURROGATE;
                        }
                        if (*p++ != 'u') {
                            c->top = head;
                            return CJSON_PARSE_INVALID_UNICODE_SURROGATE;
                        }
                        if (!(p = cjson_parse_hex4(p, &u2))) {
                            c->top = head;
                            return CJSON_PARSE_INVALID_UNICODE_HEX;
                        }
                        if (u2 < 0xDC00 || u2 > 0xDFFF) {
                            c->top = head;
                            return CJSON_PARSE_INVALID_UNICODE_SURROGATE;
                        }
                        u = ((u - 0xD800) << 10 | (u2 - 0xDC00)) + 0x10000;
                    }
                    cjson_encode_utf8(c, u);
                    break;
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
                cjson_context_push_char(c, ch);
        }
    }
}

static int cjson_parse_string(cjson_context* c, cjson_value* v) {
    int ret;
    char* s;
    size_t len;
    if ((ret = cjson_parse_string_raw(c, &s, &len)) == CJSON_PARSE_OK)
        cjson_set_string(v, s, len);
    return ret;
}

static int cjson_parse_value(cjson_context* c, cjson_value* v);

static int cjson_parse_array(cjson_context* c, cjson_value* v) {
    if (*c->json == '[') c->json++;
    else return CJSON_PARSE_INVALID_VALUE;

    cjson_parse_whitespace(c);
    if (*c->json == ']') {
        c->json++;
        v->type = CJSON_ARRAY;
        v->data.arr.size = 0;
        v->data.arr.elem = NULL;
        return CJSON_PARSE_OK;
    }

    size_t size = 0;
    int ret;
    for (;;) {
        cjson_value e;
        cjson_init(&e);
        if ((ret = cjson_parse_value(c, &e)) != CJSON_PARSE_OK)
            break;
        memcpy(cjson_context_push(c, sizeof(cjson_value)), &e, sizeof(cjson_value));
        size++;
        
        cjson_parse_whitespace(c);
        if (*c->json == ',') {
            c->json++;
            cjson_parse_whitespace(c);
        }
        else if (*c->json == ']') {
            size_t s = sizeof(cjson_value) * size;

            c->json++;
            v->type = CJSON_ARRAY;
            v->data.arr.size = size;
            size *= sizeof(cjson_value);
            memcpy(v->data.arr.elem = (cjson_value*)malloc(s), cjson_context_pop(c, s), s);
            return CJSON_PARSE_OK;
        }
        else {
            ret = CJSON_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
            break;
        }
    }
    /* Pop and free values on the stack */
    for (size_t i = 0; i < size; ++i)
        cjson_free((cjson_value*)cjson_context_pop(c, sizeof(cjson_value)));
    return ret;
}

static int cjson_parse_object(cjson_context* c, cjson_value* v) {
    if (*c->json == '{') c->json++;
    else return CJSON_PARSE_INVALID_VALUE;

    cjson_parse_whitespace(c);
    if (*c->json == '}') {
        c->json++;
        v->type = CJSON_OBJECT;
        v->data.obj.memb = 0;
        v->data.obj.size = 0;
        return CJSON_PARSE_OK;
    }

    size_t size = 0;
    int ret;
    cjson_member m;
    m.k = NULL;
    for (;;) {
        cjson_init(&m.v);
        
        /* parse key */
        if (*c->json != '"') {
            ret = CJSON_PARSE_MISS_KEY;
            break;
        }
        char* str;
        if ((ret = cjson_parse_string_raw(c, &str, &m.klen)) != CJSON_PARSE_OK)
            break;
        memcpy(m.k = (char*)malloc(m.klen + 1), str, m.klen);
        m.k[m.klen] = '\0';
        
        /* parse ws colon ws */
        cjson_parse_whitespace(c);
        if (*c->json != ':') {
            ret = CJSON_PARSE_MISS_COLON;
            break;
        }
        c->json++;

        cjson_parse_whitespace(c);
        /* parse value */
        if ((ret = cjson_parse_value(c, &m.v)) != CJSON_PARSE_OK)
            break;
        memcpy(cjson_context_push(c, sizeof(cjson_member)), &m, sizeof(cjson_member));
        size++;
        m.k = NULL; /* ownership is transferred to member on stack */
        
        /* parse ws [comma | right-curly-brace] ws */
        cjson_parse_whitespace(c);
        if (*c->json == ',') {
            c->json++;
            cjson_parse_whitespace(c);
        }
        else if (*c->json == '}') {
            size_t s = sizeof(cjson_member) * size;
            c->json++;
            v->type = CJSON_OBJECT;
            v->data.obj.size = size;
            memcpy(v->data.obj.memb = (cjson_member*)malloc(s), cjson_context_pop(c, s), s);
            return CJSON_PARSE_OK;
        }
        else {
            ret = CJSON_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
            break;
        }
    }
    /* Pop and free members on the stack */
    free(m.k);
    for (size_t i = 0; i < size; ++i) {
        cjson_member* m = (cjson_member*)cjson_context_pop(c, sizeof(cjson_member));
        free(m->k);
        cjson_free(&m->v);
    }
    v->type = CJSON_NULL;
    return ret;
}

static int cjson_parse_value(cjson_context* c, cjson_value* v) {
    switch (*c->json) {
        case 't':  return cjson_parse_literal(c, v, "true", CJSON_TRUE);
        case 'f':  return cjson_parse_literal(c, v, "false", CJSON_FALSE);
        case 'n':  return cjson_parse_literal(c, v, "null", CJSON_NULL);
        default:   return cjson_parse_number(c, v);
        case '"':  return cjson_parse_string(c, v);
        case '[':  return cjson_parse_array(c, v);
        case '{':  return cjson_parse_object(c, v);
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
    switch (v->type) {
        case CJSON_STRING:
            free(v->data.str.s);
            break;
        case CJSON_ARRAY:
            for (size_t i = 0; i < v->data.arr.size; ++i)
                cjson_free(&v->data.arr.elem[i]);
            free(v->data.arr.elem);
            break;
        case CJSON_OBJECT:
            for (size_t i=0; i<v->data.obj.size; ++i) {
                free(v->data.obj.memb[i].k);
                cjson_free(&v->data.obj.memb[i].v);
            }
            free(v->data.obj.memb);
            break;
        default: break;
    }
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
    return v->data.num;
}

void cjson_set_number(cjson_value* v, double n) {
    cjson_free(v);
    v->data.num = n;
    v->type = CJSON_NUMBER;
}

const char* cjson_get_string(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_STRING);
    return v->data.str.s;
}

size_t cjson_get_string_length(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_STRING);
    return v->data.str.len;
}

void cjson_set_string(cjson_value* v, const char* s, size_t len) {
    assert(v != NULL && (s != NULL || len == 0));
    cjson_free(v);
    v->data.str.s = (char*)malloc(len + 1);
    memcpy(v->data.str.s, s, len);
    v->data.str.s[len] = '\0';
    v->data.str.len = len;
    v->type = CJSON_STRING;
}

size_t cjson_get_array_size(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_ARRAY);
    return v->data.arr.size;
}

const cjson_value* cjson_get_array_element(const cjson_value* v, size_t index) {
    assert(v != NULL && v->type == CJSON_ARRAY);
    assert(index < v->data.arr.size);
    return &v->data.arr.elem[index];
}

size_t cjson_get_object_size(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_OBJECT);
    return v->data.obj.size;
}

const char* cjson_get_object_key(const cjson_value* v, size_t index) {
    assert(v != NULL && v->type == CJSON_OBJECT);
    assert(index < v->data.obj.size);
    return v->data.obj.memb[index].k;
}

size_t cjson_get_object_key_length(const cjson_value* v, size_t index) {
    assert(v != NULL && v->type == CJSON_OBJECT);
    assert(index < v->data.obj.size);
    return v->data.obj.memb[index].klen;
}

cjson_value* cjson_get_object_value(const cjson_value* v, size_t index) {
    assert(v != NULL && v->type == CJSON_OBJECT);
    assert(index < v->data.obj.size);
    return &v->data.obj.memb[index].v;
}
