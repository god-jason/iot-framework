/*
 * image_test.c - 镜像头测试（对应 image.c）
 *
 * 覆盖：解析（长度不足/参数非法）、魔数校验、header_size 校验、
 *       零镜像大小拒绝、状态查询。
 */

#include "test.h"
#include "image.h"

void ut_image(void)
{
    image_header_t header;
    image_state_t state = IMAGE_INVALID;
    uint8_t raw[sizeof(image_header_t)];

    tf_begin("image/解析");

    TF_CHECK_INT(image_parse(NULL, sizeof(raw), &header), -1);
    TF_CHECK_INT(image_parse(raw, sizeof(raw), NULL), -1);
    TF_CHECK_INT(image_parse(raw, sizeof(raw) - 1U, &header), -1);
    TF_CHECK_INT(image_parse(raw, sizeof(raw), &header), 0);

    tf_end();

    tf_begin("image/校验");

    memset(&header, 0, sizeof(header));
    TF_CHECK_INT(image_verify(NULL), -1);

    TF_CHECK_INT(image_verify(&header), -1);        /* magic 错误 */
    TF_CHECK_INT(image_get_state(&state), 0);
    TF_CHECK_INT(state, IMAGE_INVALID);

    header.magic = IMAGE_MAGIC;
    header.header_size = (uint32_t)sizeof(image_header_t) - 1U;
    TF_CHECK_INT(image_verify(&header), -1);        /* header_size 过小 */

    header.header_size = (uint32_t)sizeof(image_header_t);
    header.image_size = 0U;
    TF_CHECK_INT(image_verify(&header), -1);        /* 空镜像 */

    header.image_size = 0x4000U;
    TF_CHECK_INT(image_verify(&header), 0);

    TF_CHECK_INT(image_get_state(&state), 0);
    TF_CHECK_INT(state, IMAGE_VALID);

    TF_CHECK_INT(image_get_state(NULL), -1);

    tf_end();
}
