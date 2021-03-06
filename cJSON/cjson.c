#ifdef _WINDOWS
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include "cjson.h"
#include <assert.h>  /* assert() */
#include <errno.h>   /* errno, ERANGE */
#include <math.h>    /* HUGE_VAL */
#include <stdio.h>   /* sprintf() */
#include <stdlib.h>  /* NULL, malloc(), realloc(), free(), strtod() */
#include <string.h>  /* memcpy() */

// ============================
// ========== buffer ==========
// ============================

const static int CJSON_PARSE_BUFFER_INIT_SIZE = 256;
const static int CJSON_PARSE_STRINGIFY_INIT_SIZE = 256;

#define PUTC(c, ch)         do { *(char*)cjson_context_push(c, sizeof(char)) = (ch); } while(0)
#define PUTS(c, s, len)     memcpy(cjson_context_push(c, len), s, len)

typedef struct {
    const char* json;
    char* buffer;
    size_t size, top;
} cjson_context;

static void* cjson_context_push(cjson_context* c, size_t size) {
    void* ret;
    assert(size > 0);
    if (c->top + size >= c->size) {
        if (c->size == 0)
            c->size = CJSON_PARSE_BUFFER_INIT_SIZE;
        while (c->top + size >= c->size)
            c->size += c->size >> 1;  /* c->size * 1.5 */
        c->buffer = (char*)realloc(c->buffer, c->size);
    }
    ret = c->buffer + c->top;
    c->top += size;
    return ret;
}

static void cjson_context_push_char(cjson_context* c, char ch) {
    char* p = cjson_context_push(c, sizeof(char));
    *p = ch;
}

static void cjson_context_push_str(cjson_context* c, const char* s, const int len) {
    memcpy(cjson_context_push(c, len), s, len);
}

static void* cjson_context_pop(cjson_context* c, size_t size) {
    assert(c->top >= size);
    return c->buffer + (c->top -= size);
}

// ============================
// ========== parser ==========
// ============================

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
        cjson_set_array(v, 0);
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
            c->json++;
            cjson_set_array(v, size);
            v->data.arr.size = size;
            memcpy(v->data.arr.elem, cjson_context_pop(c, sizeof(cjson_value) * size),sizeof(cjson_value) * size);
            return CJSON_PARSE_OK;
        }
        else {
            ret = CJSON_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
            break;
        }
    }
    /* Pop and free values on the buffer */
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
        cjson_set_object(v, 0);
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
        m.k = NULL; /* ownership is transferred to member on buffer */
        
        /* parse ws [comma | right-curly-brace] ws */
        cjson_parse_whitespace(c);
        if (*c->json == ',') {
            c->json++;
            cjson_parse_whitespace(c);
        }
        else if (*c->json == '}') {
            c->json++;
            cjson_set_object(v, size);
            v->data.obj.size = size;
            memcpy(v->data.obj.memb, cjson_context_pop(c, sizeof(cjson_member) * size), sizeof(cjson_member) * size);
            return CJSON_PARSE_OK;
        }
        else {
            ret = CJSON_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
            break;
        }
    }
    /* Pop and free members on the buffer */
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
    c.buffer = NULL;
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
    free(c.buffer);
    return ret;
}
// ===============================
// ========== generator ==========
// ===============================

static void cjson_stringify_string(cjson_context* c, const char* s, size_t len) {
    static const char hex_digits[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    assert(s != NULL);
    size_t size;
    char* head, *p;
    p = head = cjson_context_push(c, size = len * 6 + 2); /* "\u00xx..." */
    
    *p++ = '"';
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)s[i];
        switch (ch) {
            case '\"': *p++ = '\\'; *p++ = '\"'; break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '\b': *p++ = '\\'; *p++ = 'b';  break;
            case '\f': *p++ = '\\'; *p++ = 'f';  break;
            case '\n': *p++ = '\\'; *p++ = 'n';  break;
            case '\r': *p++ = '\\'; *p++ = 'r';  break;
            case '\t': *p++ = '\\'; *p++ = 't';  break;
            default:
                if (ch < 0x20) {
                    *p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
                    *p++ = hex_digits[ch >> 4];
                    *p++ = hex_digits[ch & 15];
                }
                else
                    *p++ = s[i];
        }
    }
    *p++ = '"';
    c->top -= size - (p - head);
}

static void cjson_stringify_value(cjson_context* c, const cjson_value* v) {
    switch (v->type) {
        case CJSON_NULL:   cjson_context_push_str(c, "null", 4); break;
        case CJSON_FALSE:  cjson_context_push_str(c, "false", 5); break;
        case CJSON_TRUE:   cjson_context_push_str(c, "true", 4); break;
        case CJSON_NUMBER: c->top -= 32 - sprintf(cjson_context_push(c, 32), "%.17g", v->data.num); break;
        case CJSON_STRING: cjson_stringify_string(c, v->data.str.s, v->data.str.len); break;
        case CJSON_ARRAY:
            cjson_context_push_char(c, '[');
            for (size_t i = 0; i < v->data.arr.size; i++) {
                if (i > 0)
                    cjson_context_push_char(c, ',');
                cjson_stringify_value(c, &v->data.arr.elem[i]);
            }
            cjson_context_push_char(c, ']');
            break;
        case CJSON_OBJECT:
            cjson_context_push_char(c, '{');
            for (size_t i = 0; i < v->data.obj.size; i++) {
                if (i > 0)
                    cjson_context_push_char(c, ',');
                cjson_stringify_string(c, v->data.obj.memb[i].k, v->data.obj.memb[i].klen);
                cjson_context_push_char(c, ':');
                cjson_stringify_value(c, &v->data.obj.memb[i].v);
            }
            cjson_context_push_char(c, '}');
            break;            
        default: assert(0 && "invalid type");
    }
}

char* cjson_stringify(const cjson_value* v, size_t* len) {
    cjson_context c;
    assert(v != NULL);
    c.buffer = (char*)malloc(c.size = CJSON_PARSE_STRINGIFY_INIT_SIZE);
    c.top = 0;
    cjson_stringify_value(&c, v);
    if (len) *len = c.top;
    cjson_context_push_char(&c, '\0');
    return c.buffer;
}

// ==============================
// ========== accessor ==========
// ==============================

void cjson_copy(cjson_value* dst, const cjson_value* src) {
    assert(src != NULL && dst != NULL && src != dst);
    switch (src->type) {
        case CJSON_STRING:
            cjson_set_string(dst, src->data.str.s, src->data.str.len);
            break;
        case CJSON_ARRAY:
            cjson_set_array(dst, src->data.arr.size);
            for(size_t i = 0; i < src->data.arr.size; ++i) {
                cjson_init(dst->data.arr.elem + i);
                cjson_copy(dst->data.arr.elem + i, src->data.arr.elem + i);
            }
            dst->data.arr.size = src->data.arr.size;
            break;
        case CJSON_OBJECT:
            cjson_set_object(dst, src->data.obj.size);
            for(size_t i = 0; i < src->data.obj.size; ++i) {
                dst->data.obj.memb[i].klen = src->data.obj.memb[i].klen;
                dst->data.obj.memb[i].k = (char*)malloc(dst->data.obj.memb[i].klen + 1);
                memcpy(dst->data.obj.memb[i].k, src->data.obj.memb[i].k, dst->data.obj.memb[i].klen);
                dst->data.obj.memb[i].k[dst->data.obj.memb[i].klen] = '\0';
                cjson_init(&dst->data.obj.memb[i].v);
                cjson_copy(&dst->data.obj.memb[i].v, &src->data.obj.memb[i].v);
            }
            dst->data.obj.size = src->data.obj.size;
            break;
        default:
            cjson_free(dst);
            memcpy(dst, src, sizeof(cjson_value));
            break;
    }
}

void cjson_move(cjson_value* dst, cjson_value* src) {
    assert(dst != NULL && src != NULL && src != dst);
    cjson_free(dst);
    memcpy(dst, src, sizeof(cjson_value));
    cjson_init(src);
}

void cjson_swap(cjson_value* lhs, cjson_value* rhs) {
    assert(lhs != NULL && rhs != NULL);
    if (lhs != rhs) {
        cjson_value temp;
        memcpy(&temp, lhs, sizeof(cjson_value));
        memcpy(lhs,   rhs, sizeof(cjson_value));
        memcpy(rhs, &temp, sizeof(cjson_value));
    }
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

int cjson_is_equal(const cjson_value* lhs, const cjson_value* rhs) {
    assert(lhs != NULL && rhs != NULL);
    if (lhs->type != rhs->type)
        return 0;
    switch (lhs->type) {
        case CJSON_STRING:
            return lhs->data.str.len == rhs->data.str.len && 
                memcmp(lhs->data.str.s, rhs->data.str.s, lhs->data.str.len) == 0;
        case CJSON_NUMBER:
            return lhs->data.num == rhs->data.num;
        case CJSON_ARRAY:
            if (lhs->data.arr.size != rhs->data.arr.size)
                return 0;
            for (size_t i = 0; i < lhs->data.arr.size; ++i)
                if (!cjson_is_equal(&lhs->data.arr.elem[i], &rhs->data.arr.elem[i]))
                    return 0;
            return 1;
        case CJSON_OBJECT:
            if (lhs->data.obj.size != rhs->data.obj.size)
                return 0;
            for (size_t i = 0; i < lhs->data.obj.size; ++i){
                size_t index = cjson_find_object_index(rhs, lhs->data.obj.memb[i].k, lhs->data.obj.memb[i].klen);
                if (index == CJSON_KEY_NOT_EXIST || !cjson_is_equal(&lhs->data.obj.memb[i].v, &rhs->data.obj.memb[index].v))
                    return 0;
            }
            return 1;
        default:
            return 1;
    }
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

void cjson_set_array(cjson_value* v, size_t capacity) {
    assert(v != NULL);
    cjson_free(v);
    v->type = CJSON_ARRAY;
    v->data.arr.size = 0;
    v->data.arr.capacity = capacity;
    v->data.arr.elem = capacity > 0 ? (cjson_value*)malloc(capacity * sizeof(cjson_value)) : NULL;
}

size_t cjson_get_array_size(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_ARRAY);
    return v->data.arr.size;
}

size_t cjson_get_array_capacity(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_ARRAY);
    return v->data.arr.capacity;
}

void cjson_reserve_array(cjson_value* v, size_t capacity) {
    assert(v != NULL && v->type == CJSON_ARRAY);
    if (v->data.arr.capacity < capacity) {
        v->data.arr.capacity = capacity;
        v->data.arr.elem = (cjson_value*)realloc(v->data.arr.elem, capacity * sizeof(cjson_value));
    }
}

void cjson_shrink_array(cjson_value* v) {
    assert(v != NULL && v->type == CJSON_ARRAY);
    if (v->data.arr.capacity > v->data.arr.size) {
        v->data.arr.capacity = v->data.arr.size;
        v->data.arr.elem = (cjson_value*)realloc(v->data.arr.elem, v->data.arr.capacity * sizeof(cjson_value));
    }
}

void cjson_clear_array(cjson_value* v) {
    assert(v != NULL && v->type == CJSON_ARRAY);
    cjson_erase_array_element(v, 0, v->data.arr.size);
}

cjson_value* cjson_get_array_element(cjson_value* v, size_t index) {
    assert(v != NULL && v->type == CJSON_ARRAY);
    assert(index < v->data.arr.size);
    return &v->data.arr.elem[index];
}

cjson_value* cjson_pushback_array_element(cjson_value* v) {
    assert(v != NULL && v->type == CJSON_ARRAY);
    if (v->data.arr.size == v->data.arr.capacity)
        cjson_reserve_array(v, v->data.arr.capacity == 0 ? 1 : v->data.arr.capacity * 2);
    cjson_init(&v->data.arr.elem[v->data.arr.size]);
    return &v->data.arr.elem[v->data.arr.size++];
}

void cjson_popback_array_element(cjson_value* v) {
    assert(v != NULL && v->type == CJSON_ARRAY && v->data.arr.size > 0);
    cjson_free(&v->data.arr.elem[--v->data.arr.size]);
}

cjson_value* cjson_insert_array_element(cjson_value* v, size_t index) {
    assert(v != NULL && v->type == CJSON_ARRAY);
    assert(index <= v->data.arr.size);   /* if index == size, then this's equivalent to pushback*/
    if (v->data.arr.size == v->data.arr.capacity) {
        cjson_reserve_array(v, v->data.arr.capacity == 0 ? 1 : v->data.arr.capacity * 2);
    }
    for(size_t i = v->data.arr.size++; i > index; --i) {
        v->data.arr.elem[i] = v->data.arr.elem[i - 1];
    }
    return &v->data.arr.elem[index]; 
}

void cjson_erase_array_element(cjson_value* v, size_t index, size_t count) {
    assert(v != NULL && v->type == CJSON_ARRAY);
    assert(index + count <= v->data.arr.size);
    if(!count) return;
    for(size_t i = index; i < index + count; ++i) {
        cjson_free(&v->data.arr.elem[i]);
        v->data.arr.elem[i] = v->data.arr.elem[i + count];
    }
    for(size_t i = index + count; i < v->data.arr.size; ++i) {
        v->data.arr.elem[i] = v->data.arr.elem[i + count];
    }
    v->data.arr.size -= count;
}

void cjson_set_object(cjson_value* v, size_t capacity) {
    assert(v != NULL);
    cjson_free(v);
    v->type = CJSON_OBJECT;
    v->data.obj.size = 0;
    v->data.obj.capacity = capacity;
    v->data.obj.memb = capacity > 0 ? (cjson_member*)malloc(capacity * sizeof(cjson_member)) : NULL;
}

size_t cjson_get_object_size(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_OBJECT);
    return v->data.obj.size;
}

size_t cjson_get_object_capacity(const cjson_value* v) {
    assert(v != NULL && v->type == CJSON_OBJECT);
    return v->data.obj.capacity;
}

void cjson_reserve_object(cjson_value* v, size_t capacity) {
    assert(v != NULL && v->type == CJSON_OBJECT);
    if (v->data.obj.capacity < capacity) {
        v->data.obj.capacity = capacity;
        v->data.obj.memb = (cjson_member*)realloc(v->data.arr.elem, capacity * sizeof(cjson_member));
    }
}

void cjson_shrink_object(cjson_value* v) {
    assert(v != NULL && v->type == CJSON_OBJECT);
    if (v->data.obj.capacity > v->data.obj.size) {
        v->data.obj.capacity = v->data.obj.size;
        v->data.obj.memb = (cjson_member*)realloc(v->data.obj.memb, v->data.obj.size * sizeof(cjson_member));
    }
}

void cjson_clear_object(cjson_value* v) {
    assert(v != NULL && v->type == CJSON_OBJECT);
    for(size_t i = 0; i < v->data.obj.size; ++i){
        free(v->data.obj.memb[i].k);
        cjson_free(&v->data.obj.memb[i].v);
    }
    v->data.obj.size = 0;
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

cjson_value* cjson_get_object_value(cjson_value* v, size_t index) {
    assert(v != NULL && v->type == CJSON_OBJECT);
    assert(index < v->data.obj.size);
    return &v->data.obj.memb[index].v;
}

size_t cjson_find_object_index(const cjson_value* v, const char* key, size_t klen) {
    assert(v != NULL && v->type == CJSON_OBJECT && key != NULL);
    for (size_t i = 0; i < v->data.obj.size; ++i)
        if (v->data.obj.memb[i].klen == klen && memcmp(v->data.obj.memb[i].k, key, klen) == 0)
            return i;
    return CJSON_KEY_NOT_EXIST;
}

cjson_value* cjson_find_object_value(cjson_value* v, const char* key, size_t klen) {
    size_t index = cjson_find_object_index(v, key, klen);
    return index != CJSON_KEY_NOT_EXIST ? &v->data.obj.memb[index].v : NULL;
}

cjson_value* cjson_set_object_value(cjson_value* v, const char* key, size_t klen) {
    assert(v != NULL && v->type == CJSON_OBJECT && key != NULL);
    if (v->data.obj.size == v->data.obj.capacity)
        cjson_reserve_object(v, v->data.obj.capacity == 0 ? 1 : v->data.obj.capacity * 2);
    memcpy(v->data.obj.memb[v->data.obj.size].k = (char*)malloc(klen + 1), key, klen);
    v->data.obj.memb[v->data.obj.size].k[klen] = '\0';
    v->data.obj.memb[v->data.obj.size].klen = klen;
    cjson_init(&(v->data.obj.memb[v->data.obj.size].v));
    return &v->data.obj.memb[v->data.obj.size++].v;
}

void cjson_remove_object_value(cjson_value* v, size_t index) {
    assert(v != NULL && v->type == CJSON_OBJECT && index < v->data.obj.size);
    free(v->data.obj.memb[index].k);
    cjson_free(&(v->data.obj.memb[index].v));
    memmove(&v->data.obj.memb[index], &v->data.obj.memb[index+1], sizeof(cjson_member)*(v->data.obj.size-index-1));
    v->data.obj.size--;
}
