#include "iot_sensor_sds011.h"
#include "iot_logger.h"

#define SDS011_HEAD0  0xAA   /* 帧头第一字节 */
#define SDS011_HEAD1  0xC0   /* 帧头第二字节 */
#define SDS011_TAIL   0xAB   /* 帧尾字节 */
#define SDS011_FRAME  10     /* 完整帧长度 */
#define SDS011_CHK    8      /* 校验字节偏移：前 8 字节累加和的低 8 位 */

int iot_sds_parse(const uint8_t *frame, uint16_t len,
                  uint16_t *pm25, uint16_t *pm10)
{
    uint8_t sum = 0;
    int i;

    if (frame == NULL || pm25 == NULL || pm10 == NULL || len < SDS011_FRAME) {
        return -1;
    }
    if (frame[0] != SDS011_HEAD0 || frame[1] != SDS011_HEAD1 ||
        frame[9] != SDS011_TAIL) {
        LOG_D("sds011", "bad frame");
        return -1;
    }
    for (i = 0; i < SDS011_CHK; i++) {
        sum = (uint8_t)(sum + frame[i]);
    }
    if (sum != frame[SDS011_CHK]) {
        LOG_D("sds011", "bad checksum");
        return -1;
    }
    /* 数据段从 frame[2] 起：pm25 = data[2..3]、pm10 = data[4..5]，均为小端。 */
    *pm25 = (uint16_t)(frame[4] | (frame[5] << 8));
    *pm10 = (uint16_t)(frame[6] | (frame[7] << 8));
    return 0;
}