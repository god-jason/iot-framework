#ifndef APP_DEVICE_H
#define APP_DEVICE_H

/*
 * app_device.h - STM32F407VET6 应用级设备初始化
 *
 * 板载 LED、PWM/风扇/步进注册、ROFS 之外的外设与 1Hz 心跳任务。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 LED，注册 device/PWM/风扇/步进，并创建 1Hz 心跳任务 */
void app_device_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_DEVICE_H */
