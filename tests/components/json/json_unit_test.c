// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* JSON */
#include "json/json.h"

void setUp(void)
{
}
void tearDown(void)
{
}

/* --- Parser: init --- */

void test_init_null_view(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_json_init(NULL, "{}", 2));
}

void test_init_null_buf(void)
{
    actrust_json_view_t v;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_json_init(&v, NULL, 0));
}

void test_init_malformed_json(void)
{
    actrust_json_view_t v;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_json_init(&v, "{missing", 8));
}

void test_init_empty_object(void)
{
    actrust_json_view_t v;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_init(&v, "{}", 2));
}

void test_init_valid_nested(void)
{
    const char         *js = "{\"a\":{\"b\":1}}";
    actrust_json_view_t v;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_init(&v, js, strlen(js)));
}

/* --- Parser: query --- */

void test_query_null_args(void)
{
    actrust_json_view_t  v;
    actrust_json_value_t val;
    actrust_json_init(&v, "{\"x\":1}", 7);

    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_json_query(NULL, "x", &val));
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_json_query(&v, NULL, &val));
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_json_query(&v, "x", NULL));
}

void test_query_missing_key(void)
{
    const char         *js = "{\"name\":\"alice\"}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_json_query(&v, "missing", &val));
}

void test_query_nested_path(void)
{
    const char         *js = "{\"a\":{\"b\":42}}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_query(&v, "a.b", &val));
    TEST_ASSERT_EQUAL(ACTRUST_JSON_TYPE_NUMBER, val.type);
}

void test_query_nested_missing_intermediate(void)
{
    const char         *js = "{\"a\":{\"b\":1}}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_json_query(&v, "a.z.y", &val));
}

/* --- get_string --- */

void test_get_string_success(void)
{
    const char         *js = "{\"k\":\"hello\"}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    actrust_json_query(&v, "k", &val);

    char   buf[32];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_get_string(&val, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL(5u, len);
    TEST_ASSERT_EQUAL_STRING("hello", buf);
}

void test_get_string_buffer_too_small(void)
{
    const char         *js = "{\"k\":\"longval\"}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    actrust_json_query(&v, "k", &val);

    char   buf[4];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL,
                      ACTRUST_ERR_CODE(actrust_json_get_string(
                          &val, buf, sizeof(buf), &len)));
}

void test_get_string_unescapes_json_sequences(void)
{
    const char         *js = "{\"k\":\"line\\n\\u4E2D\"}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    actrust_json_query(&v, "k", &val);

    char   buf[32];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_get_string(&val, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL(8u, len);
    TEST_ASSERT_EQUAL_MEMORY("line\n\xE4\xB8\xAD", buf, len);
}

void test_get_string_buffer_too_small_after_unescape(void)
{
    const char         *js = "{\"k\":\"a\\nb\"}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    actrust_json_query(&v, "k", &val);

    char   buf[3];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL,
                      ACTRUST_ERR_CODE(actrust_json_get_string(
                          &val, buf, sizeof(buf), &len)));
    TEST_ASSERT_EQUAL(3u, len);
}

/* --- get_int --- */

void test_get_int_success(void)
{
    const char         *js = "{\"n\":42}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    actrust_json_query(&v, "n", &val);

    int64_t out = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_get_int(&val, &out));
    TEST_ASSERT_EQUAL_INT64(42, out);
}

void test_get_int_negative(void)
{
    const char         *js = "{\"n\":-100}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    actrust_json_query(&v, "n", &val);

    int64_t out = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_get_int(&val, &out));
    TEST_ASSERT_EQUAL_INT64(-100, out);
}

void test_get_int_rejects_overflow(void)
{
    const char         *js = "{\"n\":92233720368547758070}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    actrust_json_query(&v, "n", &val);

    int64_t out = 0;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_json_get_int(&val, &out));
}

/* --- get_bool --- */

void test_get_bool_true(void)
{
    const char         *js = "{\"b\":true}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    actrust_json_query(&v, "b", &val);

    bool out = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_get_bool(&val, &out));
    TEST_ASSERT_TRUE(out);
}

void test_get_bool_false(void)
{
    const char         *js = "{\"b\":false}";
    actrust_json_view_t v;
    actrust_json_init(&v, js, strlen(js));

    actrust_json_value_t val;
    actrust_json_query(&v, "b", &val);

    bool out = true;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_get_bool(&val, &out));
    TEST_ASSERT_FALSE(out);
}

/* --- escape / unescape --- */

void test_escape_null_input(void)
{
    char   buf[32];
    size_t len = 0;
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_json_escape_string(NULL, buf, sizeof(buf), &len));
}

void test_escape_buffer_too_small(void)
{
    char   buf[4];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL,
                      ACTRUST_ERR_CODE(actrust_json_escape_string(
                          "hello\"world", buf, sizeof(buf), &len)));
}

void test_unescape_simple(void)
{
    const char *input = "hello\\nworld";
    char        buf[32];
    size_t      len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_unescape_string(input, strlen(input), buf,
                                                   sizeof(buf), &len));
    TEST_ASSERT_EQUAL(11u, len);
    TEST_ASSERT_EQUAL_MEMORY("hello\nworld", buf, 11);
}

void test_unescape_invalid_escape(void)
{
    const char *input = "bad\\qseq";
    char        buf[32];
    size_t      len = 0;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_json_unescape_string(input, strlen(input),
                                                       buf, sizeof(buf), &len));
}

void test_unescape_utf8_2byte(void)
{
    const char *input = "\\u00E9";
    char        buf[32];
    size_t      len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_unescape_string(input, strlen(input), buf,
                                                   sizeof(buf), &len));
    TEST_ASSERT_EQUAL(2u, len);
    TEST_ASSERT_EQUAL_MEMORY("\xC3\xA9", buf, 2);
}

void test_unescape_utf8_3byte(void)
{
    const char *input = "\\u4E2D";
    char        buf[32];
    size_t      len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_unescape_string(input, strlen(input), buf,
                                                   sizeof(buf), &len));
    TEST_ASSERT_EQUAL(3u, len);
    TEST_ASSERT_EQUAL_MEMORY("\xE4\xB8\xAD", buf, 3);
}

void test_unescape_surrogate_pair(void)
{
    const char *input = "\\uD83D\\uDE00";
    char        buf[32];
    size_t      len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_unescape_string(input, strlen(input), buf,
                                                   sizeof(buf), &len));
    TEST_ASSERT_EQUAL(4u, len);
    TEST_ASSERT_EQUAL_MEMORY("\xF0\x9F\x98\x80", buf, 4);
}

void test_unescape_lone_high_surrogate(void)
{
    const char *input = "\\uD800end";
    char        buf[32];
    size_t      len = 0;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_json_unescape_string(input, strlen(input),
                                                       buf, sizeof(buf), &len));
}

void test_unescape_lone_low_surrogate(void)
{
    const char *input = "\\uDC00";
    char        buf[32];
    size_t      len = 0;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_json_unescape_string(input, strlen(input),
                                                       buf, sizeof(buf), &len));
}

/* --- Builder --- */

void test_builder_null_args(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_json_builder_init(NULL, NULL, 0));
}

void test_builder_tiny_buffer(void)
{
    actrust_json_builder_t b;
    char                   buf[3];
    actrust_json_builder_init(&b, buf, sizeof(buf));

    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_BUF_TOO_SMALL,
        ACTRUST_ERR_CODE(actrust_json_builder_add_string(&b, "k", "v", 1u)));
}

void test_builder_roundtrip(void)
{
    char                   buf[256];
    actrust_json_builder_t b;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_init(&b, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_add_string(&b, "name", "test", 4u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_add_int(&b, "val", 99));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_add_bool(&b, "ok", true));

    char  *json = NULL;
    size_t len  = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_finish(&b, &json, &len));

    actrust_json_view_t v;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_init(&v, json, len));

    actrust_json_value_t val;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_query(&v, "name", &val));
    TEST_ASSERT_EQUAL(ACTRUST_JSON_TYPE_STRING, val.type);
}

void test_builder_string_rejects_len_mismatch(void)
{
    const char             raw[] = { 'A', '\0', 'B', '\0' };
    char                   buf[128];
    actrust_json_builder_t b;

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_init(&b, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_INVALID_ARG,
        ACTRUST_ERR_CODE(actrust_json_builder_add_string(&b, "data", raw, 3u)));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG,
                      ACTRUST_ERR_CODE(actrust_json_builder_add_string(
                          &b, "data", "AB", 1u)));
}

void test_builder_nested_object(void)
{
    char                   buf[256];
    actrust_json_builder_t b;
    actrust_json_builder_init(&b, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_open_object(&b, "inner"));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_add_int(&b, "x", 1));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_close_object(&b));

    char  *json = NULL;
    size_t len  = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_finish(&b, &json, &len));

    actrust_json_view_t v;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_init(&v, json, len));
    actrust_json_value_t val;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_query(&v, "inner.x", &val));
}

void test_builder_add_object_non_object(void)
{
    char                   buf[256];
    actrust_json_builder_t b;
    actrust_json_builder_init(&b, buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_json_builder_add_object(
                                          &b, "k", "\"not an object\""));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_null_view);
    RUN_TEST(test_init_null_buf);
    RUN_TEST(test_init_malformed_json);
    RUN_TEST(test_init_empty_object);
    RUN_TEST(test_init_valid_nested);
    RUN_TEST(test_query_null_args);
    RUN_TEST(test_query_missing_key);
    RUN_TEST(test_query_nested_path);
    RUN_TEST(test_query_nested_missing_intermediate);
    RUN_TEST(test_get_string_success);
    RUN_TEST(test_get_string_buffer_too_small);
    RUN_TEST(test_get_string_unescapes_json_sequences);
    RUN_TEST(test_get_string_buffer_too_small_after_unescape);
    RUN_TEST(test_get_int_success);
    RUN_TEST(test_get_int_negative);
    RUN_TEST(test_get_int_rejects_overflow);
    RUN_TEST(test_get_bool_true);
    RUN_TEST(test_get_bool_false);
    RUN_TEST(test_escape_null_input);
    RUN_TEST(test_escape_buffer_too_small);
    RUN_TEST(test_unescape_simple);
    RUN_TEST(test_unescape_invalid_escape);
    RUN_TEST(test_unescape_utf8_2byte);
    RUN_TEST(test_unescape_utf8_3byte);
    RUN_TEST(test_unescape_surrogate_pair);
    RUN_TEST(test_unescape_lone_high_surrogate);
    RUN_TEST(test_unescape_lone_low_surrogate);
    RUN_TEST(test_builder_null_args);
    RUN_TEST(test_builder_tiny_buffer);
    RUN_TEST(test_builder_roundtrip);
    RUN_TEST(test_builder_string_rejects_len_mismatch);
    RUN_TEST(test_builder_nested_object);
    RUN_TEST(test_builder_add_object_non_object);
    return UNITY_END();
}
