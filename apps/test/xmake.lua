-- apps/test/xmake.lua - PC 单元测试（框架 + 内嵌 *_test.c）
if not is_plat("cross") then
    target("ros-test")
        set_kind("binary")
        set_toolchains("gcc")
        set_languages("c99")
        set_warnings("all")
        add_cflags("-Wextra", "-Wshadow", "-Wformat=2", { force = true })
        ros_host_defines()
        add_defines("RS_MATH_ENABLE=1")   -- 与 ros-cmp-roshell 的主机配置一致
        add_files("app_main.c")
        -- 自测入口：内核（src/rtos）与各组件（src/<x>）统一递归收集。
        -- 目录目标默认排除 test，这里显式加入；一次递归即可覆盖两类，避免重复登记。
        add_files("../../src/rtos/*_test.c",
                  "../../src/*/*_test.c")
        add_includedirs(ROS_INCLUDEDIRS)
        add_deps("board-pc")
        for _, dep in ipairs(ROS_OBJ_HOST) do
            add_deps(dep)
        end
        ros_host_links()
        ros_enable_mapfile()
    target_end()
end
