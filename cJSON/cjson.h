#ifndef CJSON_H__
#define CJSON_H__

#include <stddef.h> /* size_t */

typedef enum {
    CJSON_NULL, 
    CJSON_FALSE, 
    CJSON_TRUE, 
    CJSON_NUMBER, 
    CJSON_STRING, 
    CJSON_ARRAY, 
    CJSON_OBJECT
} cjson_type;

typedef struct {
    union {
        struct {char* s; size_t len;} str;
        double num;
    } u;
    cjson_type type;
} cjson_value;

enum {
    CJSON_PARSE_OK = 0,
    CJSON_PARSE_EXPECT_VALUE,
    CJSON_PARSE_INVALID_VALUE,
    CJSON_PARSE_ROOT_NOT_SINGULAR,
    CJSON_PARSE_NUMBER_TOO_BIG,
    CJSON_PARSE_MISS_QUOTATION_MARK,
    CJSON_PARSE_INVALID_STRING_ESCAPE,
    CJSON_PARSE_INVALID_STRING_CHAR
};

#define cjson_init(v) do { (v)->type = CJSON_NULL; } while(0)

int cjson_parse(cjson_value* v, const char* json);

void cjson_free(cjson_value* v);

cjson_type cjson_get_type(const cjson_value* v);

void cjson_set_null(cjson_value* v);

int cjson_get_boolean(const cjson_value* v);
void cjson_set_boolean(cjson_value* v, int b);

double cjson_get_number(const cjson_value* v);
void cjson_set_number(cjson_value* v, double n);

const char* cjson_get_string(const cjson_value* v);
size_t cjson_get_string_length(const cjson_value* v);
void cjson_set_string(cjson_value* v, const char* s, size_t len);

#endif
