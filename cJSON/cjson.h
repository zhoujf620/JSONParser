#ifndef CJSON_H__
#define CJSON_H__

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
    cjson_type type;
} cjson_value;

enum {
    CJSON_PARSE_OK = 0,
    CJSON_PARSE_EXPECT_VALUE,
    CJSON_PARSE_INVALID_VALUE,
    CJSON_PARSE_ROOT_NOT_SINGULAR
};

int cjson_parse(cjson_value* v, const char* json);

cjson_type cjson_get_type(const cjson_value* v);

#endif
