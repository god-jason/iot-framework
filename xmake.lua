--[[
    iot-os 统一构建脚本（xmake >= 2.8）

    主机（Windows / Linux）使用系统 gcc：
        xmake f -c                          # 重新配置（自动识别主机平台）
        xmake b -r                          # 全量构建三个主机目标
        xmake r iot-test                    # 运行单元测试
        xmake r iot-demo                    # 运行综合示例
        xmake f -p linux -c && xmake b -r   # 在 Linux 主机上构建

    STM32 使用 arm-none-eabi-gcc（产出静态库，由 CubeMX 工程链接）：
        # 先把工具链 bin 目录加入 PATH；cross 工具链检测必须显式给 --sdk/--bin
        set PATH=C:\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin;%PATH%
        xmake f -p cross --stm32_family=f4 ^
                --sdk=C:\xpack-arm-none-eabi-gcc-15.2.1-1.1 ^
                --bin=C:\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin -c
        xmake b iot-stm32

        # Linux 下换成 --sdk=/opt/xpack-arm-none-eabi-gcc-15.2.1-1.1 等安装路径
        # 接入 STM32Cube HAL（启用完整平台外设层）时追加：
        #   --cube_hal_dir=/path/to/STM32CubeF4/Drivers/STM32F4xx_HAL_Driver/Inc

    仓库不含启动文件、链接脚本与 Cube HAL，因此 STM32 产物为静态库：
    由用户的 CubeMX 工程提供 main / startup / linker script 并链接本库。
    未指定 --cube_hal_dir 时，依赖 Cube HAL 的 stm32_os.c / stm32_<family>.c
    会自动跳过，只产出内核 + 组件 + 驱动层。

    目标一览：
        iot-pc      主机框架静态库（无 main，供外部集成）
        iot-test    单元测试可执行文件
        iot-demo    综合示例可执行文件
        iot-shell   Shell 交互示例（stdin/stdout）
        iot-stm32   STM32 交叉编译静态库（仅 -p cross 时可见）
]]

add_rules("mode.debug", "mode.release")

set_project("iot-os")
set_version("1.0.0")

-- ============================================================================
-- 配置选项
-- ============================================================================

option("stm32_family")
    set_default("f4")
    set_values("f1", "f4", "h7")
    set_description("STM32 系列：决定 CPU/FPU 选项与 IOT_STM32_Fx 宏（仅 cross 平台）")
option_end()

option("cube_hal_dir")
    set_default("")
    set_description("STM32Cube HAL 头文件目录；留空则跳过依赖 Cube HAL 的平台文件")
option_end()

option("stm32_toolchain")
    set_default("arm-none-eabi-gcc")
    set_description("STM32 交叉工具链名（默认下文自定义的 arm-none-eabi-gcc，亦可填 xmake 内置 gnu-rm）")
option_end()

option("dfp_inc")
    set_default("")
    set_description("Keil STM32F1xx_DFP 的 Device/Include 目录（留空则自动探测）")
option_end()

option("cmsis_inc")
    set_default("")
    set_description("CMSIS Core/Include 目录（留空则自动探测）")
option_end()

option("trace")
    set_default("on")
    set_values("on", "off")
    set_description("编译期启用 trace；off 时所有 IOT_TRACE_* 宏为空、零 RAM/ROM 开销")
option_end()

option("ctf")
    set_default("off")
    set_values("on", "off")
    set_description("启用可选 barectf/CTF 后端（需要 CTF packet 缓冲，默认 off）")
option_end()

-- ============================================================================
-- 工具链
--   主机：xmake 内置的 gcc（系统 PATH 中的 gcc.exe / gcc）
--   STM32：显式点名 arm-none-eabi-*，不依赖 --cross 前缀推断
-- ============================================================================

toolchain("arm-none-eabi-gcc")
    set_kind("cross")
    set_description("GNU Arm Embedded Toolchain (arm-none-eabi-gcc)")
    set_toolset("cc", "arm-none-eabi-gcc")
    set_toolset("cxx", "arm-none-eabi-g++")
    set_toolset("as", "arm-none-eabi-gcc")
    set_toolset("ar", "arm-none-eabi-ar")
    set_toolset("ld", "arm-none-eabi-gcc")
    set_toolset("sh", "arm-none-eabi-gcc")
    set_toolset("strip", "arm-none-eabi-strip")
    set_toolset("objcopy", "arm-none-eabi-objcopy")
toolchain_end()

-- ============================================================================
-- 通用描述域辅助函数
-- ============================================================================

-- 头文件搜索路径：components 下每个模块目录自动加入，新增模块无需改脚本
local function iot_includedirs()
    add_includedirs(
        "src", "hal", "vendor", "vendor/cjson",
        "vendor/tlsf", "vendor/arena", "vendor/mongoose", "vendor/barectf",
        "bootloader", "networks", "networks/ml307", "networks/eth",
        "drivers", "platforms", "platforms/pc", "platforms/stm32"
    )
    for _, dir in ipairs(os.dirs("components/*")) do
        add_includedirs(dir)
    end
end

-- 框架全量源码（平台层由各目标单独 add_files）
local function iot_framework_sources()
    add_files("src/**.c")
    add_files("bootloader/**.c")
    add_files("components/**.c")
    add_files("vendor/**.c")
    add_files("networks/**.c")
    add_files("drivers/**.c")
end

-- 排除框架内嵌的自测文件（iot-test 不调用，它需要这些 ut_xxx 入口）
local function iot_exclude_tests()
    remove_files(
        "src/*_test.c", "src/**/*_test.c",
        "src/test_*.c", "src/**/test_*.c",
        "bootloader/*_test.c", "bootloader/**/*_test.c",
        "bootloader/test_*.c", "bootloader/**/test_*.c",
        "components/*_test.c", "components/**/*_test.c",
        "components/test_*.c", "components/**/test_*.c",
        "vendor/*_test.c", "vendor/**/*_test.c",
        "vendor/test_*.c", "vendor/**/test_*.c",
        "networks/*_test.c", "networks/**/*_test.c",
        "networks/test_*.c", "networks/**/test_*.c",
        "drivers/*_test.c", "drivers/**/*_test.c",
        "drivers/test_*.c", "drivers/**/test_*.c",
        "platforms/*_test.c", "platforms/**/*_test.c",
        "platforms/test_*.c", "platforms/**/test_*.c"
    )
end

-- 主机平台宏：Windows 用 Winsock，Linux/macOS 用 POSIX
local function iot_host_defines()
    add_defines("IOT_PLATFORM_PC")
    add_defines("IOT_NETWORK_ETH")
    if is_plat("windows", "mingw") then
        add_defines("_WIN32_WINNT=0x0600")
    else
        -- ucontext / nanosleep / select / strdup 等 POSIX 声明
        add_defines("_GNU_SOURCE")
    end
end

-- 编译期裁剪开关：xmake f --trace=off -c
local function iot_trace_defines()
    if get_config("trace") == "off" then
        add_defines("IOT_USE_TRACE=0")
    end
    -- PC 平台默认构建 CTF 后端：可与文本行文件同时输出；
    -- STM32 等仍由 --ctf 选项控制，避免默认占用 MCU 的 packet 缓冲。
    if is_plat("windows", "mingw") then
        add_defines("IOT_TRACE_BACKEND_BARECTF=1")
    elseif get_config("ctf") == "on" then
        add_defines("IOT_TRACE_BACKEND_BARECTF=1")
    end
end

local function iot_host_links()
    if is_plat("windows", "mingw") then
        add_links("m", "ws2_32", "advapi32", "iphlpapi")
    else
        add_links("m", "pthread")
    end
end

-- 每个主机二进制/库目标共用的描述
local function iot_host_common(with_tests)
    set_toolchains("gcc")            -- Windows / Linux 统一使用系统 gcc
    set_languages("c99")
    set_warnings("all")

    iot_host_defines()
    iot_trace_defines()
    add_files("platforms/pc/**.c")
    iot_framework_sources()
    iot_includedirs()

    if (get_config("trace") == "off") then
        remove_files("vendor/barectf/**.c")   -- 裁剪模式下不编译 CTF 生成代码
    end

    if not with_tests then
        iot_exclude_tests()
    end
end

-- STM32 系列 -> CPU/FPU 编译选项
local function iot_stm32_cpuflags(family)
    if family == "f1" then
        return {"-mcpu=cortex-m3", "-mthumb"}
    elseif family == "h7" then
        return {"-mcpu=cortex-m7", "-mthumb",
                "-mfpu=fpv5-d16", "-mfloat-abi=hard"}
    end
    return {"-mcpu=cortex-m4", "-mthumb",
            "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"}
end

-- 在 Keil / CMSIS pack 的常见安装位置查找已安装的版本目录
local function iot_find_pack_version(rel)
    local roots = {
        path.join(os.getenv("LOCALAPPDATA") or "", "Arm", "Packs", rel),
        path.join("C:/Keil_v5/ARM/PACK", rel)
    }
    for _, root in ipairs(roots) do
        for _, ver in ipairs(os.dirs(path.join(root, "*"))) do
            return ver
        end
    end
    return nil
end

-- ============================================================================
-- 主机目标（Windows / Linux / macOS）
-- ============================================================================

if not is_plat("cross") then

    -- 框架静态库：没有 main，供外部工程集成；改为 static 后全量构建不再链接失败
    target("iot-pc")
        set_kind("static")
        iot_host_common(false)
    target_end()

    -- 单元测试：保留框架内嵌的 *_test.c（src/test_main.c 会调用其 ut_xxx）
    target("iot-test")
        set_kind("binary")
        iot_host_common(true)
        iot_host_links()
    target_end()

    -- 综合示例：串口控制台 + shell + LED 闪烁（真实延时）
    target("iot-demo")
        set_kind("binary")
        iot_host_common(false)
        add_defines("IOT_PC_REAL_DELAY")
        add_files("examples/demo.c")
        iot_host_links()
    target_end()

    -- Shell 交互示例：标准输入输出 + 内核任务（默认绑定见 platforms/pc/pc_shell.c）
    target("iot-shell")
        set_kind("binary")
        iot_host_common(false)
        add_defines("SHELL_SHELL_STACK=8192", "SHELL_JOB_STACK=4096")
        add_files("examples/shell_demo.c")
        iot_host_links()
    target_end()

end

-- ============================================================================
-- STM32 交叉编译目标（xmake f -p cross）
-- ============================================================================

if is_plat("cross") then

    local family  = get_config("stm32_family") or "f4"
    local hal_dir = get_config("cube_hal_dir") or ""
    local cpuflags = iot_stm32_cpuflags(family)

    target("iot-stm32")
        set_kind("static")
        set_toolchains(get_config("stm32_toolchain") or "arm-none-eabi-gcc")
        set_languages("c99")
        set_warnings("all")

        add_defines("IOT_PLATFORM_STM32")
        add_defines("IOT_STM32_" .. family:upper())
        iot_trace_defines()

        -- force=true：CPU/FPU 选项必须原样传给 arm-none-eabi-gcc，
        -- 否则 xmake 会用宿主编译器探测并可能静默丢弃这些 flag
        for _, flag in ipairs(cpuflags) do
            add_cflags(flag, {force = true})
            add_asflags(flag, {force = true})
        end
        add_cflags("-ffunction-sections", "-fdata-sections", "-fno-common",
                   {force = true})

        -- 内核 + Bootloader 公共逻辑 + 组件 + 驱动 + 平台层 + 内存后端 + 蜂窝模组
        add_files("src/**.c")
        add_files("bootloader/**.c")
        add_files("components/**.c")
        add_files("drivers/**.c")
        add_files("platforms/stm32/**.c")
        add_files("vendor/tlsf/**.c")
        add_files("vendor/arena/**.c")
        add_files("vendor/cjson/**.c")
        if (get_config("trace") ~= "off") and (get_config("ctf") == "on") then
            add_files("vendor/barectf/**.c")
        end
        add_files("networks/ml307/**.c")

        iot_exclude_tests()

        -- 主机网络栈依赖 socket/winsock，MCU 上不编译
        remove_files("vendor/mongoose/**.c", "networks/eth/**.c")

        if hal_dir ~= "" then
            add_includedirs(hal_dir)
        else
            -- 这些文件直接 include stm32fxxx_hal.h，无 Cube HAL 时跳过
            remove_files("platforms/stm32/stm32_os.c",
                         "platforms/stm32/stm32_f1.c",
                         "platforms/stm32/stm32_f4.c",
                         "platforms/stm32/stm32_h7.c")
        end

        iot_includedirs()

        set_basename("iot-stm32-" .. family)

        on_load(function (target)
            if (get_config("cube_hal_dir") or "") == "" then
                cprint("${yellow}[iot-stm32] 未指定 --cube_hal_dir：" ..
                       "已跳过依赖 STM32Cube HAL 的外设平台层" ..
                       "（stm32_os.c / stm32_<family>.c）${clear}")
            end
            cprint("${dim}[iot-stm32] family=%s, toolchain=%s, kind=static${clear}",
                   get_config("stm32_family") or "f4",
                   get_config("stm32_toolchain") or "arm-none-eabi-gcc")
            if (get_config("sdk") or "") == "" then
                cprint("${yellow}[iot-stm32] 未指定 --sdk/--bin，xmake 无法定位交叉工具链。${clear}")
                cprint("${yellow}  请用 xmake f -p cross --sdk=<工具链根目录> --bin=<根目录/bin> -c 重新配置${clear}")
            end
            cprint("${dim}[iot-stm32] 另需确保 arm-none-eabi-gcc 位于 PATH 中${clear}")
        end)
    target_end()

    -- ========================================================================
    -- 可烧录固件（STM32F103C8T6 / Medium Density / PC13 LED）
    -- 链接 libiot-stm32-f1.a + 板级启动代码，产出 .elf/.hex/.bin
    -- ========================================================================
    if family == "f1" then
        local board_dir = "boards/stm32f103c8"
        local dfp       = iot_find_pack_version("Keil/STM32F1xx_DFP")
        local cmsis     = iot_find_pack_version("ARM/CMSIS")
        local dfp_inc   = get_config("dfp_inc") or ""
        local cmsis_inc = get_config("cmsis_inc") or ""
        if dfp_inc == "" then
            dfp_inc = dfp and path.join(dfp, "Device", "Include") or nil
        end
        if cmsis_inc == "" then
            cmsis_inc = cmsis and path.join(cmsis, "CMSIS", "Core", "Include") or nil
        end

        target("iot-fw-f103c8")
            set_kind("binary")
            set_toolchains(get_config("stm32_toolchain") or "arm-none-eabi-gcc")
            set_languages("c99")
            set_warnings("all")
            set_strip("none")           -- 保留 ELF 符号表，便于 ST-Link/GDB 调试
            add_deps("iot-stm32")

            add_defines("IOT_PLATFORM_STM32", "IOT_STM32_F1")
            add_defines("STM32F10X_MD", "HSE_VALUE=8000000")
            iot_trace_defines()
            add_defines("LOG_LEVEL=LOG_LEVEL_NONE")

            for _, flag in ipairs(cpuflags) do
                add_cflags(flag, {force = true})
                add_asflags(flag, {force = true})
                add_ldflags(flag, {force = true})
            end
            add_cflags("-ffunction-sections", "-fdata-sections", {force = true})

            add_files(path.join(board_dir, "*.c"))
            add_files(path.join(board_dir, "*.S"))
            add_includedirs(board_dir, "src", "components/common")
            if dfp_inc then
                add_includedirs(dfp_inc)
            end
            if cmsis_inc then
                add_includedirs(cmsis_inc)
            end

            -- 自定义启动文件与向量表，禁用 crt0；nano newlib 减小体积
            add_ldflags("-nostartfiles",
                        "-T" .. path.join(board_dir, "stm32f103c8.ld"),
                        "--specs=nano.specs",
                        "-Wl,--gc-sections",
                        {force = true})

            on_load(function (target)
                if not dfp_inc or not cmsis_inc then
                    cprint("${yellow}[firmware] 未找到 Keil STM32F1xx_DFP 或 CMSIS pack，" ..
                           "请用 --dfp_inc / --cmsis_inc 指定头文件目录${clear}")
                else
                    cprint("${dim}[firmware] DFP: %s${clear}", dfp_inc)
                    cprint("${dim}[firmware] CMSIS: %s${clear}", cmsis_inc)
                end
            end)

            after_build(function (target)
                local elf = target:targetfile()
                if not os.isfile(elf) then
                    return
                end
                local oc  = target:tool("objcopy") or "arm-none-eabi-objcopy"
                local dir = target:targetdir()
                local hex = path.join(dir, target:basename() .. ".hex")
                local bin = path.join(dir, target:basename() .. ".bin")
                os.execv(oc, {"-O", "ihex", elf, hex})
                os.execv(oc, {"-O", "binary", elf, bin})
                cprint("${green}[firmware] HEX: %s${clear}", hex)
                cprint("${green}[firmware] BIN: %s${clear}", bin)
            end)
        target_end()
    end

end
