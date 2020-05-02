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

typedef struct cjson_value cjson_value;
typedef struct cjson_member cjson_member;

struct cjson_value {
    union {
        struct {cjson_member* memb; size_t size;} obj;
        struct {cjson_value* elem; size_t size;} arr; /* array:  elements, element count */
        struct {char* s; size_t len;} str;           /* string: null-terminated string, string length */
        double num;                                   /* number */
    } data;
    cjson_type type;
};

struct cjson_member {
    char* k; size_t klen;   /* member key string, key string length */
    cjson_value v;           /* member value */
};

enum {
    CJSON_PARSE_OK = 0,
    CJSON_PARSE_EXPECT_VALUE,
    CJSON_PARSE_INVALID_VALUE,
    CJSON_PARSE_ROOT_NOT_SINGULAR,
    CJSON_PARSE_NUMBER_TOO_BIG,
    CJSON_PARSE_MISS_QUOTATION_MARK,
    CJSON_PARSE_INVALID_STRING_ESCAPE,
    CJSON_PARSE_INVALID_STRING_CHAR,
    CJSON_PARSE_INVALID_UNICODE_HEX,
    CJSON_PARSE_INVALID_UNICODE_SURROGATE,
    CJSON_PARSE_MISS_COMMA_OR_SQUARE_BRACKET,
    CJSON_PARSE_MISS_KEY,
    CJSON_PARSE_MISS_COLON,
    CJSON_PARSE_MISS_COMMA_OR_CURLY_BRACKET
};

#define cjson_init(v) do {(v)->type = CJSON_NULL;} while(0)

int cjson_parse(cjson_value* v, const char* json);
char* cjson_stringify(const cjson_value* v, size_t* length);

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

size_t cjson_get_array_size(const cjson_value* v);
const cjson_value* cjson_get_array_element(const cjson_value* v, size_t index);

size_t cjson_get_object_size(const cjson_value* v);
const char* cjson_get_object_key(const cjson_value* v, size_t index);
size_t cjson_get_object_key_length(const cjson_value* v, size_t index);
cjson_value* cjson_get_object_value(const cjson_value* v, size_t index);

#endif
