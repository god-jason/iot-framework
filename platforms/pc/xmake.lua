-- platforms/pc/xmake.lua - PC 平台层（主机目标）
if not is_plat("cross") then
    ros_object("ros-platform-pc", {
        scope = "host",
        files = { "*.c" },
    })
end
