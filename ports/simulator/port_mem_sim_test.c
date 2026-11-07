/* 模拟器 port_mem 后端测试：malloc/calloc/realloc/free 基本语义。 */
#include "test.h"
#include "port_mem.h"

#include <stdint.h>
#include <string.h>

void ut_port_mem(void)
{
    uint8_t *p;
    uint8_t *q;
    size_t i;

    tf_begin("port_mem/分配与读写");
    p = (uint8_t *)port_malloc(32U);
    TF_CHECK(p != NULL);
    if (p != NULL) {
        memset(p, 0x5A, 32U);
        TF_CHECK_INT(p[0], 0x5A);
        TF_CHECK_INT(p[31], 0x5A);
        port_free(p);
    }
    port_free(NULL);   /* 空释放安全 */
    tf_end();

    tf_begin("port_mem/calloc 清零");
    p = (uint8_t *)port_calloc(4U, 8U);
    TF_CHECK(p != NULL);
    if (p != NULL) {
        int zero = 1;

        for (i = 0; i < 32U; i++) {
            if (p[i] != 0U) {
                zero = 0;
                break;
            }
        }
        TF_CHECK(zero);
        port_free(p);
    }
    tf_end();

    tf_begin("port_mem/realloc");
    p = (uint8_t *)port_malloc(8U);
    TF_CHECK(p != NULL);
    if (p != NULL) {
        memset(p, 0x11, 8U);
        q = (uint8_t *)port_realloc(p, 64U);
        TF_CHECK(q != NULL);
        if (q != NULL) {
            TF_CHECK_INT(q[0], 0x11);
            TF_CHECK_INT(q[7], 0x11);
            memset(q, 0x22, 64U);
            port_free(q);
        }
    }
    q = (uint8_t *)port_realloc(NULL, 16U);
    TF_CHECK(q != NULL);
    port_free(q);
    tf_end();

    tf_begin("port_mem/溢出保护");
    TF_CHECK(port_calloc((size_t)-1, 2U) == NULL);
    tf_end();
}
