/*
 * stm32f108cb_b.ld - STM32F1 128KB Flash 板 B 区固件链接脚本（GNU ld）
 *
 * B 区：FLASH 0x08010000 起 56KB；SRAM 20KB @ 0x20000000。
 * 与 A 区脚本仅链接基址不同，用于生成 OTA 升级包（写入 B 区后启动）。
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08010000, LENGTH = 56K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* 主栈（MSP）从 SRAM 顶端向下增长 */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* 给 newlib 的 _sbrk 留堆空间，栈顶预留 2KB 主栈 */
_Min_Heap_Size  = 0x400;
_Min_Stack_Size = 0x800;

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
        _etext  = .;
        _sidata = _etext;          /* .data 的 LMA */
    } > FLASH

    .data : AT (_sidata)
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit .noinit.*)
        . = ALIGN(4);
    } > RAM

    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE ( end   = . );
        PROVIDE ( _end  = . );
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
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
