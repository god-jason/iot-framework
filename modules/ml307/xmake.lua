-- modules/ml307/xmake.lua - ML307R 蜂窝模组（仅交叉编译）
if is_plat("cross") then
    ros_object("ros-module-ml307", {
        scope = "cross",
        files = { "*.c" },
    })
end
