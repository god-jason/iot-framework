/*
 * startup_stm32f407.S - STM32F407 (Cortex-M4) GCC 启动文件
 *
 * 与 Cube 提供的 gcc 启动文件等价，但有意不为 SVC/PendSV/SysTick 建立
 * 弱别名：让向量表的未定义引用把 libros-stm32-f4.a 中的 RTOS 移植层
 * 对象拉入链接（Cube 版本会给它们绑 Default_Handler，导致 tick 与任务
 * 切换失效）。
 *
 * Reset_Handler 完成 .data 复制、.bss 清零后依次调用 SystemInit()
 * （Cube system_stm32f4xx.c：向量表 + FPU）与 main()。
 */

.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

.global g_pfnVectors
.global Default_Handler

/* ---------------------------------------------------------------- 向量表 */
.section .isr_vector, "a", %progbits
.type g_pfnVectors, %object
g_pfnVectors:
    .word _estack                       /* 0: 主栈指针初值 */
    .word Reset_Handler                 /* 1: Reset */
    .word NMI_Handler                   /* 2: NMI */
    .word HardFault_Handler             /* 3: HardFault */
    .word MemManage_Handler             /* 4: MemManage */
    .word BusFault_Handler              /* 5: BusFault */
    .word UsageFault_Handler            /* 6: UsageFault */
    .word 0                             /* 7..10: 保留 */
    .word 0
    .word 0
    .word 0
    .word SVC_Handler                   /* 11: SVCall */
    .word DebugMon_Handler              /* 12: DebugMonitor */
    .word 0                             /* 13: 保留 */
    .word PendSV_Handler                /* 14: PendSV（RTOS 切换） */
    .word SysTick_Handler               /* 15: SysTick（RTOS tick） */
    /* STM32F407 共 82 个外部中断，全部落进 Default_Handler */
    .rept 82
    .word Default_Handler
    .endr
    .size g_pfnVectors, . - g_pfnVectors

/* ------------------------------------------------------------ Reset_Handler */
.section .text.Reset_Handler, "ax", %progbits
.weak Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    /* .data: FLASH(_sidata) -> RAM(_sdata.._edata) */
    ldr   r0, =_sidata
    ldr   r1, =_sdata
    ldr   r2, =_edata
    subs  r2, r2, r1
    ble   2f
1:
    ldrb  r3, [r0]
    adds  r0, #1
    strb  r3, [r1]
    adds  r1, #1
    subs  r2, #1
    bgt   1b
2:
    /* .bss 清零 */
    ldr   r0, =_sbss
    ldr   r1, =_ebss
    movs  r2, #0
3:
    cmp   r0, r1
    bge   4f
    str   r2, [r0]
    adds  r0, #4
    b     3b
4:
    bl    SystemInit
    bl    main
    b     .                             /* main 返回则停机 */
    .ltorg
    .size Reset_Handler, . - Reset_Handler

/* --------------------------------------------------------- 默认中断处理 */
.section .text.Default_Handler, "ax", %progbits
.type Default_Handler, %function
Default_Handler:
    b     .
    .size Default_Handler, . - Default_Handler

/* 只给不由 RTOS 提供的系统异常建立弱别名 */
    .weak      NMI_Handler
    .thumb_set NMI_Handler, Default_Handler
    .weak      HardFault_Handler
    .thumb_set HardFault_Handler, Default_Handler
    .weak      MemManage_Handler
    .thumb_set MemManage_Handler, Default_Handler
    .weak      BusFault_Handler
    .thumb_set BusFault_Handler, Default_Handler
    .weak      UsageFault_Handler
    .thumb_set UsageFault_Handler, Default_Handler
    .weak      DebugMon_Handler
    .thumb_set DebugMon_Handler, Default_Handler
    /* SVC / PendSV / SysTick 刻意不在此定义：让向量表的未定义引用
     * 把 libros-stm32-f4.a 中的 RTOS 移植层对象拉入链接。 */

.end
