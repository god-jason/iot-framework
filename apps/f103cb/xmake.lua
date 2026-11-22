-- apps/f103cb/xmake.lua - F103CB + ML307 演示应用（由 ros-fw-f103cb 链接）
if is_plat("cross") and ROS_FAMILY == "f1" then
    ros_object("app-f103cb", {
        scope = "none",
        files = { "app_main.c" },
        includedirs = { path.join(ROS_ROOT, "boards/stm32f103cb") },
    })
end
