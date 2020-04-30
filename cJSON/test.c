#include "cjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int main_ret = 0;
static int test_count = 0;
static int test_pass = 0;

#define EXPECT_EQ_BASE(equality, expect, actual, format) \
    do {\
        test_count++;\
        if (equality)\
            test_pass++;\
        else {\
            fprintf(stderr, "%s:%d: expect: " format " actual: " format "\n", __FILE__, __LINE__, expect, actual);\
            main_ret = 1;\
        }\
    } while(0)

#define EXPECT_EQ_INT(expect, actual) EXPECT_EQ_BASE((expect) == (actual), expect, actual, "%d")
#define EXPECT_EQ_DOUBLE(expect, actual) EXPECT_EQ_BASE((expect) == (actual), expect, actual, "%.17g")
#define EXPECT_EQ_STRING(expect, actual, alength) \
    EXPECT_EQ_BASE(sizeof(expect) - 1 == alength && memcmp(expect, actual, alength) == 0, expect, actual, "%s")
#define EXPECT_TRUE(actual) EXPECT_EQ_BASE((actual) != 0, "true", "false", "%s")
#define EXPECT_FALSE(actual) EXPECT_EQ_BASE((actual) == 0, "false", "true", "%s")

static void test_parse_null() {
    cjson_value v;
    cjson_init(&v);
    cjson_set_boolean(&v, 0);
    EXPECT_EQ_INT(CJSON_PARSE_OK, cjson_parse(&v, "null"));
    EXPECT_EQ_INT(CJSON_NULL, cjson_get_type(&v));
    cjson_free(&v);}

static void test_parse_true() {
    cjson_value v;
    cjson_init(&v);
    cjson_set_boolean(&v, 0);
    EXPECT_EQ_INT(CJSON_PARSE_OK, cjson_parse(&v, "true"));
    EXPECT_EQ_INT(CJSON_TRUE, cjson_get_type(&v));
    cjson_free(&v);
}

static void test_parse_false() {
    cjson_value v;
    cjson_init(&v);
    cjson_set_boolean(&v, 1);
    EXPECT_EQ_INT(CJSON_PARSE_OK, cjson_parse(&v, "false"));
    EXPECT_EQ_INT(CJSON_FALSE, cjson_get_type(&v));
    cjson_free(&v);
}

#define TEST_NUMBER(expect, json)\
    do {\
        cjson_value v;\
        cjson_init(&v);\
        EXPECT_EQ_INT(CJSON_PARSE_OK, cjson_parse(&v, json));\
        EXPECT_EQ_INT(CJSON_NUMBER, cjson_get_type(&v));\
        EXPECT_EQ_DOUBLE(expect, cjson_get_number(&v));\
        cjson_free(&v);\
    } while(0)

static void test_parse_number() {
    TEST_NUMBER(0.0, "0");
    TEST_NUMBER(0.0, "-0");
    TEST_NUMBER(0.0, "-0.0");
    TEST_NUMBER(1.0, "1");
    TEST_NUMBER(-1.0, "-1");
    TEST_NUMBER(1.5, "1.5");
    TEST_NUMBER(-1.5, "-1.5");
    TEST_NUMBER(3.1416, "3.1416");
    TEST_NUMBER(1E10, "1E10");
    TEST_NUMBER(1e10, "1e10");
    TEST_NUMBER(1E+10, "1E+10");
    TEST_NUMBER(1E-10, "1E-10");
    TEST_NUMBER(-1E10, "-1E10");
    TEST_NUMBER(-1e10, "-1e10");
    TEST_NUMBER(-1E+10, "-1E+10");
    TEST_NUMBER(-1E-10, "-1E-10");
    TEST_NUMBER(1.234E+10, "1.234E+10");
    TEST_NUMBER(1.234E-10, "1.234E-10");
    TEST_NUMBER(0.0, "1e-10000"); /* must underflow */

    TEST_NUMBER(1.0000000000000002, "1.0000000000000002"); /* the smallest number > 1 */
    TEST_NUMBER( 4.9406564584124654e-324, "4.9406564584124654e-324"); /* minimum denormal */
    TEST_NUMBER(-4.9406564584124654e-324, "-4.9406564584124654e-324");
    TEST_NUMBER( 2.2250738585072009e-308, "2.2250738585072009e-308");  /* Max subnormal double */
    TEST_NUMBER(-2.2250738585072009e-308, "-2.2250738585072009e-308");
    TEST_NUMBER( 2.2250738585072014e-308, "2.2250738585072014e-308");  /* Min normal positive double */
    TEST_NUMBER(-2.2250738585072014e-308, "-2.2250738585072014e-308");
    TEST_NUMBER( 1.7976931348623157e+308, "1.7976931348623157e+308");  /* Max double */
    TEST_NUMBER(-1.7976931348623157e+308, "-1.7976931348623157e+308");
}

#define TEST_STRING(expect, json)\
    do {\
        cjson_value v;\
        cjson_init(&v);\
        EXPECT_EQ_INT(CJSON_PARSE_OK, cjson_parse(&v, json));\
        EXPECT_EQ_INT(CJSON_STRING, cjson_get_type(&v));\
        EXPECT_EQ_STRING(expect, cjson_get_string(&v), cjson_get_string_length(&v));\
        cjson_free(&v);\
    } while(0)

static void test_parse_string() {
    TEST_STRING("", "\"\"");
    TEST_STRING("Hello", "\"Hello\"");
    TEST_STRING("Hello\nWorld", "\"Hello\\nWorld\"");
    TEST_STRING("\" \\ / \b \f \n \r \t", "\"\\\" \\\\ \\/ \\b \\f \\n \\r \\t\"");
}

#define TEST_ERROR(error, json)\
    do {\
        cjson_value v;\
        cjson_init(&v);\
        EXPECT_EQ_INT(error, cjson_parse(&v, json));\
        EXPECT_EQ_INT(CJSON_NULL, cjson_get_type(&v));\
        cjson_free(&v);\
    } while(0)

static void test_parse_expect_value() {
    TEST_ERROR(CJSON_PARSE_EXPECT_VALUE, "");
    TEST_ERROR(CJSON_PARSE_EXPECT_VALUE, " ");
}

static void test_parse_invalid_value() {
    TEST_ERROR(CJSON_PARSE_INVALID_VALUE, "nul");
    TEST_ERROR(CJSON_PARSE_INVALID_VALUE, "?");

    /* invalid number */
    TEST_ERROR(CJSON_PARSE_INVALID_VALUE, "+0");
    TEST_ERROR(CJSON_PARSE_INVALID_VALUE, "+1");
    TEST_ERROR(CJSON_PARSE_INVALID_VALUE, ".123"); /* at least one digit before '.' */
    TEST_ERROR(CJSON_PARSE_INVALID_VALUE, "1.");   /* at least one digit after '.' */
    TEST_ERROR(CJSON_PARSE_INVALID_VALUE, "INF");
    TEST_ERROR(CJSON_PARSE_INVALID_VALUE, "inf");
    TEST_ERROR(CJSON_PARSE_INVALID_VALUE, "NAN");
    TEST_ERROR(CJSON_PARSE_INVALID_VALUE, "nan");
}

static void test_parse_root_not_singular() {
    TEST_ERROR(CJSON_PARSE_ROOT_NOT_SINGULAR, "null x");

    /* invalid number */
    TEST_ERROR(CJSON_PARSE_ROOT_NOT_SINGULAR, "0123"); /* after zero should be '.' , 'E' , 'e' or nothing */
    TEST_ERROR(CJSON_PARSE_ROOT_NOT_SINGULAR, "0x0");
    TEST_ERROR(CJSON_PARSE_ROOT_NOT_SINGULAR, "0x123");
}

static void test_parse_number_too_big() {
    TEST_ERROR(CJSON_PARSE_NUMBER_TOO_BIG, "1e309");
    TEST_ERROR(CJSON_PARSE_NUMBER_TOO_BIG, "-1e309");
}

static void test_parse_missing_quotation_mark() {
    TEST_ERROR(CJSON_PARSE_MISS_QUOTATION_MARK, "\"");
    TEST_ERROR(CJSON_PARSE_MISS_QUOTATION_MARK, "\"abc");
}

static void test_parse_invalid_string_escape() {
    TEST_ERROR(CJSON_PARSE_INVALID_STRING_ESCAPE, "\"\\v\"");
    TEST_ERROR(CJSON_PARSE_INVALID_STRING_ESCAPE, "\"\\'\"");
    TEST_ERROR(CJSON_PARSE_INVALID_STRING_ESCAPE, "\"\\0\"");
    TEST_ERROR(CJSON_PARSE_INVALID_STRING_ESCAPE, "\"\\x12\"");
}

static void test_parse_invalid_string_char() {
    TEST_ERROR(CJSON_PARSE_INVALID_STRING_CHAR, "\"\x01\"");
    TEST_ERROR(CJSON_PARSE_INVALID_STRING_CHAR, "\"\x1F\"");
}

static void test_access_null() {
    cjson_value v;
    cjson_init(&v);
    cjson_set_string(&v, "a", 1);
    cjson_set_null(&v);
    EXPECT_EQ_INT(CJSON_NULL, cjson_get_type(&v));
    cjson_free(&v);
}

static void test_access_boolean() {
    cjson_value v;
    cjson_init(&v);
    cjson_set_string(&v, "a", 1);
    cjson_set_boolean(&v, 1);
    EXPECT_TRUE(cjson_get_boolean(&v));
    cjson_set_boolean(&v, 0);
    EXPECT_FALSE(cjson_get_boolean(&v));
    cjson_free(&v);
}

static void test_access_number() {
    cjson_value v;
    cjson_init(&v);
    cjson_set_string(&v, "a", 1);
    cjson_set_number(&v, 1234.5);
    EXPECT_EQ_DOUBLE(1234.5, cjson_get_number(&v));
    cjson_free(&v);
}

static void test_access_string() {
    cjson_value v;
    cjson_init(&v);
    cjson_set_string(&v, "", 0);
    EXPECT_EQ_STRING("", cjson_get_string(&v), cjson_get_string_length(&v));
    cjson_set_string(&v, "Hello", 5);
    EXPECT_EQ_STRING("Hello", cjson_get_string(&v), cjson_get_string_length(&v));
    cjson_free(&v);
}

static void test_parse() {
    test_parse_null();
    test_parse_true();
    test_parse_false();
    test_parse_number();
    test_parse_string();
    test_parse_expect_value();
    test_parse_invalid_value();
    test_parse_root_not_singular();
    test_parse_number_too_big();
    test_parse_missing_quotation_mark();
    test_parse_invalid_string_escape();
    test_parse_invalid_string_char();

    test_access_null();
    test_access_boolean();
    test_access_number();
    test_access_string();
}

int main() {
    test_parse();
    printf("%d/%d (%3.2f%%) passed\n", test_pass, test_count, test_pass * 100.0 / test_count);
    return main_ret;
}
