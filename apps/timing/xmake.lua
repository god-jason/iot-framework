-- apps/timing/xmake.lua - PC 延时精度测试（实时模式）
if not is_plat("cross") then
    target("ros-timing")
        set_kind("binary")
        set_toolchains("gcc")
        set_languages("c99")
        set_warnings("all")
        add_cflags("-Wextra", "-Wshadow", "-Wformat=2", { force = true })
        ros_host_defines()
        add_defines("ROS_PC_REAL_TIME")
        add_files("app_main.c")
        add_includedirs(ROS_INCLUDEDIRS)
        add_deps("board-pc")
        for _, dep in ipairs(ROS_OBJ_HOST) do
            add_deps(dep)
        end
        ros_host_links()
        ros_enable_mapfile()
    target_end()
end
