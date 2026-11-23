/*
 * bootloader.ld - STM32 Bootloader 通用链接脚本（F1/F4 共用）
 *
 * 仅使用 Flash 首个分区（Bootloader 自身），不定义堆；
 * 主栈从 SRAM 顶端向下增长（Bootloader 不使用堆）。
 *
 * 分区大小由板级 xmake 通过 --defsym 传入，需在 -T 之前定义：
 *   BOOT_FLASH_LEN  Bootloader 分区大小（F1 8K / F4 16K）
 *   BOOT_RAM_LEN    片内 SRAM 大小（F1 20K / F4 128K）
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = BOOT_FLASH_LEN
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = BOOT_RAM_LEN
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.glue_7 .glue_7t)
        *(.eh_frame)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH
    _sidata = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.note*)
        *(.comment*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
