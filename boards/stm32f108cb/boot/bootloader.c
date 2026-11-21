/*
 * bootloader.c - STM32F1（128KB Flash）A/B OTA Bootloader
 *
 * ============================================================================
 * 一、功能
 * ============================================================================
 *   1. A/B 双区：默认启动 A 区；OTA 只写入非活动区，升级后 pending 指向新槽，
 *      App 调用 boot_confirm() 确认启动成功后 active 切到新槽，原槽成为下一次
 *      OTA 的目标区。
 *   2. 上电等待 UART1（PA9 发送 / PA10 接收，115200 8N1）500ms，收到字符
 *      'R'（或 'r'）进入升级流程，其它字符忽略。
 *   3. 回滚：
 *      - pending 槽连续 BOOT_MAX_TRIES 次启动仍未确认 → 回到 active 槽；
 *      - App HardFault 记录到 meta.crash，超过 BOOT_MAX_CRASH 次 → 切到另一槽。
 *   4. 启动时通过 UART1 打印上一次崩溃日志（含寄存器、故障状态与栈快照）。
 *   5. 校验：OTA 传输时边收边算 CRC32，收完后与主机给出的 CRC 比对；
 *      启动前对记录过长度的槽再做一次整段 CRC32 校验。手工烧录的槽没有
 *      长度记录，只校验向量表。
 *   6. 体积：不依赖 HAL / RTOS / libc（-nostdlib -Os），约 3KB，
 *      限制在 Flash 前 8KB 内。
 *
 * ============================================================================
 * 二、Flash 布局（128KB Flash）
 * ============================================================================
 *   区域        起始地址        大小
 *   Bootloader  0x08000000      8KB
 *   Slot A      0x08002000      56KB
 *   Slot B      0x08010000      56KB
 *   Meta 页 0   0x0801E000      1KB（两页轮换写，掉电保留一份）
 *   Meta 页 1   0x0801E400      1KB
 *   崩溃日志    0x0801E800      2KB（文本追加写，写满整区擦除）
 *
 * ============================================================================
 * 三、升级协议（Bootloader 收到 'R' 之后）
 * ============================================================================
 *   应答采用不可打印控制字符，避免与 UART 日志里的 'K'/'E' 等字母混淆：
 *     ACK = 0x06，NAK = 0x15
 *
 *   Boot -> Host : ACK                          （就绪：可发送长度/CRC）
 *   Host -> Boot : u32 len（小端，1..槽大小）
 *                  u32 crc32（小端，覆盖后面 len 字节负载）
 *   Boot -> Host : ACK                          （目标区擦除完成，可发负载）
 *   Host -> Boot : len 字节负载（每字节等待上限 3s）
 *   Boot -> Host : ACK 成功（随后 MCU 复位，启动新槽）
 *                  NAK 失败（长度/CRC/Flash 错误，返回继续启动旧槽）
 *
 * ============================================================================
 * 四、App 侧配合
 * ============================================================================
 *   - 包含 boot/boot_meta.h，与 Bootloader 共用元数据实现（boot_meta.c）；
 *   - 链接基址：A 区 0x08002000，B 区包链接 0x08010000（OTA 包按目标槽链接）；
 *   - 启动时执行 SCB->VTOR = 自身基址（Bootloader 跳转前也会设置一次）；
 *   - 初始化完成后调用一次 boot_confirm()；
 *   - HardFault_Handler 中调用 boot_crash_count() 后复位。
 *
 * ============================================================================
 * 五、构建
 * ============================================================================
 *   xmake build ros-boot-f108cb  →  build/cross/arm/release/ros-boot-f108cb.bin
 *   目标编译 startup_stm32f10x_md.S + boot/bootloader.c + boot/boot_meta.c。
 */

#include "boot_meta.h"

/* ==========================================================================
 * 六、Bootloader 本体
 * ========================================================================== */

#define UART_BRR_8MHZ_115200    69U     /* 8MHz / 115200 ≈ 69.44，误差 0.6% */
#define UPDATE_BYTE_TIMEOUT_MS  3000U   /* 升级时单字节接收超时 */
#define UPDATE_HANDSHAKE_MS     500U    /* 上电等待 'R' 的时间 */
#define BOOT_ACK_CHAR           0x06U   /* 应答：就绪 / 擦除完成 / 升级成功 */
#define BOOT_NAK_CHAR           0x15U   /* 应答：升级失败 */

/*
 * 运行在 HSI 8MHz：串口 115200 与 Flash 编程都够用，省去 PLL 配置代码
 * （体积优先）。SystemCoreClock 供 CMSIS 头使用。
 */
uint32_t SystemCoreClock = 8000000U;

static volatile uint32_t s_ms;          /* SysTick 毫秒计数 */

/* 时钟保持 HSI 8MHz（启动文件会调用） */
void SystemInit(void)
{
}

/* 1ms SysTick：只做毫秒计数 */
void SysTick_Handler(void)
{
    s_ms++;
}

/* Bootloader 不使用 SVC / PendSV，占位避免进入 Default_Handler 死循环 */
void SVC_Handler(void)
{
    for (;;) {
    }
}

void PendSV_Handler(void)
{
    for (;;) {
    }
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = s_ms;

    while ((s_ms - start) < ms) {
    }
}

/* ---------------------------- UART1 ---------------------------- */

/* USART1：PA9 复用推挽输出，PA10 上拉输入，115200 8N1 */
static void uart_init(void)
{
    RCC->APB2ENR |= (1UL << 2) | (1UL << 14);       /* IOPAEN | USART1EN */

    GPIOA->CRH &= ~(0x0FUL << 4);
    GPIOA->CRH |= (0x0BUL << 4);
    GPIOA->CRH &= ~(0x0FUL << 8);
    GPIOA->CRH |= (0x08UL << 8);
    GPIOA->ODR |= (1UL << 10);

    USART1->BRR = UART_BRR_8MHZ_115200;
    USART1->CR1 = (1UL << 2) | (1UL << 3) | (1UL << 13);  /* RE|TE|UE */
}

/* 接收一个字节；timeout_ms 为 0 时不等待。返回 -1 表示超时 */
static int uart_getc(uint32_t timeout_ms)
{
    uint32_t start = s_ms;

    for (;;) {
        if ((USART1->SR & (1UL << 5)) != 0U) {        /* RXNE */
            return (int)(USART1->DR & 0xFFU);
        }
        if (timeout_ms != 0U && (s_ms - start) >= timeout_ms) {
            return -1;
        }
    }
}

/* 发送一个字节（等待发送寄存器空） */
static void uart_putc(uint8_t c)
{
    while ((USART1->SR & (1UL << 7)) == 0U) {          /* TXE */
    }
    USART1->DR = c;
}

/* 等待最后一个字节完全送出（复位前调用） */
static void uart_flush(void)
{
    while ((USART1->SR & (1UL << 6)) == 0U) {          /* TC */
    }
}

/* 输出字符串 / 十六进制 / 十进制（仅日志用，避免引入 printf） */
static void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc((uint8_t)*s++);
    }
}

static void uart_put_hex32(uint32_t v)
{
    int shift;

    uart_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        uint32_t d = (v >> (uint32_t)shift) & 0x0FU;

        uart_putc((uint8_t)(d < 10U ? ('0' + d) : ('A' + d - 10U)));
    }
}

static void uart_put_dec(uint32_t v)
{
    char buf[10];
    uint32_t i = 0U;

    if (v == 0U) {
        uart_putc('0');
        return;
    }
    while (v > 0U && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (i > 0U) {
        uart_putc((uint8_t)buf[--i]);
    }
}

/* 槽号转 'A' / 'B' */
static char slot_char(uint32_t slot)
{
    return (slot == 0U) ? 'A' : 'B';
}

/*
 * 打印上次崩溃日志（BOOT_LOG_BASE 起 2KB 文本，由 App 的 HardFault 写入）。
 * 擦除态 0xFF 或 '\0' 视为记录结束；只输出可打印字符与换行。
 */
static void print_crash_log(void)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)BOOT_LOG_BASE;
    uint32_t i;

    if (p[0] == 0xFFU || p[0] == 0U) {
        return;                                 /* 没有崩溃记录 */
    }
    uart_puts("\r\n[BOOT] last crash log:\r\n");
    for (i = 0U; i < BOOT_LOG_SIZE; i++) {
        uint8_t c = p[i];

        if (c == 0xFFU || c == 0U) {
            break;
        }
        if ((c >= 0x20U && c < 0x7FU) || c == '\r' || c == '\n') {
            uart_putc(c);
        }
    }
    uart_puts("\r\n");
}

/*
 * 在 ms 毫秒内等待升级口令 'R'（大小写均可），其它字符直接丢弃。
 * 返回 true 表示需要进入升级。
 */
static bool wait_update_req(uint32_t ms)
{
    uint32_t start = s_ms;

    while ((s_ms - start) < ms) {
        int c = uart_getc(ms - (s_ms - start));

        if (c < 0) {
            break;                      /* 超时 */
        }
        if (c == 'R' || c == 'r') {
            return true;
        }
    }
    return false;
}

/* 读取 4 字节小端整数（升级协议的长度 / CRC 字段） */
static int uart_read_u32(uint32_t *out)
{
    uint32_t v = 0U;
    uint32_t i;

    for (i = 0U; i < 4U; i++) {
        int c = uart_getc(UPDATE_BYTE_TIMEOUT_MS);

        if (c < 0) {
            return -1;
        }
        v |= (uint32_t)(uint8_t)c << (i * 8U);
    }
    *out = v;
    return 0;
}

/* ---------------------------- 槽与跳转 ---------------------------- */

static uint32_t slot_base(uint32_t slot)
{
    return (slot == 0U) ? BOOT_SLOT_BASE0 : BOOT_SLOT_BASE1;
}

/*
 * 判断槽内程序是否可启动：
 *   1) 向量表首字（初始 MSP）落在 SRAM 0x20000000~0x20005000；
 *   2) 复位向量（第二个字）落在本槽地址范围内；
 *   3) 若该槽由 OTA 写入（meta 记录了长度），再对 len 字节做 CRC32 全量校验。
 */
static bool slot_valid(const boot_meta_t *m, uint32_t slot)
{
    uint32_t base = slot_base(slot);
    uint32_t sp = *(volatile uint32_t *)(uintptr_t)base;
    uint32_t pc = *(volatile uint32_t *)(uintptr_t)(base + 4U);

    if (sp < 0x20000000U || sp > 0x20005000U) {
        return false;
    }
    if (pc < base || pc >= (base + BOOT_SLOT_SIZE)) {
        return false;
    }
    if (m->len[slot] != 0U && m->len[slot] <= BOOT_SLOT_SIZE) {
        return boot_crc32((const void *)(uintptr_t)base, m->len[slot])
               == m->crc[slot];
    }
    return true;
}

/*
 * 跳转到指定槽：
 *   - 关闭 SysTick，避免跳转后立刻用 App 的向量表进中断；
 *   - SCB->VTOR 指向新槽向量表；
 *   - 关中断、切 MSP 到 App 初始栈顶、线程模式改回 MSP，再跳复位向量。
 * 该函数正常不返回。
 */
static void jump_to(uint32_t base)
{
    uint32_t sp = *(volatile uint32_t *)(uintptr_t)base;
    uint32_t pc = *(volatile uint32_t *)(uintptr_t)(base + 4U);

    /* 关闭 SysTick 并清掉挂起位，避免跳转后立刻用 App 向量表进中断 */
    SysTick->CTRL = 0U;
    SysTick->VAL = 0U;
    SCB->ICSR = (1UL << 25);                        /* PENDSTCLR */

    SCB->VTOR = base;

    __asm volatile ("cpsid i" ::: "memory");
    __asm volatile ("msr msp, %0" :: "r" (sp));
    __asm volatile ("msr control, %0" :: "r" (0));
    __asm volatile ("isb");
    /* 必须恢复开中断：App 启动阶段依赖 SysTick / UART 中断 */
    __asm volatile ("cpsie i" ::: "memory");
    ((void (*)(void))(uintptr_t)pc)();

    for (;;) {
    }
}

/* ---------------------------- 升级流程 ---------------------------- */

/*
 * 接收 OTA 镜像并写入非活动槽。
 * 成功：更新元数据（pending=新槽）、回复 ACK、复位进入新槽；
 * 失败：回复 NAK 并返回调用者继续启动旧槽。
 * 采用 64 字节缓冲：边收边算 CRC、边写 Flash，避免占用大块 RAM。
 */
static void do_update(boot_meta_t *m)
{
    uint32_t target = (m->active == 0U) ? 1U : 0U;  /* 非活动槽即目标区 */
    uint32_t base = slot_base(target);
    uint32_t len = 0U;
    uint32_t crc = 0U;
    uint32_t got = 0U;
    uint32_t calc = 0xFFFFFFFFU;
    uint8_t buf[64];
    uint32_t n = 0U;
    uint32_t next_dot = 4096U;

    uart_puts("[BOOT] OTA request, target slot ");
    uart_putc((uint8_t)slot_char(target));
    uart_puts("\r\n");
    uart_putc(BOOT_ACK_CHAR);                                 /* 就绪 */
    if (uart_read_u32(&len) != 0 || uart_read_u32(&crc) != 0 ||
        len == 0U || len > BOOT_SLOT_SIZE) {
        uart_puts("[BOOT] OTA error: bad header\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }
    uart_puts("[BOOT] OTA len=");
    uart_put_hex32(len);
    uart_puts(" crc=");
    uart_put_hex32(crc);
    uart_puts("\r\n");

    if (boot_flash_erase(base, len) != 0) {         /* 按需擦除目标区 */
        uart_puts("[BOOT] OTA error: erase failed\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }
    uart_puts("[BOOT] OTA erase done, receiving");
    uart_putc(BOOT_ACK_CHAR);                                 /* 擦除完成，可发负载 */

    while (got < len) {
        int c = uart_getc(UPDATE_BYTE_TIMEOUT_MS);

        if (c < 0) {
            uart_puts("\r\n[BOOT] OTA error: rx timeout\r\n");
            uart_putc(BOOT_NAK_CHAR);
            return;
        }
        buf[n++] = (uint8_t)c;
        got++;
        /* 缓冲满或收完：先算 CRC 再写 Flash（写地址始终 2 字节对齐） */
        if (n == sizeof(buf) || got == len) {
            calc = boot_crc32_update(calc, buf, n);
            if (boot_flash_write(base + got - n, buf, n) != 0) {
                uart_puts("\r\n[BOOT] OTA error: flash write\r\n");
                uart_putc(BOOT_NAK_CHAR);
                return;
            }
            n = 0U;
        }
        if (got >= next_dot) {                      /* 每 4KB 打一个进度点 */
            uart_putc('.');
            next_dot += 4096U;
        }
    }
    uart_puts("\r\n");

    if ((calc ^ 0xFFFFFFFFU) != crc) {              /* 整包 CRC 不符 */
        uart_puts("[BOOT] OTA error: crc mismatch\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }

    /* 记录新槽信息，pending 指向它；下次启动先试新槽，由 App 确认后转正 */
    m->pending = target;
    m->tries = 0U;
    m->len[target] = len;
    m->crc[target] = crc;
    if (boot_meta_save(m) != 0) {
        uart_puts("[BOOT] OTA error: meta save\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }

    uart_puts("[BOOT] OTA ok, pending slot ");
    uart_putc((uint8_t)slot_char(target));
    uart_puts(", rebooting\r\n");
    uart_putc(BOOT_ACK_CHAR);
    uart_flush();
    NVIC_SystemReset();                             /* 复位并启动新槽 */
}

/* ---------------------------- main ---------------------------- */

/*
 * 启动流程：
 *   1) 初始化 1ms 时基与 UART1，打印元数据；
 *   2) 崩溃超限（crash > BOOT_MAX_CRASH）则回退到另一分区；
 *   3) 等待 500ms 升级口令 'R'，命中则进入升级；
 *   4) 选择启动槽（pending 优先），无效则尝试另一槽；
 *   5) 未确认的 pending 槽累计失败次数，达到上限回滚到 active；
 *   6) 目标槽无效时停留在 Bootloader 反复等待升级；
 *   7) 跳转到目标槽。
 */
int main(void)
{
    boot_meta_t m;
    uint32_t target;

    (void)SysTick_Config(8000U);                    /* 8MHz / 1000 = 1ms */
    uart_init();
    boot_meta_load(&m);

    /* 启动信息：当前元数据（active / pending / tries / crash） */
    uart_puts("\r\n[BOOT] meta active=");
    uart_putc((uint8_t)slot_char(m.active));
    uart_puts(" pending=");
    if (m.pending == BOOT_SLOT_NONE) {
        uart_puts("none");
    } else {
        uart_putc((uint8_t)slot_char(m.pending));
    }
    uart_puts(" tries=");
    uart_put_dec(m.tries);
    uart_puts(" crash=");
    uart_put_dec(m.crash);
    uart_puts("\r\n");

    /* 打印上一次崩溃的现场记录（如有） */
    print_crash_log();

    /* 2) 连续崩溃超限：回退到另一个分区 */
    if (m.crash > BOOT_MAX_CRASH) {
        uint32_t other = m.active ^ 1U;

        uart_puts("[BOOT] crash ");
        uart_put_dec(m.crash);
        uart_puts(" > ");
        uart_put_dec(BOOT_MAX_CRASH);
        uart_puts(", rollback to slot ");
        uart_putc((uint8_t)slot_char(other));
        uart_puts("\r\n");
        m.active = other;
        m.pending = BOOT_SLOT_NONE;
        m.tries = 0U;
        m.crash = 0U;
        (void)boot_meta_save(&m);
    }

    /* 3) 启动前 500ms 升级握手 */
    uart_puts("[BOOT] wait 'R' 500ms\r\n");
    if (wait_update_req(UPDATE_HANDSHAKE_MS)) {
        do_update(&m);                              /* 成功不返回 */
    }

    /* 4) 选择启动槽：有 pending 先试新槽，否则跑 active */
    target = (m.pending != BOOT_SLOT_NONE) ? m.pending : m.active;
    if (!slot_valid(&m, target)) {
        uint32_t other = target ^ 1U;               /* 0/1 互换 */

        uart_puts("[BOOT] slot ");
        uart_putc((uint8_t)slot_char(target));
        uart_puts(" invalid\r\n");
        if (slot_valid(&m, other)) {
            uart_puts("[BOOT] fallback to slot ");
            uart_putc((uint8_t)slot_char(other));
            uart_puts(", clear pending\r\n");
            target = other;
            m.pending = BOOT_SLOT_NONE;             /* 回落到旧槽 */
            (void)boot_meta_save(&m);
        } else {
            /* 两槽都没有可运行程序：留在 Bootloader 等待升级 */
            uart_puts("[BOOT] no valid slot, wait OTA\r\n");
            for (;;) {
                if (wait_update_req(1000U)) {
                    do_update(&m);
                }
            }
        }
    }

    /* 5) 崩溃回滚：pending 槽连续 BOOT_MAX_TRIES 次未确认则退回 active */
    if (m.pending != BOOT_SLOT_NONE) {
        if (m.tries >= BOOT_MAX_TRIES) {
            uart_puts("[BOOT] slot ");
            uart_putc((uint8_t)slot_char(m.pending));
            uart_puts(" unconfirmed ");
            uart_put_dec(m.tries);
            uart_puts(" times, rollback to slot ");
            uart_putc((uint8_t)slot_char(m.active));
            uart_puts("\r\n");
            m.pending = BOOT_SLOT_NONE;
            m.tries = 0U;
            (void)boot_meta_save(&m);
            target = m.active;                      /* 回到原活动槽 */
        } else {
            m.tries++;                              /* 本次启动尝试 +1 */
            (void)boot_meta_save(&m);
            uart_puts("[BOOT] try slot ");
            uart_putc((uint8_t)slot_char(target));
            uart_puts(" (");
            uart_put_dec(m.tries);
            uart_puts("/3)\r\n");
        }
    }

    /* 6) 回滚目标也必须有效，否则停在 Bootloader */
    if (!slot_valid(&m, target)) {
        uart_puts("[BOOT] target invalid, wait OTA\r\n");
        for (;;) {
            if (wait_update_req(1000U)) {
                do_update(&m);
            }
        }
    }

    uart_puts("[BOOT] boot slot ");
    uart_putc((uint8_t)slot_char(target));
    uart_puts("\r\n");
    uart_flush();                                   /* 等日志发完再跳转 */

    delay_ms(10U);
    jump_to(slot_base(target));
    return 0;                                       /* 不会到这里 */
}
