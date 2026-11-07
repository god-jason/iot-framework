-- iot-os 构建脚本（xmake）
-- 默认编译 Windows 模拟版本（mcu/simulator）用于功能测试。
-- 后续可通过 target("stm32f1") / target("stm32f4") 加入交叉编译目标。

add_rules("mode.debug", "mode.release")

target("iot-sim")
    set_kind("binary")
    set_toolchains("gcc")

    -- 平台模拟实现（含模拟入口 main）
    add_files("ports/simulator/**.c")

    -- 框架源码（纯 C，与平台无关）
    add_files("src/**.c")
    add_files("vendor/**.c")
    add_files("boot/**.c")
    add_files("modules/**.c")

    -- 外设驱动（含传感器/总线）
    add_files("drivers/**.c")

    -- 头文件搜索路径
    add_includedirs(
        "src", "src/port", "vendor", "vendor/cjson",
        "src/crc", "src/hash",
        "boot",
        "src/action", "src/behavior", "src/planner", "src/executor",
        "src/navigation", "src/mapping",
        "src/filter", "src/statistics",
        "src/interpolation", "src/pid", "src/motion",
        "modules/ml307", "src/at", "src/modbus", "src/storage",
        "src/motor", "src/encoder", "src/servo", "src/stepper",
        "src/fan", "src/breath_led", "src/fota",
        "drivers", "ports", "ports/simulator"
    )

    set_warnings("all")
    set_languages("c99")

    -- Windows 模拟需要链接数学库
    if is_plat("windows", "mingw") then
        add_links("m")
    end

-- 单元测试目标：框架源码 + tests/，入口为 tests/test_main.c
target("iot-test")
    set_kind("binary")
    set_toolchains("gcc")

    -- 模拟平台实现（不包含带 main 的 sim_main.c）
    add_files("ports/simulator/port_sim.c")

    -- 框架源码
    add_files("src/**.c")
    add_files("vendor/**.c")
    add_files("boot/**.c")
    add_files("modules/**.c")
    add_files("drivers/**.c")

    -- 测试用例
    add_files("tests/**.c")

    add_includedirs(
        "src", "src/port", "vendor", "vendor/cjson",
        "src/crc", "src/hash",
        "boot",
        "src/action", "src/behavior", "src/planner", "src/executor",
        "src/navigation", "src/mapping",
        "src/filter", "src/statistics",
        "src/interpolation", "src/pid", "src/motion",
        "modules/ml307", "src/at", "src/modbus", "src/storage",
        "src/motor", "src/encoder", "src/servo", "src/stepper",
        "src/fan", "src/breath_led", "src/fota",
        "drivers", "ports", "ports/simulator",
        "tests"
    )

    set_warnings("all")
    set_languages("c99")

    if is_plat("windows", "mingw") then
        add_links("m")
    end