-- platforms/stm32f4/xmake.lua - STM32F4 外设（Cube HAL）与 F407VE 板级/固件
-- 公共实现见 platforms/stm32/xmake.lua
if is_plat("cross") and ROS_FAMILY == "f4" then
    local board = "platforms/stm32f4"
    local chip = ROS_CHIP or ""
    local incs = { path.join(ROS_ROOT, board) }
    local cmsis_dev = (ROS_HAL_DRV ~= "")
        and path.join(ROS_HAL_DRV, "CMSIS", "Device", "ST", "STM32F4xx", "Include") or ""
    if ROS_HAL_INC ~= "" then table.insert(incs, ROS_HAL_INC) end
    if cmsis_dev ~= "" then table.insert(incs, cmsis_dev) end
    if ROS_CMSIS_INC ~= "" then table.insert(incs, ROS_CMSIS_INC) end

    local hal_ok = (ROS_HAL_SRC ~= "" and os.isdir(ROS_HAL_SRC))
    local fw_deps = {}
    for _, dep in ipairs(ROS_OBJ_CROSS) do table.insert(fw_deps, dep) end

    -- Cube HAL 子集（仅固件使用）
    if hal_ok then
        local hal_modules = {
            "stm32f4xx_hal.c", "stm32f4xx_hal_cortex.c",
            "stm32f4xx_hal_rcc.c", "stm32f4xx_hal_rcc_ex.c",
            "stm32f4xx_hal_gpio.c", "stm32f4xx_hal_uart.c",
            "stm32f4xx_hal_dma.c", "stm32f4xx_hal_dma_ex.c",
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
            scope = "none",
            files = hal_files,
            defines = { "USE_HAL_DRIVER", ROS_CHIP, "HSE_VALUE=8000000" },
            includedirs = incs,
        })
        table.insert(fw_deps, "hal-f407ve")
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
        table.insert(fw_deps, "ros-platform-stm32f4")
    end

    -- App 板级对象（共享 boot/config/ota 实现依赖本板 partition.h，故在此编译）
    ros_object("board-f407ve", {
        scope = "none",
        files = { "board.c", "boot_flash.c", "board_rtc.c",
                  "startup_stm32f407.S",
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
        files = { "startup_stm32f407.S", "boot_flash.c",
                  path.join(ROS_ROOT, "platforms/stm32/bootloader.c"),
                  path.join(ROS_ROOT, "platforms/stm32/boot_meta.c"),
                  path.join(ROS_ROOT, "platforms/stm32/boot_config.c") },
        defines = { ROS_CHIP, "HSE_VALUE=8000000" },
        includedirs = incs,
        cflags = { "-ffreestanding" },
    })

    table.insert(fw_deps, "board-f407ve")
    table.insert(fw_deps, "app-f407ve")

    ros_firmware("ros-fw-f407ve", {
        deps = fw_deps,
        defines = { "ROS_PLATFORM_STM32", "ROS_STM32_F4", "USE_HAL_DRIVER", ROS_CHIP,
                    "HSE_VALUE=8000000",
                    "LOG_LEVEL=LOG_LEVEL_INFO" },
        includedirs = incs,
        ldflags = {
            "-nostartfiles",
            "-T" .. board .. "/stm32f407ve.ld",
            "--specs=nano.specs",
            "-Wl,--gc-sections",
            "-lm",
        },
        after_load = function (target)
            if not hal_ok then
                cprint("${yellow}[f407ve] 未找到 Cube HAL 源码，请用 " ..
                       "--cube_hal_dir=<.../STM32F4xx_HAL_Driver> 指定${clear}")
            end
            cprint("${dim}[f407ve] chip=" .. chip .. "${clear}")
        end,
    })

    ros_bootloader("ros-boot-f407ve", {
        deps = { "boot-f407ve" },
        defines = { ROS_CHIP, "HSE_VALUE=8000000" },
        includedirs = incs,
        ldflags = {
            "-nostartfiles", "-nostdlib",
            "-Wl,--defsym=BOOT_FLASH_LEN=0x4000",    -- Bootloader 16KB
            "-Wl,--defsym=BOOT_RAM_LEN=0x20000",     -- SRAM 128KB
            "-T" .. path.join(ROS_ROOT, "platforms/stm32/bootloader.ld"),
            "-Wl,--gc-sections",
        },
    })
end
