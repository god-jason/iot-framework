-- iot-os 构建脚本（xmake）
-- 默认编译 Windows 模拟版本（mcu/simulator）用于功能测试。
-- STM32 统一平台位于 platforms/stm32/，交叉编译时加入该目录下的 .c，
-- 并在目标宏中定义 IOT_STM32_F1 / IOT_STM32_F4 / IOT_STM32_H7 之一。

add_rules("mode.debug", "mode.release")

target("iot-pc")
    set_kind("binary")
    set_toolchains("gcc")
    add_defines("IOT_PLATFORM_PC")
    add_defines("IOT_NETWORK_PC")
    add_defines("_WIN32_WINNT=0x0600")

    -- 平台模拟实现（含模拟入口 main）
    add_files("platforms/pc/**.c")

    -- 框架源码（纯 C，与平台无关）
    add_files("src/**.c")
    add_files("vendor/**.c")
    add_files("boot/**.c")
    add_files("networks/**.c")

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
        "networks/*_test.c", "networks/**/*_test.c",
        "networks/test_*.c", "networks/**/test_*.c",
        "drivers/*_test.c", "drivers/**/*_test.c",
        "drivers/test_*.c", "drivers/**/test_*.c",
        "platforms/pc/*_test.c"
    )

    -- 头文件搜索路径
    add_includedirs(
        "src", "src/core", "vendor", "vendor/cjson",
        "vendor/tlsf", "vendor/arena",
        "vendor/mongoose",
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
        "networks/ml307", "src/at", "src/modbus", "src/storage",
        "networks/pc",
        "src/motor", "src/encoder", "src/servo", "src/stepper",
        "src/fan", "src/breath_led", "src/fota", "src/scheduler",
        "src/console",
        "drivers", "platforms", "platforms/pc"
    )

    set_warnings("all")
    set_languages("c99")

    -- Windows 模拟需要链接数学库与 Winsock（Mongoose 网络）
    if is_plat("windows", "mingw") then
        add_links("m", "ws2_32", "advapi32", "iphlpapi")
    end

-- 单元测试目标：框架源码 + 同目录测试文件，入口为 src/test_main.c
target("iot-test")
    set_kind("binary")
    set_toolchains("gcc")
    add_defines("IOT_PLATFORM_PC")
    add_defines("IOT_NETWORK_PC")
    add_defines("_WIN32_WINNT=0x0600")

    -- 模拟平台实现
    add_files("platforms/pc/pc.c")
    add_files("platforms/pc/pc_mem.c")
    add_files("platforms/pc/pc_os.c")
    add_files("platforms/pc/pc_uart.c")

    -- 框架源码（测试文件与源码同目录）
    add_files("src/**.c")
    add_files("vendor/**.c")
    add_files("boot/**.c")
    add_files("networks/**.c")
    add_files("drivers/**.c")

    add_includedirs(
        "src", "src/core", "vendor", "vendor/cjson",
        "vendor/tlsf", "vendor/arena",
        "vendor/mongoose",
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
        "networks/ml307", "src/at", "src/modbus", "src/storage",
        "networks/pc",
        "src/motor", "src/encoder", "src/servo", "src/stepper",
        "src/fan", "src/breath_led", "src/fota", "src/scheduler",
        "src/console",
        "drivers", "platforms", "platforms/pc"
    )

    set_warnings("all")
    set_languages("c99")

    if is_plat("windows", "mingw") then
        add_links("m", "ws2_32", "advapi32", "iphlpapi")
    end

-- 综合示例：串口控制台 + shell 命令 + LED 闪烁（模拟平台）
target("iot-demo")
    set_kind("binary")
    set_toolchains("gcc")
    add_defines("IOT_PLATFORM_PC")
    add_defines("IOT_NETWORK_PC")
    add_defines("_WIN32_WINNT=0x0600")
    add_defines("IOT_PC_REAL_DELAY")

    add_files("examples/demo.c")
    add_files("platforms/pc/**.c")
    add_files("src/**.c")
    add_files("vendor/**.c")
    add_files("boot/**.c")
    add_files("networks/**.c")
    add_files("drivers/**.c")

    remove_files(
        "src/*_test.c", "src/**/*_test.c",
        "src/test_*.c", "src/**/test_*.c",
        "vendor/*_test.c", "vendor/**/*_test.c",
        "vendor/test_*.c", "vendor/**/test_*.c",
        "boot/*_test.c", "boot/**/*_test.c",
        "boot/test_*.c", "boot/**/test_*.c",
        "networks/*_test.c", "networks/**/*_test.c",
        "networks/test_*.c", "networks/**/test_*.c",
        "drivers/*_test.c", "drivers/**/*_test.c",
        "drivers/test_*.c", "drivers/**/test_*.c"
    )

    add_includedirs(
        "src", "src/core", "vendor", "vendor/cjson",
        "vendor/tlsf", "vendor/arena",
        "vendor/mongoose",
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
        "networks/ml307", "src/at", "src/modbus", "src/storage",
        "networks/pc",
        "src/motor", "src/encoder", "src/servo", "src/stepper",
        "src/fan", "src/breath_led", "src/fota", "src/scheduler",
        "src/console",
        "drivers", "platforms", "platforms/pc"
    )

    set_warnings("all")
    set_languages("c99")

    if is_plat("windows", "mingw") then
        add_links("m", "ws2_32", "advapi32", "iphlpapi")
    end