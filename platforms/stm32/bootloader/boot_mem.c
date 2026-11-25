/*
 * boot_mem.c - Bootloader 专用最小 libc 子集（-nostdlib）
 *
 * components/kvfs 会用到 memcpy/memset/memcmp；Cortex-M3 上还会用到
 * 软件除法辅助 __aeabi_uidiv。Bootloader 不链接 libc/libgcc，
 * 因此在这里提供最小实现，只编入 boot-* 目标。
 *
 * 注意：以 -ffreestanding 编译，避免编译器把循环改写成对自身的调用。
 */

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    while (n > 0U) {
        *d++ = *s++;
        n--;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;

    while (n > 0U) {
        *d++ = (uint8_t)c;
        n--;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;

    while (n > 0U) {
        if (*pa != *pb) {
            return (int)*pa - (int)*pb;
        }
        pa++;
        pb++;
        n--;
    }
    return 0;
}

/* 无硬件除法的内核（Cortex-M3）由编译器引用 */
uint32_t __aeabi_uidiv(uint32_t num, uint32_t den)
{
    uint32_t quot = 0U;
    uint32_t bit = 1U;

    if (den == 0U) {
        return 0xFFFFFFFFU;
    }
    while ((den & 0x80000000U) == 0U && (den << 1U) <= num) {
        den <<= 1U;
        bit <<= 1U;
    }
    while (bit != 0U) {
        if (num >= den) {
            num -= den;
            quot |= bit;
        }
        den >>= 1U;
        bit >>= 1U;
    }
    return quot;
}
