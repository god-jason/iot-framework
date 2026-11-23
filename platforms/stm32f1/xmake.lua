-- platforms/stm32f1/xmake.lua - STM32F1 外设（纯寄存器）与 F103CB 板级/固件
-- 公共实现见 platforms/stm32/xmake.lua
if is_plat("cross") and ROS_FAMILY == "f1" then
    local board = "platforms/stm32f1"
    -- 闭包（after_load）运行时看不到根脚本的全局变量，先捕获为局部变量
    local dfp_inc = ROS_DFP_INC or ""
    local cmsis_inc = ROS_CMSIS_INC or ""
    local incs = { path.join(ROS_ROOT, board) }
    if dfp_inc ~= "" then table.insert(incs, dfp_inc) end
    if cmsis_inc ~= "" then table.insert(incs, cmsis_inc) end

    -- 寄存器外设实现（板级文件由下方 board 目标单独编译）
    ros_object("ros-platform-stm32f1", {
        scope = "cross",
        files = { "*.c" },
        exclude = { "board.c", "boot_flash.c", "board_rtc.c" },
        defines = { "STM32F10X_MD", "HSE_VALUE=8000000" },
        includedirs = incs,
    })

    -- App 板级对象：启动文件 + board*.c + 平台共享 boot/config/ota 实现
    --（共享实现依赖本板 partition.h，故由板级目标编译）
    ros_object("board-f103cb", {
        scope = "none",
        files = { "board.c", "boot_flash.c", "board_rtc.c",
                  "startup_stm32f10x_md.S",
                  path.join(ROS_ROOT, "platforms/stm32/stm32_main.c"),
                  path.join(ROS_ROOT, "platforms/stm32/stm32_meta.c"),
                  path.join(ROS_ROOT, "platforms/stm32/stm32_config.c"),
                  path.join(ROS_ROOT, "platforms/stm32/stm32_ota.c") },
        defines = { "STM32F10X_MD", "HSE_VALUE=8000000" },
        includedirs = incs,
    })

    -- Bootloader 板级对象（不依赖内核 / libc）
    ros_object("boot-f103cb", {
        scope = "none",
        files = { "startup_stm32f10x_md.S", "boot_flash.c",
                  path.join(ROS_ROOT, "platforms/stm32/bootloader.c"),
                  path.join(ROS_ROOT, "platforms/stm32/stm32_meta.c") },
        defines = { "STM32F10X_MD", "HSE_VALUE=8000000" },
        includedirs = incs,
        cflags = { "-ffreestanding" },
    })

    local fw_deps = {}
    for _, dep in ipairs(ROS_OBJ_CROSS) do table.insert(fw_deps, dep) end
    table.insert(fw_deps, "board-f103cb")
    table.insert(fw_deps, "app-f103cb")

    ros_firmware("ros-fw-f103cb", {
        deps = fw_deps,
        defines = { "ROS_PLATFORM_STM32", "ROS_STM32_F1", "STM32F10X_MD",
                    "HSE_VALUE=8000000", "ROS_USE_TRACE=0", "LOG_LEVEL=LOG_LEVEL_NONE" },
        includedirs = incs,
        ldflags = {
            "-nostartfiles",
            "-T" .. board .. "/stm32f103cb.ld",
            "--specs=nano.specs",
            "-Wl,--gc-sections",
        },
        after_load = function (target)
            if dfp_inc == "" or cmsis_inc == "" then
                cprint("${yellow}[firmware] 未找到 Keil STM32F1xx_DFP 或 CMSIS pack，" ..
                       "请用 --dfp_inc / --cmsis_inc 指定头文件目录${clear}")
            else
                cprint("${dim}[firmware] DFP: " .. dfp_inc .. "${clear}")
                cprint("${dim}[firmware] CMSIS: " .. cmsis_inc .. "${clear}")
            end
        end,
    })

    ros_bootloader("ros-boot-f103cb", {
        deps = { "boot-f103cb" },
        defines = { "STM32F10X_MD", "HSE_VALUE=8000000" },
        includedirs = incs,
        ldflags = {
            "-nostartfiles", "-nostdlib",
            "-Wl,--defsym=BOOT_FLASH_LEN=0x2000",    -- Bootloader 8KB
            "-Wl,--defsym=BOOT_RAM_LEN=0x5000",      -- SRAM 20KB
            "-T" .. path.join(ROS_ROOT, "platforms/stm32/bootloader.ld"),
            "-Wl,--gc-sections",
        },
    })
end
