--[[
    robotos 统一构建脚本（xmake >= 2.8）

    主机（Windows / Linux）：
        xmake f -c                          # 重新配置（自动识别主机平台）
        xmake b -r                          # 全量构建
        xmake r ros-test                    # 运行单元测试
        xmake b ros-demo                    # 只构建综合示例
        xmake b ros-shell                   # 只构建 Shell 交互示例

    STM32（arm-none-eabi-gcc）：
        # F1 固件（纯寄存器，不需要 Cube HAL）
        xmake f -p cross --stm32_family=f1 --cube_hal_dir="" ^
                --sdk=C:\xpack-arm-none-eabi-gcc-15.2.1-1.1 ^
                --bin=C:\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin -c
        xmake b f103cb

        # F4 固件（需要 Cube HAL 源码）
        xmake f -p cross --stm32_family=f4 --stm32_chip=STM32F407xx ^
                --cube_hal_dir=C:\path\to\STM32F4xx_HAL_Driver ^
                --sdk=... --bin=... -c
        xmake b f407ve

    目录结构（每个子目录一个独立 target）：
        src/rtos/    -> ros-core（实时内核）
        src/hal/     -> 硬件抽象接口（仅头文件，无独立目标）
        src/<x>/     -> ros-cmp-<x>（业务/算法组件）
        vendor/<x>/  -> ros-vendor-<x>
        drivers/     -> ros-drivers（静态库，按需链接）
        modules/<x>/ -> ros-module-<x>
        platforms/<x>/ -> 库目标（ros-platform-<x> / 板级对象库）
        apps/<x>/    -> 最终可执行目标（PC 程序、嵌入式固件与 Bootloader）

    目标一览：
        ros-pc / ros-test / ros-demo / ros-shell     主机目标
        ros-stm32                                   交叉编译静态库（供外部 CubeMX 工程）
        f103cb / f103cb-boot                        F103CB 固件与 Bootloader
        f407ve / f407ve-boot                        F407VE 固件与 Bootloader
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
    set_description("STM32Cube HAL 目录（可传 .../STM32F4xx_HAL_Driver、STM32H7xx_HAL_Driver 或其 Inc）；留空则跳过依赖 Cube HAL 的平台文件")
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

option("log_level")
    set_default("info")
    set_values("none", "error", "warn", "info", "debug")
    set_description("全局日志级别；必须作用于每个编译单元（不能只在 binary 目标上定义）")
option_end()

-- ============================================================================
-- 工具链
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
-- 全局构建上下文（供各子目录 xmake.lua 使用）
-- ============================================================================

ROS_ROOT      = os.projectdir()
ROS_OBJ_HOST  = {}
ROS_OBJ_CROSS = {}
ROS_INCLUDEDIRS = {}

local family  = get_config("stm32_family") or "f4"
local hal_dir = get_config("cube_hal_dir") or ""
local chip    = get_config("stm32_chip") or "STM32F407xx"

-- 归一化到 <...>/STM32F4xx_HAL_Driver 目录
local hal_root = ""
if hal_dir ~= "" then
    hal_root = (hal_dir:gsub("[/\\]+$", ""))
    if path.basename(hal_root):lower() == "inc" then
        hal_root = path.directory(hal_root)
    end
end
ROS_HAL_ROOT   = hal_root
ROS_HAL_DRV    = (hal_root ~= "") and path.directory(hal_root) or ""
ROS_HAL_INC    = (hal_root ~= "") and path.join(hal_root, "Inc") or ""
ROS_HAL_SRC    = (hal_root ~= "") and path.join(hal_root, "Src") or ""
ROS_FAMILY     = family
ROS_CHIP       = chip

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

-- DFP / CMSIS 头文件目录（F1 的寄存器实现与板级文件需要）
local dfp_pack   = ros_find_pack_version("Keil/STM32F1xx_DFP")
local cmsis_pack = ros_find_pack_version("ARM/CMSIS")
ROS_DFP_INC = get_config("dfp_inc") or ""
if ROS_DFP_INC == "" and dfp_pack then
    ROS_DFP_INC = path.join(dfp_pack, "Device", "Include")
end
ROS_CMSIS_INC = get_config("cmsis_inc") or ""
if ROS_CMSIS_INC == "" and cmsis_pack then
    ROS_CMSIS_INC = path.join(cmsis_pack, "CMSIS", "Core", "Include")
end
local cmsis_dev_inc = ""
if family == "f4" and ROS_HAL_DRV ~= "" then
    cmsis_dev_inc = path.join(ROS_HAL_DRV, "CMSIS", "Device", "ST", "STM32F4xx", "Include")
end

-- 头文件搜索路径：src/rtos + src/hal + src/<组件> + vendor + modules + drivers + platforms
-- 统一转成绝对路径：各子目录 xmake.lua 中的相对 include 会以该目录为基准解析
do
    local function ap(p) return path.join(ROS_ROOT, p) end
    local incs = { ap("src/rtos"), ap("src/hal"), ap("drivers"),
                   ap("platforms"), ap("platforms/pc"),
                   ap("platforms/stm32"),
                   ap("platforms/stm32/bootloader") }
    -- 板级头（board.h / partition.h）各系列同名：只把当前系列目录加入搜索路径，
    -- 避免 stm32f1/stm32f4 的 board.h 互相覆盖。
    if is_plat("cross") then
        incs[#incs + 1] = ap("platforms/stm32" .. family)
    end
    for _, d in ipairs(os.dirs("vendor/*")) do incs[#incs + 1] = ap(d) end
    -- src/ 下同时存放内核（rtos）、HAL 头（hal）与组件：前两者已单独加入，这里跳过
    for _, d in ipairs(os.dirs("src/*")) do
        local name = path.basename(d)

        if name ~= "rtos" and name ~= "hal" then incs[#incs + 1] = ap(d) end
    end
    for _, d in ipairs(os.dirs("modules/*")) do incs[#incs + 1] = ap(d) end
    if is_plat("cross") then
        if ROS_HAL_INC ~= "" then incs[#incs + 1] = ROS_HAL_INC end
        if cmsis_dev_inc ~= "" then incs[#incs + 1] = cmsis_dev_inc end
        if ROS_CMSIS_INC ~= "" then incs[#incs + 1] = ROS_CMSIS_INC end
        if ROS_DFP_INC ~= "" then incs[#incs + 1] = ROS_DFP_INC end
        if family == "f4" and ROS_CMSIS_INC ~= "" then incs[#incs + 1] = ROS_CMSIS_INC end
    end
    ROS_INCLUDEDIRS = incs
end

-- ============================================================================
-- 通用配置辅助
-- ============================================================================

local function ros_cpuflags()
    if family == "f1" then
        return { "-mcpu=cortex-m3", "-mthumb" }
    elseif family == "h7" then
        return { "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard" }
    end
    return { "-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard" }
end

local function ros_trace_defines()
    if get_config("trace") == "off" then
        add_defines("ROS_USE_TRACE=0")
    end
end

-- 交叉编译目标的公共宏（arch / 系列 / 内存后端 / F1 裁剪）
local function ros_cross_defines(extra)
    add_defines("ROS_PLATFORM_STM32", "ROS_STM32_" .. family:upper(), "ROS_MEM_PORT")
    if family == "f1" then
        add_defines("ROS_USE_TRACE=0")
        -- F1 的 tickless 仍为 WFI 占位（未做 LPTIM 补偿），先关闭避免 tick 失真
        add_defines("ROS_USE_TICKLESS=0")
        add_defines("ROS_MEM_HEAP_SIZE=10752")
        add_defines("ML307_NET_MAX_SOCKETS=1", "ML307_NET_RX_MAX=256")
        add_defines("NET_STREAM_TX_MAX=64", "NET_STREAM_RX_MAX=64")
        -- F1 堆紧张：调度器任务表只需 3 个槽位，减半降低堆占用
        add_defines("SCHEDULER_MAX_ENTRIES=4")
        add_cflags("-Oz", { force = true })
    elseif family == "f4" or family == "h7" then
        -- F4/H7 SRAM 充足：内核 TLSF 堆从默认 32KB 调到 64KB
        add_defines("ROS_MEM_HEAP_SIZE=65536")
        if family == "h7" then
            -- H7 板级 SYSCLK 480MHz；缺这个宏时 trace 时基按 72MHz 换算，
            -- 所有时间戳/周期统计整体偏离，tickless 补偿也会失真
            add_defines("ROS_TRACE_CLOCK_HZ=480000000", "ROS_USE_TICKLESS=0")
        end
        if family == "f4" then
            -- SysTick/tickless 时基与板级 SYSCLK 一致（默认宏为 72MHz）
            add_defines("ROS_TRACE_CLOCK_HZ=168000000")
            -- tickless 空闲补偿存在“tick 推进与实际时间不一致”的问题，
            -- 精度优先：f4 关闭 tickless，使用固定 1ms SysTick（误差 ≤1 tick）
            add_defines("ROS_USE_TICKLESS=0")
        end
    end
    ros_trace_defines()
    if extra then
        add_defines(extra)
    end
end

-- 交叉编译目标公共编译参数（CPU / 段 / 调试信息）
local function ros_cross_flags()
    for _, flag in ipairs(ros_cpuflags()) do
        add_cflags(flag, { force = true })
        add_asflags(flag, { force = true })
    end
    add_cflags("-ffunction-sections", "-fdata-sections", "-fno-common", { force = true })
    add_cflags("-g", { force = true })
end

function ros_host_defines()
    add_defines("ROS_PLATFORM_PC")
    if is_plat("windows", "mingw") then
        add_defines("_WIN32_WINNT=0x0600")
    else
        add_defines("_GNU_SOURCE")
    end
    ros_trace_defines()
end

function ros_host_links()
    if is_plat("windows", "mingw") then
        add_links("m", "ws2_32", "advapi32", "iphlpapi", "winmm")
    else
        add_links("m", "pthread")
    end
end

-- 为可执行目标生成链接 map 文件（<targetfile>.map）
function ros_enable_mapfile()
    after_load(function (target)
        if target:kind() == "binary" then
            local mapfile = target:targetfile() .. ".map"
            mapfile = mapfile:gsub("\\", "/")
            target:add("ldflags", "-Wl,-Map=" .. mapfile, { force = true })
        end
    end)
end

-- 固件构建后导出 hex/bin 并打印 map 路径
-- 生成 hex/bin/elf/map；opts.ota = true 时再生成带 fota 包头的 .ota 文件
function ros_fw_objcopy(opts)
    opts = opts or {}
    local ota_tool = path.join(ROS_ROOT, "tools", "build_ota.py")
    after_build(function (target)
        local elf = target:targetfile()
        if not os.isfile(elf) then
            return
        end
        local oc  = target:tool("objcopy") or "arm-none-eabi-objcopy"
        local dir = target:targetdir()
        local hex = path.join(dir, target:basename() .. ".hex")
        local bin = path.join(dir, target:basename() .. ".bin")
        local map = elf .. ".map"
        local elf_out = path.join(dir, target:basename() .. ".elf")
        os.execv(oc, { "-O", "ihex", elf, hex })
        os.execv(oc, { "-O", "binary", elf, bin })
        if elf ~= elf_out then
            os.cp(elf, elf_out)
        end
        cprint("${green}[firmware] HEX: %s${clear}", hex)
        cprint("${green}[firmware] BIN: %s${clear}", bin)
        cprint("${green}[firmware] ELF: %s${clear}", elf_out)
        if os.isfile(map) then
            cprint("${green}[firmware] MAP: %s${clear}", map)
        else
            cprint("${yellow}[firmware] MAP 未生成: %s${clear}", map)
        end

        -- 固件 bin -> 带 OTA 包头的 .ota（工具见 tools/build_ota.py）
        if opts.ota then
            local ota = path.join(dir, target:basename() .. ".ota")
            local argv = { ota_tool, "--bin", bin, "-o", ota }
            if opts.ota_compress then
                table.insert(argv, "--compress")
            end
            if os.isfile(ota) then
                os.rm(ota)
            end
            local pys = { "python", "python3" }
            local env_py = os.getenv("PYTHON")
            if env_py and env_py ~= "" then
                table.insert(pys, 1, env_py)
            end
            -- 逐个候选尝试（python 可能是 Python 2 或缺失），但全部失败时必须
            -- 让构建失败：否则会产出一个"没有 OTA 包"的成功固件。
            local last_err = nil
            for _, py in ipairs(pys) do
                if not os.isfile(ota) then
                    local ok, err = pcall(function () os.execv(py, argv) end)
                    if not ok then
                        last_err = err
                    end
                end
            end
            if os.isfile(ota) then
                cprint("${green}[firmware] OTA: %s${clear}", ota)
            else
                raise("OTA 生成失败（需要 Python 3）：%s", tostring(last_err))
            end
        end
    end)
end

local function ros_exclude_tests()
    remove_files("*_test.c", "test_*.c")
end

-- 目标公共头文件与告警
local function ros_target_common(opts)
    set_languages("c99")
    set_warnings("all")
    add_cflags("-Wextra", "-Wshadow", "-Wformat=2", { force = true })
    -- 全局宏：写在二进制目标（ros_firmware）上的 add_defines 不会传递给任何 .c，
    -- 因此日志级别在这里统一注入到每个目录/应用对象目标。
    add_defines("LOG_LEVEL=LOG_LEVEL_" .. string.upper(get_config("log_level") or "info"))
    add_includedirs(ROS_INCLUDEDIRS)
    if opts and opts.includedirs then
        add_includedirs(opts.includedirs)
    end
    if opts and opts.defines then
        add_defines(opts.defines)
    end
end

-- ============================================================================
-- 子目录目标辅助函数
-- ============================================================================

-- 注册一个目录目标。
--   name          目标名
--   opts.kind     "object"（默认）或 "static"
--   opts.scope    "both"（默认）/ "host" / "cross" / nil（不加入汇总列表）
--   opts.files    源文件通配（默认 {"*.c"}）
--   opts.exclude  要排除的文件
--   opts.defines  额外宏
--   opts.includedirs 额外头文件目录
--   opts.keep_tests   true 时保留 *_test.c
--   opts.default  是否默认构建（默认 true）
function ros_object(name, opts)
    opts = opts or {}
    target(name)
        set_kind(opts.kind or "object")
        ros_target_common(opts)
        if is_plat("cross") then
            set_toolchains(get_config("stm32_toolchain") or "arm-none-eabi-gcc")
            set_optimize("smallest")
            ros_cross_defines()
            ros_cross_flags()
        else
            set_toolchains("gcc")
            ros_host_defines()
        end
        if opts.cflags then
            add_cflags(opts.cflags, { force = true })
        end
        add_files(opts.files or { "*.c" })
        if opts.exclude then
            remove_files(opts.exclude)
        end
        if not opts.keep_tests then
            ros_exclude_tests()
        end
        if opts.default == false then
            set_default(false)
        end
    target_end()

    local scope = opts.scope or "both"
    if scope == "both" then
        table.insert(ROS_OBJ_HOST, name)
        table.insert(ROS_OBJ_CROSS, name)
    elseif scope == "host" then
        table.insert(ROS_OBJ_HOST, name)
    elseif scope == "cross" then
        table.insert(ROS_OBJ_CROSS, name)
    end
end

-- 可烧录固件目标：框架对象 + 板级对象 + 应用对象
function ros_firmware(name, opts)
    target(name)
        set_kind("binary")
        set_toolchains(get_config("stm32_toolchain") or "arm-none-eabi-gcc")
        set_languages("c99")
        set_warnings("all")
        set_optimize("smallest")
        set_strip("none")
        add_cflags("-Wextra", "-Wshadow", "-Wformat=2", { force = true })
        add_includedirs(ROS_INCLUDEDIRS)
        if opts.includedirs then
            add_includedirs(opts.includedirs)
        end
        if opts.defines then
            add_defines(opts.defines)
        end
        ros_trace_defines()
        for _, flag in ipairs(ros_cpuflags()) do
            add_cflags(flag, { force = true })
            add_asflags(flag, { force = true })
            add_ldflags(flag, { force = true })
        end
        add_cflags("-ffunction-sections", "-fdata-sections", { force = true })
        add_cflags("-g", { force = true })
        if family == "f1" then
            add_cflags("-Oz", { force = true })
        end
        ros_enable_mapfile()
        for _, dep in ipairs(opts.deps or {}) do
            add_deps(dep)
        end
        if opts.ldflags then
            add_ldflags(opts.ldflags, { force = true })
        end
        if opts.after_load then
            on_load(opts.after_load)
        end
        ros_fw_objcopy({ ota = true, ota_compress = opts.ota_compress })
    target_end()
end

-- Bootloader：只链接板级启动/元数据，不依赖内核
function ros_bootloader(name, opts)
    target(name)
        set_kind("binary")
        set_toolchains(get_config("stm32_toolchain") or "arm-none-eabi-gcc")
        set_languages("c99")
        set_warnings("all")
        set_optimize("smallest")
        set_strip("none")
        add_cflags("-g", { force = true })
        ros_enable_mapfile()
        add_defines(opts.defines or {})
        for _, flag in ipairs(ros_cpuflags()) do
            add_cflags(flag, { force = true })
            add_asflags(flag, { force = true })
            add_ldflags(flag, { force = true })
        end
        add_cflags("-ffreestanding", "-ffunction-sections", "-fdata-sections", { force = true })
        for _, dep in ipairs(opts.deps or {}) do
            add_deps(dep)
        end
        add_includedirs(opts.includedirs or {})
        add_ldflags(opts.ldflags, { force = true })
        ros_fw_objcopy()
    target_end()
end

-- ============================================================================
-- 引入各目录（顺序：基础库 -> 组件 -> 模块 -> 平台 -> PC 板级 -> 应用 -> 固件板级）
-- ============================================================================

includes("src/rtos")
includes("drivers")

for _, dir in ipairs(os.dirs("vendor/*")) do
    includes(dir)
end
for _, dir in ipairs(os.dirs("src/*")) do
    local name = path.basename(dir)

    -- rtos 已在上面单独引入；hal 只有头文件、没有独立目标
    if name ~= "rtos" and name ~= "hal" then
        includes(dir)          -- 重复 includes 同一目录会重复定义目标
    end
end
for _, dir in ipairs(os.dirs("modules/*")) do
    includes(dir)
end
for _, dir in ipairs(os.dirs("platforms/*")) do
    includes(dir)
end

for _, dir in ipairs(os.dirs("apps/*")) do
    includes(dir)
end

-- ============================================================================
-- 主机聚合目标：ros-pc 静态库（无 main，供外部集成）
-- ============================================================================

if not is_plat("cross") then

    target("ros-pc")
        set_kind("static")
        set_toolchains("gcc")
        set_languages("c99")
        set_warnings("all")
        add_cflags("-Wextra", "-Wshadow", "-Wformat=2", { force = true })
        ros_host_defines()
        -- 与 ros-cmp-roshell 主机配置保持一致，避免同一份源码两套语义
        add_defines("RS_MATH_ENABLE=1")
        -- 复用子目录对象目标：各目标已用 ros_exclude_tests() 排除测试，
        -- 且 ros-platform-pc 已排除 pc_main.c；避免同一批 .c 编译两遍。
        for _, dep in ipairs(ROS_OBJ_HOST) do
            add_deps(dep)
        end
        add_includedirs(ROS_INCLUDEDIRS)
    target_end()

end

-- ============================================================================
-- 交叉编译静态库：供外部 CubeMX 工程链接（与目录目标并存）
-- ============================================================================

if is_plat("cross") then

    target("ros-stm32")
        set_kind("static")
        set_toolchains(get_config("stm32_toolchain") or "arm-none-eabi-gcc")
        set_languages("c99")
        set_warnings("all")
        set_optimize("smallest")
        add_cflags("-Wextra", "-Wshadow", "-Wformat=2", { force = true })
        add_cflags("-g", { force = true })
        ros_cross_defines()
        ros_cross_flags()
        if family == "f1" then
            add_defines("STM32F10X_MD", "HSE_VALUE=8000000")
        end
        if ROS_HAL_ROOT ~= "" then
            add_defines("USE_HAL_DRIVER", chip, "ROS_TRACE_CLOCK_HZ=168000000")
        end
        -- 复用子目录对象目标：net/http/mqtt 的平台裁剪、板级文件排除、
        -- stm32_os.c 的 HAL 条件编译、测试排除均由各自 xmake.lua 负责。
        for _, dep in ipairs(ROS_OBJ_CROSS) do
            add_deps(dep)
        end
        add_includedirs(ROS_INCLUDEDIRS)

        set_basename("ros-stm32-" .. family)

        on_load(function (target)
            if (get_config("cube_hal_dir") or "") == "" then
                cprint("${yellow}[ros-stm32] 未指定 --cube_hal_dir：" ..
                       "已跳过依赖 STM32Cube HAL 的外设平台层${clear}")
            end
            cprint("${dim}[ros-stm32] family=%s, toolchain=%s, kind=static${clear}",
                   family, get_config("stm32_toolchain") or "arm-none-eabi-gcc")
            if (get_config("sdk") or "") == "" then
                cprint("${yellow}[ros-stm32] 未指定 --sdk/--bin，xmake 无法定位交叉工具链。${clear}")
            end
        end)
    target_end()

end
