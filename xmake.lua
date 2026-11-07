-- iot-os 构建脚本（xmake）
-- 默认编译 Windows 模拟版本（mcu/simulator）用于功能测试。
-- STM32 统一平台位于 ports/stm32/，交叉编译时加入该目录下的 .c，
-- 并在目标宏中定义 PORT_STM32_F1 / PORT_STM32_F4 / PORT_STM32_H7 之一。

add_rules("mode.debug", "mode.release")

target("iot-sim")
    set_kind("binary")
    set_toolchains("gcc")
    add_defines("PORT_PLATFORM_SIMULATOR")

    -- 平台模拟实现（含模拟入口 main）
    add_files("ports/simulator/**.c")

    -- 框架源码（纯 C，与平台无关）
    add_files("src/**.c")
    add_files("vendor/**.c")
    add_files("boot/**.c")
    add_files("modules/**.c")

    -- 外设驱动（含传感器/总线）
    add_files("drivers/**.c")

    -- 测试文件与源码同目录，模拟运行目标不编译测试
    remove_files(
        "src/*_test.c", "src/**/*_test.c",
        "src/test_*.c", "src/**/test_*.c",
        "vendor/*_test.c", "vendor/**/*_test.c",
        "vendor/test_*.c", "vendor/**/test_*.c",
        "boot/*_test.c", "boot/**/*_test.c",
        "boot/test_*.c", "boot/**/test_*.c",
        "modules/*_test.c", "modules/**/*_test.c",
        "modules/test_*.c", "modules/**/test_*.c",
        "drivers/*_test.c", "drivers/**/*_test.c",
        "drivers/test_*.c", "drivers/**/test_*.c",
        "ports/simulator/*_test.c"
    )

    -- 头文件搜索路径
    add_includedirs(
        "src", "src/port", "vendor", "vendor/cjson",
        "vendor/tlsf", "vendor/arena",
        "src/crypto",
        "src/bytes",
        "src/common",
        "src/tlv",
        "src/datetime",
        "boot",
        "src/action", "src/behavior", "src/planner", "src/executor",
        "src/navigation", "src/mapping",
        "src/filter", "src/statistics",
        "src/interpolation", "src/pid", "src/motion",
        "modules/ml307", "src/at", "src/modbus", "src/storage",
        "src/motor", "src/encoder", "src/servo", "src/stepper",
        "src/fan", "src/breath_led", "src/fota", "src/scheduler",
        "src/console",
        "drivers", "ports", "ports/simulator"
    )

    set_warnings("all")
    set_languages("c99")

    -- Windows 模拟需要链接数学库
    if is_plat("windows", "mingw") then
        add_links("m")
    end

-- 单元测试目标：框架源码 + 同目录测试文件，入口为 src/test_main.c
target("iot-test")
    set_kind("binary")
    set_toolchains("gcc")
    add_defines("PORT_PLATFORM_SIMULATOR")

    -- 模拟平台实现（不包含带 main 的 sim_main.c，包含模拟平台测试）
    add_files("ports/simulator/port_sim.c")
    add_files("ports/simulator/port_mem_sim.c")
    add_files("ports/simulator/port_os_sim.c")
    add_files("ports/simulator/*_test.c")

    -- 框架源码（测试文件与源码同目录）
    add_files("src/**.c")
    add_files("vendor/**.c")
    add_files("boot/**.c")
    add_files("modules/**.c")
    add_files("drivers/**.c")

    add_includedirs(
        "src", "src/port", "vendor", "vendor/cjson",
        "vendor/tlsf", "vendor/arena",
        "src/crypto",
        "src/bytes",
        "src/common",
        "src/tlv",
        "src/datetime",
        "boot",
        "src/action", "src/behavior", "src/planner", "src/executor",
        "src/navigation", "src/mapping",
        "src/filter", "src/statistics",
        "src/interpolation", "src/pid", "src/motion",
        "modules/ml307", "src/at", "src/modbus", "src/storage",
        "src/motor", "src/encoder", "src/servo", "src/stepper",
        "src/fan", "src/breath_led", "src/fota", "src/scheduler",
        "src/console",
        "drivers", "ports", "ports/simulator"
    )

    set_warnings("all")
    set_languages("c99")

    if is_plat("windows", "mingw") then
        add_links("m")
    end