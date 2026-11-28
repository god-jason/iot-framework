/*
 * strings_test.c - 通用字符串处理回归测试
 */

#include "test.h"
#include "ros.h"
#include "common.h"

void ut_strings(void)
{
    char buf[32];
    char line[] = "  ab\t\r\n";
    char csv[] = "a,b,,c";
    char csv2[] = "a,b,,c";
    char *items[4];
    int32_t iv;
    uint32_t uv;
    float fv;
    bool b;

    tf_begin("strings/通用字符串处理");

    /* 大小写不敏感比较 */
    TF_CHECK(str_icmp("On", "oN"));
    TF_CHECK(!str_icmp("on", "off"));
    TF_CHECK(str_icmp(NULL, NULL));
    TF_CHECK(!str_icmp(NULL, "x"));
    TF_CHECK(str_nicmp("ABCd", "abcd", 4U));
    TF_CHECK(str_nicmp("AB", "ab", 2U));
    TF_CHECK(!str_nicmp("ABC", "abd", 3U));

    /* 前后缀 / 数字判断 */
    TF_CHECK(str_starts_with("http://x", "http://"));
    TF_CHECK(!str_starts_with("http://x", "https://"));
    TF_CHECK(str_ends_with("a.bin", ".bin"));
    TF_CHECK(!str_ends_with("bin", ".bin"));
    TF_CHECK(str_is_digits("0123"));
    TF_CHECK(!str_is_digits("12a"));
    TF_CHECK(!str_is_digits(""));

    /* 安全拷贝 */
    memset(buf, 0x5A, sizeof(buf));
    TF_CHECK_INT((int)str_copy(buf, 8U, "abcdefghij"), 7);
    TF_CHECK(strcmp(buf, "abcdefg") == 0);
    TF_CHECK_INT((int)str_copy(NULL, 8U, "x"), 0);

    /* 裁剪 / 去行尾 */
    TF_CHECK_INT((int)str_trim(line), 2);
    TF_CHECK(strcmp(line, "ab") == 0);
    line[0] = ' ';
    line[1] = ' ';
    line[2] = '\0';
    TF_CHECK_INT((int)str_trim(line), 0);
    TF_CHECK(strcmp(buf, "abcdefg") == 0);      /* buf 不受影响 */
    TF_CHECK_INT((int)str_copy(buf, sizeof(buf), NULL), 0);
    TF_CHECK(buf[0] == '\0');
    str_copy(buf, sizeof(buf), "abc\r\n");
    TF_CHECK_INT((int)str_strip_eol(buf), 3);
    TF_CHECK(strcmp(buf, "abc") == 0);

    /* 大小写转换 */
    str_copy(buf, sizeof(buf), "aBc");
    TF_CHECK_INT((int)str_to_upper(buf), 3);
    TF_CHECK(strcmp(buf, "ABC") == 0);
    TF_CHECK_INT((int)str_to_lower(buf), 3);
    TF_CHECK(strcmp(buf, "abc") == 0);
    TF_CHECK_INT(str_upper_char('q'), 'Q');
    TF_CHECK_INT(str_lower_char('Q'), 'q');
    TF_CHECK_INT(str_upper_char('1'), '1');

    /* 跳过空白 */
    TF_CHECK(strcmp(str_skip_ws("  \tab"), "ab") == 0);
    TF_CHECK(str_skip_ws(NULL) == NULL);

    /* 数字解析 */
    TF_CHECK(str_parse_int("42", &iv) && iv == 42);
    TF_CHECK(str_parse_int("-7", &iv) && iv == -7);
    TF_CHECK(!str_parse_int("4x", &iv));
    TF_CHECK(!str_parse_int("", &iv));
    TF_CHECK(str_parse_uint("42", &uv) && uv == 42U);
    TF_CHECK(!str_parse_uint("-1", &uv));
    TF_CHECK(!str_parse_uint("4a", &uv));

    /* 浮点解析 */
    TF_CHECK(str_parse_float("42", &fv) && fv > 41.99f && fv < 42.01f);
    TF_CHECK(str_parse_float("-1.5", &fv) && fv > -1.51f && fv < -1.49f);
    TF_CHECK(str_parse_float("1e3", &fv) && fv > 999.9f && fv < 1000.1f);
    TF_CHECK(str_parse_float("1.5e-7", &fv) && fv > 0.0f && fv < 1.0f);
    TF_CHECK(str_parse_float(".5", &fv) && fv > 0.49f && fv < 0.51f);
    TF_CHECK(str_parse_float("1.", &fv) && fv > 0.99f && fv < 1.01f);
    TF_CHECK(!str_parse_float("1.5x", &fv));
    TF_CHECK(!str_parse_float("", &fv));
    TF_CHECK(!str_parse_float(NULL, &fv));
    TF_CHECK(!str_parse_float("1e39", &fv));
    TF_CHECK(!str_parse_float("1e-999", &fv));

    /* 布尔解析 */
    TF_CHECK(str_parse_bool("ON", &b) && b);
    TF_CHECK(str_parse_bool("0", &b) && !b);
    TF_CHECK(!str_parse_bool("maybe", &b));

    /* 堆复制（调用方负责释放） */
    {
        char *dup = str_dup("hello");

        TF_CHECK(dup != NULL);
        if (dup != NULL) {
            TF_CHECK(strcmp(dup, "hello") == 0);
            ros_mem_free(dup);
        }
        TF_CHECK(str_dup(NULL) == NULL);
    }

    /* 原地切分 */
    TF_CHECK_INT((int)str_split(csv, ',', items, 4U), 3);
    TF_CHECK(strcmp(items[0], "a") == 0);
    TF_CHECK(strcmp(items[1], "b") == 0);
    TF_CHECK(strcmp(items[2], "c") == 0);
    TF_CHECK_INT((int)str_split(csv2, ',', items, 2U), 2);
    TF_CHECK(strcmp(items[0], "a") == 0);
    TF_CHECK(strcmp(items[1], "b") == 0);
    TF_CHECK_INT((int)str_split(csv, ',', NULL, 4U), 0);

    /* 格式化输出 */
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%d %u %x %X",
                            -42, 42U, 0xbeefU, 0xbeefU), 16);
    TF_CHECK(strcmp(buf, "-42 42 beef BEEF") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%04lX", 0x2aUL), 4);
    TF_CHECK(strcmp(buf, "002A") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%-5s|", "ab"), 6);
    TF_CHECK(strcmp(buf, "ab   |") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%02u", 7U), 2);
    TF_CHECK(strcmp(buf, "07") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%.*s", 3, "abcdef"), 3);
    TF_CHECK(strcmp(buf, "abc") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%.3d", 7), 3);
    TF_CHECK(strcmp(buf, "007") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%ld", -1234567890L), 11);
    TF_CHECK(strcmp(buf, "-1234567890") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%p", (void *)0x1234), 6);
    TF_CHECK(strcmp(buf, "0x1234") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%c%%", 'A'), 2);
    TF_CHECK(strcmp(buf, "A%") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%s", (char *)NULL), 6);
    TF_CHECK(strcmp(buf, "(null)") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%*s", -5, "ab"), 5);
    TF_CHECK(strcmp(buf, "ab   ") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), "%q"), 2);   /* 未知转换原样输出 */
    TF_CHECK(strcmp(buf, "%q") == 0);
    TF_CHECK_INT(str_printf(buf, sizeof(buf), ""), 0);
    TF_CHECK(buf[0] == '\0');

    /* 截断与 cap=0（C99 返回值语义） */
    {
        char small[5];
        int n = str_printf(small, sizeof(small), "%s", "abcdefgh");

        TF_CHECK_INT(n, 8);
        TF_CHECK(strcmp(small, "abcd") == 0);
        TF_CHECK_INT(str_printf(NULL, 0U, "%d", 1234), 4);
        TF_CHECK_INT(str_printf(small, 0U, "%s", "xyz"), 3);
    }

    tf_end();
}
