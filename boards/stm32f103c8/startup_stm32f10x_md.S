/*
 * startup_stm32f10x_md.S - STM32F103C8 (Medium Density) GCC 启动文件
 *
 * Keil STM32F1xx_DFP 只提供 ARMASM 格式的启动文件，本文件为等价的
 * GNU as 版本，供 arm-none-eabi-gcc 使用：
 *   - 向量表位于 .isr_vector（链接脚本把它放在 0x08000000）；
 *   - Reset_Handler 完成 .data 复制、.bss 清零后调用 SystemInit() / main()；
 *   - PendSV_Handler / SysTick_Handler 由 libros-stm32-f1.a 提供弱定义，
 *     向量表引用它们会把 RTOS 移植层对象从静态库中拉入链接。
 */

.syntax unified
.cpu cortex-m3
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
    /* STM32F10x MD 共 43 个外部中断，全部落进 Default_Handler */
    .rept 43
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
     * 把 libros-stm32-f1.a 中的 RTOS 移植层对象拉入链接。 */

.end
