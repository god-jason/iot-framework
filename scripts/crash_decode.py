#!/usr/bin/env python3
"""
crash_decode.py - 把 ros_crash 崩溃记录解码为函数名 / 源码行 / 疑似调用栈

用法：
    # 串口/日志里的崩溃文本直接管道传入
    type crash.txt | python scripts/crash_decode.py --elf build/cross/arm/release/ros-fw-f407

    # 或指定文件
    python scripts/crash_decode.py --elf ros-fw-f407 crash.txt

    # 指定工具链前缀（未加入 PATH 时）
    python scripts/crash_decode.py --elf ros-fw-f407 --addr2line \\
        C:/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin/arm-none-eabi-addr2line crash.txt

原理：
    - PC / LR 直接交给 addr2line 解析（ELF 需带 -g 才有文件名/行号）；
    - 栈快照中落在代码区的字视为“候选返回地址”，批量解析后按地址顺序输出；
    - addr2line 查不到符号时退回 nm 的最近符号（仍能给出函数名，行号为 ??）。

注意：
    脚本针对 STM32（32 位地址、代码区 0x08000000 起）优化；
    主机 64 位崩溃地址会被 ros_crash 截断，仅能作参考。
"""

import argparse
import bisect
import os
import re
import shutil
import subprocess
import sys


REASON_RE = re.compile(r"reason=([A-Z_]+)")
TASK_RE = re.compile(r"\btask=(\S+)")
BUILD_RE = re.compile(r"\bbuild=([^\r\n]*?)(?:\s+task=|\r|$)")
TICK_RE = re.compile(r"\btick=(\d+)")
PC_RE = re.compile(r"\bpc=([0-9A-Fa-f]{8})\b")
LR_RE = re.compile(r"\blr=([0-9A-Fa-f]{8})\b")
SP_RE = re.compile(r"\bsp=([0-9A-Fa-f]{8})\b")
FILE_RE = re.compile(r"\bfile=([^\s:]+):(\d+)")
STACK_RE = re.compile(
    r"stack(?:\[([0-9A-Fa-f]{8})\]| ([0-9A-Fa-f]{8})):"
    r"((?:\s+[0-9A-Fa-f]{8})*)"
)


def die(msg):
    print("error: " + msg, file=sys.stderr)
    sys.exit(2)


def existing(path):
    """容忍 Windows 下省略 .exe，以及仅写工具名的情况。"""
    if not path:
        return None
    if os.path.isfile(path):
        return path
    if os.name == "nt" and os.path.isfile(path + ".exe"):
        return path + ".exe"
    return shutil.which(path)


def find_tool(explicit, names):
    if explicit:
        found = existing(explicit)
        if found:
            return found
        die("找不到工具: " + explicit)
    for name in names:
        path = existing(name)
        if path:
            return path
    return None


def find_nm(addr2line):
    """按 addr2line 的路径/前缀推导 nm，保证 xpack 工具链也能找到。"""
    base = os.path.basename(addr2line)
    name = base.replace("addr2line", "nm")
    cand = os.path.join(os.path.dirname(addr2line), name)
    found = existing(cand)
    if found:
        return found
    if base != "addr2line":
        found = existing(name)
        if found:
            return found
    return existing("nm")


def run(cmd):
    try:
        return subprocess.run(cmd, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,
                              universal_newlines=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        die("执行失败: %s\n%s" % (" ".join(cmd), exc))


def run_soft(cmd):
    """执行可选工具；失败时返回空串，不影响主流程。"""
    try:
        return subprocess.run(cmd, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,
                              universal_newlines=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return ""


def resolve(addr2line, elf, addrs):
    """批量解析地址，返回 {addr: (func, file:line)}。"""
    uniq = sorted(set(addrs))
    if not uniq:
        return {}
    cmd = [addr2line, "-f", "-C", "-e", elf] + ["0x%X" % a for a in uniq]
    lines = run(cmd).splitlines()
    result = {}
    for idx, addr in enumerate(uniq):
        func = lines[idx * 2].strip() if idx * 2 < len(lines) else "??"
        loc = lines[idx * 2 + 1].strip() if idx * 2 + 1 < len(lines) else "??:0"
        result[addr] = (func, loc)
    return result


def load_nm_symbols(nm, elf):
    out = run_soft([nm, "-n", "-C", "--defined-only", elf])
    symbols = []
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) < 3:
            continue
        try:
            addr = int(parts[0], 16)
        except ValueError:
            continue
        symbols.append((addr, parts[2]))
    symbols.sort()
    return symbols


def nm_lookup(symbols, addr):
    if not symbols:
        return None
    addrs = [item[0] for item in symbols]
    idx = bisect.bisect_right(addrs, addr) - 1
    if idx < 0:
        return None
    return symbols[idx][1]


def parse_report(text):
    info = {}
    for key, rx in (("reason", REASON_RE), ("task", TASK_RE),
                    ("build", BUILD_RE), ("tick", TICK_RE),
                    ("pc", PC_RE), ("lr", LR_RE), ("sp", SP_RE)):
        match = rx.search(text)
        if match:
            info[key] = match.group(1).strip()

    file_match = FILE_RE.search(text)
    if file_match:
        info["file"] = file_match.group(1)
        info["line"] = file_match.group(2)

    words = []
    for match in STACK_RE.finditer(text):
        base = int(match.group(1) or match.group(2), 16)
        for idx, word in enumerate(match.group(3).split()):
            words.append((base + idx * 4, int(word, 16)))
    return info, words


def parse_range(text):
    low, high = text.split("-")
    return int(low, 0), int(high, 0)


def fmt_addr(addr):
    return "0x%08X" % addr


def describe(addr, resolved, symbols, indent=""):
    func, loc = resolved.get(addr, ("??", "??:0"))
    if func == "??" and symbols is not None:
        name = nm_lookup(symbols, addr & ~1)
        if name:
            func = name + " (nm)"
    text = "%s%s -> %s" % (indent, fmt_addr(addr), func)
    if loc and loc != "??:0":
        text += "  at " + loc
    return text


def main():
    parser = argparse.ArgumentParser(
        description="解码 ros_crash 崩溃记录（ELF 需带 -g）")
    parser.add_argument("report", nargs="?", help="崩溃文本文件；省略则读 stdin")
    parser.add_argument("--elf", required=True, help="固件 ELF（ros-fw-xxx）")
    parser.add_argument("--addr2line", help="addr2line 路径（默认自动探测）")
    parser.add_argument("--code-range", default="0x08000000-0x10000000",
                        help="栈扫描的代码地址范围（默认 STM32 Flash）")
    args = parser.parse_args()

    if not os.path.isfile(args.elf):
        die("ELF 不存在: " + args.elf)

    text = open(args.report, "r", encoding="utf-8", errors="replace").read() \
        if args.report else sys.stdin.read()
    if not text.strip():
        die("崩溃文本为空")

    info, words = parse_report(text)
    if "pc" not in info and "lr" not in info:
        die("未找到 pc=/lr= 字段，请确认输入是 ros_crash 输出")

    addr2line = find_tool(args.addr2line,
                          ["arm-none-eabi-addr2line", "addr2line"])
    if addr2line is None:
        die("未找到 addr2line，请用 --addr2line 指定")
    nm = find_nm(addr2line)
    symbols = load_nm_symbols(nm, args.elf) if nm else None

    low, high = parse_range(args.code_range)

    pc = int(info["pc"], 16) if "pc" in info else None
    lr = int(info["lr"], 16) if "lr" in info else None
    sp = int(info["sp"], 16) if "sp" in info else None

    # 栈快照中落在代码区的值：疑似返回地址（去掉 Thumb 位后查符号）
    scan = []
    for slot, value in words:
        target = value & ~1
        if low <= target < high:
            scan.append((slot, value, target))

    want = [a for a in (pc, lr) if a is not None]
    want += [item[2] for item in scan]
    resolved = resolve(addr2line, args.elf, want)

    print("=== 崩溃摘要 ===")
    print("reason : %s" % info.get("reason", "?"))
    print("task   : %s" % info.get("task", "?"))
    if "build" in info:
        print("build  : %s" % info["build"])
    if "tick" in info:
        print("tick   : %s" % info["tick"])
    if "file" in info:
        print("assert : %s:%s" % (info["file"], info["line"]))
    if sp is not None:
        print("sp     : %s" % fmt_addr(sp))

    print("\n=== 关键地址 ===")
    if pc is not None:
        print(describe(pc, resolved, symbols, "pc "))
    if lr is not None:
        print(describe(lr, resolved, symbols, "lr "))

    print("\n=== 栈扫描（代码区 %s - %s）===" % (fmt_addr(low), fmt_addr(high)))
    if not scan:
        print("（栈快照中未发现代码地址，可能栈已损坏或快照不足）")
    else:
        seen = set()
        for slot, value, target in scan:
            key = (slot, target)
            if key in seen:
                continue
            seen.add(key)
            print("[sp+0x%02X] %s -> %s" %
                  (slot - sp if sp is not None and slot >= sp else 0,
                   fmt_addr(value),
                   describe(target, resolved, symbols).split("-> ", 1)[1]))


if __name__ == "__main__":
    main()
