#ifndef HAL_ETH_H
#define HAL_ETH_H

/*
 * 以太网 MAC：速率/双工/校验卸载配置，启停与单帧收发。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 HAL_INVALID_PARAM。
 * PHY 链路协商与协议栈（ARP/IP/TCP）由上层完成，本模块只处理 MAC 层帧。
 */

#include "hal_def.h"
#include "hal_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 以太网配置 */
typedef struct {
    uint32_t speed;             /* 链路速率（Mbps），如 10/100/1000 */
    bool full_duplex;           /* true 全双工，false 半双工 */
    bool checksum_offload;      /* true 启用硬件校验和卸载 */
} hal_eth_config_t;

/* 以太网句柄 */
typedef struct hal_eth {
    hal_handle_base_t base;    /* 通用句柄基类，勿移动位置 */
    hal_eth_config_t config;   /* 初始化时保存的配置副本 */

    hal_dma_t *rx_dma;  /* 接收 DMA 通道，可为 NULL */
    hal_dma_t *tx_dma;  /* 发送 DMA 通道，可为 NULL */

    hal_callback_t callback;   /* 收发完成/错误回调，可为 NULL */
    void *user_data;            /* 回调上下文，可为 NULL */
} hal_eth_t;

/*
 * 初始化以太网 MAC。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；speed 需为硬件支持值。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；速率不支持返回 HAL_NOT_SUPPORTED。
 */
int hal_eth_init(hal_eth_t *handle, const hal_eth_config_t *config);

/* 反初始化以太网 MAC；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
int hal_eth_deinit(hal_eth_t *handle);

/* 启动 MAC 收发；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
int hal_eth_start(hal_eth_t *handle);

/* 停止 MAC 收发；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
int hal_eth_stop(hal_eth_t *handle);

/*
 * 发送一帧以太网数据。
 * data   : 完整帧（含目的/源 MAC 与类型字段），不可为 NULL；
 * length : 帧长度（字节），小于最小帧长时由硬件补零的情况依平台而定。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；发送队列满返回 HAL_BUSY。
 */
int hal_eth_transmit(hal_eth_t *handle, const void *data, size_t length);

/*
 * 接收一帧以太网数据。
 * buffer           : 接收缓冲区，不可为 NULL；
 * buffer_size      : 缓冲区容量（字节）；
 * received_length  : 实际帧长输出，不可为 NULL。
 * 返回             : HAL_OK；HAL_INVALID_PARAM；无数据返回 HAL_TIMEOUT；
 *                     帧长超过缓冲区返回 HAL_OVERFLOW（帧被丢弃）。
 */
int hal_eth_receive(hal_eth_t *handle, void *buffer,
                               size_t buffer_size, size_t *received_length);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ETH_H */
