-- platforms/stm32/xmake.lua - STM32 各系列共用的公共实现
--
-- 本目录只放与 F1/F4/H7 无关的代码（io / rtos / 内存 / trace / 软件 RTC /
-- newlib 系统调用 / 时基），具体外设实现位于 stm32f1、stm32f4、stm32h7。
if is_plat("cross") then
    -- 与系列无关的公共实现
    local shared = { "stm32_io.c", "stm32_rtos.c", "stm32_mem_tlsf.c",
                     "stm32_trace.c", "stm32_rtc_soft.c", "stm32_syscalls.c" }
    -- 依赖板级 partition.h 的共享文件由各 board 目标编译（见 platforms/stm32f*/xmake.lua）；
    -- 这些文件位于 platforms/stm32/bootloader，不会被本目录 "*.c" 通配匹配。
    local exclude = { "stm32_ota.c",
                      "stm32_main.c",         -- 由各 board 目标编译（需要 board.h）
                      "stm32_shell.c" }       -- 由应用选择是否编入本文件

    local files
    local defines = {}
    local incs = {}

    if ROS_HAL_ROOT == "" then
        -- 无 Cube HAL（F1）：时基由板级实现，stm32_os.c 不参与
        files = shared
    else
        -- 有 Cube HAL：包含 stm32_os.c（HAL_GetTick / PWR 睡眠）
        files = { "*.c" }
        defines = { "USE_HAL_DRIVER", ROS_CHIP }
        if ROS_HAL_INC ~= "" then table.insert(incs, ROS_HAL_INC) end
        if ROS_CMSIS_INC ~= "" then table.insert(incs, ROS_CMSIS_INC) end
        if ROS_HAL_DRV ~= "" then
            local dev = path.join(ROS_HAL_DRV, "CMSIS", "Device", "ST",
                                  "STM32" .. ROS_FAMILY:upper() .. "xx", "Include")
            if os.isdir(dev) then table.insert(incs, dev) end
        end
    end

    ros_object("ros-platform-stm32", {
        scope = "cross",
        files = files,
        exclude = exclude,
        defines = defines,
        includedirs = incs,
    })
end
