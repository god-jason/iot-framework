/*
 * 驱动层测试：验证驱动与 src/port 抽象、simulator 的闭环。
 * 覆盖参数校验、I2C 寄存器注入、显存操作与 SPI 初始化路径。
 */
#include "test_framework.h"

#include "port.h"
#include "common/driver_io.h"
#include "aht20/aht20.h"
#include "bmp280/bmp280.h"
#include "bmp280/bmp280_register.h"
#include "pcf8574/pcf8574.h"
#include "ssd1306/ssd1306.h"
#include "ssd1306/ssd1306_register.h"
#include "max7219/max7219.h"

static port_i2c_handle_t s_i2c;
static port_spi_handle_t s_spi;

static int drivers_port_setup(void)
{
    port_i2c_config_t i2c_config;
    port_spi_config_t spi_config;

    i2c_config.mode = PORT_I2C_MODE_MASTER;
    i2c_config.speed = PORT_I2C_SPEED_FAST;
    i2c_config.address_mode = PORT_I2C_ADDR_7BIT;
    i2c_config.own_address = 0U;

    spi_config.mode = PORT_SPI_MODE_MASTER;
    spi_config.direction = PORT_SPI_DIRECTION_2LINES;
    spi_config.baudrate = 1000000U;
    spi_config.cpol = PORT_SPI_CPOL_LOW;
    spi_config.cpha = PORT_SPI_CPHA_1EDGE;
    spi_config.bit_order = PORT_SPI_BITORDER_MSB;
    spi_config.data_bits = 8U;

    if (port_i2c_init(&s_i2c, &i2c_config) != PORT_OK) {
        return -1;
    }
    if (port_spi_init(&s_spi, &spi_config) != PORT_OK) {
        return -1;
    }
    return 0;
}

static void drivers_inject_bmp280(void)
{
    uint8_t chip_id = 0x58U;
    uint8_t calib[24];
    uint16_t i;

    /* t1 = 27504, t2 = 26435, t3 = -1000, p1 = 36477 ... 构造一组非零校准值 */
    for (i = 0U; i < sizeof(calib); i++) {
        calib[i] = (uint8_t)(i + 1U);
    }
    calib[0] = 0x30U; calib[1] = 0x6BU;   /* t1 = 0x6B30 */
    calib[2] = 0x63U; calib[3] = 0x67U;   /* t2 */
    calib[4] = 0x18U; calib[5] = 0xFCU;   /* t3 */
    calib[6] = 0x3DU; calib[7] = 0x8EU;   /* p1 */
    calib[8] = 0x1EU; calib[9] = 0x92U;   /* p2 */

    (void)port_i2c_mem_write(&s_i2c, BMP280_DEFAULT_ADDRESS, BMP280_REG_CHIP_ID, 1U,
                             &chip_id, 1U, 100U);
    (void)port_i2c_mem_write(&s_i2c, BMP280_DEFAULT_ADDRESS, BMP280_REG_CALIB_00, 1U,
                             calib, sizeof(calib), 100U);
}

void ut_drivers(void)
{
    tf_begin("drivers");
    TF_CHECK_INT(drivers_port_setup(), 0);

    /* ---- 参数校验：空句柄/空总线必须被拒绝 ---- */
    TF_CHECK_INT(aht20_init(NULL, &s_i2c, 0x38U), PORT_INVALID_PARAM);
    TF_CHECK_INT(bmp280_init(NULL, &s_i2c, 0x76U), PORT_INVALID_PARAM);
    TF_CHECK_INT(pcf8574_init(NULL, &s_i2c, 0x20U), PORT_INVALID_PARAM);
    TF_CHECK_INT(ssd1306_init(NULL, NULL), PORT_INVALID_PARAM);

    /* ---- 数据格式工具 ---- */
    {
        const uint8_t be[3] = { 0x01U, 0x02U, 0x03U };
        const uint8_t le[2] = { 0x34U, 0x12U };
        const uint8_t neg[3] = { 0xFFU, 0xFFU, 0xFEU };

        TF_CHECK_INT(driver_be16(be), 0x0102);
        TF_CHECK_INT(driver_le16(le), 0x1234);
        TF_CHECK_INT(driver_be24(be), 0x010203);
        TF_CHECK_INT(driver_be24(neg), -2);
    }

    /* ---- pcf8574：写后读回应与影子寄存器一致 ---- */
    {
        pcf8574_handle_t pcf;
        uint8_t value = 0U;

        TF_CHECK_INT(pcf8574_init(&pcf, &s_i2c, PCF8574_DEFAULT_ADDRESS), DRIVER_OK);
        TF_CHECK_INT(pcf8574_write(&pcf, 0x5AU), DRIVER_OK);
        TF_CHECK_INT(pcf.output_latch, 0x5AU);
        TF_CHECK_INT(pcf8574_read(&pcf, &value), DRIVER_OK);
    }

    /* ---- bmp280：注入 chip id 与校准后应初始化成功 ---- */
    {
        bmp280_handle_t bmp;
        uint8_t chip_id = 0U;

        memset(&bmp, 0, sizeof(bmp));
        drivers_inject_bmp280();
        TF_CHECK_INT(bmp280_get_chip_id(&bmp, &chip_id), PORT_INVALID_PARAM);
        TF_CHECK_INT(bmp280_init(&bmp, &s_i2c, BMP280_DEFAULT_ADDRESS), DRIVER_OK);
        TF_CHECK_INT(bmp280_get_chip_id(&bmp, &chip_id), DRIVER_OK);
        TF_CHECK_INT(chip_id, 0x58);
        TF_CHECK_INT(bmp.calib.t1, 0x6B30);
        TF_CHECK_INT(bmp.calib.p1, 0x8E3D);
    }

    /* ---- ssd1306：显存绘制与清屏 ---- */
    {
        static uint8_t gram[SSD1306_GRAM_SIZE];
        ssd1306_handle_t oled;
        ssd1306_config_t cfg;

        memset(&cfg, 0, sizeof(cfg));
        cfg.bus_type = SSD1306_BUS_I2C;
        cfg.i2c.handle = &s_i2c;
        cfg.i2c.address = SSD1306_DEFAULT_I2C_ADDRESS;
        cfg.i2c.timeout_ms = 100U;
        cfg.gram = gram;

        TF_CHECK_INT(ssd1306_init(&oled, &cfg), DRIVER_OK);
        TF_CHECK_INT(ssd1306_draw_pixel(&oled, 0U, 0U, true), DRIVER_OK);
        TF_CHECK_INT(gram[0], 0x01);
        TF_CHECK_INT(ssd1306_draw_pixel(&oled, 127U, 63U, true), DRIVER_OK);
        TF_CHECK_INT(gram[SSD1306_GRAM_SIZE - 1U], 0x80);
        TF_CHECK_INT(ssd1306_draw_pixel(&oled, 128U, 0U, true), PORT_INVALID_PARAM);
        TF_CHECK_INT(ssd1306_clear(&oled), DRIVER_OK);
        TF_CHECK_INT(gram[0], 0x00);
    }

    /* ---- max7219：SPI 路径初始化 ---- */
    {
        max7219_handle_t led;

        TF_CHECK_INT(max7219_init(&led, &s_spi, 0U, 2U), DRIVER_OK);
        TF_CHECK_INT(max7219_set_intensity(&led, 0x0FU), DRIVER_OK);
        TF_CHECK_INT(max7219_set_digit(&led, 8U, 0x01U), PORT_INVALID_PARAM);
        TF_CHECK_INT(max7219_clear(&led), DRIVER_OK);
        TF_CHECK_INT(max7219_deinit(&led), DRIVER_OK);
    }

    tf_end();
}
