-- platforms/pc/xmake.lua - PC 平台层（主机目标）
if not is_plat("cross") then
    -- PC 入口（main + app 任务创建），供各主机可执行目标依赖
    ros_object("board-pc", {
        scope = "none",
        files = { "pc_main.c" },
    })

    ros_object("ros-platform-pc", {
        scope = "host",
        files = { "*.c" },
        -- shell 进入逻辑按应用选择：apps/shell 用 app_shell.c，
        -- 其他应用需要默认实现时显式加入本文件（见 apps/demo/xmake.lua）
        -- pc_main.c 由单独的 board-pc 目标编译，避免静态库内出现 main
        exclude = { "pc_shell.c", "pc_main.c" },
    })
end
