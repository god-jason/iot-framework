/*
 * stm32f407ve.ld - STM32F407VE APP 区固件链接脚本（GNU ld）
 *
 * APP 区：FLASH 0x08010000 起 192KB（S4 64KB + S5 128KB）；SRAM 128KB @0x20000000。
 * 前部为 Bootloader / KV 双区 / 错误日志，OTA 区 0x08040000 存放升级包
 * （S6+S7 共 256KB）。
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08010000, LENGTH = 192K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

_Min_Heap_Size  = 0x400;
_Min_Stack_Size = 0x800;

/* newlib 堆上限：主栈预留区之下，_sbrk 不再越过栈 */
PROVIDE ( __heap_limit = _estack - _Min_Stack_Size );

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
        _sidata = _etext;
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
