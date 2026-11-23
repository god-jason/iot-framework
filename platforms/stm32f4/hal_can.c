/*
 * hal_can.c - STM32F4 CAN HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

#define STM32_CAN_MAX_FILTER_BANK 27U

static uint32_t stm32_can_mode(hal_can_mode_e mode)
{
    switch (mode) {
    case HAL_CAN_MODE_LOOPBACK:
        return CAN_MODE_LOOPBACK;
    case HAL_CAN_MODE_SILENT:
        return CAN_MODE_SILENT;
    case HAL_CAN_MODE_SILENT_LOOPBACK:
        return CAN_MODE_SILENT_LOOPBACK;
    case HAL_CAN_MODE_NORMAL:
    default:
        return CAN_MODE_NORMAL;
    }
}

static void stm32_can_timing(uint32_t bitrate, uint32_t *prescaler,
                          uint32_t *bs1, uint32_t *bs2, uint32_t *sjw)
{
    uint32_t pclk = HAL_RCC_GetPCLK1Freq();
    uint32_t tq_total;
    uint32_t presc = 1U;
    uint32_t tqs;

    if (bitrate == 0U) {
        bitrate = 500000U;
    }
    tq_total = pclk / bitrate;
    if (tq_total < 4U) {
        tq_total = 4U;
    }

    while (((tq_total / presc) > 25U) && (presc < 1024U)) {
        presc++;
    }

    tqs = tq_total / presc;
    if (tqs < 4U) {
        tqs = 4U;
    }
    if (tqs > 25U) {
        tqs = 25U;
    }

    *prescaler = presc;
    *bs1 = (tqs > 3U) ? (tqs - 3U) : 1U;
    *bs2 = 2U;
    *sjw = 1U;
}

static void stm32_can_frame_to_header(const hal_can_frame_t *frame,
                                   CAN_TxHeaderTypeDef *header)
{
    memset(header, 0, sizeof(*header));
    if (frame->id_type == HAL_CAN_ID_EXTENDED) {
        header->ExtId = frame->id & 0x1FFFFFFFU;
        header->IDE = CAN_ID_EXT;
    } else {
        header->StdId = frame->id & 0x7FFU;
        header->IDE = CAN_ID_STD;
    }
    header->RTR = (frame->frame_type == HAL_CAN_FRAME_REMOTE) ?
                  CAN_RTR_REMOTE : CAN_RTR_DATA;
    header->DLC = frame->dlc;
    header->TransmitGlobalTime = DISABLE;
}

static uint32_t *stm32_can_filter_bank(const hal_can_t *handle);

int hal_can_init(hal_can_t *handle,
                            const hal_can_config_t *config)
{
    CAN_HandleTypeDef *hcan;
    uint32_t prescaler;
    uint32_t bs1;
    uint32_t bs2;
    uint32_t sjw;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (config->enable_fd) {
        return HAL_NOT_SUPPORTED;
    }

    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if ((hcan == NULL) || (hcan->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    stm32_can_timing(config->bitrate, &prescaler, &bs1, &bs2, &sjw);

    memset(&hcan->Init, 0, sizeof(hcan->Init));
    hcan->Init.Prescaler = prescaler;
    hcan->Init.Mode = stm32_can_mode(config->mode);
    hcan->Init.SyncJumpWidth = sjw;
    hcan->Init.TimeSeg1 = bs1;
    hcan->Init.TimeSeg2 = bs2;
    hcan->Init.TimeTriggeredMode = DISABLE;
    hcan->Init.AutoBusOff = ENABLE;
    hcan->Init.AutoWakeUp = ENABLE;
    hcan->Init.AutoRetransmission = ENABLE;
    hcan->Init.ReceiveFifoLocked = DISABLE;
    hcan->Init.TransmitFifoPriority = DISABLE;

    if (HAL_CAN_Init(hcan) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    {
        uint32_t *bank_ptr = stm32_can_filter_bank(handle);

        if (bank_ptr != NULL) {
            *bank_ptr = 0U;
        }
    }

    (void)HAL_CAN_ActivateNotification(
        hcan, CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_TX_MAILBOX_EMPTY |
              CAN_IT_ERROR);
    return HAL_OK;
}

int hal_can_deinit(hal_can_t *handle)
{
    CAN_HandleTypeDef *hcan;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan != NULL) {
        (void)HAL_CAN_Stop(hcan);
        (void)HAL_CAN_DeInit(hcan);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_can_start(hal_can_t *handle)
{
    CAN_HandleTypeDef *hcan;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }
    return ros_cube_status(HAL_CAN_Start(hcan));
}

int hal_can_stop(hal_can_t *handle)
{
    CAN_HandleTypeDef *hcan;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }
    return ros_cube_status(HAL_CAN_Stop(hcan));
}

int hal_can_transmit(hal_can_t *handle,
                                const hal_can_frame_t *frame,
                                uint32_t timeout)
{
    CAN_HandleTypeDef *hcan;
    CAN_TxHeaderTypeDef header;
    uint32_t mailbox;
    uint32_t start_tick;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (frame == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if ((frame->fd) || (frame->dlc > 8U)) {
        return HAL_NOT_SUPPORTED;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }

    stm32_can_frame_to_header(frame, &header);
    start_tick = HAL_GetTick();

    for (;;) {
        status = HAL_CAN_AddTxMessage(hcan, &header,
                                      (uint8_t *)frame->data, &mailbox);
        if (status == HAL_OK) {
            return HAL_OK;
        }
        if (timeout == HAL_TIMEOUT_NONE) {
            return HAL_TIMEOUT;
        }
        if ((timeout != HAL_TIMEOUT_FOREVER) &&
            ((HAL_GetTick() - start_tick) >= timeout)) {
            return HAL_TIMEOUT;
        }
    }
}

int hal_can_receive(hal_can_t *handle,
                               hal_can_frame_t *frame, uint32_t timeout)
{
    CAN_HandleTypeDef *hcan;
    CAN_RxHeaderTypeDef header;
    uint32_t start_tick;

    if ((handle == NULL) || (frame == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }

    start_tick = HAL_GetTick();

    for (;;) {
        if (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0U) {
            memset(frame, 0, sizeof(*frame));
            if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &header,
                                     frame->data) != HAL_OK) {
                return HAL_ERROR;
            }
            frame->id = (header.IDE == CAN_ID_EXT) ?
                        header.ExtId : header.StdId;
            frame->id_type = (header.IDE == CAN_ID_EXT) ?
                             HAL_CAN_ID_EXTENDED : HAL_CAN_ID_STANDARD;
            frame->frame_type = (header.RTR == CAN_RTR_REMOTE) ?
                                HAL_CAN_FRAME_REMOTE : HAL_CAN_FRAME_DATA;
            frame->dlc = (uint8_t)header.DLC;
            frame->fd = false;
            frame->brs = false;
            return HAL_OK;
        }

        if (timeout == HAL_TIMEOUT_NONE) {
            return HAL_TIMEOUT;
        }
        if ((timeout != HAL_TIMEOUT_FOREVER) &&
            ((HAL_GetTick() - start_tick) >= timeout)) {
            return HAL_TIMEOUT;
        }
    }
}

int hal_can_transmit_it(hal_can_t *handle,
                                   const hal_can_frame_t *frame)
{
    CAN_HandleTypeDef *hcan;
    CAN_TxHeaderTypeDef header;
    uint32_t mailbox;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (frame == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if ((frame->fd) || (frame->dlc > 8U)) {
        return HAL_NOT_SUPPORTED;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }

    stm32_can_frame_to_header(frame, &header);
    status = HAL_CAN_AddTxMessage(hcan, &header, (uint8_t *)frame->data,
                                  &mailbox);
    return ros_cube_status(status);
}

#define STM32_CAN_FILTER_OWNERS 2U

static struct {
    const hal_can_t *owner;
    uint32_t bank;
} s_can_filter_owners[STM32_CAN_FILTER_OWNERS];

static uint32_t *stm32_can_filter_bank(const hal_can_t *handle)
{
    uint32_t i;

    for (i = 0U; i < STM32_CAN_FILTER_OWNERS; i++) {
        if (s_can_filter_owners[i].owner == handle) {
            return &s_can_filter_owners[i].bank;
        }
    }
    for (i = 0U; i < STM32_CAN_FILTER_OWNERS; i++) {
        if (s_can_filter_owners[i].owner == NULL) {
            s_can_filter_owners[i].owner = handle;
            s_can_filter_owners[i].bank = 0U;
            return &s_can_filter_owners[i].bank;
        }
    }
    return NULL;
}

int hal_can_add_filter(hal_can_t *handle,
                                  const hal_can_filter_t *filter)
{
    CAN_HandleTypeDef *hcan;
    CAN_FilterTypeDef config;
    uint32_t bank;
    uint32_t *bank_ptr;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (filter == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }

    bank_ptr = stm32_can_filter_bank(handle);
    if (bank_ptr == NULL) {
        return HAL_NO_MEMORY;
    }
    bank = *bank_ptr;
    if (bank > STM32_CAN_MAX_FILTER_BANK) {
        return HAL_NO_MEMORY;
    }

    memset(&config, 0, sizeof(config));
    config.FilterBank = bank;
    config.FilterMode = CAN_FILTERMODE_IDMASK;
    config.FilterScale = CAN_FILTERSCALE_32BIT;
    config.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    config.FilterActivation = CAN_FILTER_ENABLE;
    config.SlaveStartFilterBank = 14U;

    if (filter->id_type == HAL_CAN_ID_EXTENDED) {
        config.FilterIdHigh = (uint16_t)(filter->id >> 13);
        config.FilterIdLow = (uint16_t)((filter->id << 3) & 0xFFFFU);
        config.FilterMaskIdHigh = (uint16_t)(filter->mask >> 13);
        config.FilterMaskIdLow = (uint16_t)((filter->mask << 3) & 0xFFFFU);
    } else {
        config.FilterIdHigh = (uint16_t)((filter->id << 5) & 0xFFFFU);
        config.FilterIdLow = 0U;
        config.FilterMaskIdHigh = (uint16_t)((filter->mask << 5) & 0xFFFFU);
        config.FilterMaskIdLow = 0U;
    }

    status = HAL_CAN_ConfigFilter(hcan, &config);
    if (status == HAL_OK) {
        *bank_ptr = bank + 1U;
    }
    return ros_cube_status(status);
}

int hal_can_clear_filters(hal_can_t *handle)
{
    CAN_HandleTypeDef *hcan;
    CAN_FilterTypeDef config;
    uint32_t bank;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }

    for (bank = 0U; bank <= STM32_CAN_MAX_FILTER_BANK; bank++) {
        memset(&config, 0, sizeof(config));
        config.FilterBank = bank;
        config.FilterActivation = CAN_FILTER_DISABLE;
        (void)HAL_CAN_ConfigFilter(hcan, &config);
    }
    {
        uint32_t *bank_ptr = stm32_can_filter_bank(handle);

        if (bank_ptr != NULL) {
            *bank_ptr = 0U;
        }
    }
    return HAL_OK;
}

void ros_stm32_can_irq_handler(hal_can_t *handle)
{
    CAN_HandleTypeDef *hcan;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    HAL_CAN_IRQHandler(hcan);

    if (hcan->ErrorCode != HAL_CAN_ERROR_NONE) {
        handle->base.error = HAL_ERROR;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_ERROR, handle->user_data);
        }
        hcan->ErrorCode = HAL_CAN_ERROR_NONE;
    } else if (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0U) {
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_RX_COMPLETE,
                             handle->user_data);
        }
    } else if (HAL_CAN_GetTxMailboxesFreeLevel(hcan) > 0U) {
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_TX_COMPLETE,
                             handle->user_data);
        }
    }
}


#endif /* ROS_STM32_F4 */
