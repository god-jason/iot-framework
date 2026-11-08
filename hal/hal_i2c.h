#ifndef HAL_I2C_H
#define HAL_I2C_H

/*
 * I2C 主机/从机：总线速率、地址模式配置，主机收发、寄存器访问、
 * Repeated Start 组合传输、设备探测与总线恢复。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 HAL_INVALID_PARAM（查询类返回默认值）。
 * 地址参数按 7/10 位原始地址传入，是否需要左移由平台实现决定。
 */

#include "hal_def.h"
#include "hal_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 主从模式 */
typedef enum {
    HAL_I2C_MODE_MASTER = 0,   /* 主机 */
    HAL_I2C_MODE_SLAVE         /* 从机 */
} hal_i2c_mode_t;

/* 总线速率档位 */
typedef enum {
    HAL_I2C_SPEED_STANDARD = 0,/* 标准模式 100 kHz */
    HAL_I2C_SPEED_FAST,        /* 快速模式 400 kHz */
    HAL_I2C_SPEED_FAST_PLUS    /* 快速+ 1 MHz */
} hal_i2c_speed_t;

/* 地址宽度 */
typedef enum {
    HAL_I2C_ADDR_7BIT = 0,     /* 7 位地址 */
    HAL_I2C_ADDR_10BIT         /* 10 位地址 */
} hal_i2c_addr_mode_t;

/* I2C 配置 */
typedef struct {
    hal_i2c_mode_t mode;           /* 主从模式 */
    hal_i2c_speed_t speed;         /* 总线速率档位 */
    hal_i2c_addr_mode_t address_mode; /* 地址宽度 */

    uint16_t own_address;           /* 从机模式下的本机地址，主机模式忽略 */
} hal_i2c_config_t;

/* I2C 句柄 */
typedef struct hal_i2c {
    hal_handle_base_t base;        /* 通用句柄基类，勿移动位置 */

    hal_i2c_config_t config;       /* 初始化时保存的配置副本 */

    hal_dma_t *tx_dma;      /* 发送 DMA 通道，可为 NULL */
    hal_dma_t *rx_dma;      /* 接收 DMA 通道，可为 NULL */

    hal_callback_t callback;       /* 事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} hal_i2c_t;

/*
 * 初始化 I2C。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；从机模式需提供合法 own_address。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；速率不支持返回 HAL_NOT_SUPPORTED。
 */
hal_status_t hal_i2c_init(hal_i2c_t *handle, const hal_i2c_config_t *config);

/* 反初始化 I2C；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
hal_status_t hal_i2c_deinit(hal_i2c_t *handle);

/* 主机接口 */
/*
 * 主机发送。
 * address : 目标从机地址（按 address_mode 解释）。
 * data    : 发送缓冲区，length 为 0 时可为 NULL。
 * length  : 字节数。
 * timeout : 最大等待毫秒数。
 * 返回    : HAL_OK；HAL_INVALID_PARAM；HAL_TIMEOUT；从机无应答返回 HAL_IO_ERROR。
 */
hal_status_t hal_i2c_master_transmit(hal_i2c_t *handle, uint16_t address,
                                       const uint8_t *data, size_t length, uint32_t timeout);

/* 主机接收；data 缓冲区不可为 NULL，语义同 master_transmit */
hal_status_t hal_i2c_master_receive(hal_i2c_t *handle, uint16_t address,
                                      uint8_t *data, size_t length, uint32_t timeout);

/* 组合传输，支持 Repeated Start */
/*
 * 先发 tx 再收 rx，中间不产生停止位（Repeated Start）。
 * tx_data/tx_length : 发送段，可为 NULL/0；
 * rx_data/rx_length : 接收段，可为 NULL/0；
 * 返回              : HAL_OK；HAL_INVALID_PARAM（两段均为空）；HAL_TIMEOUT。
 */
hal_status_t hal_i2c_master_transfer(hal_i2c_t *handle, uint16_t address,
                                       const uint8_t *tx_data, size_t tx_length,
                                       uint8_t *rx_data, size_t rx_length, uint32_t timeout);

/* 寄存器访问 */
/*
 * 写寄存器：先发送寄存器地址（reg_size 字节），再发送数据。
 * reg           : 寄存器地址；
 * reg_size      : 寄存器地址字节数，仅支持 1/2/3/4；
 * data          : 待写数据，不可为 NULL；
 * 返回          : HAL_OK；HAL_INVALID_PARAM；HAL_TIMEOUT；HAL_IO_ERROR。
 */
hal_status_t hal_i2c_mem_write(hal_i2c_t *handle, uint16_t address,
                                 uint16_t reg, uint8_t reg_size,
                                 const uint8_t *data, size_t length, uint32_t timeout);

/* 读寄存器：先写寄存器地址再读数据；data 缓冲区不可为 NULL */
hal_status_t hal_i2c_mem_read(hal_i2c_t *handle, uint16_t address,
                                uint16_t reg, uint8_t reg_size,
                                uint8_t *data, size_t length, uint32_t timeout);

/* DMA */
/* DMA 主机发送；data 需在完成回调前保持有效 */
hal_status_t hal_i2c_master_transmit_dma(hal_i2c_t *handle, uint16_t address,
                                           const uint8_t *data, size_t length);

/* DMA 主机接收；data 需在完成回调前保持有效 */
hal_status_t hal_i2c_master_receive_dma(hal_i2c_t *handle, uint16_t address,
                                          uint8_t *data, size_t length);

/* 总线管理 */
/*
 * 探测从机是否应答。
 * trials  : 最大尝试次数，0 视为 1 次；
 * timeout : 每次尝试的最大等待毫秒数。
 * 返回    : HAL_OK 表示收到应答；HAL_TIMEOUT / HAL_IO_ERROR 表示无应答或失败。
 */
hal_status_t hal_i2c_is_device_ready(hal_i2c_t *handle, uint16_t address,
                                       uint32_t trials, uint32_t timeout);

/*
 * 总线恢复：主机在从机拉死 SDA 时通过时钟脉冲释放总线。
 * 返回 : HAL_OK 成功；HAL_INVALID_PARAM；平台无此能力返回 HAL_NOT_SUPPORTED。
 * 注意 : 会短暂接管引脚，调用前应确保没有进行中的传输。
 */
hal_status_t hal_i2c_bus_recover(hal_i2c_t *handle);

/* 中止当前传输；无传输时返回 HAL_OK */
hal_status_t hal_i2c_abort(hal_i2c_t *handle);

/* 查询当前状态；handle 为 NULL 时返回 HAL_STATE_RESET */
hal_state_t hal_i2c_get_state(hal_i2c_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* HAL_I2C_H */
