#!/usr/bin/env python3
"""
Generate profile_data.c/.h for 983_manager from bios-ver-11.txt and panels.json.

Runtime model:
  - board / power init remains handwritten C
  - non-EDID register traffic comes from a BIOS-derived base profile
  - EDID payloads come from versioned .bin files under app/983_manager/edid/
  - the generated selector table maps DIP state to the desired profile

If an EDID binary is missing for a manifest entry, this script bootstraps it
from the matching BIOS profile and writes the canonical checksum-fixed bytes.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BIOS_PATH = REPO_ROOT / "bios-scripts" / "bios-ver-11.txt"
MANIFEST_PATH = REPO_ROOT / "rh850-baremetal-demo" / "app" / "983_manager" / "panels.json"
OUT_C_PATH = REPO_ROOT / "rh850-baremetal-demo" / "app" / "983_manager" / "profile_data.c"
OUT_H_PATH = REPO_ROOT / "rh850-baremetal-demo" / "app" / "983_manager" / "profile_data.h"


BASE_AP0 = sum(1 << bit for bit in range(7, 15))


def ap0_for_on_dips(*bits: int) -> int:
    value = BASE_AP0
    for bit in bits:
        value &= ~(1 << bit)
    return value


@dataclass(frozen=True)
class BiosSourceSpec:
    key: str
    ap0_value: int


@dataclass(frozen=True)
class ManifestProfile:
    key: str
    display_name: str
    bios_source: str
    core_mask: int
    dip7_on: bool
    edid_bin_rel: str

    @property
    def enum_name(self) -> str:
        return "PROFILE_" + self.key.upper()

    @property
    def edid_symbol(self) -> str:
        return f"g_edid_{self.key}"

    @property
    def edid_bin_path(self) -> Path:
        return REPO_ROOT / self.edid_bin_rel


@dataclass(frozen=True)
class ResolvedSource:
    key: str
    ops_symbol: str
    ops: list[tuple[str, int, int, int]]
    default_edid: list[int]


BIOS_SOURCES = {
    "dip0_10g8": BiosSourceSpec("dip0_10g8", ap0_for_on_dips(7)),
    "dip0_6g75": BiosSourceSpec("dip0_6g75", ap0_for_on_dips(7, 14)),
    "dip1": BiosSourceSpec("dip1", ap0_for_on_dips(8)),
    "dip2": BiosSourceSpec("dip2", ap0_for_on_dips(9)),
    "dip3_oldi": BiosSourceSpec("dip3_oldi", ap0_for_on_dips(10)),
    "dip4_10g8": BiosSourceSpec("dip4_10g8", ap0_for_on_dips(11)),
    "dip4_6g75": BiosSourceSpec("dip4_6g75", ap0_for_on_dips(11, 14)),
    "dip5": BiosSourceSpec("dip5", ap0_for_on_dips(12)),
    "dip0_dip1_10g8": BiosSourceSpec("dip0_dip1_10g8", ap0_for_on_dips(7, 8)),
    "dip0_dip1_6g75": BiosSourceSpec("dip0_dip1_6g75", ap0_for_on_dips(7, 8, 14)),
    "dip0_dip2_10g8": BiosSourceSpec("dip0_dip2_10g8", ap0_for_on_dips(7, 9)),
    "dip0_dip2_6g75": BiosSourceSpec("dip0_dip2_6g75", ap0_for_on_dips(7, 9, 14)),
}

RATE_SUFFIXES = ("_10g8", "_6g75")


def write_if_changed(path: Path, data: bytes) -> None:
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def write_text_if_changed(path: Path, text: str) -> None:
    data = text.encode("utf-8")
    write_if_changed(path, data)


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


def resolve_source_ops(source_key: str, instructions, labels):
    source = BIOS_SOURCES[source_key]
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
            ap0_acc = source.ap0_value
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
                raise ValueError(f"Unsupported read length in {source_key}: {line}")
            ops.append(("read", int(tokens[3], 16) >> 1, int(tokens[4], 16), 0))
            pc += 1
            continue

        if tokens[0] == "X":
            ops.append(("delay", 0, 0, int(tokens[3], 16)))
            pc += 1
            continue

        pc += 1

    return ops


def extract_edid_blob(profile_key: str, ops):
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
                raise ValueError(f"{profile_key}: multiple EDID sequences found")

            scan = idx + 2
            while scan < len(ops) and len(edid_bytes) < 256:
                cur = ops[scan]
                if cur[0] == "write" and cur[1] == 0x18 and cur[2] == 0x42:
                    edid_bytes.append(cur[3])
                    scan += 1
                    continue
                break

            if len(edid_bytes) != 256:
                raise ValueError(f"{profile_key}: EDID page sequence incomplete")

            extracted_ops.append(("load_edid", 0, 0, 0))
            idx = scan
            found = True
            continue

        extracted_ops.append(op)
        idx += 1

    return extracted_ops, edid_bytes


def normalize_edid(profile_key: str, edid_bytes: list[int]):
    corrected = []

    if not edid_bytes:
        raise ValueError(f"{profile_key}: EDID is empty")

    if (len(edid_bytes) % 128) != 0:
        raise ValueError(f"{profile_key}: EDID length {len(edid_bytes)} is not block aligned")

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


def load_manifest():
    raw = json.loads(MANIFEST_PATH.read_text())
    raw_profiles = raw.get("profiles")
    if not isinstance(raw_profiles, list) or not raw_profiles:
        raise ValueError("panels.json must contain a non-empty 'profiles' list")

    profiles = []
    seen_keys = set()
    seen_selectors = set()

    for entry in raw_profiles:
        key = entry["key"]
        display_name = entry["display_name"]
        bios_source = entry["bios_source"]
        selection = entry["selection"]
        edid_bin_rel = entry["edid_bin"]

        if not re.fullmatch(r"[a-z0-9_]+", key):
            raise ValueError(f"Invalid profile key '{key}'")
        if key in seen_keys:
            raise ValueError(f"Duplicate profile key '{key}'")
        if bios_source not in BIOS_SOURCES:
            raise ValueError(f"{key}: unknown bios_source '{bios_source}'")

        core_mask = int(selection["core_mask"])
        dip7_on = bool(selection["dip7_on"])
        selector = (core_mask, dip7_on)
        if selector in seen_selectors:
            raise ValueError(f"{key}: duplicate selector core_mask=0x{core_mask:02X} dip7_on={int(dip7_on)}")

        seen_keys.add(key)
        seen_selectors.add(selector)
        profiles.append(
            ManifestProfile(
                key=key,
                display_name=display_name,
                bios_source=bios_source,
                core_mask=core_mask,
                dip7_on=dip7_on,
                edid_bin_rel=edid_bin_rel,
            )
        )

    return profiles


def load_edid_for_profile(profile: ManifestProfile, default_edid: list[int]):
    path = profile.edid_bin_path

    if path.exists():
        edid_bytes = list(path.read_bytes())
    else:
        edid_bytes = list(default_edid)

    corrections = normalize_edid(profile.key, edid_bytes)
    write_if_changed(path, bytes(edid_bytes))
    return edid_bytes, corrections


def strip_rate_suffix(source_key: str):
    for suffix in RATE_SUFFIXES:
        if source_key.endswith(suffix):
            return source_key[:-len(suffix)], suffix[1:]
    return source_key, None


def common_prefix_len(lhs, rhs):
    length = 0

    for left_op, right_op in zip(lhs, rhs):
        if left_op != right_op:
            break
        length += 1

    return length


def common_suffix_len(lhs, rhs, prefix_len: int):
    max_suffix = min(len(lhs), len(rhs)) - prefix_len
    length = 0

    while length < max_suffix:
        if lhs[len(lhs) - 1 - length] != rhs[len(rhs) - 1 - length]:
            break
        length += 1

    return length


def build_source_blocks(resolved_sources):
    source_blocks = {}
    rate_pairs = {}
    used_sources = set()

    for source_key in sorted(resolved_sources):
        base_key, rate = strip_rate_suffix(source_key)
        if rate is not None:
            rate_pairs.setdefault(base_key, {})[rate] = source_key

    for base_key in sorted(rate_pairs):
        pair = rate_pairs[base_key]
        key_10g8 = pair.get("10g8")
        key_6g75 = pair.get("6g75")

        if (key_10g8 is None) or (key_6g75 is None):
            continue

        ops_10g8 = resolved_sources[key_10g8].ops
        ops_6g75 = resolved_sources[key_6g75].ops
        prefix_len = common_prefix_len(ops_10g8, ops_6g75)
        suffix_len = common_suffix_len(ops_10g8, ops_6g75, prefix_len)

        # Only split when the shared content is material. Small matches are not
        # worth the extra per-profile block metadata.
        if (prefix_len + suffix_len) < 32:
            continue

        end_10g8 = len(ops_10g8) - suffix_len if suffix_len != 0 else len(ops_10g8)
        end_6g75 = len(ops_6g75) - suffix_len if suffix_len != 0 else len(ops_6g75)

        blocks_10g8 = []
        blocks_6g75 = []

        if prefix_len != 0:
            prefix_ops = ops_10g8[:prefix_len]
            prefix_symbol = f"g_ops_blk_{base_key}_common_pre"
            blocks_10g8.append((prefix_symbol, prefix_ops))
            blocks_6g75.append((prefix_symbol, prefix_ops))

        middle_10g8 = ops_10g8[prefix_len:end_10g8]
        middle_6g75 = ops_6g75[prefix_len:end_6g75]
        if middle_10g8:
            blocks_10g8.append((f"g_ops_blk_{key_10g8}_mid", middle_10g8))
        if middle_6g75:
            blocks_6g75.append((f"g_ops_blk_{key_6g75}_mid", middle_6g75))

        if suffix_len != 0:
            suffix_ops = ops_10g8[end_10g8:]
            suffix_symbol = f"g_ops_blk_{base_key}_common_post"
            blocks_10g8.append((suffix_symbol, suffix_ops))
            blocks_6g75.append((suffix_symbol, suffix_ops))

        source_blocks[key_10g8] = blocks_10g8
        source_blocks[key_6g75] = blocks_6g75
        used_sources.add(key_10g8)
        used_sources.add(key_6g75)

    for source_key in sorted(resolved_sources):
        if source_key in used_sources:
            continue
        source_blocks[source_key] = [(f"g_ops_blk_{source_key}", resolved_sources[source_key].ops)]

    return source_blocks


def emit_header(profiles):
    lines = [
        "/*",
        " * profile_data.h - generated from panels.json / bios-ver-11.txt",
        " *",
        " * Do not hand-edit this file. Re-run:",
        " *   python3 rh850-baremetal-demo/tools/gen_983_manager_profiles.py",
        " */",
        "",
        "#ifndef APP_983_MANAGER_PROFILE_DATA_H",
        "#define APP_983_MANAGER_PROFILE_DATA_H",
        "",
        '#include "dr7f701686.dvf.h"',
        "",
        "typedef enum",
        "{",
        "    INIT_OP_WRITE = 0u,",
        "    INIT_OP_READ = 1u,",
        "    INIT_OP_DELAY_MS = 2u,",
        "    INIT_OP_LOAD_EDID = 3u",
        "} init_op_type_t;",
        "",
        "typedef struct",
        "{",
        "    uint8  type;",
        "    uint8  dev_addr;",
        "    uint8  reg_addr;",
        "    uint8  value;",
        "    uint16 delay_ms;",
        "} init_op_t;",
        "",
        "typedef struct",
        "{",
        "    const init_op_t *ops;",
        "    uint16           op_count;",
        "} init_op_block_t;",
        "",
        "typedef enum",
        "{",
    ]

    for idx, profile in enumerate(profiles):
        suffix = "," if idx + 1 < len(profiles) else ","
        lines.append(f"    {profile.enum_name} = {idx}u{suffix}")
    lines.extend([
        "    PROFILE_COUNT",
        "} profile_id_t;",
        "",
        "typedef struct",
        "{",
        "    const char        *name;",
        "    const init_op_block_t *blocks;",
        "    uint8              block_count;",
        "    const uint8       *edid;",
        "    uint16             edid_len;",
        "} profile_data_t;",
        "",
        "extern const profile_data_t g_profile_data[PROFILE_COUNT];",
        "const profile_data_t *profile_select(uint8 dip_on_mask);",
        "",
        "#endif /* APP_983_MANAGER_PROFILE_DATA_H */",
        "",
    ])

    write_text_if_changed(OUT_H_PATH, "\n".join(lines))


def emit_c(profiles, resolved_sources, corrections_by_profile, profile_edids):
    corrected_lines = []
    for profile in profiles:
        for block, checksum in corrections_by_profile[profile.key]:
            corrected_lines.append(
                f" *   {profile.key}: block {block} checksum corrected to 0x{checksum:02X}"
            )

    lines = [
        "/*",
        " * profile_data.c - generated from panels.json / bios-ver-11.txt",
        " *",
        " * Do not hand-edit this file. Re-run:",
        " *   python3 rh850-baremetal-demo/tools/gen_983_manager_profiles.py",
    ]
    if corrected_lines:
        lines.extend([" *", " * EDID checksum fixes applied during generation:"])
        lines.extend(corrected_lines)
    lines.extend([
        " */",
        "",
        '#include "profile_data.h"',
        "",
        "#define OP_WRITE(dev, reg, val)  { INIT_OP_WRITE, (dev), (reg), (val), 0u }",
        "#define OP_READ(dev, reg)        { INIT_OP_READ, (dev), (reg), 0u, 0u }",
        "#define OP_DELAY_MS(ms)         { INIT_OP_DELAY_MS, 0u, 0u, 0u, (ms) }",
        "#define OP_LOAD_EDID()          { INIT_OP_LOAD_EDID, 0u, 0u, 0u, 0u }",
        "",
    ])

    unique_edids = []
    edid_symbol_by_bytes = {}
    profile_edid_symbol = {}

    for profile in profiles:
        edid_bytes = tuple(profile_edids[profile.key])
        shared_symbol = edid_symbol_by_bytes.get(edid_bytes)
        if shared_symbol is None:
            shared_symbol = profile.edid_symbol
            edid_symbol_by_bytes[edid_bytes] = shared_symbol
            unique_edids.append((shared_symbol, edid_bytes))
        profile_edid_symbol[profile.key] = shared_symbol

    for edid_symbol, edid_bytes in unique_edids:
        lines.append(f"static const uint8 {edid_symbol}[] =")
        lines.append("{")
        for start in range(0, len(edid_bytes), 16):
            chunk = ", ".join(f"0x{value:02X}u" for value in edid_bytes[start:start + 16])
            lines.append(f"    {chunk},")
        lines.append("};")
        lines.append("")

    source_blocks = build_source_blocks(resolved_sources)
    unique_blocks = []
    block_symbol_by_content = {}
    source_block_refs = {}

    for source_key in sorted(source_blocks):
        refs = []
        for block_symbol_hint, ops in source_blocks[source_key]:
            ops_content = tuple(ops)
            block_symbol = block_symbol_by_content.get(ops_content)
            if block_symbol is None:
                block_symbol = block_symbol_hint
                block_symbol_by_content[ops_content] = block_symbol
                unique_blocks.append((block_symbol, ops))
            refs.append((block_symbol, len(ops)))
        source_block_refs[source_key] = refs

    for block_symbol, ops in unique_blocks:
        lines.append(f"static const init_op_t {block_symbol}[] =")
        lines.append("{")
        for op in ops:
            if op[0] == "write":
                lines.append(f"    OP_WRITE(0x{op[1]:02X}u, 0x{op[2]:02X}u, 0x{op[3]:02X}u),")
            elif op[0] == "read":
                lines.append(f"    OP_READ(0x{op[1]:02X}u, 0x{op[2]:02X}u),")
            elif op[0] == "delay":
                lines.append(f"    OP_DELAY_MS({op[3]}u),")
            elif op[0] == "load_edid":
                lines.append("    OP_LOAD_EDID(),")
            else:
                raise ValueError(op[0])
        lines.append("};")
        lines.append("")

    for source_key in sorted(source_block_refs):
        block_list_symbol = f"g_blocks_src_{source_key}"
        lines.append(f"static const init_op_block_t {block_list_symbol}[] =")
        lines.append("{")
        for block_symbol, block_len in source_block_refs[source_key]:
            lines.append(f"    {{ {block_symbol}, {block_len}u }},")
        lines.append("};")
        lines.append("")

    lines.extend([
        "typedef struct",
        "{",
        "    uint8 core_mask;",
        "    uint8 dip7_on;",
        "    uint8 profile_id;",
        "} profile_selector_t;",
        "",
        "static const profile_selector_t g_profile_selectors[] =",
        "{",
    ])
    for profile in profiles:
        lines.append(
            f"    {{ 0x{profile.core_mask:02X}u, {1 if profile.dip7_on else 0}u, {profile.enum_name} }},"
        )
    lines.extend([
        "};",
        "",
        "const profile_data_t g_profile_data[PROFILE_COUNT] =",
        "{",
    ])
    for profile in profiles:
        block_list_symbol = f"g_blocks_src_{profile.bios_source}"
        block_refs = source_block_refs[profile.bios_source]
        edid_bytes = profile_edids[profile.key]
        lines.append(
            f'    {{ "{profile.display_name}", {block_list_symbol}, '
            f'{len(block_refs)}u, '
            f"{profile_edid_symbol[profile.key]}, {len(edid_bytes)}u }},"
        )
    lines.extend([
        "};",
        "",
        "const profile_data_t *profile_select(uint8 dip_on_mask)",
        "{",
        "    uint16 idx;",
        "    uint8 core_mask = (uint8)(dip_on_mask & 0x3Fu);",
        "    uint8 dip7_on = (uint8)(((dip_on_mask & 0x80u) != 0u) ? 1u : 0u);",
        "",
        "    for (idx = 0u; idx < (uint16)(sizeof(g_profile_selectors) / sizeof(g_profile_selectors[0])); idx++)",
        "    {",
        "        if ((g_profile_selectors[idx].core_mask == core_mask) &&",
        "            (g_profile_selectors[idx].dip7_on == dip7_on))",
        "        {",
        "            return &g_profile_data[g_profile_selectors[idx].profile_id];",
        "        }",
        "    }",
        "",
        "    return (const profile_data_t *)0;",
        "}",
        "",
    ])

    write_text_if_changed(OUT_C_PATH, "\n".join(lines))

def main():
    profiles = load_manifest()
    instructions, labels = parse_bios()

    resolved_sources = {}
    for bios_source in sorted({profile.bios_source for profile in profiles}):
        ops = resolve_source_ops(bios_source, instructions, labels)
        ops, default_edid = extract_edid_blob(bios_source, ops)
        normalize_edid(bios_source, default_edid)
        resolved_sources[bios_source] = ResolvedSource(
            key=bios_source,
            ops_symbol=f"g_ops_src_{bios_source}",
            ops=ops,
            default_edid=default_edid,
        )

    corrections_by_profile = {}
    profile_edids = {}
    for profile in profiles:
        resolved = resolved_sources[profile.bios_source]
        edid_bytes, corrections = load_edid_for_profile(profile, resolved.default_edid)
        corrections_by_profile[profile.key] = corrections
        profile_edids[profile.key] = edid_bytes

    emit_header(profiles)
    emit_c(profiles, resolved_sources, corrections_by_profile, profile_edids)


if __name__ == "__main__":
    main()
