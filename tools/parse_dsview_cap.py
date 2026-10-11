#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
解析 DSView 导出的 PD / IIC 抓包 CSV，还原"已协商好 PPS → 拔线 → 重插"场景的时序。
用法: python parse_cap.py <pd.csv> <iic.csv> [win_start_ms] [win_end_ms]
"""
import csv, sys

def fnum(s):
    s = s.strip()
    if not s:
        return None
    try:
        return float(s)
    except ValueError:
        return None

def load_pd(path):
    """返回 [(t_ms, full_text)]，full_text 只在该报文的首行(Preamble)出现。"""
    out = []
    with open(path, newline='', encoding='utf-8', errors='replace') as f:
        r = csv.reader(f)
        next(r, None)
        for row in r:
            if len(row) < 2:
                continue
            t = fnum(row[1])
            full = row[7].strip() if len(row) > 7 else ''
            if full and t is not None:
                out.append((t / 1e6, full))
    return out

def load_iic(path):
    evs = []
    with open(path, newline='', encoding='utf-8', errors='replace') as f:
        r = csv.reader(f)
        next(r, None)
        for row in r:
            if len(row) < 3:
                continue
            t = fnum(row[1])
            if t is None:
                continue
            evs.append((t / 1e6, row[2].strip()))
    return evs

def decode_iic(evs):
    """把裸事件流还原成事务: (t_ms, 'W'/'R', addr8, [data...])"""
    txs = []
    addr = None
    rw = None
    data = []
    t0 = None
    for t, e in evs:
        if e == 'Start' or e.startswith('Start'):
            addr, rw, data, t0 = None, None, [], t
        elif e.startswith('Address write:'):
            addr = int(e.split(':')[1].strip(), 16); rw = 'W'; t0 = t
        elif e.startswith('Address read:'):
            addr = int(e.split(':')[1].strip(), 16); rw = 'R'; t0 = t
        elif e.startswith('Data write:'):
            data.append(int(e.split(':')[1].strip(), 16))
        elif e.startswith('Data read:'):
            data.append(int(e.split(':')[1].strip(), 16))
        elif e == 'Stop':
            if addr is not None:
                txs.append((t0, rw, addr, data))
            addr, rw, data = None, None, []
    if addr is not None:
        txs.append((t0, rw, addr, data))
    return txs

def dev_name(a8):
    if a8 in (0x44, 0x45):
        return 'FUSB302'
    if a8 in (0x88, 0x89):
        return 'INA226'
    if a8 in (0xA0, 0xA1):
        return 'EEPROM'
    if a8 in (0xA2, 0xA3):
        return 'RTC'
    return '?'

def main():
    pd = load_pd(sys.argv[1])
    iic = load_iic(sys.argv[2])
    lo = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
    hi = float(sys.argv[4]) if len(sys.argv) > 4 else 1e18

    print('================ PD 报文 (%g..%g ms) ================' % (lo, hi))
    for t, txt in pd:
        if lo <= t <= hi:
            print('  %10.3f ms  %s' % (t, txt))

    print('\n================ FUSB302 I2C 事务 (%g..%g ms) ================' % (lo, hi))
    txs = decode_iic(iic)
    for t, rw, a8, data in txs:
        if not (lo <= t <= hi):
            continue
        d = dev_name(a8)
        if d == 'FUSB302':
            if rw == 'W' and data:
                reg = data[0]
                for v in data[1:]:
                    print('  %10.3f ms  %-8s W reg=0x%02X val=0x%02X' % (t, d, reg, v))
                if len(data) == 1:
                    print('  %10.3f ms  %-8s W reg=0x%02X (addr-only)' % (t, d, reg))
            elif rw == 'R':
                reg = '?'
                print('  %10.3f ms  %-8s R  data=%s' % (t, d, ' '.join('%02X' % x for x in data)))
            else:
                print('  %10.3f ms  %-8s %s addr=%02X data=%s' % (t, d, rw, a8, ' '.join('%02X' % x for x in data)))
        else:
            print('  %10.3f ms  %-8s %s addr=%02X data=%s' % (t, d, rw, a8, ' '.join('%02X' % x for x in data)))

if __name__ == '__main__':
    main()
