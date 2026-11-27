-- apps/shell/xmake.lua - PC roshell 交互示例
if not is_plat("cross") then
    target("ros-shell")
        set_kind("binary")
        set_toolchains("gcc")
        set_languages("c99")
        set_warnings("all")
        add_cflags("-Wextra", "-Wshadow", "-Wformat=2", { force = true })
        ros_host_defines()
        add_defines("SHELL_SHELL_STACK=8192", "SHELL_JOB_STACK=4096",
                    "ROS_PC_REAL_TIME")
        add_files("app_main.c", "app_shell.c", "app_fs.c", "app_master.c")
        add_includedirs(ROS_INCLUDEDIRS)
        add_deps("board-pc")
        for _, dep in ipairs(ROS_OBJ_HOST) do
            add_deps(dep)
        end
        ros_host_links()
        ros_enable_mapfile()
    target_end()
end
