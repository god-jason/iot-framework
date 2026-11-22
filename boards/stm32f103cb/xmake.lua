-- boards/stm32f103cb/xmake.lua - F103CB 板级、App 固件与 Bootloader
if is_plat("cross") and ROS_FAMILY == "f1" then
    local board = "boards/stm32f103cb"
    -- 闭包（after_load）运行时看不到根脚本的全局变量，先捕获为局部变量
    local dfp_inc = ROS_DFP_INC or ""
    local cmsis_inc = ROS_CMSIS_INC or ""
    local incs = { path.join(ROS_ROOT, board) }
    if dfp_inc ~= "" then table.insert(incs, dfp_inc) end
    if cmsis_inc ~= "" then table.insert(incs, cmsis_inc) end

    -- App 板级对象：启动文件 + board*.c + main.c + syscalls.c（不含 bootloader.c）
    ros_object("board-f103cb", {
        scope = "none",
        files = { "*.c", "*.S" },
        exclude = { "bootloader.c" },
        defines = { "STM32F10X_MD", "HSE_VALUE=8000000" },
        includedirs = incs,
    })

    -- Bootloader 板级对象（不依赖内核 / libc）
    ros_object("boot-f103cb", {
        scope = "none",
        files = { "startup_stm32f10x_md.S", "bootloader.c", "boot_meta.c" },
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
            "-T" .. board .. "/bootloader.ld",
            "-Wl,--gc-sections",
        },
    })
end
