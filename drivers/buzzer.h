#ifndef BUZZER_H
#define BUZZER_H

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
} buzzer_t;

int buzzer_init(buzzer_t *buzzer, uint8_t pin, uint8_t active_level);
int buzzer_beep(buzzer_t *buzzer, uint32_t ms);   /* 鸣叫一段时间(阻塞) */
int buzzer_start(buzzer_t *buzzer);               /* 开始持续鸣叫 */
int buzzer_stop(buzzer_t *buzzer);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_H */