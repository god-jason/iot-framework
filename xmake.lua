-- iot-frame 构建脚本（xmake）
-- 默认编译 Windows 模拟版本用于功能测试。STM32 交叉编译时加入 platforms/stm32 下的 .c，
-- 并在目标宏中定义 IOT_STM32_F1 / IOT_STM32_F4 / IOT_STM32_H7 之一。

add_rules("mode.debug", "mode.release")

-- 各目标共用的源码与头文件搜索路径
function iot_common()
    add_files("src/**.c")
    add_files("components/**.c")
    add_files("vendor/**.c")
    add_files("boot/**.c")
    add_files("networks/**.c")
    add_files("drivers/**.c")

    add_includedirs(
        "src", "hal", "vendor", "vendor/cjson",
        "vendor/tlsf", "vendor/arena", "vendor/mongoose",
        "boot", "networks", "networks/ml307", "networks/eth",
        "drivers", "platforms", "platforms/pc"
    )

    -- components 下每个模块目录都作为头文件搜索路径，新增模块无需改脚本
    for _, dir in ipairs(os.dirs("components/*")) do
        add_includedirs(dir)
    end
end

-- 模拟/示例目标排除测试文件
function iot_exclude_tests()
    remove_files(
        "src/*_test.c", "src/**/*_test.c",
        "src/test_*.c", "src/**/test_*.c",
        "components/*_test.c", "components/**/*_test.c",
        "components/test_*.c", "components/**/test_*.c",
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
end

target("iot-pc")
    set_kind("binary")
    set_toolchains("gcc")
    add_defines("IOT_PLATFORM_PC")
    add_defines("IOT_NETWORK_ETH")
    add_defines("_WIN32_WINNT=0x0600")

    -- 平台模拟实现
    add_files("platforms/pc/**.c")
    iot_common()
    iot_exclude_tests()

    set_warnings("all")
    set_languages("c99")

    -- Windows 模拟需要链接数学库与 Winsock（Mongoose 网络）
    if is_plat("windows", "mingw") then
        add_links("m", "ws2_32", "advapi32", "iphlpapi")
    end

-- 单元测试目标：框架源码 + tests/ 测试套件，入口为 tests/test_main.c
target("iot-test")
    set_kind("binary")
    set_toolchains("gcc")
    add_defines("IOT_PLATFORM_PC")
    add_defines("IOT_NETWORK_ETH")
    add_defines("_WIN32_WINNT=0x0600")

    add_files("platforms/pc/**.c")
    iot_common()
    add_files("tests/**.c")
    add_includedirs("tests")

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
    add_defines("IOT_NETWORK_ETH")
    add_defines("_WIN32_WINNT=0x0600")
    add_defines("IOT_PC_REAL_DELAY")

    add_files("examples/demo.c")
    add_files("platforms/pc/**.c")
    iot_common()
    iot_exclude_tests()

    set_warnings("all")
    set_languages("c99")

    if is_plat("windows", "mingw") then
        add_links("m", "ws2_32", "advapi32", "iphlpapi")
    end
