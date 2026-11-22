/*
 * bootloader.ld - STM32F407VE APP/BACKUP/OTA Bootloader 链接脚本
 *
 * 仅使用 S0（16KB @0x08000000）；S1 为 Boot Meta，S2-S3 为配置，S4 为错误日志，
 * App 区从 0x08020000 开始，备份/升级包区各 128KB 位于 0x08040000 / 0x08060000。
 * RAM 128KB，主栈从 SRAM 顶端向下增长（Bootloader 不使用堆）。
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 16K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
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
