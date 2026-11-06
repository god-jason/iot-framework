#ifndef IOT_DRV_BUZZER_H
#define IOT_DRV_BUZZER_H

/*
 * 蜂鸣器驱动（GPIO 高电平触发，或 PWM 做音量）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t pin;
    uint8_t active_level;
} iot_buzzer_t;

int iot_buzzer_init(iot_buzzer_t *buzzer, uint8_t pin, uint8_t active_level);
int iot_buzzer_beep(iot_buzzer_t *buzzer, uint32_t ms);   /* 鸣叫一段时间(阻塞) */
int iot_buzzer_start(iot_buzzer_t *buzzer);               /* 开始持续鸣叫 */
int iot_buzzer_stop(iot_buzzer_t *buzzer);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_BUZZER_H */