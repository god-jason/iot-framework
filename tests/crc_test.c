/* crypto/crc/crc.c per-file 单元测试。
 * 说明：crc 模块只有一次性接口，不提供流式 update，因此无法做“分段与一次性一致”的
 * 对比；此处通过标准校验值、空输入、反射/非反射变体与参数校验覆盖实现分支。 */
#include "test_framework.h"
#include "crc.h"

static void crc_config_init(crc_config_t *cfg, uint32_t poly, uint32_t init,
                            uint32_t xor_out, uint8_t width,
                            bool ref_in, bool ref_out)
{
    cfg->polynomial = poly;
    cfg->initial = init;
    cfg->xor_out = xor_out;
    cfg->width = width;
    cfg->reflect_in = ref_in;
    cfg->reflect_out = ref_out;
}

void ut_crc(void)
{
    static const char *s = "123456789";
    crc_config_t cfg;

    tf_begin("crc 标准校验值");
    TF_CHECK_INT(crc8(s, 9), 0xF4u);
    TF_CHECK_INT(crc16_modbus(s, 9), 0x4B37u);
    TF_CHECK_INT(crc16_ccitt(s, 9), 0x29B1u);
    TF_CHECK_INT(crc32(s, 9), 0xCBF43926u);
    tf_end();

    tf_begin("crc 空输入与单字节");
    TF_CHECK_INT(crc8("", 0), 0x00u);
    TF_CHECK_INT(crc16_modbus("", 0), 0xFFFFu);
    TF_CHECK_INT(crc16_ccitt("", 0), 0xFFFFu);
    TF_CHECK_INT(crc32("", 0), 0x00000000u);

    /* data == NULL 且 len == 0 合法 */
    TF_CHECK_INT(crc8(NULL, 0), 0x00u);
    TF_CHECK_INT(crc16_modbus(NULL, 0), 0xFFFFu);
    TF_CHECK_INT(crc16_ccitt(NULL, 0), 0xFFFFu);
    TF_CHECK_INT(crc32(NULL, 0), 0x00000000u);

    TF_CHECK_INT(crc32("a", 1), 0xE8B7BE43u);
    tf_end();

    tf_begin("crc 通用接口与命名接口一致性");
    crc_config_init(&cfg, 0x07u, 0x00u, 0x00u, 8, false, false);
    TF_CHECK_INT(crc_calculate(&cfg, s, 9), crc8(s, 9));

    crc_config_init(&cfg, 0x8005u, 0xFFFFu, 0x0000u, 16, true, false);
    TF_CHECK_INT(crc_calculate(&cfg, s, 9), crc16_modbus(s, 9));

    crc_config_init(&cfg, 0x1021u, 0xFFFFu, 0x0000u, 16, false, false);
    TF_CHECK_INT(crc_calculate(&cfg, s, 9), crc16_ccitt(s, 9));

    crc_config_init(&cfg, 0x04C11DB7u, 0xFFFFFFFFu, 0xFFFFFFFFu, 32, true, false);
    TF_CHECK_INT(crc_calculate(&cfg, s, 9), crc32(s, 9));
    tf_end();

    tf_begin("crc 32 位非反射与 xor_out");
    /* CRC-32/MPEG-2 */
    crc_config_init(&cfg, 0x04C11DB7u, 0xFFFFFFFFu, 0x00000000u, 32, false, false);
    TF_CHECK_INT(crc_calculate(&cfg, s, 9), 0x0376E6E7u);

    /* CRC-32/BZIP2 */
    crc_config_init(&cfg, 0x04C11DB7u, 0xFFFFFFFFu, 0xFFFFFFFFu, 32, false, false);
    TF_CHECK_INT(crc_calculate(&cfg, s, 9), 0xFC891918u);

    /* xor_out 直接作用于结果 */
    crc_config_init(&cfg, 0x1021u, 0xFFFFu, 0xFFFFu, 16, false, false);
    TF_CHECK_INT(crc_calculate(&cfg, s, 9), (0x29B1u ^ 0xFFFFu));
    tf_end();

    tf_begin("crc reflect_out 分支");
    /* MODBUS 结果 0x4B37 再按 16 位反射 => 0xECD2 */
    crc_config_init(&cfg, 0x8005u, 0xFFFFu, 0x0000u, 16, true, true);
    TF_CHECK_INT(crc_calculate(&cfg, s, 9), 0xECD2u);

    /* 反射与非反射对同一数据结果应不同 */
    crc_config_init(&cfg, 0x8005u, 0xFFFFu, 0x0000u, 16, false, false);
    TF_CHECK(crc_calculate(&cfg, s, 9) != crc16_modbus(s, 9));
    tf_end();

    tf_begin("crc 参数错误路径");
    crc_config_init(&cfg, 0x1021u, 0xFFFFu, 0x0000u, 16, false, false);
    TF_CHECK_INT(crc_calculate(NULL, s, 9), 0u);
    TF_CHECK_INT(crc_calculate(&cfg, NULL, 9), 0u);
    TF_CHECK_INT(crc_calculate(&cfg, NULL, 0), 0xFFFFu);   /* len 0 时允许 NULL */

    cfg.width = 0;
    TF_CHECK_INT(crc_calculate(&cfg, s, 9), 0u);
    cfg.width = 33;
    TF_CHECK_INT(crc_calculate(&cfg, s, 9), 0u);
    cfg.width = 16;
    cfg.reflect_in = false;
    cfg.reflect_out = false;
    TF_CHECK_INT(crc_calculate(&cfg, s, 1), crc16_ccitt(s, 1));
    tf_end();

    tf_begin("crc 长度边界");
    {
        uint8_t buf[4] = { 0x00, 0xFF, 0x80, 0x01 };
        TF_CHECK_INT(crc32(buf, 0), 0u);
        TF_CHECK_INT(crc8(buf, 1), crc8(buf, 1));
        TF_CHECK(crc32(buf, 4) == crc32(buf, 4));
        TF_CHECK(crc32(buf, 4) != crc32(buf, 3));
    }
    tf_end();
}
