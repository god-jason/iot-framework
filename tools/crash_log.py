#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""crash_log.py - 解析 robotos A/B OTA 的崩溃日志，并用 ELF 还原崩溃现场

本工具读取 App HardFault 写入 Flash 的文本日志，结合带调试信息的 ELF
（addr2line / nm）解析 PC、LR 与栈快照，输出寄存器、故障状态与源码位置。

输入二选一：
    --dump <file>   日志区原始 Flash 转储（二进制）：
                    F1 0x08006000 起 4KB；F4 0x08010000 起 64KB
    位置参数        文本文件（串口捕获的 Bootloader 输出或日志文本），省略读 stdin

典型用法：
    # 1) 从板子上导出日志区（F1 4KB / F4 64KB）
    ST-LINK_CLI.exe -c SWD HotPlug -Dump 0x08006000 0x1000 crashlog.bin
    ST-LINK_CLI.exe -c SWD HotPlug -Dump 0x08010000 0x10000 crashlog_f4.bin
    python tools/crash_log.py --dump crashlog.bin

    # 2) 直接解析串口抓到的启动日志
    python tools/crash_log.py serial.txt

    # 3) 指定 ELF（默认 build/cross/arm/release/ros-fw-f103cb.elf）
    python tools/crash_log.py --elf build/cross/arm/release/ros-fw-f103cb.elf serial.txt

依赖：arm-none-eabi-addr2line / arm-none-eabi-nm（自动探测 PATH 与 xpack 默认目录）
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import sys

# 复用 scripts/crash_decode.py 中的符号解析逻辑
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))
import crash_decode as cd  # noqa: E402

REASON_RE = re.compile(r"reason=([A-Z_]+)")
SLOT_RE = re.compile(r"\bslot=([AB])\b")
CRASH_RE = re.compile(r"\bcrash=(\d+)\b")
PC_RE = re.compile(r"\bpc=([0-9A-Fa-f]{8})\b")
LR_RE = re.compile(r"\blr=([0-9A-Fa-f]{8})\b")
SP_RE = re.compile(r"\bsp=([0-9A-Fa-f]{8})\b")
XPSR_RE = re.compile(r"\bxpsr=([0-9A-Fa-f]{8})\b")
REG_RE = re.compile(r"\b(r\d+|r12)=([0-9A-Fa-f]{8})\b")
CFSR_RE = re.compile(r"\bcfsr=([0-9A-Fa-f]{8})\b")
HFSR_RE = re.compile(r"\bhfsr=([0-9A-Fa-f]{8})\b")
EXCR_RE = re.compile(r"\bexcr=([0-9A-Fa-f]{8})\b")
MMFAR_RE = re.compile(r"\bmmfar=([0-9A-Fa-f]{8})\b")
BFAR_RE = re.compile(r"\bbfar=([0-9A-Fa-f]{8})\b")
STACK_RE = re.compile(
    r"stack\[([0-9A-Fa-f]{8})\]:((?:\s+[0-9A-Fa-f]{8})*)"
)

CFSR_BITS = [
    (1 << 0, "IACCVIOL"), (1 << 1, "DACCVIOL"), (1 << 3, "MUNSTKERR"),
    (1 << 4, "MSTKERR"), (1 << 5, "MLSPERR"), (1 << 7, "MMARVALID"),
    (1 << 8, "IBUSERR"), (1 << 9, "PRECISERR"), (1 << 10, "IMPRECISERR"),
    (1 << 11, "UNSTKERR"), (1 << 12, "STKERR"), (1 << 13, "LSPERR"),
    (1 << 15, "BFARVALID"),
    (1 << 16, "UNDEFINSTR"), (1 << 17, "INVSTATE"), (1 << 18, "INVPC"),
    (1 << 19, "NOCP"), (1 << 24, "UNALIGNED"), (1 << 25, "DIVBYZERO"),
]
HFSR_BITS = [
    (1 << 1, "VECTTBL"), (1 << 30, "FORCED"), (1 << 31, "DEBUGEVT"),
]

TOOLCHAIN_DIRS = [
    r"C:\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin",
]
DEFAULT_BUILD_DIR = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "build", "cross", "arm", "release"))


def die(msg: str) -> None:
    print("error: " + msg, file=sys.stderr)
    sys.exit(2)


def read_text(args) -> str:
    """读取原始 Flash 转储或文本输入，返回可解析的字符串。"""
    if args.dump:
        try:
            with open(args.dump, "rb") as f:
                data = f.read()
        except OSError as exc:
            die("读取转储失败: %s" % exc)
        # 擦除态 0xFF 与二进制噪声丢弃，只保留文本字符
        return "".join(chr(b) if (32 <= b < 127 or b in (10, 13)) else "" for b in data)

    if args.text:
        try:
            with open(args.text, "r", encoding="utf-8", errors="replace") as f:
                return f.read()
        except OSError as exc:
            die("读取日志失败: %s" % exc)

    return sys.stdin.read()


def first_record(text: str) -> str:
    """串口捕获里可能包含多次崩溃；只取第一条 reason= 开头的记录。"""
    start = text.find("reason=")
    if start < 0:
        return text
    end = text.find("reason=", start + 1)
    return text[start:] if end < 0 else text[start:end]


def find_addr2line(explicit: str | None) -> str:
    if explicit:
        found = cd.existing(explicit)
        if not found:
            die("找不到 addr2line: " + explicit)
        return found
    for name in ("arm-none-eabi-addr2line", "arm-none-eabi-addr2line.exe"):
        found = shutil.which(name)
        if found:
            return found
    for folder in TOOLCHAIN_DIRS:
        for name in ("arm-none-eabi-addr2line.exe", "arm-none-eabi-addr2line"):
            path = os.path.join(folder, name)
            if os.path.isfile(path):
                return path
    die("未找到 arm-none-eabi-addr2line，请用 --addr2line 指定")


def default_elf(slot: str | None) -> str:
    _ = slot
    return os.path.join(DEFAULT_BUILD_DIR, "ros-fw-f103cb.elf")


def bits_text(value: int, table) -> str:
    names = [name for bit, name in table if value & bit]
    return " ".join(names) if names else "(无)"


def main() -> int:
    ap = argparse.ArgumentParser(
        description="解析 A/B OTA 崩溃日志并用 ELF 还原崩溃现场",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("text", nargs="?", help="日志文本文件；省略则读 stdin")
    ap.add_argument("--dump", help="日志区原始 Flash 转储（F1 0x08006000/4KB，F4 0x08010000/64KB）")
    ap.add_argument("--elf", help="固件 ELF；默认 build/cross/arm/release/ros-fw-f103cb.elf")
    ap.add_argument("--addr2line", help="arm-none-eabi-addr2line 路径")
    ap.add_argument("--code-range", default="0x08000000-0x10000000",
                    help="栈扫描的代码地址范围")
    args = ap.parse_args()

    if args.dump and args.text:
        die("--dump 与文本文件只能二选一")

    text = first_record(read_text(args))
    if not text.strip():
        die("日志为空")

    info = {}
    for key, rx in (("reason", REASON_RE), ("slot", SLOT_RE), ("crash", CRASH_RE),
                    ("pc", PC_RE), ("lr", LR_RE), ("sp", SP_RE),
                    ("xpsr", XPSR_RE), ("cfsr", CFSR_RE), ("hfsr", HFSR_RE),
                    ("excr", EXCR_RE), ("mmfar", MMFAR_RE), ("bfar", BFAR_RE)):
        match = rx.search(text)
        if match:
            info[key] = match.group(1)

    if "pc" not in info:
        die("未找到 pc= 字段，确认输入是崩溃日志（或 Flash 转储）")

    regs = {}
    for name, value in REG_RE.findall(text):
        regs[name] = int(value, 16)

    words = []
    for match in STACK_RE.finditer(text):
        base = int(match.group(1), 16)
        for idx, word in enumerate(match.group(2).split()):
            words.append((base + idx * 4, int(word, 16)))

    elf = args.elf or default_elf(info.get("slot"))
    if not os.path.isfile(elf):
        die("ELF 不存在: %s（可用 --elf 指定）" % elf)

    addr2line = find_addr2line(args.addr2line)
    nm = cd.find_nm(addr2line)
    symbols = cd.load_nm_symbols(nm, elf) if nm else None

    low, high = (int(x, 0) for x in args.code_range.split("-"))
    pc = int(info["pc"], 16)
    lr = int(info["lr"], 16) if "lr" in info else None
    sp = int(info["sp"], 16) if "sp" in info else None

    scan = []
    for slot, value in words:
        target = value & ~1
        if low <= target < high:
            scan.append((slot, value, target))

    want = [pc] + ([lr] if lr is not None else []) + [item[2] for item in scan]
    resolved = cd.resolve(addr2line, elf, want)

    print("=== 崩溃摘要 ===")
    print("reason : %s" % info.get("reason", "?"))
    print("slot   : %s" % info.get("slot", "?"))
    if "crash" in info:
        print("crash  : %s（自本次固件安装以来）" % info["crash"])
    print("ELF    : %s" % elf)

    print("\n=== 现场寄存器 ===")
    print(cd.describe(pc, resolved, symbols, "pc "))
    if lr is not None:
        print(cd.describe(lr, resolved, symbols, "lr "))
    if sp is not None:
        print("sp     %s" % cd.fmt_addr(sp))
    if "xpsr" in info:
        print("xpsr   0x%s" % info["xpsr"].upper())
    if regs:
        order = [n for n in ("r0", "r1", "r2", "r3", "r12") if n in regs]
        print("        " + " ".join("%s=%08X" % (n, regs[n]) for n in order))

    print("\n=== 故障状态 ===")
    cfsr = int(info["cfsr"], 16) if "cfsr" in info else 0
    if "cfsr" in info:
        print("cfsr   0x%08X  %s" % (cfsr, bits_text(cfsr, CFSR_BITS)))
    if "hfsr" in info:
        hfsr = int(info["hfsr"], 16)
        print("hfsr   0x%08X  %s" % (hfsr, bits_text(hfsr, HFSR_BITS)))
    if "mmfar" in info:
        note = "" if cfsr & (1 << 7) else "  (MMARVALID=0，无效)"
        print("mmfar  0x%s%s" % (info["mmfar"].upper(), note))
    if "bfar" in info:
        note = "  <- 访问出错的地址" if cfsr & (1 << 15) else "  (BFARVALID=0，无效)"
        print("bfar   0x%s%s" % (info["bfar"].upper(), note))
    if "excr" in info:
        excr = int(info["excr"], 16)
        stack_name = "PSP" if (excr & 4) else "MSP"
        mode = "中断/异常" if (excr & 0xF) == 0x1 else "线程"
        print("excr   0x%s  (%s, %s)" % (info["excr"].upper(), stack_name, mode))

    print("\n=== 栈扫描（代码区 %s - %s）===" % (cd.fmt_addr(low), cd.fmt_addr(high)))
    if not scan:
        print("（未发现代码地址）")
    else:
        seen = set()
        for slot, value, target in scan:
            key = (slot, target)
            if key in seen:
                continue
            seen.add(key)
            offset = slot - sp if sp is not None and slot >= sp else 0
            print("[sp+0x%02X] %s -> %s" % (
                offset, cd.fmt_addr(value),
                cd.describe(target, resolved, symbols).split("-> ", 1)[1]))

    return 0


if __name__ == "__main__":
    sys.exit(main())
