-- apps/f407ve/xmake.lua - F407VE + ML307 演示应用（由 ros-fw-f407ve 链接）
if is_plat("cross") and ROS_FAMILY == "f4" then
    ros_object("app-f407ve", {
        scope = "none",
        files = { "app_main.c" },
        defines = { "USE_HAL_DRIVER", ROS_CHIP, "HSE_VALUE=8000000" },
        includedirs = { path.join(ROS_ROOT, "platforms/stm32f4") },
    })
end
