-- apps/f103cb/xmake.lua - F103CB + ML307 演示应用（由 ros-fw-f103cb 链接）
if is_plat("cross") and ROS_FAMILY == "f1" then
    ros_object("app-f103cb", {
        scope = "none",
        files = { "app_main.c", "app_uart.c", "app_device.c", "app_ml307.c",
                  "app_sched.c", "app_shell.c" },
        defines = { "STM32F10X_MD", "HSE_VALUE=8000000" },
        includedirs = { path.join(ROS_ROOT, "platforms/stm32f1") },
    })
end
