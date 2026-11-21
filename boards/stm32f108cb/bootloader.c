/*
 * bootloader.c - STM32F1（128KB Flash）A/B OTA Bootloader（单文件实现）
 *
 * ============================================================================
 * 一、功能
 * ============================================================================
 *   1. A/B 双区：默认启动 A 区；OTA 只写入非活动区，升级确认后 active 切到
 *      新槽，原槽自动成为下一次 OTA 的目标区。
 *   2. 上电等待 UART1（PA9 发送 / PA10 接收，115200 8N1）500ms，收到字符
 *      'R'（或 'r'）进入升级流程，其它字符忽略。
 *   3. 崩溃回滚：升级后 pending 槽连续 BOOT_MAX_TRIES 次启动仍未收到
 *      boot_confirm()（App 未完成初始化 → 视为崩溃），自动清除 pending，
 *      回到 active 槽（默认 A 区）。
 *   4. 校验：OTA 传输时边收边算 CRC32，收完后与主机给出的 CRC 比对；
 *      启动前对记录过长度的槽再做一次整段 CRC32 校验。手工烧录的槽没有
 *      长度记录，只校验向量表。
 *   5. 体积：不依赖 HAL / RTOS / libc（-nostdlib -Os），实测约 1.8KB，
 *      限制在 Flash 前 8KB 内。
 *
 * ============================================================================
 * 二、Flash 布局（默认按实测 128KB Flash；64KB 片定义 F1_BOOT_FLASH_64K）
 * ============================================================================
 *   区域        起始地址        大小
 *   Bootloader  0x08000000      8KB
 *   Slot A      0x08002000      56KB
 *   Slot B      0x08010000      56KB
 *   Meta 页 0   0x0801E000      1KB（两页轮换写，掉电保留一份）
 *   Meta 页 1   0x0801E400      1KB
 *
 *   64KB 片：A=0x08002000/24KB、B=0x08008000/24KB、Meta=0x0800E000/0x0800E400。
 *   注意：此时 App 必须小于 24KB。
 *
 * ============================================================================
 * 三、升级协议（Bootloader 收到 'R' 之后）
 * ============================================================================
 *   Boot -> Host : 'K'                          （就绪）
 *   Host -> Boot : u32 len（小端，1..槽大小）
 *                  u32 crc32（小端，覆盖后面 len 字节负载）
 *   Host -> Boot : len 字节负载（每字节等待上限 3s）
 *   Boot -> Host : 'K' 成功（随后 MCU 复位，启动新槽）
 *                  'E' 失败（长度/CRC/Flash 错误，返回继续启动旧槽）
 *
 * ============================================================================
 * 四、App 侧配合（把本文件加入 App 工程，且不要定义 BOOTLOADER_BUILD）
 * ============================================================================
 *   - 链接基址：A 区 0x08002000（B 区包用 0x08010000，OTA 包按目标槽链接）；
 *   - 启动时执行 SCB->VTOR = 自身基址（Bootloader 跳转前也会设置一次）；
 *   - 初始化完成后调用一次 boot_confirm()，确认本次启动成功：
 *       - 若当前是升级后的新槽：active 切到新槽、清除 pending；
 *       - 若只是普通启动：清除 tries 计数。
 *     未调用时，pending 槽累计 3 次启动后会被 Bootloader 判定失败并回滚。
 *   不定义 BOOTLOADER_BUILD 时，本文件只编译共享部分（元数据/Flash/CRC/
 *   boot_confirm），不会引入 main、UART、中断向量等 Bootloader 专用代码。
 *
 * ============================================================================
 * 五、构建
 * ============================================================================
 *   xmake build ros-boot-f108cb  →  build/cross/arm/release/ros-boot-f108cb.bin
 *   目标定义在 xmake.lua，仅编译 startup_stm32f10x_md.S 与本文件。
 */

#include "stm32f10x.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==========================================================================
 * 一、分区与元数据定义
 * ========================================================================== */

#if defined(F1_BOOT_FLASH_64K)
#define BOOT_SLOT_BASE0     0x08002000U     /* A 区起始 */
#define BOOT_SLOT_BASE1     0x08008000U     /* B 区起始 */
#define BOOT_SLOT_SIZE      (24U * 1024U)   /* 单槽大小 */
#define BOOT_META_PAGE0     0x0800E000U     /* 元数据页 0 */
#define BOOT_META_PAGE1     0x0800E400U     /* 元数据页 1 */
#else
#define BOOT_SLOT_BASE0     0x08002000U
#define BOOT_SLOT_BASE1     0x08010000U
#define BOOT_SLOT_SIZE      (56U * 1024U)
#define BOOT_META_PAGE0     0x0801E000U
#define BOOT_META_PAGE1     0x0801E400U
#endif

#define BOOT_FLASH_PAGE     1024U           /* F103 中容量：1KB/页 */
#define BOOT_META_MAGIC     0x54454D42U     /* 'BMET'，元数据结构有效标识 */
#define BOOT_SLOT_NONE      0xFFFFFFFFU     /* pending = 无待确认槽 */
#define BOOT_MAX_TRIES      3U              /* 新槽最多尝试启动次数 */

/*
 * 启动元数据：保存在两个独立的 1KB 页中，seq 较大的有效副本优先。
 * 结构按 4 字节对齐，长度 36 字节，写入时按半字（2 字节）编程。
 */
typedef struct {
    uint32_t magic;         /* BOOT_META_MAGIC */
    uint32_t seq;           /* 每次保存自增，用于选择最新副本 */
    uint32_t active;        /* 当前运行槽：0=A，1=B */
    uint32_t pending;       /* 待确认槽：0/1，BOOT_SLOT_NONE 表示无 */
    uint32_t tries;         /* pending 槽已连续启动的次数 */
    uint32_t len[2];        /* 各槽镜像长度；0 表示未通过 OTA 写入 */
    uint32_t crc[2];        /* 各槽镜像 CRC32（仅 OTA 写入时有效） */
    uint32_t meta_crc;      /* 前面所有字段的 CRC32 */
} boot_meta_t;

/* Flash 寄存器位（避免与设备头宏重名） */
#define BOOT_FLASH_SR_BSY       (1UL << 0)
#define BOOT_FLASH_SR_PGERR     (1UL << 2)
#define BOOT_FLASH_SR_WRPRTERR  (1UL << 4)
#define BOOT_FLASH_CR_PG        (1UL << 0)
#define BOOT_FLASH_CR_PER       (1UL << 1)
#define BOOT_FLASH_CR_STRT      (1UL << 6)
#define BOOT_FLASH_CR_LOCK      (1UL << 7)

#define BOOT_FLASH_TIMEOUT      0x200000UL  /* Flash 忙等上限（约几十 ms） */

/* ==========================================================================
 * 二、CRC32（CRC-32/IEEE，多项式 0xEDB88320，无查表，节省 Flash）
 * ========================================================================== */

/*
 * 增量计算：crc 初值传 0xFFFFFFFF，最后再异或 0xFFFFFFFF。
 * 逐位实现只有几十字节代码，56KB 全量校验在 8MHz 下约 0.1~0.2s。
 */
static uint32_t boot_crc32_update(uint32_t crc, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;

    while (len-- > 0U) {
        crc ^= (uint32_t)(*p++);
        for (i = 0U; i < 8U; i++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return crc;
}

/* 一次性计算（内部使用） */
static uint32_t boot_crc32(const void *data, uint32_t len)
{
    return boot_crc32_update(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;
}

/* ==========================================================================
 * 三、Flash 驱动（F103 寄存器操作，擦除按页、编程按半字）
 * ========================================================================== */

#if defined(BOOTLOADER_BUILD)
/*
 * -nostdlib 构建时编译器仍可能把结构体拷贝/清零优化成 memcpy/memset，
 * 这里给出最小实现；App 构建（定义了 libc）不会走这些定义。
 */
void *memcpy(void *dst, const void *src, unsigned int n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    while (n-- > 0U) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int c, unsigned int n)
{
    uint8_t *d = (uint8_t *)dst;

    while (n-- > 0U) {
        *d++ = (uint8_t)c;
    }
    return dst;
}
#endif

/* 等待 Flash 操作完成并检查错误标志；返回 0 成功 */
static int flash_wait(void)
{
    uint32_t timeout = BOOT_FLASH_TIMEOUT;

    while ((FLASH->SR & BOOT_FLASH_SR_BSY) != 0U) {
        if (--timeout == 0U) {
            return -1;
        }
    }
    if ((FLASH->SR & (BOOT_FLASH_SR_PGERR | BOOT_FLASH_SR_WRPRTERR)) != 0U) {
        FLASH->SR = BOOT_FLASH_SR_PGERR | BOOT_FLASH_SR_WRPRTERR;  /* 写 1 清除 */
        return -1;
    }
    return 0;
}

/* 解锁 Flash（重复调用安全） */
static void flash_unlock(void)
{
    if ((FLASH->CR & BOOT_FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
}

/* 上锁 Flash */
static void flash_lock(void)
{
    FLASH->CR |= BOOT_FLASH_CR_LOCK;
}

/*
 * 擦除 addr 开始、覆盖 len 字节的 Flash 区域（按页向上取整）。
 * 约束：addr 必须在 A 区之后且按页对齐；不允许擦除 Bootloader 自身。
 */
static int boot_flash_erase(uint32_t addr, uint32_t len)
{
    uint32_t page;
    uint32_t count;
    int rc = 0;

    if (len == 0U || addr < BOOT_SLOT_BASE0 ||
        (addr % BOOT_FLASH_PAGE) != 0U) {
        return -1;
    }
    count = (len + BOOT_FLASH_PAGE - 1U) / BOOT_FLASH_PAGE;

    flash_unlock();
    for (page = 0U; page < count; page++) {
        uint32_t p = addr + page * BOOT_FLASH_PAGE;

        if (flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = BOOT_FLASH_CR_PER;      /* 页擦除 */
        FLASH->AR = p;                      /* 目标地址 */
        FLASH->CR |= BOOT_FLASH_CR_STRT;    /* 启动擦除 */
        if (flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = 0U;
    }
    flash_lock();
    return rc;
}

/*
 * 从 addr 开始写入 len 字节（半字编程）。
 * 奇数长度时最后一个半字的高字节补 0xFF（Flash 擦除态），不参与 CRC。
 * 约束：addr 必须 2 字节对齐且在 A 区之后。
 */
static int boot_flash_write(uint32_t addr, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;
    int rc = 0;

    if (data == NULL || len == 0U || addr < BOOT_SLOT_BASE0 ||
        (addr & 1U) != 0U) {
        return -1;
    }

    flash_unlock();
    for (i = 0U; i < len; i += 2U) {
        uint16_t hw = (uint16_t)p[i];

        if ((i + 1U) < len) {
            hw |= (uint16_t)((uint16_t)p[i + 1U] << 8);
        } else {
            hw |= 0xFF00U;
        }
        if (flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = BOOT_FLASH_CR_PG;       /* 半字编程 */
        *(volatile uint16_t *)(uintptr_t)(addr + i) = hw;
        if (flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = 0U;
    }
    flash_lock();
    return rc;
}

/* ==========================================================================
 * 四、启动元数据（校验 / 加载 / 保存 / App 确认）
 * ========================================================================== */

/* 检查一份元数据副本是否合法 */
static bool meta_valid(const boot_meta_t *m)
{
    if (m->magic != BOOT_META_MAGIC) {
        return false;
    }
    if (m->meta_crc != boot_crc32(m, (uint32_t)sizeof(*m) - 4U)) {
        return false;
    }
    if (m->active > 1U) {
        return false;
    }
    return (m->pending == BOOT_SLOT_NONE) || (m->pending <= 1U);
}

/*
 * 读取最新有效元数据。
 * 两页都无效（首次上电）时返回默认值：active=A、pending=NONE。
 */
static void boot_meta_load(boot_meta_t *m)
{
    const boot_meta_t *p0 = (const boot_meta_t *)(uintptr_t)BOOT_META_PAGE0;
    const boot_meta_t *p1 = (const boot_meta_t *)(uintptr_t)BOOT_META_PAGE1;
    bool v0 = meta_valid(p0);
    bool v1 = meta_valid(p1);

    if (m == NULL) {
        return;
    }
    /* seq 用有符号差值比较，允许回绕 */
    if (v0 && (!v1 || (int32_t)(p0->seq - p1->seq) >= 0)) {
        *m = *p0;
        return;
    }
    if (v1) {
        *m = *p1;
        return;
    }

    memset(m, 0, sizeof(*m));
    m->magic = BOOT_META_MAGIC;
    m->active = 0U;
    m->pending = BOOT_SLOT_NONE;
}

/*
 * 保存元数据：seq 自增后写入与奇偶对应的页（两页轮换），写完回读校验。
 * 轮换写保证任一时刻至少有一页是完整的旧副本。
 */
static int boot_meta_save(boot_meta_t *m)
{
    uint32_t page;
    uint32_t i;
    const uint16_t *hw;
    const uint16_t *verify;

    if (m == NULL) {
        return -1;
    }
    m->magic = BOOT_META_MAGIC;
    m->meta_crc = boot_crc32(m, (uint32_t)sizeof(*m) - 4U);
    m->seq++;

    page = ((m->seq & 1U) != 0U) ? BOOT_META_PAGE0 : BOOT_META_PAGE1;
    if (boot_flash_erase(page, BOOT_FLASH_PAGE) != 0) {
        return -1;
    }
    hw = (const uint16_t *)m;
    for (i = 0U; i < (uint32_t)(sizeof(*m) / 2U); i++) {
        if (boot_flash_write(page + i * 2U, &hw[i], 2U) != 0) {
            return -1;
        }
    }

    verify = (const uint16_t *)(uintptr_t)page;
    for (i = 0U; i < (uint32_t)(sizeof(*m) / 2U); i++) {
        if (verify[i] != hw[i]) {
            return -1;                      /* 回读不一致：视为失败 */
        }
    }
    return 0;
}

/*
 * 对外接口（Bootloader 与 App 均可调用）：
 *   App 初始化完成后调用一次，确认当前槽启动成功。
 *   - 有 pending：active 切换到 pending 槽，清除 pending 与 tries；
 *   - 无 pending：仅清零 tries（普通启动）。
 * 若 App 从不调用，pending 槽累计 3 次启动后会被回滚到旧槽。
 */
void boot_confirm(void)
{
    boot_meta_t m;

    boot_meta_load(&m);
    if (m.pending != BOOT_SLOT_NONE) {
        m.active = m.pending;
        m.pending = BOOT_SLOT_NONE;
        m.tries = 0U;
        (void)boot_meta_save(&m);
    } else if (m.tries != 0U) {
        m.tries = 0U;
        (void)boot_meta_save(&m);
    }
}

/* ==========================================================================
 * 五、Bootloader 本体（仅定义 BOOTLOADER_BUILD 时编译）
 * ========================================================================== */

#if defined(BOOTLOADER_BUILD)

#define UART_BRR_8MHZ_115200    69U     /* 8MHz / 115200 ≈ 69.44，误差 0.6% */
#define UPDATE_BYTE_TIMEOUT_MS  3000U   /* 升级时单字节接收超时 */
#define UPDATE_HANDSHAKE_MS     500U    /* 上电等待 'R' 的时间 */

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

    SysTick->CTRL = 0U;
    SCB->VTOR = base;

    __asm volatile ("cpsid i" ::: "memory");
    __asm volatile ("msr msp, %0" :: "r" (sp));
    __asm volatile ("msr control, %0" :: "r" (0));
    __asm volatile ("isb");
    ((void (*)(void))(uintptr_t)pc)();

    for (;;) {
    }
}

/* ---------------------------- 升级流程 ---------------------------- */

/*
 * 接收 OTA 镜像并写入非活动槽。
 * 成功：更新元数据（pending=新槽）、回复 'K'、复位进入新槽；
 * 失败：回复 'E' 并返回调用者继续启动旧槽。
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

    uart_putc('K');                                 /* 就绪 */
    if (uart_read_u32(&len) != 0 || uart_read_u32(&crc) != 0 ||
        len == 0U || len > BOOT_SLOT_SIZE) {
        uart_putc('E');
        return;
    }
    if (boot_flash_erase(base, len) != 0) {         /* 按需擦除目标区 */
        uart_putc('E');
        return;
    }

    while (got < len) {
        int c = uart_getc(UPDATE_BYTE_TIMEOUT_MS);

        if (c < 0) {
            uart_putc('E');
            return;
        }
        buf[n++] = (uint8_t)c;
        got++;
        /* 缓冲满或收完：先算 CRC 再写 Flash（写地址始终 2 字节对齐） */
        if (n == sizeof(buf) || got == len) {
            calc = boot_crc32_update(calc, buf, n);
            if (boot_flash_write(base + got - n, buf, n) != 0) {
                uart_putc('E');
                return;
            }
            n = 0U;
        }
    }

    if ((calc ^ 0xFFFFFFFFU) != crc) {              /* 整包 CRC 不符 */
        uart_putc('E');
        return;
    }

    /* 记录新槽信息，pending 指向它；下次启动会先试新槽 */
    m->pending = target;
    m->tries = 0U;
    m->len[target] = len;
    m->crc[target] = crc;
    if (boot_meta_save(m) != 0) {
        uart_putc('E');
        return;
    }

    uart_putc('K');
    uart_flush();
    NVIC_SystemReset();                             /* 复位并启动新槽 */
}

/* ---------------------------- main ---------------------------- */

/*
 * 启动流程：
 *   1) 初始化 1ms 时基与 UART1；
 *   2) 等待 500ms 升级口令 'R'，命中则进入升级；
 *   3) 选择启动槽（pending 优先），无效则尝试另一槽；
 *   4) 未确认的 pending 槽累计失败次数，达到上限回滚到 active；
 *   5) 目标槽无效时停留在 Bootloader 反复等待升级；
 *   6) 跳转到目标槽。
 */
int main(void)
{
    boot_meta_t m;
    uint32_t target;

    (void)SysTick_Config(8000U);                    /* 8MHz / 1000 = 1ms */
    uart_init();
    boot_meta_load(&m);

    /* 1) 启动前 500ms 升级握手 */
    if (wait_update_req(UPDATE_HANDSHAKE_MS)) {
        do_update(&m);                              /* 成功不返回 */
    }

    /* 2) 选择启动槽：有 pending 先试新槽，否则跑 active */
    target = (m.pending != BOOT_SLOT_NONE) ? m.pending : m.active;
    if (!slot_valid(&m, target)) {
        uint32_t other = target ^ 1U;               /* 0/1 互换 */

        if (slot_valid(&m, other)) {
            target = other;
            m.pending = BOOT_SLOT_NONE;             /* 回落到旧槽 */
            (void)boot_meta_save(&m);
        } else {
            /* 两槽都没有可运行程序：留在 Bootloader 等待升级 */
            for (;;) {
                if (wait_update_req(1000U)) {
                    do_update(&m);
                }
            }
        }
    }

    /* 3) 崩溃回滚：pending 槽连续 BOOT_MAX_TRIES 次未确认则退回 active */
    if (m.pending != BOOT_SLOT_NONE) {
        if (m.tries >= BOOT_MAX_TRIES) {
            m.pending = BOOT_SLOT_NONE;
            m.tries = 0U;
            (void)boot_meta_save(&m);
            target = m.active;                      /* 回到 A 区（原活动槽） */
        } else {
            m.tries++;                              /* 本次启动尝试 +1 */
            (void)boot_meta_save(&m);
        }
    }

    /* 4) 回滚目标也必须有效，否则停在 Bootloader */
    if (!slot_valid(&m, target)) {
        for (;;) {
            if (wait_update_req(1000U)) {
                do_update(&m);
            }
        }
    }

    delay_ms(10U);                                  /* 等串口最后一字节发完 */
    jump_to(slot_base(target));
    return 0;                                       /* 不会到这里 */
}

#endif /* BOOTLOADER_BUILD */
