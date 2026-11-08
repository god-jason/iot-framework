/* motion_source.c 单元测试：字符串数据源的逐行读取与定位。 */
#include "test.h"
#include "motion_source.h"

void ut_motion_source(void)
{
    motion_source_t src;
    char line[32];
    uint32_t offset = 0U;

    tf_begin("motion_source/字符串源逐行读取");
    motion_source_string_init(&src, "\xEF\xBB\xBFSTEP 0 ON\r\n\nTAIL");
    TF_CHECK_INT(src.open(&src, NULL), 0);
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 1);
    TF_CHECK_STR(line, "STEP 0 ON");
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 1);
    TF_CHECK_STR(line, "");
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 1);
    TF_CHECK_STR(line, "TAIL");
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 0);
    TF_CHECK_INT(src.close(&src), 0);
    tf_end();

    tf_begin("motion_source/定位与异常参数");
    motion_source_string_init(&src, "AAA\nBBB\n");
    TF_CHECK_INT(src.open(&src, NULL), 0);
    TF_CHECK_INT(src.tell(&src, &offset), 0);
    TF_CHECK_INT((int)offset, 0);
    TF_CHECK_INT(src.seek(&src, 4U), 0);
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 1);
    TF_CHECK_STR(line, "BBB");
    TF_CHECK_INT(src.tell(&src, &offset), 0);
    TF_CHECK_INT((int)offset, 8);
    TF_CHECK_INT(src.seek(&src, 99U), -1);
    TF_CHECK_INT(src.tell(&src, NULL), -1);
    TF_CHECK_INT(src.read_line(&src, NULL, sizeof(line)), -1);
    TF_CHECK_INT(src.read_line(&src, line, 0U), -1);
    tf_end();

    tf_begin("motion_source/超长行与空源");
    {
        static char text[160];

        memset(text, 'X', 99U);
        text[99] = '\n';
        memcpy(&text[100], "OK\n", 4U);
        motion_source_string_init(&src, text);
        TF_CHECK_INT(src.open(&src, NULL), 0);
        TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), -2);
        TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 1);
        TF_CHECK_STR(line, "OK");
    }
    motion_source_string_init(&src, NULL);
    TF_CHECK_INT((int)src.length, 0);
    TF_CHECK_INT(src.open(&src, NULL), -1);
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 0);
    tf_end();
}
