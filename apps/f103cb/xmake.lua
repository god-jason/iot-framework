-- apps/f103cb/xmake.lua - F103CB + ML307 应用、固件与 Bootloader
if is_plat("cross") and ROS_FAMILY == "f1" then
    local app_dir = path.join(ROS_ROOT, "apps/f103cb")
    local board_dir = path.join(ROS_ROOT, "platforms/stm32f1")
    local dfp_inc = ROS_DFP_INC or ""
    local cmsis_inc = ROS_CMSIS_INC or ""
    local incs = { app_dir, board_dir }
    if dfp_inc ~= "" then table.insert(incs, dfp_inc) end
    if cmsis_inc ~= "" then table.insert(incs, cmsis_inc) end

    ros_object("app-f103cb", {
        scope = "none",
        files = { "app_main.c", "app_uart.c", "app_device.c", "app_ml307.c",
                  "app_sched.c", "app_shell.c" },
        defines = { "STM32F10X_MD", "HSE_VALUE=8000000" },
        includedirs = incs,
    })

    -- 最终固件目标：平台库 + 板级对象 + 应用对象
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
            "-T" .. path.join(app_dir, "stm32f103cb.ld"),
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
            "-T" .. path.join(app_dir, "bootloader.ld"),
            "-Wl,--gc-sections",
        },
    })
end
