-- apps/f407ve/xmake.lua - F407VE + ML307 应用、固件与 Bootloader
if is_plat("cross") and ROS_FAMILY == "f4" then
    local app_dir = path.join(ROS_ROOT, "apps/f407ve")
    local board_dir = path.join(ROS_ROOT, "platforms/stm32f4")
    local chip = ROS_CHIP or ""
    local incs = { app_dir, board_dir,
                   path.join(ROS_ROOT, "drivers/w25qxx") }
    local cmsis_dev = (ROS_HAL_DRV ~= "")
        and path.join(ROS_HAL_DRV, "CMSIS", "Device", "ST", "STM32F4xx", "Include") or ""
    if ROS_HAL_INC ~= "" then table.insert(incs, ROS_HAL_INC) end
    if cmsis_dev ~= "" then table.insert(incs, cmsis_dev) end
    if ROS_CMSIS_INC ~= "" then table.insert(incs, ROS_CMSIS_INC) end

    local hal_ok = (ROS_HAL_SRC ~= "" and os.isdir(ROS_HAL_SRC))

    ros_object("app-f407ve", {
        scope = "none",
        files = { "app_main.c", "app_config.c", "app_device.c", "app_fs.c",
                  "app_ml307.c", "app_master.c", "app_stress.c", "app_memtest.c",
                  "app_timing.c", "app_shelltest.c" },
        defines = { "USE_HAL_DRIVER", ROS_CHIP, "HSE_VALUE=8000000" },
        includedirs = incs,
    })

    -- 最终固件目标：平台库（含 HAL 子集）+ 板级对象 + 应用对象
    local fw_deps = {}
    for _, dep in ipairs(ROS_OBJ_CROSS) do table.insert(fw_deps, dep) end
    table.insert(fw_deps, "board-f407ve")
    table.insert(fw_deps, "app-f407ve")

    ros_firmware("f407ve", {
        deps = fw_deps,
        -- 本板 partition.h 启用 FLASH_OTA_COMPRESS，.ota 用 raw deflate 打包
        ota_compress = true,
        defines = { "ROS_PLATFORM_STM32", "ROS_STM32_F4", "USE_HAL_DRIVER", ROS_CHIP,
                    "HSE_VALUE=8000000" },
        includedirs = incs,
        ldflags = {
            "-nostartfiles",
            "-T" .. path.join(app_dir, "stm32f407ve.ld"),
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

    ros_bootloader("f407ve-boot", {
        deps = { "boot-f407ve", "ros-cmp-fkv" },
        defines = { ROS_CHIP, "HSE_VALUE=8000000" },
        includedirs = incs,
        ldflags = {
            "-nostartfiles", "-nostdlib",
            "-Wl,--defsym=BOOT_FLASH_LEN=0x4000",    -- Bootloader 16KB
            "-Wl,--defsym=BOOT_RAM_LEN=0x20000",     -- SRAM 128KB
            "-T" .. path.join(app_dir, "bootloader.ld"),
            "-Wl,--gc-sections",
        },
    })
end
