#include "ccs811.h"
#include "iot_logger.h"

#define CCS811_ADDR       0x5A

#define CCS811_REG_STATUS 0x00   /* 状态：bit3 数据就绪 */
#define CCS811_REG_RESULT 0x02   /* 算法结果：CO2(2) + TVOC(2) + ... */
#define CCS811_REG_HW_ID  0x20   /* 硬件 id，应为 0x81 */
#define CCS811_REG_APP    0xF4   /* APP 控制寄存器 */

#define CCS811_HW_ID      0x81
#define CCS811_APP_START  0x01

#define CCS811_MASK_READY 0x08   /* 数据就绪位 */

int ccs811_init(const i2c_bus_t *bus)
{
    uint8_t id = 0;

    if (bus == NULL) {
        return -1;
    }
    if (i2c_read_reg(bus, CCS811_ADDR, CCS811_REG_HW_ID, &id) != 0 ||
        id != CCS811_HW_ID) {
        LOG_E("ccs811", "hw id fail 0x%02x", id);
        return -1;
    }
    /* 启动 APP：切换过程器件会 NACK，忽略返回值 */
    (void)i2c_write_reg(bus, CCS811_ADDR, CCS811_REG_APP, CCS811_APP_START);
    LOG_I("ccs811", "init ok");
    return 0;
}

int ccs811_read(const i2c_bus_t *bus, uint16_t *co2, uint16_t *tvoc)
{
    uint8_t st = 0;
    uint8_t d[8];

    if (bus == NULL || co2 == NULL || tvoc == NULL) {
        return -1;
    }
    if (i2c_read_reg(bus, CCS811_ADDR, CCS811_REG_STATUS, &st) != 0) {
        return -1;
    }
    if ((st & CCS811_MASK_READY) == 0) {
        LOG_D("ccs811", "data not ready");
        return -1;
    }
    /* 读 0x02 起 8 字节：CO2、TVOC、status、error 与原始电流/电压 */
    if (i2c_read_regs(bus, CCS811_ADDR, CCS811_REG_RESULT, d, 8) != 0) {
        return -1;
    }
    *co2 = (uint16_t)((d[0] << 8) | d[1]);
    *tvoc = (uint16_t)((d[2] << 8) | d[3]);
    return 0;
}