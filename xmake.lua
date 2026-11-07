-- iot-os 构建脚本（xmake）
-- 默认编译 Windows 模拟版本（mcu/simulator）用于功能测试。
-- 后续可通过 target("stm32f1") / target("stm32f4") 加入交叉编译目标。

add_rules("mode.debug", "mode.release")

target("iot-sim")
    set_kind("binary")
    set_toolchains("gcc")

    -- 平台模拟实现（含模拟入口 main）
    add_files("ports/simulator/**.c")
    add_files("ports/iot_mcu.c")

    -- 框架源码（纯 C，与平台无关）
    add_files("core/**.c")
    add_files("port/**.c")
    add_files("crypto/**.c")
    add_files("container/**.c")
    add_files("boot/**.c")
    add_files("protocol/**.c")
    add_files("robot/**.c")
    add_files("algorithm/**.c")
    add_files("components/**.c")

    -- 外设驱动（含传感器/总线）
    add_files("drivers/**.c")

    -- 头文件搜索路径
    add_includedirs(
        "core", "port",
        "crypto/crc", "crypto/hash",
        "container/list", "container/queue", "container/ringbuf", "container/pool",
        "boot",
        "protocol/at", "protocol/modbus", "protocol/mqtt", "protocol/http",
        "robot/action", "robot/behavior", "robot/planner", "robot/executor",
        "robot/navigation", "robot/mapping",
        "algorithm/filter", "algorithm/statistics",
        "algorithm/interpolation", "algorithm/control", "algorithm/motion",
        "components/modem/ml307", "components/storage",
        "components/motor", "components/encoder", "components/servo", "components/stepper",
        "components/fan", "components/breath_led", "components/fota",
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
    add_files("ports/simulator/iot_port_sim.c")
    add_files("ports/iot_mcu.c")

    -- 框架源码
    add_files("core/**.c")
    add_files("port/**.c")
    add_files("crypto/**.c")
    add_files("container/**.c")
    add_files("boot/**.c")
    add_files("protocol/**.c")
    add_files("robot/**.c")
    add_files("algorithm/**.c")
    add_files("components/**.c")
    add_files("drivers/**.c")

    -- 测试用例
    add_files("tests/**.c")

    add_includedirs(
        "core", "port",
        "crypto/crc", "crypto/hash",
        "container/list", "container/queue", "container/ringbuf", "container/pool",
        "boot",
        "protocol/at", "protocol/modbus", "protocol/mqtt", "protocol/http",
        "robot/action", "robot/behavior", "robot/planner", "robot/executor",
        "robot/navigation", "robot/mapping",
        "algorithm/filter", "algorithm/statistics",
        "algorithm/interpolation", "algorithm/control", "algorithm/motion",
        "components/modem/ml307", "components/storage",
        "components/motor", "components/encoder", "components/servo", "components/stepper",
        "components/fan", "components/breath_led", "components/fota",
        "drivers", "ports", "ports/simulator",
        "tests"
    )

    set_warnings("all")
    set_languages("c99")

    if is_plat("windows", "mingw") then
        add_links("m")
    end