--[[
    robotos 统一构建脚本（xmake >= 2.8）

    主机（Windows / Linux）使用系统 gcc：
        xmake f -c                          # 重新配置（自动识别主机平台）
        xmake b -r                          # 全量构建三个主机目标
        xmake r ros-test                    # 运行单元测试
        xmake r ros-demo                    # 运行综合示例
        xmake f -p linux -c && xmake b -r   # 在 Linux 主机上构建

    STM32 使用 arm-none-eabi-gcc：
        # 先把工具链 bin 目录加入 PATH；cross 工具链检测必须显式给 --sdk/--bin
        set PATH=C:\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin;%PATH%

        # 静态库（由外部 CubeMX 工程链接）
        xmake f -p cross --stm32_family=f4 ^
                --sdk=C:\xpack-arm-none-eabi-gcc-15.2.1-1.1 ^
                --bin=C:\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin -c
        xmake b ros-stm32

        # 可烧录固件：STM32F407VET6（需要 Cube HAL，产物含 .elf/.hex/.bin）
        xmake f -p cross --stm32_family=f4 --stm32_chip=STM32F407xx ^
                --cube_hal_dir=C:\path\to\STM32F4xx_HAL_Driver ^
                --sdk=... --bin=... -c
        xmake b ros-fw-f407

        # 可烧录固件：STM32F103C8T6（纯寄存器实现，不需要 Cube HAL）
        xmake f -p cross --stm32_family=f1 --cube_hal_dir="" ... -c
        xmake b ros-fw-f103c8

        # Linux 下换成 --sdk=/opt/xpack-arm-none-eabi-gcc-15.2.1-1.1 等安装路径
        # --cube_hal_dir 可传 .../STM32F4xx_HAL_Driver 或其 Inc 子目录

    未指定 --cube_hal_dir 时，依赖 Cube HAL 的 stm32_os.c / stm32_<family>.c
    会自动跳过，只产出内核 + 组件 + 驱动层。

    目标一览：
        ros-pc        主机框架静态库（无 main，供外部集成）
        ros-test      单元测试可执行文件
        ros-demo      综合示例可执行文件
        ros-shell     Shell 交互示例（stdin/stdout）
        ros-stm32     STM32 交叉编译静态库（仅 -p cross 时可见）
        ros-fw-f407   STM32F407VET6 可烧录固件（PA6/PA7 LED，USART1）
        ros-fw-f103c8 STM32F103C8T6 可烧录固件（PC13 LED，USART1）
]]

add_rules("mode.debug", "mode.release")

set_project("robotos")
set_version("1.0.0")

-- ============================================================================
-- 配置选项
-- ============================================================================

option("stm32_family")
    set_default("f4")
    set_values("f1", "f4", "h7")
    set_description("STM32 系列：决定 CPU/FPU 选项与 ROS_STM32_Fx 宏（仅 cross 平台）")
option_end()

option("cube_hal_dir")
    set_default("")
    set_description("STM32Cube HAL 目录（可传 .../STM32F4xx_HAL_Driver 或其 Inc）；留空则跳过依赖 Cube HAL 的平台文件")
option_end()

option("stm32_chip")
    set_default("STM32F407xx")
    set_description("目标芯片型号宏（如 STM32F407xx），仅 cross 平台使用")
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
    set_description("编译期启用 trace；off 时所有 ROS_TRACE_* 宏为空、零 RAM/ROM 开销")
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
local function ros_includedirs()
    add_includedirs(
        "src", "hal", "vendor", "vendor/cjson",
        "vendor/tlsf", "vendor/arena", "vendor/mongoose",
        "bootloader", "networks", "networks/ml307", "networks/eth",
        "drivers", "platforms", "platforms/pc", "platforms/stm32"
    )
    for _, dir in ipairs(os.dirs("components/*")) do
        add_includedirs(dir)
    end
end

-- 框架全量源码（平台层由各目标单独 add_files）
local function ros_framework_sources()
    add_files("src/**.c")
    add_files("bootloader/**.c")
    add_files("components/**.c")
    add_files("vendor/**.c")
    add_files("networks/**.c")
    add_files("drivers/**.c")
end

-- 排除框架内嵌的自测文件（ros-test 不调用，它需要这些 ut_xxx 入口）
local function ros_exclude_tests()
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
local function ros_host_defines()
    add_defines("ROS_PLATFORM_PC")
    add_defines("ROS_NETWORK_ETH")
    if is_plat("windows", "mingw") then
        add_defines("_WIN32_WINNT=0x0600")
    else
        -- ucontext / nanosleep / select / strdup 等 POSIX 声明
        add_defines("_GNU_SOURCE")
    end
end

-- 编译期裁剪开关：xmake f --trace=off -c
local function ros_trace_defines()
    if get_config("trace") == "off" then
        add_defines("ROS_USE_TRACE=0")
    end
end

local function ros_host_links()
    if is_plat("windows", "mingw") then
        add_links("m", "ws2_32", "advapi32", "iphlpapi")
    else
        add_links("m", "pthread")
    end
end

-- 每个主机二进制/库目标共用的描述
local function ros_host_common(with_tests)
    set_toolchains("gcc")            -- Windows / Linux 统一使用系统 gcc
    set_languages("c99")
    set_warnings("all")

    ros_host_defines()
    ros_trace_defines()
    add_files("platforms/pc/**.c")
    ros_framework_sources()
    ros_includedirs()

    if not with_tests then
        ros_exclude_tests()
    end
end

-- STM32 系列 -> CPU/FPU 编译选项
local function ros_stm32_cpuflags(family)
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
local function ros_find_pack_version(rel)
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
    target("ros-pc")
        set_kind("static")
        ros_host_common(false)
    target_end()

    -- 单元测试：保留框架内嵌的 *_test.c（src/test_main.c 会调用其 ut_xxx）
    target("ros-test")
        set_kind("binary")
        ros_host_common(true)
        ros_host_links()
    target_end()

    -- 综合示例：串口控制台 + shell + LED 闪烁（真实延时）
    target("ros-demo")
        set_kind("binary")
        ros_host_common(false)
        add_defines("ROS_PC_REAL_DELAY")
        add_files("examples/demo.c")
        ros_host_links()
    target_end()

    -- Shell 交互示例：标准输入输出 + 内核任务（默认绑定见 platforms/pc/pc_shell.c）
    target("ros-shell")
        set_kind("binary")
        ros_host_common(false)
        add_defines("SHELL_SHELL_STACK=8192", "SHELL_JOB_STACK=4096")
        -- 交互演示：模拟时间跟随墙钟（1 tick = 1ms），便于观察 ps/state 变化
        add_defines("ROS_PC_REAL_TIME")
        add_files("examples/shell_demo.c")
        ros_host_links()
    target_end()

end

-- ============================================================================
-- STM32 交叉编译目标（xmake f -p cross）
-- ============================================================================

if is_plat("cross") then

    local family  = get_config("stm32_family") or "f4"
    local hal_dir = get_config("cube_hal_dir") or ""
    local chip    = get_config("stm32_chip") or "STM32F407xx"
    local cpuflags = ros_stm32_cpuflags(family)

    -- 归一化到 <...>/STM32F4xx_HAL_Driver 目录：允许传该目录或其中的 Inc
    local hal_root = ""
    if hal_dir ~= "" then
        hal_root = (hal_dir:gsub("[/\\]+$", ""))
        if path.basename(hal_root):lower() == "inc" then
            hal_root = path.directory(hal_root)
        end
    end
    local hal_drivers = (hal_root ~= "") and path.directory(hal_root) or ""
    local hal_inc     = (hal_root ~= "") and path.join(hal_root, "Inc") or ""
    local hal_src     = (hal_root ~= "") and path.join(hal_root, "Src") or ""
    local cmsis_dev_inc = (hal_drivers ~= "")
        and path.join(hal_drivers, "CMSIS", "Device", "ST", "STM32F4xx", "Include") or ""
    local cmsis_pack = ros_find_pack_version("ARM/CMSIS")
    local cmsis_core = get_config("cmsis_inc") or ""
    if cmsis_core == "" and cmsis_pack then
        cmsis_core = path.join(cmsis_pack, "CMSIS", "Core", "Include")
    end

    target("ros-stm32")
        set_kind("static")
        set_toolchains(get_config("stm32_toolchain") or "arm-none-eabi-gcc")
        set_languages("c99")
        set_warnings("all")

        add_defines("ROS_PLATFORM_STM32")
        add_defines("ROS_STM32_" .. family:upper())
        -- 内存后端由平台提供（TLSF 静态堆），否则 ros_mem.c 默认走 libc
        add_defines("ROS_MEM_PORT")
        -- STM32F1 资源受限：trace 与崩溃记录固定裁剪（需要时删除这两行）
        if family == "f1" then
            add_defines("ROS_USE_TRACE=0", "ROS_CRASH_ENABLE=0")
        end
        ros_trace_defines()

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
        add_files("networks/ml307/**.c")

        ros_exclude_tests()

        -- 主机网络栈依赖 socket/winsock，MCU 上不编译
        remove_files("vendor/mongoose/**.c", "networks/eth/**.c")

        if hal_root ~= "" then
            add_includedirs(hal_inc)
            if family == "f4" then
                -- stm32_f4.c / stm32_os.c 需要 Cube + CMSIS 头文件与器件宏
                add_defines("USE_HAL_DRIVER", chip)
                add_defines("ROS_TRACE_CLOCK_HZ=168000000")
                if cmsis_dev_inc ~= "" then
                    add_includedirs(cmsis_dev_inc)
                end
                if cmsis_core ~= "" then
                    add_includedirs(cmsis_core)
                end
            end
        else
            -- 这些文件直接 include stm32fxxx_hal.h，无 Cube HAL 时跳过
            remove_files("platforms/stm32/stm32_os.c",
                         "platforms/stm32/stm32_f1.c",
                         "platforms/stm32/stm32_f4.c",
                         "platforms/stm32/stm32_h7.c")
        end

        ros_includedirs()

        set_basename("ros-stm32-" .. family)

        on_load(function (target)
            if (get_config("cube_hal_dir") or "") == "" then
                cprint("${yellow}[ros-stm32] 未指定 --cube_hal_dir：" ..
                       "已跳过依赖 STM32Cube HAL 的外设平台层" ..
                       "（stm32_os.c / stm32_<family>.c）${clear}")
            end
            cprint("${dim}[ros-stm32] family=%s, toolchain=%s, kind=static${clear}",
                   get_config("stm32_family") or "f4",
                   get_config("stm32_toolchain") or "arm-none-eabi-gcc")
            if (get_config("sdk") or "") == "" then
                cprint("${yellow}[ros-stm32] 未指定 --sdk/--bin，xmake 无法定位交叉工具链。${clear}")
                cprint("${yellow}  请用 xmake f -p cross --sdk=<工具链根目录> --bin=<根目录/bin> -c 重新配置${clear}")
            end
            cprint("${dim}[ros-stm32] 另需确保 arm-none-eabi-gcc 位于 PATH 中${clear}")
        end)
    target_end()

    -- ========================================================================
    -- 可烧录固件（STM32F103C8T6 / Medium Density / PC13 LED）
    -- 链接 libros-stm32-f1.a + 板级启动代码，产出 .elf/.hex/.bin
    -- ========================================================================
    if family == "f1" then
        local board_dir = "boards/stm32f103c8"
        local dfp       = ros_find_pack_version("Keil/STM32F1xx_DFP")
        local cmsis     = ros_find_pack_version("ARM/CMSIS")
        local dfp_inc   = get_config("dfp_inc") or ""
        local cmsis_inc = get_config("cmsis_inc") or ""
        if dfp_inc == "" then
            dfp_inc = dfp and path.join(dfp, "Device", "Include") or nil
        end
        if cmsis_inc == "" then
            cmsis_inc = cmsis and path.join(cmsis, "CMSIS", "Core", "Include") or nil
        end

        target("ros-fw-f103c8")
            set_kind("binary")
            set_toolchains(get_config("stm32_toolchain") or "arm-none-eabi-gcc")
            set_languages("c99")
            set_warnings("all")
            set_strip("none")           -- 保留 ELF 符号表，便于 ST-Link/GDB 调试
            add_deps("ros-stm32")

            add_defines("ROS_PLATFORM_STM32", "ROS_STM32_F1")
            add_defines("STM32F10X_MD", "HSE_VALUE=8000000")
            -- 与 ros-stm32 库保持一致：F1 裁剪 trace 与崩溃记录
            add_defines("ROS_USE_TRACE=0", "ROS_CRASH_ENABLE=0")
            ros_trace_defines()
            add_defines("LOG_LEVEL=LOG_LEVEL_NONE")

            for _, flag in ipairs(cpuflags) do
                add_cflags(flag, {force = true})
                add_asflags(flag, {force = true})
                add_ldflags(flag, {force = true})
            end
            add_cflags("-ffunction-sections", "-fdata-sections", {force = true})

            add_files(path.join(board_dir, "*.c"))
            add_files(path.join(board_dir, "*.S"))
            add_includedirs(board_dir)
            ros_includedirs()
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

    -- ========================================================================
    -- 可烧录固件（STM32F407VET6 / Cortex-M4 / PA6+PA7 LED / USART1）
    -- 链接 ros-stm32 + Cube HAL 子集 + 板级启动代码，产出 .elf/.hex/.bin
    -- 需要 --cube_hal_dir 指向 STM32F4xx_HAL_Driver
    -- ========================================================================
    if family == "f4" then
        local board_dir = "boards/stm32f407"
        local sys_src   = (hal_drivers ~= "")
            and path.join(hal_drivers, "CMSIS", "Device", "ST", "STM32F4xx",
                          "Source", "Templates", "system_stm32f4xx.c") or ""
        local hal_modules = {
            "stm32f4xx_hal.c", "stm32f4xx_hal_cortex.c",
            "stm32f4xx_hal_rcc.c", "stm32f4xx_hal_rcc_ex.c",
            "stm32f4xx_hal_gpio.c", "stm32f4xx_hal_uart.c",
            "stm32f4xx_hal_dma.c", "stm32f4xx_hal_dma_ex.c",
            "stm32f4xx_hal_pwr.c", "stm32f4xx_hal_pwr_ex.c",
            "stm32f4xx_hal_flash.c", "stm32f4xx_hal_flash_ex.c"
        }

        target("ros-fw-f407")
            set_kind("binary")
            set_toolchains(get_config("stm32_toolchain") or "arm-none-eabi-gcc")
            set_languages("c99")
            set_warnings("all")
            set_strip("none")           -- 保留 ELF 符号表，便于 ST-Link/GDB 调试
            add_deps("ros-stm32")

            add_defines("ROS_PLATFORM_STM32", "ROS_STM32_F4",
                        "USE_HAL_DRIVER", chip, "HSE_VALUE=8000000")
            ros_trace_defines()
            add_defines("LOG_LEVEL=LOG_LEVEL_INFO")

            for _, flag in ipairs(cpuflags) do
                add_cflags(flag, {force = true})
                add_asflags(flag, {force = true})
                add_ldflags(flag, {force = true})
            end
            add_cflags("-ffunction-sections", "-fdata-sections", {force = true})

            add_files(path.join(board_dir, "*.c"))
            add_files(path.join(board_dir, "*.S"))
            add_includedirs(board_dir)
            ros_includedirs()

            if hal_src ~= "" then
                for _, name in ipairs(hal_modules) do
                    add_files(path.join(hal_src, name))
                end
                add_files(sys_src)
                add_includedirs(hal_inc)
                if cmsis_dev_inc ~= "" then
                    add_includedirs(cmsis_dev_inc)
                end
                if cmsis_core ~= "" then
                    add_includedirs(cmsis_core)
                end
            end

            -- 自定义启动文件与向量表，禁用 crt0；nano newlib 减小体积
            add_ldflags("-nostartfiles",
                        "-T" .. path.join(board_dir, "stm32f407ve.ld"),
                        "--specs=nano.specs",
                        "-Wl,--gc-sections",
                        {force = true})

            on_load(function (target)
                if hal_src == "" or not os.isdir(hal_src) then
                    cprint("${yellow}[f407] 未找到 Cube HAL 源码，请用 " ..
                           "--cube_hal_dir=<.../STM32F4xx_HAL_Driver> 指定${clear}")
                end
                if cmsis_dev_inc ~= "" and not os.isdir(cmsis_dev_inc) then
                    cprint("${yellow}[f407] 未找到 CMSIS Device 头文件目录: %s${clear}",
                           cmsis_dev_inc)
                end
                cprint("${dim}[f407] chip=%s${clear}", chip)
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
