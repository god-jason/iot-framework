-- platforms/stm32h7/xmake.lua - STM32H7 外设（Cube HAL2 骨架）
-- 公共实现见 platforms/stm32/xmake.lua
--
-- H7 的 Cube HAL 源码（STM32H7xx_HAL_Driver）需通过 --cube_hal_dir 指定；
-- stm32h7xx_hal_conf.h 由板级/用户工程提供（本目录不提供默认配置）。
if is_plat("cross") and ROS_FAMILY == "h7" then
    local incs = {}
    -- H7 的 CMSIS 设备头位于 STM32H7xx_HAL_Driver/CMSIS/Device/ST/STM32H7xx/Include
    local cmsis_dev = (ROS_HAL_DRV ~= "")
        and path.join(ROS_HAL_DRV, "CMSIS", "Device", "ST", "STM32H7xx", "Include") or ""
    if ROS_HAL_INC ~= "" then table.insert(incs, ROS_HAL_INC) end
    if cmsis_dev ~= "" then table.insert(incs, cmsis_dev) end
    if ROS_CMSIS_INC ~= "" then table.insert(incs, ROS_CMSIS_INC) end

    ros_object("ros-platform-stm32h7", {
        scope = "cross",
        files = { "*.c" },
        defines = { "USE_HAL_DRIVER", ROS_CHIP },
        includedirs = incs,
    })
end
