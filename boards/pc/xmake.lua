-- boards/pc/xmake.lua - PC 平台入口（供主机可执行目标依赖）
if not is_plat("cross") then
    ros_object("board-pc", {
        scope = "none",
        files = { "main.c" },
    })
end
