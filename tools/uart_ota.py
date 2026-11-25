#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""uart_ota.py - robotos Bootloader 串口 OTA 上位机（F1 App/OTA、F4 APP/BACKUP/OTA）

与 platforms/stm32/bootloader/bootloader.c 的升级协议保持一致：

    Boot -> Host : ACK(0x06)                （就绪，可发长度/CRC）
    Host -> Boot : u32 len + u32 crc32(LE)  （crc32 覆盖后 len 字节）
    Boot -> Host : ACK                      （目标槽擦除完成，可发负载）
    Host -> Boot : len 字节镜像
    Boot -> Host : ACK 成功（随后 MCU 复位进入新槽）
                   NAK(0x15) 失败（长度/CRC/Flash 错误，回到旧槽启动）

应答使用不可打印控制字符，与 Bootloader 打印的 UART 日志（含 'K'/'E' 字母）
互不干扰。

典型用法：
    # 默认 COM3、自动用 ST-LINK 复位目标后进入 Bootloader 窗口
    python tools/uart_ota.py --bin build/cross/arm/release/ros-fw-f103cb.bin

    # 指定串口 / 波特率
    python tools/uart_ota.py -p COM5 -b 115200 --bin xxx.bin

    # STM32F407VE（APP/BACKUP/OTA）：镜像写入 OTA 区，需放大单槽容量
    python tools/uart_ota.py --slot-size 131072 --bin build/cross/arm/release/ros-fw-f407ve.bin

    # 不用 ST-LINK（板子没接调试器）：脚本持续敲门，期间手动按复位键
    python tools/uart_ota.py --no-reset --bin xxx.bin

依赖：Python 3 + pyserial（pip install pyserial）
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import time
import zlib

# 协议字符
REQ = b"R"          # Host -> Boot：请求升级
ACK = b"\x06"       # Boot -> Host：就绪 / 擦除完成 / 升级成功
ERR = b"\x15"       # Boot -> Host：升级失败（NAK）

DEFAULT_SLOT_SIZE = 52 * 1024       # 与 F1 bootloader.c 的 FLASH_OTA_SIZE 一致
STLINK_CANDIDATES = [
    r"C:\Program Files (x86)\STMicroelectronics\STM32 ST-LINK Utility\ST-LINK Utility\ST-LINK_CLI.exe",
    r"C:\Program Files\STMicroelectronics\STM32 ST-LINK Utility\ST-LINK Utility\ST-LINK_CLI.exe",
]


def find_stlink() -> str | None:
    for path in STLINK_CANDIDATES:
        if os.path.isfile(path):
            return path
    return None


def boost_timer_resolution() -> None:
    """Windows 默认 Sleep 粒度约 15.6ms；提高到 1ms 让分片间隔更精确。"""
    if os.name != "nt":
        return
    try:
        import ctypes

        ctypes.windll.winmm.timeBeginPeriod(1)
    except Exception:  # noqa: BLE001 - 仅优化，失败不影响功能
        pass


def crc32(data: bytes) -> int:
    """标准 CRC-32/IEEE（多项式 0xEDB88320），与 MCU 端逐位实现等价。"""
    return zlib.crc32(data) & 0xFFFFFFFF


def knock(ser, timeout_s: float, log: bytearray) -> bool:
    """在 Bootloader 的 500ms 握手窗口内发送 'R'，收到 'K' 即返回。

    Bootloader 收到 'R' 后会先打印日志再回 'K'，所以每次发 'R' 后要留足
    300ms 的监听时间：既能把日志收下来，又不会在它等待长度字段时再发 'R'
    （多余的 'R' 会被当作长度首字节，导致 "bad header"）。
    """
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        ser.write(REQ)
        ser.flush()
        turn_end = min(deadline, time.monotonic() + 0.3)
        while time.monotonic() < turn_end:
            ser.timeout = max(0.01, turn_end - time.monotonic())
            b = ser.read(1)
            if b == ACK:
                return True
            if b:
                log.extend(b)
    return False


def read_until(ser, targets: bytes, timeout_s: float, log: bytearray) -> bytes:
    """读取直到出现 targets 中的字节（返回它），其余字节收进 log；超时返回 b''。"""
    deadline = time.monotonic() + timeout_s
    while True:
        remain = deadline - time.monotonic()
        if remain <= 0:
            return b""
        ser.timeout = min(remain, 0.2)
        b = ser.read(1)
        if not b:
            continue
        if b in targets:
            return b
        log.extend(b)


def dump_log(log: bytearray) -> None:
    text = log.decode("utf-8", errors="replace").strip()
    if text:
        print("--- Bootloader 输出 ---")
        print(text)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="robotos A/B Bootloader 串口 OTA 上位机",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument("--bin", required=True, help="待升级的固件 bin（按目标槽地址链接）")
    ap.add_argument("-p", "--port", default="COM3", help="串口设备名")
    ap.add_argument("-b", "--baud", type=int, default=115200, help="波特率")
    ap.add_argument("--slot-size", type=lambda s: int(s, 0), default=DEFAULT_SLOT_SIZE,
                    help="单槽容量（字节），用于长度合法性检查")
    ap.add_argument("--chunk", type=int, default=64,
                    help="发送分片大小（字节）；Bootloader 缓冲为 64B，且写 Flash 期间不接收，"
                         "分片不要超过 64")
    ap.add_argument("--delay", type=float, default=0.016,
                    help="分片之间的间隔（秒）：覆盖 Bootloader 写 Flash 的时间，避免 UART 溢出丢字节")
    ap.add_argument("--reset-timeout", type=float, default=3.0,
                    help="复位后敲门的最长时间（秒）")
    ap.add_argument("--erase-timeout", type=float, default=20.0,
                    help="等待目标槽擦除完成的时间（秒）")
    ap.add_argument("--result-timeout", type=float, default=30.0,
                    help="负载发完后等待最终结果的时间（秒）")
    ap.add_argument("--stlink", default=None,
                    help="ST-LINK_CLI.exe 路径；默认自动探测常见安装位置")
    ap.add_argument("--no-reset", action="store_true",
                    help="不自动复位，需在敲门期间手动按复位键")
    args = ap.parse_args()

    if args.chunk > 64:
        print(f"提示：分片 {args.chunk}B 超过 Bootloader 的 64B 缓冲，已调整为 64B")
        args.chunk = 64
    boost_timer_resolution()

    try:
        import serial  # pyserial
    except ImportError:
        print("缺少 pyserial：请先执行  python -m pip install pyserial", file=sys.stderr)
        return 2

    try:
        with open(args.bin, "rb") as f:
            data = f.read()
    except OSError as exc:
        print(f"读取固件失败：{exc}", file=sys.stderr)
        return 2

    if not data:
        print("固件为空", file=sys.stderr)
        return 2
    if len(data) > args.slot_size:
        print(f"固件 {len(data)} 字节超过单槽容量 {args.slot_size} 字节", file=sys.stderr)
        return 2

    crc = crc32(data)
    print(f"固件: {args.bin}")
    print(f"大小: {len(data)} 字节  CRC32: 0x{crc:08X}")

    # ---- 打开串口 ----
    try:
        ser = serial.Serial(args.port, args.baud, bytesize=8, parity="N",
                            stopbits=1, timeout=0.03, write_timeout=10)
    except Exception as exc:  # noqa: BLE001 - 串口异常类型随平台不同
        print(f"打开串口 {args.port} 失败：{exc}", file=sys.stderr)
        print("提示：关闭串口助手 / 其它占用该端口的程序后重试", file=sys.stderr)
        return 2

    ok = False
    log = bytearray()
    try:
        # ---- 复位目标（可选），让 Bootloader 进入 500ms 升级窗口 ----
        if args.no_reset:
            print("未自动复位：请在此期间按一下开发板复位键 …")
            reset_timeout = max(args.reset_timeout, 10.0)
        else:
            stlink = args.stlink or find_stlink()
            if stlink is None:
                print("未找到 ST-LINK_CLI.exe，改为手动复位模式（请按复位键）", file=sys.stderr)
                reset_timeout = max(args.reset_timeout, 10.0)
            else:
                subprocess.run([stlink, "-c", "SWD", "-Rst"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               check=False)
                reset_timeout = args.reset_timeout

        # ---- 1) 敲门：'R' -> 'K' ----
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        if not knock(ser, reset_timeout, log):
            dump_log(log)
            print("未收到 Bootloader 就绪响应 'K'：确认固件包含新 Bootloader、串口接线正确、"
                  "复位时机是否落在 500ms 窗口内", file=sys.stderr)
            return 3
        print("握手成功，发送长度与 CRC …")

        # ---- 2) 长度 + CRC ----
        ser.write(struct.pack("<II", len(data), crc))
        ser.flush()

        b = read_until(ser, ACK + ERR, args.erase_timeout, log)
        if b != ACK:
            dump_log(log)
            print(f"目标槽擦除失败（响应 {b!r}）", file=sys.stderr)
            return 4
        print("擦除完成，开始传输 …")

        # ---- 3) 分片限速发送负载 ----
        sent = 0
        total = len(data)
        next_report = 10
        t0 = time.monotonic()
        while sent < total:
            n = min(args.chunk, total - sent)
            ser.write(data[sent:sent + n])
            sent += n
            # Bootloader 中途出错会回 'E' 并退出升级；其余是进度点/日志
            if ser.in_waiting:
                early = ser.read(ser.in_waiting)
                if ERR in early:
                    dump_log(log)
                    print(f"\n升级中止：Bootloader 返回 NAK（已发送 {sent}/{total} 字节）",
                          file=sys.stderr)
                    return 5
                log.extend(early)
            if sent * 100 // total >= next_report:
                print(f"  {next_report}%  ({sent}/{total})")
                next_report += 10
            if args.delay > 0:
                time.sleep(args.delay)
        ser.flush()
        print(f"负载发送完毕，用时 {time.monotonic() - t0:.1f}s，等待校验结果 …")

        # ---- 4) 最终结果 ----
        b = read_until(ser, ACK + ERR, args.result_timeout, log)
        if b == ACK:
            ok = True
        else:
            print(f"升级失败：响应 {b!r}（NAK 0x15 表示 CRC/Flash 错误）", file=sys.stderr)

        # 成功后 Bootloader 会立即复位并启动新槽（含 500ms 握手 + 元数据写入），
        # 留 2.5s 抓完整的启动选择日志与新固件输出
        if ok:
            time.sleep(2.5)
            ser.timeout = 0.2
            log.extend(ser.read_all())
    finally:
        ser.close()

    dump_log(log)

    if ok:
        print("UPGRADE OK：升级包已写入 OTA 区，MCU 复位后由 Bootloader 升级到 APP 区")
        return 0

    return 6


if __name__ == "__main__":
    sys.exit(main())
