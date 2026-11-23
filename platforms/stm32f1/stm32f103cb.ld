/*
 * stm32f103cb.ld - STM32F1 128KB Flash 板 App 固件链接脚本（GNU ld）
 *
 * App 区：FLASH 0x08006000 起 52KB（带 ML307 全量测试的 demo 放宽到 60KB，
 * 与 OTA 区重叠，仅用于功能验证）；SRAM 20KB @ 0x20000000。
 * Bootloader 占 0x08000000..0x08001FFF（8KB），Boot Meta/配置/错误日志在前部，
 * 升级包区在 0x08013000（52KB）。App 启动时把 SCB->VTOR 指向 App 区基址。
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08006000, LENGTH = 60K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* 主栈（MSP）从 SRAM 顶端向下增长 */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* 给 newlib 的 _sbrk 留堆空间（固件不使用 newlib malloc），栈顶预留 1KB 主栈
 * （任务上下文在 PSP，MSP 仅中断使用；省出的 512B 给 TLSF 任务堆） */
_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x400;

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
