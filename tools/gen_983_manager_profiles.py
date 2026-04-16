#!/usr/bin/env python3
"""
Generate profile_data.c for 983_manager from bios-ver-11.txt.

The runtime stays simple:
  - board / power init is handwritten C
  - the per-profile I2C register traffic is emitted as typed op tables

This script resolves the BIOS label flow for each supported DIP profile and
also fixes EDID checksum bytes when the source script contains a bad block.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BIOS_PATH = REPO_ROOT / "bios-scripts" / "bios-ver-11.txt"
OUT_C_PATH = REPO_ROOT / "rh850-baremetal-demo" / "app" / "983_manager" / "profile_data.c"


BASE_AP0 = sum(1 << bit for bit in range(7, 15))


def ap0_for_on_dips(*bits: int) -> int:
    value = BASE_AP0
    for bit in bits:
        value &= ~(1 << bit)
    return value


@dataclass(frozen=True)
class ProfileSpec:
    key: str
    enum_name: str
    symbol_name: str
    display_name: str
    ap0_value: int


PROFILES = [
    ProfileSpec("dip0_10g8", "PROFILE_DIP0_10G8", "g_ops_dip0_10g8",
                "DIP0 15.6in 2560x1440 10.8Gbps", ap0_for_on_dips(7)),
    ProfileSpec("dip0_6g75", "PROFILE_DIP0_6G75", "g_ops_dip0_6g75",
                "DIP0 15.6in 2560x1440 6.75Gbps", ap0_for_on_dips(7, 14)),
    ProfileSpec("dip1", "PROFILE_DIP1", "g_ops_dip1",
                "DIP1 14.6in 2560x1440", ap0_for_on_dips(8)),
    ProfileSpec("dip2", "PROFILE_DIP2", "g_ops_dip2",
                "DIP2 14.6in 1920x1080", ap0_for_on_dips(9)),
    ProfileSpec("dip3", "PROFILE_DIP3_OLDI", "g_ops_dip3_oldi",
                "DIP3 12.3in 1920x720 OLDI", ap0_for_on_dips(10)),
    ProfileSpec("dip4_10g8", "PROFILE_DIP4_10G8", "g_ops_dip4_10g8",
                "DIP4 27in 4032x756 10.8Gbps", ap0_for_on_dips(11)),
    ProfileSpec("dip4_6g75", "PROFILE_DIP4_6G75", "g_ops_dip4_6g75",
                "DIP4 27in 4032x756 6.75Gbps", ap0_for_on_dips(11, 14)),
    ProfileSpec("dip5", "PROFILE_DIP5", "g_ops_dip5",
                "DIP5 17.3in 2880x1620", ap0_for_on_dips(12)),
    ProfileSpec("dip0_dip1_10g8", "PROFILE_DIP0_DIP1_10G8", "g_ops_dip0_dip1_10g8",
                "DIP0+DIP1 35.6in 6480x960 10.8Gbps", ap0_for_on_dips(7, 8)),
    ProfileSpec("dip0_dip1_6g75", "PROFILE_DIP0_DIP1_6G75", "g_ops_dip0_dip1_6g75",
                "DIP0+DIP1 35.6in 6480x960 6.75Gbps", ap0_for_on_dips(7, 8, 14)),
    ProfileSpec("dip0_dip2_10g8", "PROFILE_DIP0_DIP2_10G8", "g_ops_dip0_dip2_10g8",
                "DIP0+DIP2 12.3in 1920x720 DP 10.8Gbps", ap0_for_on_dips(7, 9)),
    ProfileSpec("dip0_dip2_6g75", "PROFILE_DIP0_DIP2_6G75", "g_ops_dip0_dip2_6g75",
                "DIP0+DIP2 12.3in 1920x720 DP 6.75Gbps", ap0_for_on_dips(7, 9, 14)),
]


def label_def(value: str) -> str:
    return str(int(value))


def label_ref(value: str) -> str:
    return str(int(value, 16))


def parse_bios():
    instructions = []
    labels = {}

    for line_no, raw_line in enumerate(BIOS_PATH.read_text().splitlines(), 1):
        line = raw_line.split("//", 1)[0].strip()
        if not line:
            continue

        label_match = re.match(r"@label\((\d+)\)", line)
        if label_match:
            key = label_def(label_match.group(1))
            labels[key] = len(instructions)
            instructions.append(("label", key, line_no, line))
            continue

        instructions.append(("cmd", line, line_no, line))

    return instructions, labels


def resolve_profile_ops(spec: ProfileSpec, instructions, labels):
    pc = 0
    ap0_acc = 0
    ops = []

    while pc < len(instructions):
        kind, payload, _line_no, _raw = instructions[pc]

        if kind == "label":
            pc += 1
            continue

        line = payload
        if line == "@stop":
            break

        if line.startswith("print ") or line.startswith("@sleep("):
            pc += 1
            continue

        tokens = line.split()

        if tokens[:2] == ["p", "B0"] and tokens[2] == "20":
            ap0_acc = spec.ap0_value
            pc += 1
            continue

        if tokens[:3] == ["e", "30", "08"]:
            ap0_acc &= int(tokens[3], 16)
            pc += 1
            continue

        if tokens[0] == "e" and tokens[1] in ("20", "21") and tokens[3] == "08":
            target = label_ref(tokens[2])
            compare_value = int(tokens[4], 16)
            should_jump = (ap0_acc != compare_value) if tokens[1] == "20" else (ap0_acc == compare_value)
            pc = labels[target] if should_jump else pc + 1
            continue

        if tokens[0] == "e" and tokens[1] == "2F":
            pc = labels[label_ref(tokens[2])]
            continue

        if tokens[0] == "I" and tokens[1] != "FF":
            ops.append(("write", int(tokens[3], 16) >> 1, int(tokens[4], 16), int(tokens[5], 16)))
            pc += 1
            continue

        if tokens[0] == "i":
            if tokens[2] != "01":
                raise ValueError(f"Unsupported read length in {spec.key}: {line}")
            ops.append(("read", int(tokens[3], 16) >> 1, int(tokens[4], 16), 0))
            pc += 1
            continue

        if tokens[0] == "X":
            ops.append(("delay", 0, 0, int(tokens[3], 16)))
            pc += 1
            continue

        pc += 1

    return ops


def extract_edid_blob(spec: ProfileSpec, ops):
    extracted_ops = []
    edid_bytes = []
    idx = 0
    found = False

    while idx < len(ops):
        op = ops[idx]
        if idx + 1 < len(ops) and \
           (op[0], op[1], op[2], op[3]) == ("write", 0x18, 0x40, 0x36) and \
           (ops[idx + 1][0], ops[idx + 1][1], ops[idx + 1][2], ops[idx + 1][3]) == ("write", 0x18, 0x41, 0x00):
            if found:
                raise ValueError(f"{spec.key}: multiple EDID sequences found")

            scan = idx + 2
            while scan < len(ops) and len(edid_bytes) < 256:
                cur = ops[scan]
                if cur[0] == "write" and cur[1] == 0x18 and cur[2] == 0x42:
                    edid_bytes.append(cur[3])
                    scan += 1
                    continue
                break

            if len(edid_bytes) != 256:
                raise ValueError(f"{spec.key}: EDID page sequence incomplete")

            extracted_ops.append(("load_edid", 0, 0, 0))
            idx = scan
            found = True
            continue

        extracted_ops.append(op)
        idx += 1

    return extracted_ops, edid_bytes


def fix_edid_checksums(spec: ProfileSpec, edid_bytes):
    corrected = []

    if not edid_bytes:
        return corrected

    if (len(edid_bytes) % 128) != 0:
        raise ValueError(f"{spec.key}: EDID length {len(edid_bytes)} is not block aligned")

    block_count = min(1 + edid_bytes[126], len(edid_bytes) // 128)
    for block in range(block_count):
        start = block * 128
        end = start + 128
        checksum = sum(edid_bytes[start:end]) & 0xFF
        if checksum != 0:
            new_checksum = (-sum(edid_bytes[start:end - 1])) & 0xFF
            edid_bytes[end - 1] = new_checksum
            corrected.append((block, new_checksum))

    return corrected


def emit_c(profiles_with_ops):
    corrected_lines = []
    for spec, corrections, _ops, _edid_bytes in profiles_with_ops:
        for block, checksum in corrections:
            corrected_lines.append(
                f" *   {spec.key}: block {block} checksum corrected to 0x{checksum:02X}"
            )

    header = [
        "/*",
        " * profile_data.c - generated from bios-scripts/bios-ver-11.txt",
        " *",
        " * Do not hand-edit this file. Re-run:",
        " *   python3 rh850-baremetal-demo/tools/gen_983_manager_profiles.py",
    ]
    if corrected_lines:
        header.extend([" *", " * EDID checksum fixes applied during generation:"])
        header.extend(corrected_lines)
    header.extend([" */", "", '#include "profile_data.h"', "", "#define OP_WRITE(dev, reg, val)  { INIT_OP_WRITE, (dev), (reg), (val), 0u }",
                   "#define OP_READ(dev, reg)        { INIT_OP_READ, (dev), (reg), 0u, 0u }",
                   "#define OP_DELAY_MS(ms)         { INIT_OP_DELAY_MS, 0u, 0u, 0u, (ms) }",
                   "#define OP_LOAD_EDID()          { INIT_OP_LOAD_EDID, 0u, 0u, 0u, 0u }", ""])

    body = []
    for spec, _corrections, ops, edid_bytes in profiles_with_ops:
        if edid_bytes:
            body.append(f"static const uint8 g_edid_{spec.key}[] =")
            body.append("{")
            for start in range(0, len(edid_bytes), 16):
                chunk = ", ".join(f"0x{value:02X}u" for value in edid_bytes[start:start + 16])
                body.append(f"    {chunk},")
            body.append("};")
            body.append("")

    for spec, _corrections, ops, _edid_bytes in profiles_with_ops:
        body.append(f"static const init_op_t {spec.symbol_name}[] =")
        body.append("{")
        for op in ops:
            if op[0] == "write":
                body.append(f"    OP_WRITE(0x{op[1]:02X}u, 0x{op[2]:02X}u, 0x{op[3]:02X}u),")
            elif op[0] == "read":
                body.append(f"    OP_READ(0x{op[1]:02X}u, 0x{op[2]:02X}u),")
            elif op[0] == "delay":
                body.append(f"    OP_DELAY_MS({op[3]}u),")
            elif op[0] == "load_edid":
                body.append("    OP_LOAD_EDID(),")
            else:
                raise ValueError(op[0])
        body.append("};")
        body.append("")

    body.append("const profile_data_t g_profile_data[PROFILE_COUNT] =")
    body.append("{")
    for spec, _corrections, _ops, edid_bytes in profiles_with_ops:
        edid_ref = f"g_edid_{spec.key}" if edid_bytes else "(const uint8 *)0"
        body.append(f'    {{ "{spec.display_name}", {spec.symbol_name}, '
                    f'(uint16)(sizeof({spec.symbol_name}) / sizeof({spec.symbol_name}[0])), '
                    f'{edid_ref}, {len(edid_bytes)}u }},')
    body.append("};")
    body.append("")

    OUT_C_PATH.write_text("\n".join(header + body) + "\n")


def main():
    instructions, labels = parse_bios()
    profiles_with_ops = []

    for spec in PROFILES:
        ops = resolve_profile_ops(spec, instructions, labels)
        ops, edid_bytes = extract_edid_blob(spec, ops)
        corrections = fix_edid_checksums(spec, edid_bytes)
        profiles_with_ops.append((spec, corrections, ops, edid_bytes))

    emit_c(profiles_with_ops)


if __name__ == "__main__":
    main()
