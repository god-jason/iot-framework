# iot-os

面向 MCU 的轻量实时内核 + 硬件抽象 + 机器人/物联网组件库。

以 STM32 为主要目标，同时支持 PC 模拟与 AVR。对象默认可完全静态分配，内核零动态内存；
也提供可选的 `iot_*_create()` / `iot_*_destroy()` 动态对象接口，按需选择。
同一份业务代码可在 Windows 上确定性回归，再平移到单片机运行。

---

## 特性

| 分类 | 说明 |
| --- | --- |
| 实时内核 | 固定优先级抢占 + 同优先级时间片；信号量、互斥量（优先级继承）、定长队列、流式缓冲、变长消息队列、事件组、任务通知、软件定时器（默认独立 timer 任务）、运行时统计 |
| 默认零动态内存 | TCB、栈、队列缓冲、定时器可由调用者提供，启动后内存占用可静态核算；另有可选的动态创建接口 |
| 硬件抽象 | 35 个外设接口头（GPIO/UART/SPI/I2C/CAN/ADC/DAC/PWM/编码器/DMA/Flash/RTC/USB/ETH…） |
| 多平台 | PC 模拟（Fiber/ucontext）、STM32F1/F4/H7、AVR |
| 器件驱动 | 174 个传感器/显示/存储/无线/识别类驱动 |
| 网络 | 统一 `iot_net` / `iot_http` / `iot_mqtt` 门面，后端可换以太网（Mongoose）或 4G（ML307 AT） |
| 机器人控制 | 运动学、里程计、轨迹规划（梯形/S 曲线）、串级闭环、姿态融合、EKF 定位、栅格建图、A*/D* 导航、安全状态机 |
| 运维 | 参数表、异步日志、Modbus、上位机/CAN 协议、Bootloader 与 FOTA |
| 测试 | 与实现同目录的单元测试，单次回归 1 万+ 断言 |

---

## 目录结构

```
iot-os/
├── src/            实时内核（iot_*），实现与测试同目录
├── hal/            硬件抽象接口（hal_*）
├── components/     业务/算法组件（40 个模块）
├── drivers/        器件驱动（174 个）
├── networks/       网络门面 + eth / ml307 后端
├── bootloader/     分区、镜像校验、升级、串口升级协议（平台无关）
├── platforms/      pc / stm32 / avr 平台实现（含 *_bootloader 移植层）
├── boards/         板级工程（启动文件、链接脚本、main）
├── vendor/         mongoose、cjson、tlsf、arena
├── examples/       综合示例
├── docs/           设计文档
└── xmake.lua       构建脚本
```

---

## 快速开始

### 环境

- [xmake](https://xmake.io/) 2.7+
- 主机：gcc（Windows 建议 TDM-GCC / MinGW-w64）
- 交叉编译：`arm-none-eabi-gcc`

### 构建与运行

```bash
# 单元测试
xmake build iot-test
xmake run iot-test

# 综合示例：控制台 + shell + LED
xmake build iot-demo
xmake run iot-demo

# 主机静态库（供外部工程集成）
xmake build iot-pc
```

### 目标一览

| 目标 | 类型 | 说明 |
| --- | --- | --- |
| `iot-pc` | static | 主机静态库 |
| `iot-test` | binary | 单元测试入口 `src/test_main.c` |
| `iot-demo` | binary | 示例程序 |
| `iot-stm32` | static | STM32 交叉编译库 |
| `iot-fw-f103c8` | binary | 可烧录固件（elf/hex/bin） |

---

## 最小示例

```c
#include "iot.h"

static iot_task_t s_task;
static uint8_t    s_stack[2048];

static void blink(void *arg)
{
    (void)arg;
    for (;;) {
        /* 业务逻辑 */
        iot_sleep_ms(500);
    }
}

int main(void)
{
    iot_init();
    iot_task_create(&s_task, "blink", 3, blink, NULL,
                    s_stack, sizeof(s_stack));
    iot_sched_start();      /* 通常不返回 */
    return 0;
}
```

### 同步原语

```c
static iot_sem_t   s_sem;
static iot_mutex_t s_mutex;
static iot_queue_t s_queue;
static uint8_t     s_qbuf[8 * sizeof(int)];

iot_sem_init(&s_sem, 0);
iot_sem_take(&s_sem, IOT_TIMEOUT_FOREVER);

iot_mutex_lock(&s_mutex, 100);
iot_mutex_unlock(&s_mutex);

iot_queue_init(&s_queue, s_qbuf, sizeof(int), 8);
iot_queue_send(&s_queue, &value, 0);
iot_queue_recv(&s_queue, &value, IOT_TIMEOUT_FOREVER);
```

### 中断中唤醒任务

```c
void USART1_IRQHandler(void)
{
    bool need_yield = false;
    iot_sem_give_from_isr(&s_sem, &need_yield);
    /* 内核已自动请求 PendSV，无需手动切换 */
}
```

### 外设访问

```c
hal_gpio_config_t cfg = {
    .mode = HAL_GPIO_MODE_OUTPUT,
    .output = HAL_GPIO_OUTPUT_PP,
    .pull = HAL_GPIO_NOPULL,
    .speed = HAL_GPIO_SPEED_LOW,
    .alternate = 0,
    .initial_level = HAL_GPIO_LOW,
};
hal_gpio_init(HAL_PIN(0, 13), &cfg);
hal_gpio_write(HAL_PIN(0, 13), HAL_GPIO_HIGH);
```

---

## 机器人控制链路

```
传感器 → perception / encoder / imu
       → odometry → localization(EKF)
       → navigation(A*/D*) → trajectory(梯形/S曲线)
       → drive(位置环+速度环) → motor / servo / stepper
       → HAL PWM/GPIO/TIM
       （safety 全程监控急停/限位/过流）
```

| 环节 | 组件 |
| --- | --- |
| 运动学 | `components/kinematics`（差速/阿克曼/两连杆臂） |
| 里程计 | `components/odometry` |
| 轨迹 | `components/trajectory`（梯形、S 曲线、多轴插补） |
| 闭环 | `components/drive`（串级 PID） |
| 姿态 | `components/imu`（Mahony） |
| 定位 | `components/localization`（EKF） |
| 感知 | `components/perception` |
| 建图/导航 | `components/mapping`、`components/navigation` |
| 安全 | `components/safety` |
| 任务编排 | `components/action`、`planner`、`executor`、`behavior` |

---

## STM32 交叉编译

```bash
# 仅构建内核+组件静态库
xmake f -p cross --stm32_family=f1 --sdk=<工具链根目录> --bin=<根目录>/bin -c
xmake build iot-stm32

# 带 Cube HAL 与外设平台层
xmake f -p cross --stm32_family=f1 --cube_hal_dir=<STM32CubeF1 路径> -c
xmake build iot-stm32

# 可烧录固件（F103C8T6）
xmake f -p cross --stm32_family=f1 \
        --dfp_inc=<STM32F1xx_DFP>/Device/Include \
        --cmsis_inc=<CMSIS>/Core/Include -c
xmake build iot-fw-f103c8
```

`stm32_family` 支持 `f1` / `f4` / `h7`。
固件目标产出 `.elf` / `.hex` / `.bin` 到 `build` 对应目录。

---

## 测试

```bash
xmake build iot-test && xmake run iot-test
```

- 测试文件与实现同目录，命名 `*_test.c`，入口统一为 `void ut_xxx(void)`；
- `src/test_main.c` 汇总调用全部套件；
- 断言宏在 `components/common/test.h`：`TF_CHECK` / `TF_CHECK_INT` / `TF_CHECK_STR` / `TF_CHECK_NEAR` / `TF_CHECK_MEM`。

---

## 设计约定

| 约定 | 说明 |
| --- | --- |
| 返回码 | 内核与 HAL 统一返回 `int`：0 成功，负值为错误码 |
| 默认静态分配 | 对象可由调用者提供；需要运行期增减时使用 `iot_*_create()` / `iot_*_destroy()` |
| 命名前缀 | 内核 `iot_`、HAL `hal_`、组件用模块名前缀 |
| tick 回绕 | 时间比较统一使用 `IOT_TICK_AFTER/BEFORE` |
| ISR 限制 | 中断内禁止阻塞 API，只能使用 `*_from_isr` |
| 裁剪 | 内核开关 `src/iot_config.h`，外设开关 `hal/hal_config.h` |

---

## 文档

- [产品设计文档](docs/设计.md)：架构、模块职责、控制链路、构建部署、已知限制与规划。
- 各模块的接口语义以头文件注释为准（中文详细注释）。

---

## 已知限制

1. `navigation`、`motion`、`mapping` 为单实例静态工作区，非线程安全；
2. `stepper_move` 为阻塞式发脉冲，高频场景建议改用定时器驱动；
3. `planner` 同一时刻只执行一个计划，优先级字段尚未参与调度；
4. 软件定时器回调默认在专用 `timer` 任务执行（`IOT_USE_TIMER_TASK=0` 可退回 tick 上下文）；
5. STM32 端 tickless 仍为 WFI 占位，未按低功耗定时器补偿 tick；
6. 电机驱动 IC（TB6612/A4988/TMC 系列）需按 `drive_ops_t` 自行适配。

---

## 许可证

本项目采用 [MIT License](LICENSE)。

`vendor/` 下的第三方代码（如 Mongoose）遵循其各自文件中的许可证，不适用本项目的 MIT 协议。
