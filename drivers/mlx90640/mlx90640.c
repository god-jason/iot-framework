#include "mlx90640.h"
#include "mlx90640_register.h"
#include "log.h"

static int mlx90640_write_reg(mlx90640_handle_t *handle, uint16_t reg, uint16_t value)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(value >> 8);
    buf[1] = (uint8_t)(value & 0xFFU);
    return (int)driver_i2c_write_reg16(&handle->i2c, reg, buf, sizeof(buf));
}

static int mlx90640_read_reg(mlx90640_handle_t *handle, uint16_t reg, uint16_t *value)
{
    uint8_t buf[2];
    int ret;

    ret = (int)driver_i2c_read_reg16(&handle->i2c, reg, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }
    *value = (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
    return DRIVER_OK;
}

/* RAM 只支持块读（地址在块内自动递增），按 16 字分块搬运。
 * 注意：MLX90640 的寄存器地址是"字地址"，一个字占一个地址，步进为 1。 */
static int mlx90640_read_words(mlx90640_handle_t *handle, uint16_t ram_addr,
                               int16_t *dst, size_t words)
{
    uint8_t buf[MLX90640_CHUNK_WORDS * 2U];
    size_t index = 0U;

    while (index < words) {
        size_t chunk = words - index;
        size_t i;
        int ret;

        if (chunk > MLX90640_CHUNK_WORDS) {
            chunk = MLX90640_CHUNK_WORDS;
        }
        ret = (int)driver_i2c_read_reg16(&handle->i2c,
                                         (uint16_t)(ram_addr + (uint16_t)index),
                                         buf, chunk * 2U);
        if (ret != DRIVER_OK) {
            return ret;
        }
        for (i = 0U; i < chunk; i++) {
            dst[index + i] = (int16_t)(uint16_t)(((uint16_t)buf[i * 2U] << 8) |
                                                 (uint16_t)buf[i * 2U + 1U]);
        }
        index += chunk;
    }
    return DRIVER_OK;
}

/* 更新子页号并清除标志，使下一帧可以被缓存 */
static int mlx90640_update_subpage(mlx90640_handle_t *handle)
{
    uint16_t status = 0U;
    int ret;

    ret = mlx90640_read_reg(handle, MLX90640_REG_STATUS, &status);
    if (ret != DRIVER_OK) {
        return ret;
    }

    handle->subpage = ((status & MLX90640_STATUS_SUBPAGE) != 0U) ? 1U : 0U;
    if ((status & MLX90640_STATUS_ERROR_MASK) != 0U) {
        LOG_W("mlx90640", "status 0x%04X reports error", (unsigned)status);
    }
    return mlx90640_write_reg(handle, MLX90640_REG_STATUS, MLX90640_STATUS_SUBPAGE_CLR);
}

static float mlx90640_raw_to_celsius(int16_t raw)
{
    /* 未校准线性近似，系数与限制见 mlx90640_register.h / mlx90640.h */
    return ((float)((uint16_t)raw >> MLX90640_RAW_SHIFT) * MLX90640_K_PER_LSB) -
           MLX90640_KELVIN_OFFSET;
}

int mlx90640_init(mlx90640_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint16_t control2 = 0U;
    uint16_t check = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : MLX90640_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->frame_period_ms = MLX90640_FRAME_PERIOD_8HZ_MS;
    handle->subpage = 0U;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("mlx90640", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    /* 只改刷新率（CONTROL2[2:0]），分辨率与 Chess 模式保持出厂值 */
    ret = mlx90640_read_reg(handle, MLX90640_REG_CONTROL2, &control2);
    if (ret != DRIVER_OK) {
        return ret;
    }
    control2 = (uint16_t)((control2 & (uint16_t)~MLX90640_CONTROL2_RATE_MASK) |
                          MLX90640_REFRESH_RATE_8HZ);
    ret = mlx90640_write_reg(handle, MLX90640_REG_CONTROL2, control2);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = mlx90640_read_reg(handle, MLX90640_REG_CONTROL2, &check);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if ((check & MLX90640_CONTROL2_RATE_MASK) != MLX90640_REFRESH_RATE_8HZ) {
        LOG_E("mlx90640", "refresh rate readback 0x%04X", (unsigned)check);
        return (int)PORT_HW_ERROR;
    }

    /* 上电后器件自由运行，等两帧以上让 RAM 内容稳定 */
    driver_delay_ms(MLX90640_WARMUP_MS);
    handle->initialized = true;
    LOG_I("mlx90640", "init ok at 0x%02X, 8Hz, subpage=%u",
          (unsigned)handle->i2c.address, (unsigned)handle->subpage);
    return DRIVER_OK;
}

int mlx90640_deinit(mlx90640_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int mlx90640_get_status(mlx90640_handle_t *handle, uint16_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return mlx90640_read_reg(handle, MLX90640_REG_STATUS, status);
}

int mlx90640_read_raw(mlx90640_handle_t *handle, int16_t *raw, size_t len)
{
    int ret;

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (len == 0U || len > MLX90640_PIXELS) {
        return (int)PORT_INVALID_PARAM;
    }

    driver_delay_ms(handle->frame_period_ms);
    (void)mlx90640_update_subpage(handle);

    ret = mlx90640_read_words(handle, MLX90640_REG_RAM_START, raw, len);
    if (ret != DRIVER_OK) {
        LOG_E("mlx90640", "ram read failed: %d", ret);
        return ret;
    }
    return DRIVER_OK;
}

int mlx90640_read(mlx90640_handle_t *handle, float *temperatures, size_t len)
{
    int16_t chunk[MLX90640_CHUNK_WORDS];
    size_t index = 0U;
    int ret;

    if (handle == NULL || temperatures == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (len == 0U || len > MLX90640_PIXELS) {
        return (int)PORT_INVALID_PARAM;
    }

    driver_delay_ms(handle->frame_period_ms);
    (void)mlx90640_update_subpage(handle);

    /* 分块读取并就地转换，避免在栈上申请整帧缓冲区 */
    while (index < len) {
        size_t count = len - index;
        size_t i;

        if (count > MLX90640_CHUNK_WORDS) {
            count = MLX90640_CHUNK_WORDS;
        }
        ret = mlx90640_read_words(handle,
                                  (uint16_t)(MLX90640_REG_RAM_START + (uint16_t)index),
                                  chunk, count);
        if (ret != DRIVER_OK) {
            LOG_E("mlx90640", "ram read failed: %d", ret);
            return ret;
        }
        for (i = 0U; i < count; i++) {
            temperatures[index + i] = mlx90640_raw_to_celsius(chunk[i]);
        }
        index += count;
    }
    return DRIVER_OK;
}
