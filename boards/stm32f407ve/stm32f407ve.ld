/*
 * stm32f407ve.ld - STM32F407VE A 区固件链接脚本（GNU ld）
 *
 * A 区：FLASH 0x08020000 起 128KB（S5）；SRAM 128KB @0x20000000。
 * Bootloader/配置/日志/Meta 在前部，B 区 0x08040000 为备份，
 * C 区 0x08060000 存放升级包；B/C 区内容均为按本地址链接的镜像副本。
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08020000, LENGTH = 128K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

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
