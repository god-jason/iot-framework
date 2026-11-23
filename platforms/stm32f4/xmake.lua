-- platforms/stm32f4/xmake.lua - STM32F4 外设库与 F407VE 板级对象
-- 公共实现见 platforms/stm32/xmake.lua；最终固件/Bootloader 目标定义在
-- apps/f407ve/xmake.lua。
if is_plat("cross") and ROS_FAMILY == "f4" then
    local board = "platforms/stm32f4"
    local app_dir = path.join(ROS_ROOT, "apps/f407ve")
    local chip = ROS_CHIP or ""
    local incs = { path.join(ROS_ROOT, board), app_dir }
    local cmsis_dev = (ROS_HAL_DRV ~= "")
        and path.join(ROS_HAL_DRV, "CMSIS", "Device", "ST", "STM32F4xx", "Include") or ""
    if ROS_HAL_INC ~= "" then table.insert(incs, ROS_HAL_INC) end
    if cmsis_dev ~= "" then table.insert(incs, cmsis_dev) end
    if ROS_CMSIS_INC ~= "" then table.insert(incs, ROS_CMSIS_INC) end

    local hal_ok = (ROS_HAL_SRC ~= "" and os.isdir(ROS_HAL_SRC))

    -- Cube HAL 子集库（供固件链接）
    if hal_ok then
        local hal_modules = {
            "stm32f4xx_hal.c", "stm32f4xx_hal_cortex.c",
            "stm32f4xx_hal_rcc.c", "stm32f4xx_hal_rcc_ex.c",
            "stm32f4xx_hal_gpio.c", "stm32f4xx_hal_uart.c",
            "stm32f4xx_hal_dma.c", "stm32f4xx_hal_dma_ex.c",
            "stm32f4xx_hal_spi.c",
            "stm32f4xx_hal_pwr.c", "stm32f4xx_hal_pwr_ex.c",
            "stm32f4xx_hal_flash.c", "stm32f4xx_hal_flash_ex.c",
            "stm32f4xx_hal_tim.c", "stm32f4xx_hal_tim_ex.c",
            "stm32f4xx_hal_rtc.c", "stm32f4xx_hal_rtc_ex.c"
        }
        local hal_files = {}
        for _, name in ipairs(hal_modules) do
            table.insert(hal_files, path.join(ROS_HAL_SRC, name))
        end
        local sys_src = (ROS_HAL_DRV ~= "")
            and path.join(ROS_HAL_DRV, "CMSIS", "Device", "ST", "STM32F4xx",
                          "Source", "Templates", "system_stm32f4xx.c") or ""
        if sys_src ~= "" and os.isfile(sys_src) then
            table.insert(hal_files, sys_src)
        end

        ros_object("hal-f407ve", {
            scope = "cross",
            files = hal_files,
            defines = { "USE_HAL_DRIVER", ROS_CHIP, "HSE_VALUE=8000000" },
            includedirs = incs,
        })
    end

    -- 外设适配模块（板级文件由下方 board 目标单独编译）
    if hal_ok then
        ros_object("ros-platform-stm32f4", {
            scope = "cross",
            files = { "*.c" },
            exclude = { "board.c", "boot_flash.c", "board_rtc.c" },
            defines = { "USE_HAL_DRIVER", ROS_CHIP, "ROS_TRACE_CLOCK_HZ=168000000" },
            includedirs = incs,
        })
    end

    -- App 板级对象（共享 boot/config/ota 实现依赖本板 partition.h，故在此编译）
    ros_object("board-f407ve", {
        scope = "none",
        files = { "board.c", "boot_flash.c", "board_rtc.c",
                  path.join(app_dir, "startup_stm32f407.S"),
                  path.join(ROS_ROOT, "platforms/stm32/stm32_main.c"),
                  path.join(ROS_ROOT, "platforms/stm32/stm32_shell.c"),
                  path.join(ROS_ROOT, "platforms/stm32/boot_meta.c"),
                  path.join(ROS_ROOT, "platforms/stm32/boot_crash.c"),
                  path.join(ROS_ROOT, "platforms/stm32/boot_config.c"),
                  path.join(ROS_ROOT, "platforms/stm32/stm32_config.c"),
                  path.join(ROS_ROOT, "platforms/stm32/stm32_ota.c") },
        defines = { "USE_HAL_DRIVER", ROS_CHIP, "HSE_VALUE=8000000" },
        includedirs = incs,
    })

    -- Bootloader 板级对象
    ros_object("boot-f407ve", {
        scope = "none",
        files = { path.join(app_dir, "startup_stm32f407.S"), "boot_flash.c",
                  path.join(ROS_ROOT, "platforms/stm32/bootloader.c"),
                  path.join(ROS_ROOT, "platforms/stm32/boot_meta.c"),
                  path.join(ROS_ROOT, "platforms/stm32/boot_config.c") },
        defines = { ROS_CHIP, "HSE_VALUE=8000000" },
        includedirs = incs,
        cflags = { "-ffreestanding" },
    })
end
