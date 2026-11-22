-- platforms/stm32/xmake.lua - STM32 平台层（仅交叉编译）
if is_plat("cross") then
    local exclude = {}
    local defines = {}
    local incs = {}

    if ROS_HAL_ROOT == "" then
        -- 无 Cube HAL：只用寄存器实现（F1）
        exclude = { "stm32_os.c", "stm32_f4.c", "stm32_h7.c" }
        if ROS_FAMILY ~= "f1" then
            table.insert(exclude, "stm32_f1.c")
        end
        if ROS_FAMILY == "f1" then
            defines = { "STM32F10X_MD", "HSE_VALUE=8000000" }
            if ROS_DFP_INC ~= "" then table.insert(incs, ROS_DFP_INC) end
            if ROS_CMSIS_INC ~= "" then table.insert(incs, ROS_CMSIS_INC) end
        end
    else
        -- 有 Cube HAL：保留 os + 当前系列，避免对象链接时符号重复
        if ROS_FAMILY ~= "f1" then table.insert(exclude, "stm32_f1.c") end
        if ROS_FAMILY ~= "f4" then table.insert(exclude, "stm32_f4.c") end
        if ROS_FAMILY ~= "h7" then table.insert(exclude, "stm32_h7.c") end
        defines = { "USE_HAL_DRIVER", ROS_CHIP, "ROS_TRACE_CLOCK_HZ=168000000" }
        if ROS_HAL_INC ~= "" then table.insert(incs, ROS_HAL_INC) end
        if ROS_CMSIS_INC ~= "" then table.insert(incs, ROS_CMSIS_INC) end
    end

    ros_object("ros-platform-stm32", {
        scope = "cross",
        files = { "*.c" },
        exclude = exclude,
        defines = defines,
        includedirs = incs,
    })
end
