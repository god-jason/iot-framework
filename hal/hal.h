#ifndef HAL_H
#define HAL_H

/*
 * HAL 统一入口头文件。
 * 上层只包含 hal.h，即可使用全部外设接口。
 * 具体平台实现在 platforms/<platform>/ 下，不进入公共 API。
 *
 * 说明：
 *   - 公共 API 不依赖任何 RTOS 头或厂商 HAL 头；
 *   - 各模块是否可用由 hal_config.h 中的 HAL_USING_* 开关裁剪，
 *     被裁剪的模块仍可编译，但运行时返回 HAL_NOT_SUPPORTED；
 *   - 包含顺序仅影响编译期可见性，无运行期副作用。
 */

#include "hal_def.h"
#include "hal_config.h"

#include "hal_clock.h"
#include "hal_pin.h"

#include "hal_gpio.h"
#include "hal_exti.h"
#include "hal_dma.h"

#include "hal_uart.h"
#include "hal_spi.h"
#include "hal_i2c.h"
#include "hal_i2s.h"
#include "hal_can.h"

#include "hal_adc.h"
#include "hal_dac.h"
#include "hal_opamp.h"

#include "hal_tim.h"
#include "hal_pwm.h"
#include "hal_capture.h"

#include "hal_wdg.h"
#include "hal_rtc.h"
#include "hal_flash.h"
#include "hal_crc.h"
#include "hal_rng.h"
#include "hal_pwr.h"

#include "hal_qspi.h"
#include "hal_sdio.h"
#include "hal_usb.h"
#include "hal_eth.h"

#endif /* HAL_H */
