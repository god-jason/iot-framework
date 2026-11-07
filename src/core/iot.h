#ifndef IOT_H
#define IOT_H

/*
 * IOT 统一入口头文件。
 * 上层只包含 iot.h，即可使用全部外设接口。
 * 具体平台实现在 platforms/<platform>/ 下，不进入公共 API。
 *
 * 说明：
 *   - 公共 API 不依赖任何 RTOS 头或厂商 HAL 头；
 *   - 各模块是否可用由 iot_config.h 中的 IOT_USING_* 开关裁剪，
 *     被裁剪的模块仍可编译，但运行时返回 IOT_NOT_SUPPORTED；
 *   - 包含顺序仅影响编译期可见性，无运行期副作用。
 */

#include "iot_def.h"
#include "iot_config.h"

#include "iot_os.h"
#include "iot_clock.h"
#include "iot_pin.h"

#include "iot_gpio.h"
#include "iot_exti.h"
#include "iot_dma.h"

#include "iot_uart.h"
#include "iot_spi.h"
#include "iot_i2c.h"
#include "iot_i2s.h"
#include "iot_can.h"

#include "iot_adc.h"
#include "iot_dac.h"
#include "iot_opamp.h"

#include "iot_tim.h"
#include "iot_pwm.h"
#include "iot_capture.h"

#include "iot_wdg.h"
#include "iot_rtc.h"
#include "iot_flash.h"
#include "iot_crc.h"
#include "iot_rng.h"
#include "iot_pwr.h"

#include "iot_qspi.h"
#include "iot_sdio.h"
#include "iot_usb.h"
#include "iot_eth.h"

#include "iot_http.h"
#include "iot_mqtt.h"
#include "iot_net.h"

#endif /* IOT_H */
