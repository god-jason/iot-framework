#include "iot_sensor_pms5003.h"
#include "iot_logger.h"

#define PMS5003_HEAD0  0x42   /* 帧头第一字节 */
#define PMS5003_HEAD1  0x4D   /* 帧头第二字节 */
#define PMS5003_FRAME  32     /* 完整帧长度 */
#define PMS5003_CHK    30     /* 校验和偏移：帧末 2 字节，等于前 30 字节累加 */

int iot_pms_parse(const uint8_t *frame, uint16_t len,
                  uint16_t *pm1, uint16_t *pm25, uint16_t *pm10)
{
    uint16_t sum = 0;
    uint16_t chk;
    int i;

    if (frame == NULL || pm1 == NULL || pm25 == NULL || pm10 == NULL ||
        len < PMS5003_FRAME) {
        return -1;
    }
    if (frame[0] != PMS5003_HEAD0 || frame[1] != PMS5003_HEAD1) {
        LOG_D("pms5003", "bad header");
        return -1;
    }
    for (i = 0; i < PMS5003_CHK; i++) {
        sum = (uint16_t)(sum + frame[i]);
    }
    chk = (uint16_t)((frame[PMS5003_CHK] << 8) | frame[PMS5003_CHK + 1]);
    if (sum != chk) {
        LOG_D("pms5003", "bad checksum");
        return -1;
    }
    /* PM1.0 / PM2.5 / PM10 均为大端 16 位。 */
    *pm1  = (uint16_t)((frame[10] << 8) | frame[11]);
    *pm25 = (uint16_t)((frame[12] << 8) | frame[13]);
    *pm10 = (uint16_t)((frame[14] << 8) | frame[15]);
    return 0;
}