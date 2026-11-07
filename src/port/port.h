#ifndef PORT_H
#define PORT_H

/*
 * PORT 统一入口头文件。
 * 上层只包含 port.h，即可使用全部外设接口。
 * 具体平台实现在 ports/<platform>/ 下，不进入公共 API。
 *
 * 说明：
 *   - 公共 API 不依赖任何 RTOS 头或厂商 HAL 头；
 *   - 各模块是否可用由 port_config.h 中的 PORT_USING_* 开关裁剪，
 *     被裁剪的模块仍可编译，但运行时返回 PORT_NOT_SUPPORTED；
 *   - 包含顺序仅影响编译期可见性，无运行期副作用。
 */

#include "port_def.h"
#include "port_config.h"

#include "port_platform.h"
#include "port_os.h"
#include "port_clock.h"
#include "port_pin.h"

#include "port_gpio.h"
#include "port_exti.h"
#include "port_dma.h"

#include "port_uart.h"
#include "port_spi.h"
#include "port_i2c.h"
#include "port_i2s.h"
#include "port_can.h"

#include "port_adc.h"
#include "port_dac.h"
#include "port_comp.h"
#include "port_opamp.h"

#include "port_tim.h"
#include "port_pwm.h"
#include "port_capture.h"
#include "port_encoder.h"

#include "port_wdg.h"
#include "port_rtc.h"
#include "port_flash.h"
#include "port_crc.h"
#include "port_rng.h"
#include "port_pwr.h"

#include "port_qspi.h"
#include "port_sdio.h"
#include "port_usb.h"
#include "port_eth.h"

#endif /* PORT_H */
