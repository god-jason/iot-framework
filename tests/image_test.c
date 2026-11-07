/* boot/image.c per-file 单元测试。
 * 说明：image_verify 只校验头部字段，不校验 sha256/签名内容（源码未实现），
 * 因此这里不针对签名做断言。 */
#include "test_framework.h"
#include "image.h"

#include <string.h>

static void make_header(image_header_t *h)
{
    memset(h, 0, sizeof(*h));
    h->magic = IMAGE_MAGIC;
    h->header_size = (uint32_t)sizeof(*h);
    h->product_id = 0x11223344u;
    h->hardware_id = 0x55667788u;
    h->version = 0x01020304u;
    h->image_size = 0x1000u;
    h->flags = 0x01u;
}

void ut_image(void)
{
    image_header_t header;
    image_header_t parsed;
    uint8_t raw[sizeof(image_header_t) + 8];
    image_state_t state;

    tf_begin("image 解析参数校验");
    make_header(&header);
    memset(raw, 0, sizeof(raw));
    memcpy(raw, &header, sizeof(header));

    TF_CHECK_INT(image_parse(NULL, sizeof(raw), &parsed), -1);
    TF_CHECK_INT(image_parse(raw, sizeof(raw), NULL), -1);
    TF_CHECK_INT(image_parse(raw, (uint32_t)sizeof(header) - 1u, &parsed), -1);
    TF_CHECK_INT(image_parse(raw, 0, &parsed), -1);
    tf_end();

    tf_begin("image 正常解析与字段拷贝");
    memset(&parsed, 0, sizeof(parsed));
    TF_CHECK_INT(image_parse(raw, (uint32_t)sizeof(header), &parsed), IOT_OK);
    TF_CHECK_INT(parsed.magic, IMAGE_MAGIC);
    TF_CHECK_INT(parsed.header_size, sizeof(image_header_t));
    TF_CHECK_INT(parsed.product_id, 0x11223344u);
    TF_CHECK_INT(parsed.hardware_id, 0x55667788u);
    TF_CHECK_INT(parsed.version, 0x01020304u);
    TF_CHECK_INT(parsed.image_size, 0x1000u);

    /* 长度大于头长度时仍只取头部 */
    memset(&parsed, 0, sizeof(parsed));
    TF_CHECK_INT(image_parse(raw, (uint32_t)sizeof(raw), &parsed), IOT_OK);
    TF_CHECK_INT(parsed.magic, IMAGE_MAGIC);

    /* 解析是拷贝语义：修改源缓冲不影响结果 */
    raw[0] ^= 0xFFu;
    TF_CHECK_INT(parsed.magic, IMAGE_MAGIC);
    tf_end();

    tf_begin("image 校验错误路径");
    make_header(&header);
    TF_CHECK_INT(image_verify(NULL), -1);

    header.magic = 0x00000000u;
    TF_CHECK_INT(image_verify(&header), -1);
    TF_CHECK_INT(image_get_state(&state), IOT_OK);
    TF_CHECK_INT(state, IMAGE_INVALID);

    make_header(&header);
    header.header_size = 0;
    TF_CHECK_INT(image_verify(&header), -1);

    make_header(&header);
    header.header_size = (uint32_t)sizeof(image_header_t) - 1u;
    TF_CHECK_INT(image_verify(&header), -1);

    make_header(&header);
    header.image_size = 0;
    TF_CHECK_INT(image_verify(&header), -1);
    tf_end();

    tf_begin("image 合法校验与状态切换");
    make_header(&header);
    TF_CHECK_INT(image_verify(&header), IOT_OK);
    TF_CHECK_INT(image_get_state(&state), IOT_OK);
    TF_CHECK_INT(state, IMAGE_VALID);

    /* header_size 恰好等于结构大小应通过 */
    header.header_size = (uint32_t)sizeof(image_header_t);
    TF_CHECK_INT(image_verify(&header), IOT_OK);
    TF_CHECK_INT(image_get_state(&state), IOT_OK);
    TF_CHECK_INT(state, IMAGE_VALID);

    /* 再次校验失败应回落到 INVALID */
    header.magic = IMAGE_MAGIC + 1u;
    TF_CHECK_INT(image_verify(&header), -1);
    TF_CHECK_INT(image_get_state(&state), IOT_OK);
    TF_CHECK_INT(state, IMAGE_INVALID);

    TF_CHECK_INT(image_get_state(NULL), -1);
    tf_end();
}
