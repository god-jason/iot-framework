#ifndef IOT_QSPI_H
#define IOT_QSPI_H

/*
 * QSPI / 四线 SPI：外部 Flash 时钟与容量配置、命令帧下发、
 * 直接数据收发以及内存映射（XIP）模式切换。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 IOT_INVALID_PARAM。
 * DMA 传输期间 data 缓冲区必须保持有效。
 */

#include "iot_def.h"
#include "iot_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* QSPI 控制器配置 */
typedef struct {
    uint32_t clock_frequency;   /* 时钟频率（Hz） */
    uint8_t flash_size;         /* Flash 容量编码（2^n 字节的 n，平台约定） */
    uint8_t clock_mode;         /* 时钟模式：0 = 模式 0，1 = 模式 3 */
    bool dual_flash;            /* true 表示双片并联 */
} iot_qspi_config_t;

/* QSPI 命令帧描述 */
typedef struct {
    uint8_t instruction;        /* 指令码 */
    uint8_t address_size;       /* 地址字节数，0 表示无地址阶段 */
    uint8_t data_lines;         /* 数据阶段线数：1/2/4 */
    uint8_t address_lines;      /* 地址阶段线数：1/2/4 */
    uint32_t address;           /* 地址值 */
    size_t data_length;         /* 数据阶段字节数 */
    uint8_t dummy_cycles;       /* 地址与数据之间的空周期数 */
} iot_qspi_command_t;

/* QSPI 句柄 */
typedef struct iot_qspi {
    iot_handle_base_t base;    /* 通用句柄基类，勿移动位置 */
    iot_qspi_config_t config;  /* 初始化时保存的配置副本 */
    iot_dma_t *dma;     /* 关联 DMA 通道，可为 NULL */
    iot_callback_t callback;   /* 事件回调，可为 NULL */
    void *user_data;            /* 回调上下文，可为 NULL */
} iot_qspi_t;

/*
 * 初始化 QSPI。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；频率/线数不支持返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_qspi_init(iot_qspi_t *handle, const iot_qspi_config_t *config);

/* 反初始化 QSPI；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_qspi_deinit(iot_qspi_t *handle);

/*
 * 下发一条 QSPI 命令帧（指令/地址/空周期阶段）。
 * command : 命令描述，不可为 NULL；data_length 非 0 时数据阶段由后续收发接口完成。
 * 返回    : IOT_OK；IOT_INVALID_PARAM；总线忙返回 IOT_BUSY。
 */
iot_status_t iot_qspi_command(iot_qspi_t *handle, const iot_qspi_command_t *command);

/*
 * 阻塞发送数据。
 * data    : 发送缓冲区，不可为 NULL；
 * length  : 字节数；
 * timeout : 最大等待毫秒数。
 * 返回    : IOT_OK；IOT_INVALID_PARAM；IOT_TIMEOUT。
 */
iot_status_t iot_qspi_transmit(iot_qspi_t *handle, const void *data,
                                 size_t length, uint32_t timeout);

/* 阻塞接收数据；data 缓冲区不可为 NULL，语义同 transmit */
iot_status_t iot_qspi_receive(iot_qspi_t *handle, void *data,
                                size_t length, uint32_t timeout);

/*
 * 进入内存映射模式，之后外部 Flash 可直接按地址读取。
 * 返回 : IOT_OK；IOT_INVALID_PARAM；平台不支持返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_qspi_memory_map(iot_qspi_t *handle);

/* 退出内存映射模式；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_qspi_memory_unmap(iot_qspi_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* IOT_QSPI_H */
