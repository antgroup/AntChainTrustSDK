// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* JSON */
#include "json/json.h"

static const char *json_str =
    "{\"wifi\":\"actrust_net\",\"retry\":3,\"enable\":true}";

void setUp(void)
{
}
void tearDown(void)
{
}

void test_json_parse(void)
{
    actrust_json_view_t view;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_init(&view, json_str, strlen(json_str)));

    actrust_json_value_t val;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_query(&view, "wifi", &val));
    TEST_ASSERT_EQUAL(ACTRUST_JSON_TYPE_STRING, val.type);

    char   str_buf[32];
    size_t len;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_get_string(
                                      &val, str_buf, sizeof(str_buf), &len));
    TEST_ASSERT_EQUAL_STRING("actrust_net", str_buf);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_query(&view, "retry", &val));
    TEST_ASSERT_EQUAL(ACTRUST_JSON_TYPE_NUMBER, val.type);

    int64_t int_val;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_get_int(&val, &int_val));
    TEST_ASSERT_EQUAL_INT64(3, int_val);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_query(&view, "enable", &val));
    TEST_ASSERT_EQUAL(ACTRUST_JSON_TYPE_BOOL, val.type);

    bool bool_val;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_get_bool(&val, &bool_val));
    TEST_ASSERT_TRUE(bool_val);
}

void test_json_build(void)
{
    char                   buf[128];
    actrust_json_builder_t builder;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_init(&builder, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_add_string(
                                      &builder, "wifi", "actrust_net", 11u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_add_int(&builder, "retry", 3));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_add_bool(&builder, "enable", true));

    char  *out_json = NULL;
    size_t out_len  = 0;
    TEST_ASSERT_EQUAL(
        ACTRUST_OK, actrust_json_builder_finish(&builder, &out_json, &out_len));
    TEST_ASSERT_NOT_NULL(out_json);
    TEST_ASSERT_GREATER_THAN(0u, out_len);
    TEST_ASSERT_EQUAL_STRING(json_str, out_json);
}

void test_json_build_object(void)
{
    char                   parameters_buf[96];
    actrust_json_builder_t parameters_builder;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_init(
                                      &parameters_builder, parameters_buf,
                                      sizeof(parameters_buf)));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_add_string(&parameters_builder,
                                                      "productKey", "pk", 2u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_add_string(&parameters_builder,
                                                      "deviceName", "dn", 2u));
    char  *parameters_json = NULL;
    size_t parameters_len  = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_finish(
                                      &parameters_builder, &parameters_json,
                                      &parameters_len));

    char                   payload_buf[192];
    actrust_json_builder_t payload_builder;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_init(&payload_builder, payload_buf,
                                                sizeof(payload_buf)));
    TEST_ASSERT_EQUAL(
        ACTRUST_OK, actrust_json_builder_add_string(&payload_builder,
                                                    "certificateOwnershipToken",
                                                    "token", 5u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_add_object(
                          &payload_builder, "parameters", parameters_json));

    char  *payload_json = NULL;
    size_t payload_len  = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_finish(&payload_builder,
                                                  &payload_json, &payload_len));

    const char *expected =
        "{\"certificateOwnershipToken\":\"token\","
        "\"parameters\":{\"productKey\":\"pk\",\"deviceName\":\"dn\"}}";
    TEST_ASSERT_EQUAL_STRING(expected, payload_json);

    char                   bad_buf[64];
    actrust_json_builder_t bad_builder;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_init(
                                      &bad_builder, bad_buf, sizeof(bad_buf)));
    actrust_err_t err = actrust_json_builder_add_object(
        &bad_builder, "parameters", "\"not_object\"");
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
}

void test_json_build_nested(void)
{
    char   buf[192];
    char  *out     = NULL;
    size_t out_len = 0u;

    actrust_json_builder_t b1;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_init(&b1, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_open_object(&b1, "a"));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_add_int(&b1, "x", 1));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_close_object(&b1));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_finish(&b1, &out, &out_len));
    TEST_ASSERT_EQUAL_STRING("{\"a\":{\"x\":1}}", out);

    actrust_json_builder_t b2;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_init(&b2, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_add_string(&b2, "k", "v", 1u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_open_object(&b2, "nested"));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_add_int(&b2, "n", 2));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_close_object(&b2));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_finish(&b2, &out, &out_len));
    TEST_ASSERT_EQUAL_STRING("{\"k\":\"v\",\"nested\":{\"n\":2}}", out);

    actrust_json_builder_t b3;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_init(&b3, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_open_object(&b3, "a"));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_open_object(&b3, "b"));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_add_int(&b3, "c", 3));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_close_object(&b3));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_builder_close_object(&b3));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_builder_finish(&b3, &out, &out_len));
    TEST_ASSERT_EQUAL_STRING("{\"a\":{\"b\":{\"c\":3}}}", out);
}

void test_json_escape_unescape(void)
{
    const char *raw              = "line1\n\"actrust\"\\x\t";
    const char *expected_escaped = "line1\\n\\\"actrust\\\"\\\\x\\t";

    char   escaped[128];
    size_t escaped_len = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_escape_string(raw, escaped, sizeof(escaped),
                                                 &escaped_len));
    TEST_ASSERT_EQUAL_STRING(expected_escaped, escaped);
    TEST_ASSERT_EQUAL(strlen(expected_escaped), escaped_len);

    char   unescaped[128];
    size_t unescaped_len = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_json_unescape_string(
                                      escaped, escaped_len, unescaped,
                                      sizeof(unescaped), &unescaped_len));
    TEST_ASSERT_EQUAL_STRING(raw, unescaped);
    TEST_ASSERT_EQUAL(strlen(raw), unescaped_len);

    const char *unicode_seq = "A\\u0042C";
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_json_unescape_string(
                          unicode_seq, strlen(unicode_seq), unescaped,
                          sizeof(unescaped), &unescaped_len));
    TEST_ASSERT_EQUAL_STRING("ABC", unescaped);

    const char   *bad_escape = "bad\\x";
    actrust_err_t err =
        actrust_json_unescape_string(bad_escape, strlen(bad_escape), unescaped,
                                     sizeof(unescaped), &unescaped_len);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_json_parse);
    RUN_TEST(test_json_build);
    RUN_TEST(test_json_build_object);
    RUN_TEST(test_json_build_nested);
    RUN_TEST(test_json_escape_unescape);
    return UNITY_END();
}
