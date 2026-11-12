/*
 * stm32f103c8.ld - STM32F103C8T6 链接脚本（GNU ld）
 *
 * FLASH 64KB @ 0x08000000, SRAM 20KB @ 0x20000000。
 * 换其它 F1 中容量芯片时只需修改下面 MEMORY 里的两个 LENGTH：
 *   64K/20K : STM32F103C8T6 (Blue Pill)
 *  128K/20K : STM32F103CBT6
 *  512K/64K : STM32F103ZET6（需同时换 HD 启动向量表与 STM32F10X_HD 宏）
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
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

    /*
     * 复位不清零段：崩溃现场记录（ros_crash）放在这里，
     * HardFault/断言后复位，下次启动仍能读到现场并上报。
     * 必须位于 .bss 之后、堆栈保留区之前，且不能被 _sbrk 覆盖。
     */
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

    /* 未使用的 newlib 元数据，避免把 C++ 异常表塞进 Flash */
    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.note*)
        *(.comment*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
